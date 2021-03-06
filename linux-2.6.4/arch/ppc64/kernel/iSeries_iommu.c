/*
 * arch/ppc64/kernel/iSeries_iommu.c
 *
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * 
 * Rewrite, cleanup: 
 *
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
 *
 * Dynamic DMA mapping support, iSeries-specific parts.
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/ppcdebug.h>

#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/LparData.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/vio.h>

#include <asm/machdep.h>

#include "pci.h"


static struct iommu_table veth_iommu_table;	/* Tce table for virtual ethernet */
static struct iommu_table vio_iommu_table;	/* Tce table for virtual I/O */

static struct iSeries_Device_Node veth_dev_node = { .LogicalSlot = 0xFF, .iommu_table = &veth_iommu_table };
static struct iSeries_Device_Node vio_dev_node  = { .LogicalSlot = 0xFF, .iommu_table = &vio_iommu_table };

static struct pci_dev _veth_dev = { .sysdata = &veth_dev_node };
static struct pci_dev _vio_dev  = { .sysdata = &vio_dev_node, .dev.bus = &pci_bus_type  };

struct pci_dev *iSeries_veth_dev = &_veth_dev;
struct device *iSeries_vio_dev = &_vio_dev.dev;

EXPORT_SYMBOL(iSeries_veth_dev);
EXPORT_SYMBOL(iSeries_vio_dev);

extern struct list_head iSeries_Global_Device_List;


static void tce_build_iSeries(struct iommu_table *tbl, long index, long npages,
			      unsigned long uaddr, int direction)
{
	u64 rc;
	union tce_entry tce;
	
	while (npages--) {
		tce.te_word = 0;
		tce.te_bits.tb_rpn = (virt_to_absolute(uaddr)) >> PAGE_SHIFT;

		if (tbl->it_type == TCE_VB) {
			/* Virtual Bus */
			tce.te_bits.tb_valid = 1;
			tce.te_bits.tb_allio = 1;
			if (direction != PCI_DMA_TODEVICE)
				tce.te_bits.tb_rdwr = 1;
		} else {
			/* PCI Bus */
			tce.te_bits.tb_rdwr = 1; /* Read allowed */
			if (direction != PCI_DMA_TODEVICE)
				tce.te_bits.tb_pciwr = 1;
		}
		
		rc = HvCallXm_setTce((u64)tbl->it_index, 
				     (u64)index, 
				     tce.te_word);
		if (rc)
			panic("PCI_DMA: HvCallXm_setTce failed, Rc: 0x%lx\n", rc);

		index++;
		uaddr += PAGE_SIZE;
	}
}

static void tce_free_iSeries(struct iommu_table *tbl, long index, long npages)
{
	u64 rc;
	union tce_entry tce;

	while (npages--) {
		tce.te_word = 0;
		rc = HvCallXm_setTce((u64)tbl->it_index,
				     (u64)index,
				     tce.te_word);

		if (rc) 
			panic("PCI_DMA: HvCallXm_setTce failed, Rc: 0x%lx\n", rc);

		index++;
	}

}

