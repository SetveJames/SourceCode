/*
 *  boot.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/config.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/irq.h>
#include <asm/pgalloc.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mpspec.h>

#ifdef CONFIG_X86_LOCAL_APIC
#include <mach_apic.h>
#include <mach_mpparse.h>
#include <asm/io_apic.h>
#endif

#define PREFIX			"ACPI: "

int acpi_noirq __initdata;	/* skip ACPI IRQ initialization */
int acpi_ht __initdata = 1;	/* enable HT */

int acpi_lapic;
int acpi_ioapic;
int acpi_strict;

#ifdef CONFIG_X86_LOCAL_APIC
static u64 acpi_lapic_addr __initdata = APIC_DEFAULT_PHYS_BASE;
#endif

/* --------------------------------------------------------------------------
                              Boot-time Configuration
   -------------------------------------------------------------------------- */

/*
 * The default interrupt routing model is PIC (8259).  This gets
 * overriden if IOAPICs are enumerated (below).
 */
enum acpi_irq_model_id		acpi_irq_model = ACPI_IRQ_MODEL_PIC;

/*
 * Temporarily use the virtual area starting from FIX_IO_APIC_BASE_END,
 * to map the target physical address. The problem is that set_fixmap()
 * provides a single page, and it is possible that the page is not
 * sufficient.
 * By using this area, we can map up to MAX_IO_APICS pages temporarily,
 * i.e. until the next __va_range() call.
 *
 * Important Safety Note:  The fixed I/O APIC page numbers are *subtracted*
 * from the fixed base.  That's why we start at FIX_IO_APIC_BASE_END and
 * count idx down while incrementing the phys address.
 */
char *__acpi_map_table(unsigned long phys, unsigned long size)
{
	unsigned long base, offset, mapped_size;
	int idx;

	if (phys + size < 8*1024*1024) 
		return __va(phys); 

	offset = phys & (PAGE_SIZE - 1);
	mapped_size = PAGE_SIZE - offset;
	set_fixmap(FIX_ACPI_END, phys);
	base = fix_to_virt(FIX_ACPI_END);

	/*
	 * Most cases can be covered by the below.
	 */
	idx = FIX_ACPI_END;
	while (mapped_size < size) {
		if (--idx < FIX_ACPI_BEGIN)
			return 0;	/* cannot handle this */
		phys += PAGE_SIZE;
		set_fixmap(idx, phys);
		mapped_size += PAGE_SIZE;
	}

	return ((unsigned char *) base + offset);
}


#ifdef CONFIG_PCI_MMCONFIG
static int __init acpi_parse_mcfg(unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_mcfg *mcfg;

	if (!phys_addr || !size)
		return -EINVAL;

	mcfg = (struct acpi_table_mcfg *) __acpi_map_table(phys_addr, size);
	if (!mcfg) {
		printk(KERN_WARNING PREFIX "Unable to map MCFG\n");
		return -ENODEV;
	}

	if (mcfg->base_reserved) {
		printk(KERN_ERR PREFIX "MMCONFIG not in low 4GB of memory\n");
		return -ENODEV;
	}

	pci_mmcfg_base_addr = mcfg->base_address;

	return 0;
}
#endif /* CONFIG_PCI_MMCONFIG */

#ifdef CONFIG_X86_LOCAL_APIC
static int __init
acpi_parse_madt (
	unsigned long		phys_addr,
	unsigned long		size)
{
	struct acpi_table_madt	*madt = NULL;

	if (!phys_addr || !size)
		return -EINVAL;

	madt = (struct acpi_table_madt *) __acpi_map_table(phys_addr, size);
	if (!madt) {
		printk(KERN_WARNING PREFIX "Unable to map MADT\n");
		return -ENODEV;
	}

	if (madt->lapic_address) {
		acpi_lapic_addr = (u64) madt->lapic_address;

		printk(KERN_DEBUG PREFIX "Local APIC address 0x%08x\n",
			madt->lapic_address);
	}

	acpi_madt_oem_check(madt->header.oem_id, madt->header.oem_table_id);
	
	return 0;
}


static int __init
acpi_parse_lapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic	*processor = NULL;

	processor = (struct acpi_table_lapic*) header;
	if (!processor)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* no utility in registering a disabled processor */
	if (processor->flags.enabled == 0)
		return 0;

	mp_register_lapic (
		processor->id,					   /* APIC ID */
		processor->flags.enabled);			  /* Enabled? */

	return 0;
}


