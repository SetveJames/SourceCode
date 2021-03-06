/*
 * BRIEF MODULE DESCRIPTION
 *	Alchemy/AMD Au1x00 pci support.
 *
 * Copyright 2001,2002,2003 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  Support for all devices (greater than 16) added by David Gathright.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-au1x00/au1000.h>
//#include <asm/pb1500.h>
#ifdef CONFIG_MIPS_PB1000
#include <asm/mach-pb1x00/pb1000.h>
#endif
#include <asm/pci_channel.h>

/* TBD */
static struct resource pci_io_resource = {
	"pci IO space", 
	(u32)PCI_IO_START,
	(u32)PCI_IO_END,
	IORESOURCE_IO
};

static struct resource pci_mem_resource = {
	"pci memory space", 
	(u32)PCI_MEM_START,
	(u32)PCI_MEM_END,
	IORESOURCE_MEM
};

extern struct pci_ops au1x_pci_ops;

static struct pci_controller au1x_controller = {
	.pci_ops	= &au1x_pci_ops,
	.io_resource	= &pci_io_resource,
	.mem_resource	= &pci_mem_resource,
};

#ifdef CONFIG_SOC_AU1500
static unsigned long virt_io_addr;
#endif

static int __init au1x_pci_setup(void)
{
#ifdef CONFIG_SOC_AU1500
	int i;
	struct pci_dev *dev;
	
	virt_io_addr = (unsigned long)ioremap(Au1500_PCI_IO_START, 
			Au1500_PCI_IO_END - Au1500_PCI_IO_START + 1);

	if (!virt_io_addr) {
		printk(KERN_ERR "Unable to ioremap pci space\n");
		return;
	}

#ifdef CONFIG_DMA_NONCOHERENT
	/* 
	 *  Set the NC bit in controller for pre-AC silicon
	 */
	au_writel( 1<<16 | au_readl(Au1500_PCI_CFG), Au1500_PCI_CFG);
	printk("Non-coherent PCI accesses enabled\n");
#endif

	set_io_port_base(virt_io_addr);
#endif

#ifdef CONFIG_MIPS_PB1000 /* This is truly board specific */
	unsigned long pci_mem_start = (unsigned long) PCI_MEM_START;

	au_writel(0, PCI_BRIDGE_CONFIG); // set extend byte to 0
	au_writel(0, SDRAM_MBAR);        // set mbar to 0
	au_writel(0x2, SDRAM_CMD);       // enable memory accesses
	au_sync_delay(1);

	// set extend byte to mbar of ext slot
	au_writel(((pci_mem_start >> 24) & 0xff) |
	       (1 << 8 | 1 << 9 | 1 << 10 | 1 << 27), PCI_BRIDGE_CONFIG);
#endif

	register_pci_controller(&au1x_controller);
}

arch_initcall(au1x_pci_setup);