void __init iommu_vio_init(void)
{
	struct iommu_table *t;
	struct iommu_table_cb cb;
	unsigned long cbp;

	cb.itc_busno = 255;    /* Bus 255 is the virtual bus */
	cb.itc_virtbus = 0xff; /* Ask for virtual bus */
	
	cbp = virt_to_absolute((unsigned long)&cb);
	HvCallXm_getTceTableParms(cbp);
	
	veth_iommu_table.it_size        = cb.itc_size / 2;
	veth_iommu_table.it_busno       = cb.itc_busno;
	veth_iommu_table.it_offset      = cb.itc_offset;
	veth_iommu_table.it_index       = cb.itc_index;
	veth_iommu_table.it_type        = TCE_VB;
	veth_iommu_table.it_entrysize	= sizeof(union tce_entry);
	veth_iommu_table.it_blocksize	= 1;

	t = iommu_init_table(&veth_iommu_table);

	if (!t)
		printk("Virtual Bus VETH TCE table failed.\n");

	vio_iommu_table.it_size         = cb.itc_size - veth_iommu_table.it_size;
	vio_iommu_table.it_busno        = cb.itc_busno;
	vio_iommu_table.it_offset       = cb.itc_offset +
		veth_iommu_table.it_size * (PAGE_SIZE/sizeof(union tce_entry));
	vio_iommu_table.it_index        = cb.itc_index;
	vio_iommu_table.it_type         = TCE_VB; 
	vio_iommu_table.it_entrysize	= sizeof(union tce_entry);
	vio_iommu_table.it_blocksize	= 1;

	t = iommu_init_table(&vio_iommu_table);

	if (!t) 
		printk("Virtual Bus VIO TCE table failed.\n");
}


/*
 * This function compares the known tables to find an iommu_table
 * that has already been built for hardware TCEs.                          
 */
static struct iommu_table *iommu_table_find(struct iommu_table * tbl)
{
	struct iSeries_Device_Node *dp;

	for (dp =  (struct iSeries_Device_Node *)iSeries_Global_Device_List.next;
	     dp != (struct iSeries_Device_Node *)&iSeries_Global_Device_List;
	     dp =  (struct iSeries_Device_Node *)dp->Device_List.next) 
		if (dp->iommu_table                 != NULL &&
		    dp->iommu_table->it_type        == TCE_PCI &&
		    dp->iommu_table->it_offset      == tbl->it_offset &&
		    dp->iommu_table->it_index       == tbl->it_index &&
		    dp->iommu_table->it_size        == tbl->it_size) 
			return dp->iommu_table;
			

	return NULL;
}

/*
 * Call Hv with the architected data structure to get TCE table info.
 * info. Put the returned data into the Linux representation of the   
 * TCE table data.                                                     
 * The Hardware Tce table comes in three flavors.                     
 * 1. TCE table shared between Buses.                                  
 * 2. TCE table per Bus.                                               
 * 3. TCE Table per IOA.                                               
 */
static void iommu_table_getparms(struct iSeries_Device_Node* dn,
				 struct iommu_table* tbl)
{
	struct iommu_table_cb *parms;

	parms = (struct iommu_table_cb*)kmalloc(sizeof(*parms), GFP_KERNEL);

	if (parms == NULL) 
		panic("PCI_DMA: TCE Table Allocation failed.");

	memset(parms, 0, sizeof(*parms));

	parms->itc_busno   = ISERIES_BUS(dn);
	parms->itc_slotno  = dn->LogicalSlot;
	parms->itc_virtbus = 0;

	HvCallXm_getTceTableParms(REALADDR(parms));

	if (parms->itc_size == 0)
		panic("PCI_DMA: parms->size is zero, parms is 0x%p", parms);

	tbl->it_size        = parms->itc_size;
	tbl->it_busno       = parms->itc_busno;
	tbl->it_offset      = parms->itc_offset;
	tbl->it_index       = parms->itc_index;
	tbl->it_entrysize   = sizeof(union tce_entry);
	tbl->it_blocksize   = 1;
	tbl->it_type        = TCE_PCI;

	kfree(parms);
}


void iommu_devnode_init(struct iSeries_Device_Node *dn) {
	struct iommu_table *tbl;

	tbl = (struct iommu_table *)kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

	iommu_table_getparms(dn, tbl);

	/* Look for existing tce table */
	dn->iommu_table = iommu_table_find(tbl);

	if (dn->iommu_table == NULL)
		dn->iommu_table = iommu_init_table(tbl);
	else
		kfree(tbl);

	return;
}


void tce_init_iSeries(void)
{
	ppc_md.tce_build = tce_build_iSeries;
	ppc_md.tce_free  = tce_free_iSeries;

	pci_iommu_init();
}