static int __init
acpi_parse_lapic_addr_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_addr_ovr *lapic_addr_ovr = NULL;

	lapic_addr_ovr = (struct acpi_table_lapic_addr_ovr*) header;
	if (!lapic_addr_ovr)
		return -EINVAL;

	acpi_lapic_addr = lapic_addr_ovr->address;

	return 0;
}

static int __init
acpi_parse_lapic_nmi (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_nmi *lapic_nmi = NULL;

	lapic_nmi = (struct acpi_table_lapic_nmi*) header;
	if (!lapic_nmi)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic_nmi->lint != 1)
		printk(KERN_WARNING PREFIX "NMI not connected to LINT 1!\n");

	return 0;
}


#endif /*CONFIG_X86_LOCAL_APIC*/

#if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_ACPI_INTERPRETER)

static int __init
acpi_parse_ioapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_ioapic *ioapic = NULL;

	ioapic = (struct acpi_table_ioapic*) header;
	if (!ioapic)
		return -EINVAL;
 
	acpi_table_print_madt_entry(header);

	mp_register_ioapic (
		ioapic->id,
		ioapic->address,
		ioapic->global_irq_base);
 
	return 0;
}


static int __init
acpi_parse_int_src_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_int_src_ovr *intsrc = NULL;

	intsrc = (struct acpi_table_int_src_ovr*) header;
	if (!intsrc)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	mp_override_legacy_irq (
		intsrc->bus_irq,
		intsrc->flags.polarity,
		intsrc->flags.trigger,
		intsrc->global_irq);

	return 0;
}


static int __init
acpi_parse_nmi_src (
	acpi_table_entry_header *header)
{
	struct acpi_table_nmi_src *nmi_src = NULL;

	nmi_src = (struct acpi_table_nmi_src*) header;
	if (!nmi_src)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries? */

	return 0;
}

#endif /* CONFIG_X86_IO_APIC */

#ifdef	CONFIG_ACPI_BUS
/*
 * "acpi_pic_sci=level" (current default)
 * programs the PIC-mode SCI to Level Trigger.
 * (NO-OP if the BIOS set Level Trigger already)
 *
 * If a PIC-mode SCI is not recognized or gives spurious IRQ7's
 * it may require Edge Trigger -- use "acpi_pic_sci=edge"
 * (NO-OP if the BIOS set Edge Trigger already)
 *
 * Port 0x4d0-4d1 are ECLR1 and ECLR2, the Edge/Level Control Registers
 * for the 8259 PIC.  bit[n] = 1 means irq[n] is Level, otherwise Edge.
 * ECLR1 is IRQ's 0-7 (IRQ 0, 1, 2 must be 0)
 * ECLR2 is IRQ's 8-15 (IRQ 8, 13 must be 0)
 */

static int __initdata	acpi_pic_sci_trigger;	/* 0: level, 1: edge */

void __init
acpi_pic_sci_set_trigger(unsigned int irq)
{
	unsigned char mask = 1 << (irq & 7);
	unsigned int port = 0x4d0 + (irq >> 3);
	unsigned char val = inb(port);

	
	printk(PREFIX "IRQ%d SCI:", irq);
	if (!(val & mask)) {
		printk(" Edge");

		if (!acpi_pic_sci_trigger) {
			printk(" set to Level");
			outb(val | mask, port);
		}
	} else {
		printk(" Level");

		if (acpi_pic_sci_trigger) {
			printk(" set to Edge");
			outb(val | mask, port);
		}
	}
	printk(" Trigger.\n");
}

int __init
acpi_pic_sci_setup(char *str)
{
	while (str && *str) {
		if (strncmp(str, "level", 5) == 0)
			acpi_pic_sci_trigger = 0;	/* force level trigger */
		if (strncmp(str, "edge", 4) == 0)
			acpi_pic_sci_trigger = 1;	/* force edge trigger */
		str = strchr(str, ',');
		if (str)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi_pic_sci=", acpi_pic_sci_setup);

#endif /* CONFIG_ACPI_BUS */

#ifdef CONFIG_X86_IO_APIC
int acpi_irq_to_vector(u32 irq)
{
	if (use_pci_vector() && !platform_legacy_irq(irq))
 		irq = IO_APIC_VECTOR(irq);
	return irq;
}
#endif

static unsigned long __init
acpi_scan_rsdp (
	unsigned long		start,
	unsigned long		length)
{
	unsigned long		offset = 0;
	unsigned long		sig_len = sizeof("RSD PTR ") - 1;

	/*
	 * Scan all 16-byte boundaries of the physical memory region for the
	 * RSDP signature.
	 */
	for (offset = 0; offset < length; offset += 16) {
		if (strncmp((char *) (start + offset), "RSD PTR ", sig_len))
			continue;
		return (start + offset);
	}

	return 0;
}

static int __init acpi_parse_sbf(unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_sbf *sb;

	if (!phys_addr || !size)
	return -EINVAL;

	sb = (struct acpi_table_sbf *) __acpi_map_table(phys_addr, size);
	if (!sb) {
		printk(KERN_WARNING PREFIX "Unable to map SBF\n");
		return -ENODEV;
	}

	sbf_port = sb->sbf_cmos; /* Save CMOS port */

	return 0;
}


#ifdef CONFIG_HPET_TIMER
extern unsigned long hpet_address;

static int __init acpi_parse_hpet(unsigned long phys, unsigned long size)
{
	struct acpi_table_hpet *hpet_tbl;

	if (!phys || !size)
		return -EINVAL;

	hpet_tbl = (struct acpi_table_hpet *) __acpi_map_table(phys, size);
	if (!hpet_tbl) {
		printk(KERN_WARNING PREFIX "Unable to map HPET\n");
		return -ENODEV;
	}

	if (hpet_tbl->addr.space_id != ACPI_SPACE_MEM) {
		printk(KERN_WARNING PREFIX "HPET timers must be located in "
		       "memory.\n");
		return -1;
	}

	hpet_address = hpet_tbl->addr.addrl;
	printk(KERN_INFO PREFIX "HPET id: %#x base: %#lx\n", hpet_tbl->id,
	       hpet_address);
	return 0;
}
#endif

/* detect the location of the ACPI PM Timer */
#ifdef CONFIG_X86_PM_TIMER
extern u32 pmtmr_ioport;

static int __init acpi_parse_fadt(unsigned long phys, unsigned long size)
{
	struct fadt_descriptor_rev2 *fadt =0;

	fadt = (struct fadt_descriptor_rev2*) __acpi_map_table(phys,size);
	if(!fadt) {
		printk(KERN_WARNING PREFIX "Unable to map FADT\n");
		return 0;
	}

	if (fadt->revision >= FADT2_REVISION_ID) {
		/* FADT rev. 2 */
		if (fadt->xpm_tmr_blk.address_space_id != ACPI_ADR_SPACE_SYSTEM_IO)
			return 0;

		pmtmr_ioport = fadt->xpm_tmr_blk.address;
	} else {
		/* FADT rev. 1 */
		pmtmr_ioport = fadt->V1_pm_tmr_blk;
	}
	if (pmtmr_ioport)
		printk(KERN_INFO PREFIX "PM-Timer IO Port: %#x\n", pmtmr_ioport);
	return 0;
}
#endif


unsigned long __init
acpi_find_rsdp (void)
{
	unsigned long		rsdp_phys = 0;

	if (efi_enabled) {
		if (efi.acpi20)
			return __pa(efi.acpi20);
		else if (efi.acpi)
			return __pa(efi.acpi);
	}
	/*
	 * Scan memory looking for the RSDP signature. First search EBDA (low
	 * memory) paragraphs and then search upper memory (E0000-FFFFF).
	 */
	rsdp_phys = acpi_scan_rsdp (0, 0x400);
	if (!rsdp_phys)
		rsdp_phys = acpi_scan_rsdp (0xE0000, 0xFFFFF);

	return rsdp_phys;
}

#ifdef	CONFIG_X86_LOCAL_APIC
/*
 * Parse LAPIC entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init
acpi_parse_madt_lapic_entries(void)
{
	int count;

	/* 
	 * Note that the LAPIC address is obtained from the MADT (32-bit value)
	 * and (optionally) overriden by a LAPIC_ADDR_OVR entry (64-bit value).
	 */

	count = acpi_table_parse_madt(ACPI_MADT_LAPIC_ADDR_OVR, acpi_parse_lapic_addr_ovr, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC address override entry\n");
		return count;
	}

	mp_register_lapic_address(acpi_lapic_addr);

	count = acpi_table_parse_madt(ACPI_MADT_LAPIC, acpi_parse_lapic,
				       MAX_APICS);
	if (!count) { 
		printk(KERN_ERR PREFIX "No LAPIC entries present\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return -ENODEV;
	}
	else if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	count = acpi_table_parse_madt(ACPI_MADT_LAPIC_NMI, acpi_parse_lapic_nmi, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}
	return 0;
}
#endif /* CONFIG_X86_LOCAL_APIC */

#if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_ACPI_INTERPRETER)
/*
 * Parse IOAPIC related entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init
acpi_parse_madt_ioapic_entries(void)
{
	int count;

	/*
	 * ACPI interpreter is required to complete interrupt setup,
	 * so if it is off, don't enumerate the io-apics with ACPI.
	 * If MPS is present, it will handle them,
	 * otherwise the system will stay in PIC mode
	 */
	if (acpi_disabled || acpi_noirq) {
		return -ENODEV;
        }

	/*
 	 * if "noapic" boot option, don't look for IO-APICs
	 */
	if (ioapic_setup_disabled()) {
		printk(KERN_INFO PREFIX "Skipping IOAPIC probe "
			"due to 'noapic' option.\n");
		return -ENODEV;
	}

	count = acpi_table_parse_madt(ACPI_MADT_IOAPIC, acpi_parse_ioapic, MAX_IO_APICS);
	if (!count) {
		printk(KERN_ERR PREFIX "No IOAPIC entries present\n");
		return -ENODEV;
	}
	else if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing IOAPIC entry\n");
		return count;
	}

	/* Build a default routing table for legacy (ISA) interrupts. */
	mp_config_acpi_legacy_irqs();

	count = acpi_table_parse_madt(ACPI_MADT_INT_SRC_OVR, acpi_parse_int_src_ovr, NR_IRQ_VECTORS);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing interrupt source overrides entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	count = acpi_table_parse_madt(ACPI_MADT_NMI_SRC, acpi_parse_nmi_src, NR_IRQ_VECTORS);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	return 0;
}
#else
static inline int acpi_parse_madt_ioapic_entries(void)
{
	return -1;
}
#endif /* !(CONFIG_X86_IO_APIC && CONFIG_ACPI_INTERPRETER) */


static void __init
acpi_process_madt(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	int count, error;

	count = acpi_table_parse(ACPI_APIC, acpi_parse_madt);
	if (count == 1) {

		/*
		 * Parse MADT LAPIC entries
		 */
		error = acpi_parse_madt_lapic_entries();
		if (!error) {
			acpi_lapic = 1;

			/*
			 * Parse MADT IO-APIC entries
			 */
			error = acpi_parse_madt_ioapic_entries();
			if (!error) {
				acpi_irq_model = ACPI_IRQ_MODEL_IOAPIC;
				acpi_irq_balance_set(NULL);
				acpi_ioapic = 1;

				smp_found_config = 1;
				clustered_apic_check();
			}
		}
	}
#endif
	return;
}

/*
 * acpi_boot_init()
 *  called from setup_arch(), always.
 *	1. checksums all tables
 *	2. enumerates lapics
 *	3. enumerates io-apics
 *
 * side effects:
 *	acpi_lapic = 1 if LAPIC found
 *	acpi_ioapic = 1 if IOAPIC found
 *	if (acpi_lapic && acpi_ioapic) smp_found_config = 1;
 *	if acpi_blacklisted() acpi_disabled = 1;
 *	acpi_irq_model=...
 *	...
 *
 * return value: (currently ignored)
 *	0: success
 *	!0: failure
 */

int __init
acpi_boot_init (void)
{
	int error;

	/*
	 * If acpi_disabled, bail out
	 * One exception: acpi=ht continues far enough to enumerate LAPICs
	 */
	if (acpi_disabled && !acpi_ht)
		 return 1;

	/* 
	 * Initialize the ACPI boot-time table parser.
	 */
	error = acpi_table_init();
	if (error) {
		acpi_disabled = 1;
		return error;
	}

	(void) acpi_table_parse(ACPI_BOOT, acpi_parse_sbf);

	/*
	 * blacklist may disable ACPI entirely
	 */
	error = acpi_blacklisted();
	if (error) {
		printk(KERN_WARNING PREFIX "BIOS listed in blacklist, disabling ACPI support\n");
		acpi_disabled = 1;
		return error;
	}

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

#ifdef CONFIG_X86_PM_TIMER
	acpi_table_parse(ACPI_FADT, acpi_parse_fadt);
#endif

#ifdef CONFIG_HPET_TIMER
	(void) acpi_table_parse(ACPI_HPET, acpi_parse_hpet);
#endif

#ifdef CONFIG_PCI_MMCONFIG
	error = acpi_table_parse(ACPI_MCFG, acpi_parse_mcfg);
	if (error)
		printk(KERN_ERR PREFIX "Error %d parsing MCFG\n", error);
#endif

	return 0;
}

