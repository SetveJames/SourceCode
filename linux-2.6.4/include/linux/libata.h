/*
   Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
   Copyright 2003-2004 Jeff Garzik

   The contents of this file are subject to the Open
   Software License version 1.1 that can be found at
   http://www.opensource.org/licenses/osl-1.1.txt and is included herein
   by reference.

   Alternatively, the contents of this file may be used under the terms
   of the GNU General Public License version 2 (the "GPL") as distributed
   in the kernel source COPYING file, in which case the provisions of
   the GPL are applicable instead of the above.  If you wish to allow
   the use of your version of this file only under the terms of the
   GPL and not to allow others to use your version of this file under
   the OSL, indicate your decision by deleting the provisions above and
   replace them with the notice and other provisions required by the GPL.
   If you do not delete the provisions above, a recipient may use your
   version of this file under either the OSL or the GPL.

 */

#ifndef __LINUX_LIBATA_H__
#define __LINUX_LIBATA_H__

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <linux/ata.h>


/*
 * compile-time options
 */
#undef ATA_FORCE_PIO		/* do not configure or use DMA */
#undef ATA_DEBUG		/* debugging output */
#undef ATA_VERBOSE_DEBUG	/* yet more debugging output */
#undef ATA_IRQ_TRAP		/* define to ack screaming irqs */
#undef ATA_NDEBUG		/* define to disable quick runtime checks */
#undef ATA_ENABLE_ATAPI		/* define to enable ATAPI support */
#undef ATA_ENABLE_PATA		/* define to enable PATA support in some
				 * low-level drivers */


/* note: prints function name for you */
#ifdef ATA_DEBUG
#define DPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#ifdef ATA_VERBOSE_DEBUG
#define VPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define VPRINTK(fmt, args...)
#endif	/* ATA_VERBOSE_DEBUG */
#else
#define DPRINTK(fmt, args...)
#define VPRINTK(fmt, args...)
#endif	/* ATA_DEBUG */

#ifdef ATA_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(unlikely(!(expr))) {                                   \
        printk(KERN_ERR "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif

/* defines only for the constants which don't work well as enums */
#define ATA_TAG_POISON		0xfafbfcfdU

enum {
	/* various global constants */
	LIBATA_MAX_PRD		= ATA_MAX_PRD / 2,
	ATA_MAX_PORTS		= 8,
	ATA_DEF_QUEUE		= 1,
	ATA_MAX_QUEUE		= 1,
	ATA_MAX_SECTORS		= 200,	/* FIXME */
	ATA_MAX_BUS		= 2,
	ATA_DEF_BUSY_WAIT	= 10000,
	ATA_SHORT_PAUSE		= (HZ >> 6) + 1,

	ATA_SHT_EMULATED	= 1,
	ATA_SHT_CMD_PER_LUN	= 1,
	ATA_SHT_THIS_ID		= -1,
	ATA_SHT_USE_CLUSTERING	= 1,

	/* struct ata_device stuff */
	ATA_DFLAG_LBA48		= (1 << 0), /* device supports LBA48 */
	ATA_DFLAG_PIO		= (1 << 1), /* device currently in PIO mode */
	ATA_DFLAG_MASTER	= (1 << 2), /* is device 0? */
	ATA_DFLAG_WCACHE	= (1 << 3), /* has write cache we can
					     * (hopefully) flush? */

	ATA_DEV_UNKNOWN		= 0,	/* unknown device */
	ATA_DEV_ATA		= 1,	/* ATA device */
	ATA_DEV_ATA_UNSUP	= 2,	/* ATA device (unsupported) */
	ATA_DEV_ATAPI		= 3,	/* ATAPI device */
	ATA_DEV_ATAPI_UNSUP	= 4,	/* ATAPI device (unsupported) */
	ATA_DEV_NONE		= 5,	/* no device */

	/* struct ata_port flags */
	ATA_FLAG_SLAVE_POSS	= (1 << 1), /* host supports slave dev */
					    /* (doesn't imply presence) */
	ATA_FLAG_PORT_DISABLED	= (1 << 2), /* port is disabled, ignore it */
	ATA_FLAG_SATA		= (1 << 3),
	ATA_FLAG_NO_LEGACY	= (1 << 4), /* no legacy mode check */
	ATA_FLAG_SRST		= (1 << 5), /* use ATA SRST, not E.D.D. */
	ATA_FLAG_MMIO		= (1 << 6), /* use MMIO, not PIO */
	ATA_FLAG_SATA_RESET	= (1 << 7), /* use COMRESET */

	/* struct ata_taskfile flags */
	ATA_TFLAG_LBA48		= (1 << 0),
	ATA_TFLAG_ISADDR	= (1 << 1), /* enable r/w to nsect/lba regs */
	ATA_TFLAG_DEVICE	= (1 << 2), /* enable r/w to device reg */

	ATA_QCFLAG_WRITE	= (1 << 0), /* read==0, write==1 */
	ATA_QCFLAG_ACTIVE	= (1 << 1), /* cmd not yet ack'd to scsi lyer */
	ATA_QCFLAG_DMA		= (1 << 2), /* data delivered via DMA */
	ATA_QCFLAG_ATAPI	= (1 << 3), /* is ATAPI packet command? */
	ATA_QCFLAG_SG		= (1 << 4), /* have s/g table? */
	ATA_QCFLAG_POLL		= (1 << 5), /* polling, no interrupts */

	/* struct ata_engine atomic flags (use test_bit, etc.) */
	ATA_EFLG_ACTIVE		= 0,	/* engine is active */

	/* various lengths of time */
	ATA_TMOUT_EDD		= 5 * HZ,	/* hueristic */
	ATA_TMOUT_PIO		= 30 * HZ,
	ATA_TMOUT_BOOT		= 30 * HZ,	/* hueristic */
	ATA_TMOUT_BOOT_QUICK	= 7 * HZ,	/* hueristic */
	ATA_TMOUT_CDB		= 30 * HZ,
	ATA_TMOUT_CDB_QUICK	= 5 * HZ,

	/* ATA bus states */
	BUS_UNKNOWN		= 0,
	BUS_DMA			= 1,
	BUS_IDLE		= 2,
	BUS_NOINTR		= 3,
	BUS_NODATA		= 4,
	BUS_TIMER		= 5,
	BUS_PIO			= 6,
	BUS_EDD			= 7,
	BUS_IDENTIFY		= 8,
	BUS_PACKET		= 9,

	/* thread states */
	THR_UNKNOWN		= 0,
	THR_PORT_RESET		= (THR_UNKNOWN + 1),
	THR_AWAIT_DEATH		= (THR_PORT_RESET + 1),
	THR_PROBE_FAILED	= (THR_AWAIT_DEATH + 1),
	THR_IDLE		= (THR_PROBE_FAILED + 1),
	THR_PROBE_SUCCESS	= (THR_IDLE + 1),
	THR_PROBE_START		= (THR_PROBE_SUCCESS + 1),
	THR_PIO_POLL		= (THR_PROBE_START + 1),
	THR_PIO_TMOUT		= (THR_PIO_POLL + 1),
	THR_PIO			= (THR_PIO_TMOUT + 1),
	THR_PIO_LAST		= (THR_PIO + 1),
	THR_PIO_LAST_POLL	= (THR_PIO_LAST + 1),
	THR_PIO_ERR		= (THR_PIO_LAST_POLL + 1),
	THR_PACKET		= (THR_PIO_ERR + 1),

	/* SATA port states */
	PORT_UNKNOWN		= 0,
	PORT_ENABLED		= 1,
	PORT_DISABLED		= 2,

	/* ata_qc_cb_t flags - note uses above ATA_QCFLAG_xxx namespace,
	 * but not numberspace
	 */
	ATA_QCFLAG_TIMEOUT	= (1 << 0),
};

/* forward declarations */
struct scsi_device;
struct ata_port_operations;
struct ata_port;
struct ata_queued_cmd;

/* typedefs */
typedef void (*ata_qc_cb_t) (struct ata_queued_cmd *qc, unsigned int flags);

struct ata_ioports {
	unsigned long		cmd_addr;
	unsigned long		data_addr;
	unsigned long		error_addr;
	unsigned long		nsect_addr;
	unsigned long		lbal_addr;
	unsigned long		lbam_addr;
	unsigned long		lbah_addr;
	unsigned long		device_addr;
	unsigned long		cmdstat_addr;
	unsigned long		ctl_addr;
	unsigned long		bmdma_addr;
	unsigned long		scr_addr;
};

struct ata_probe_ent {
	struct list_head	node;
	struct pci_dev		*pdev;
	struct ata_port_operations	*port_ops;
	Scsi_Host_Template	*sht;
	struct ata_ioports	port[ATA_MAX_PORTS];
	unsigned int		n_ports;
	unsigned int		pio_mask;
	unsigned int		udma_mask;
	unsigned int		legacy_mode;
	unsigned long		irq;
	unsigned int		irq_flags;
	unsigned long		host_flags;
	void			*mmio_base;
	void			*private_data;
};

struct ata_host_set {
	spinlock_t		lock;
	struct pci_dev		*pdev;
	unsigned long		irq;
	void			*mmio_base;
	unsigned int		n_ports;
	void			*private_data;
	struct ata_port *	ports[0];
};

struct ata_taskfile {
	unsigned long		flags;		/* ATA_TFLAG_xxx */
	u8			protocol;	/* ATA_PROT_xxx */

	u8			ctl;		/* control reg */

	u8			hob_feature;	/* additional data */
	u8			hob_nsect;	/* to support LBA48 */
	u8			hob_lbal;
	u8			hob_lbam;
	u8			hob_lbah;

	u8			feature;
	u8			nsect;
	u8			lbal;
	u8			lbam;
	u8			lbah;

	u8			device;

	u8			command;	/* IO operation */
};

struct ata_queued_cmd {
	struct ata_port		*ap;
	struct ata_device	*dev;

	struct scsi_cmnd		*scsicmd;
	void			(*scsidone)(struct scsi_cmnd *);

	struct list_head	node;
	unsigned long		flags;		/* ATA_QCFLAG_xxx */
	unsigned int		tag;
	unsigned int		n_elem;
	unsigned int		nsect;
	unsigned int		cursect;
	unsigned int		cursg;
	unsigned int		cursg_ofs;
	struct ata_taskfile	tf;
	struct scatterlist	sgent;

	struct scatterlist	*sg;

	ata_qc_cb_t		callback;

	struct semaphore	sem;

	void			*private_data;
};

struct ata_host_stats {
	unsigned long		unhandled_irq;
	unsigned long		idle_irq;
	unsigned long		rw_reqbuf;
};

struct ata_device {
	u64			n_sectors;	/* size of device, if ATA */
	unsigned long		flags;		/* ATA_DFLAG_xxx */
	unsigned int		class;		/* ATA_DEV_xxx */
	unsigned int		devno;		/* 0 or 1 */
	u16			id[ATA_ID_WORDS]; /* IDENTIFY xxx DEVICE data */
	unsigned int		pio_mode;
	unsigned int		udma_mode;

	unsigned char		vendor[8];	/* space-padded, not ASCIIZ */
	unsigned char		product[32];	/* WARNING: shorter than
						 * ATAPI7 spec size, 40 ASCII
						 * characters
						 */
};

struct ata_engine {
	unsigned long		flags;
	struct list_head	q;
};

struct ata_port {
	struct Scsi_Host	*host;	/* our co-allocated scsi host */
	struct ata_port_operations	*ops;
	unsigned long		flags;	/* ATA_FLAG_xxx */
	unsigned int		id;	/* unique id req'd by scsi midlyr */
	unsigned int		port_no; /* unique port #; from zero */

	struct ata_prd		*prd;	 /* our SG list */
	dma_addr_t		prd_dma; /* and its DMA mapping */

	struct ata_ioports	ioaddr;	/* ATA cmd/ctl/dma register blocks */

	u8			ctl;	/* cache of ATA control register */
	u8			last_ctl;	/* Cache last written value */
	unsigned int		bus_state;
	unsigned int		port_state;
	unsigned int		pio_mask;
	unsigned int		udma_mask;
	unsigned int		cbl;	/* cable type; ATA_CBL_xxx */

	struct ata_engine	eng;

	struct ata_device	device[ATA_MAX_DEVICES];

	struct ata_queued_cmd	qcmd[ATA_MAX_QUEUE];
	unsigned long		qactive;
	unsigned int		active_tag;

	struct ata_host_stats	stats;
	struct ata_host_set	*host_set;

	struct semaphore	sem;
	struct semaphore	probe_sem;

	unsigned int		thr_state;
	int			time_to_die;
	pid_t			thr_pid;
	struct completion	thr_exited;
	struct semaphore	thr_sem;
	struct timer_list	thr_timer;
	unsigned long		thr_timeout;

	void			*private_data;
};

struct ata_port_operations {
	void (*port_disable) (struct ata_port *);

	void (*dev_config) (struct ata_port *, struct ata_device *);

	void (*set_piomode) (struct ata_port *, struct ata_device *,
			     unsigned int);
	void (*set_udmamode) (struct ata_port *, struct ata_device *,
			     unsigned int);

	void (*tf_load) (struct ata_port *ap, struct ata_taskfile *tf);
	void (*tf_read) (struct ata_port *ap, struct ata_taskfile *tf);

	void (*exec_command)(struct ata_port *ap, struct ata_taskfile *tf);
	u8   (*check_status)(struct ata_port *ap);

	void (*phy_reset) (struct ata_port *ap);
	void (*phy_config) (struct ata_port *ap);

	void (*bmdma_start) (struct ata_queued_cmd *qc);
	void (*fill_sg) (struct ata_queued_cmd *qc);
	void (*eng_timeout) (struct ata_port *ap);

	irqreturn_t (*irq_handler)(int, void *, struct pt_regs *);

	u32 (*scr_read) (struct ata_port *ap, unsigned int sc_reg);
	void (*scr_write) (struct ata_port *ap, unsigned int sc_reg,
			   u32 val);

	int (*port_start) (struct ata_port *ap);
	void (*port_stop) (struct ata_port *ap);

	void (*host_stop) (struct ata_host_set *host_set);
};

struct ata_port_info {
	Scsi_Host_Template	*sht;
	unsigned long		host_flags;
	unsigned long		pio_mask;
	unsigned long		udma_mask;
	struct ata_port_operations	*port_ops;
};

struct pci_bits {
	unsigned int		reg;	/* PCI config register to read */
	unsigned int		width;	/* 1 (8 bit), 2 (16 bit), 4 (32 bit) */
	unsigned long		mask;
	unsigned long		val;
};

extern void ata_port_probe(struct ata_port *);
extern void pata_phy_config(struct ata_port *ap);
extern void sata_phy_reset(struct ata_port *ap);
extern void ata_bus_reset(struct ata_port *ap);
extern void ata_port_disable(struct ata_port *);
extern void ata_std_ports(struct ata_ioports *ioaddr);
extern int ata_pci_init_one (struct pci_dev *pdev, struct ata_port_info **port_info,
			     unsigned int n_ports);
extern void ata_pci_remove_one (struct pci_dev *pdev);
extern int ata_device_add(struct ata_probe_ent *ent);
extern int ata_scsi_detect(Scsi_Host_Template *sht);
extern int ata_scsi_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *));
extern int ata_scsi_error(struct Scsi_Host *host);
extern int ata_scsi_release(struct Scsi_Host *host);
extern int ata_scsi_slave_config(struct scsi_device *sdev);
/*
 * Default driver ops implementations
 */
extern void ata_tf_load_pio(struct ata_port *ap, struct ata_taskfile *tf);
extern void ata_tf_load_mmio(struct ata_port *ap, struct ata_taskfile *tf);
extern void ata_tf_read_pio(struct ata_port *ap, struct ata_taskfile *tf);
extern void ata_tf_read_mmio(struct ata_port *ap, struct ata_taskfile *tf);
extern u8 ata_check_status_pio(struct ata_port *ap);
extern u8 ata_check_status_mmio(struct ata_port *ap);
extern void ata_exec_command_pio(struct ata_port *ap, struct ata_taskfile *tf);
extern void ata_exec_command_mmio(struct ata_port *ap, struct ata_taskfile *tf);
extern int ata_port_start (struct ata_port *ap);
extern void ata_port_stop (struct ata_port *ap);
extern irqreturn_t ata_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
extern void ata_fill_sg(struct ata_queued_cmd *qc);
extern void ata_bmdma_start_mmio (struct ata_queued_cmd *qc);
extern void ata_bmdma_start_pio (struct ata_queued_cmd *qc);
extern int pci_test_config_bits(struct pci_dev *pdev, struct pci_bits *bits);
extern void ata_qc_complete(struct ata_queued_cmd *qc, u8 drv_stat, unsigned int done_late);
extern void ata_eng_timeout(struct ata_port *ap);
extern int ata_std_bios_param(struct scsi_device *sdev,
			      struct block_device *bdev,
			      sector_t capacity, int geom[]);


static inline unsigned long msecs_to_jiffies(unsigned long msecs)
{
	return ((HZ * msecs + 999) / 1000);
}

static inline unsigned int ata_tag_valid(unsigned int tag)
{
	return (tag < ATA_MAX_QUEUE) ? 1 : 0;
}

static inline unsigned int ata_dev_present(struct ata_device *dev)
{
	return ((dev->class == ATA_DEV_ATA) ||
		(dev->class == ATA_DEV_ATAPI));
}

static inline u8 ata_chk_err(struct ata_port *ap)
{
	if (ap->flags & ATA_FLAG_MMIO) {
		return readb((void *) ap->ioaddr.error_addr);
	}
	return inb(ap->ioaddr.error_addr);
}

static inline u8 ata_chk_status(struct ata_port *ap)
{
	return ap->ops->check_status(ap);
}

static inline u8 ata_altstatus(struct ata_port *ap)
{
	if (ap->flags & ATA_FLAG_MMIO)
		return readb(ap->ioaddr.ctl_addr);
	return inb(ap->ioaddr.ctl_addr);
}

static inline void ata_pause(struct ata_port *ap)
{
	ata_altstatus(ap);
	ndelay(400);
}

static inline u8 ata_busy_wait(struct ata_port *ap, unsigned int bits,
			       unsigned int max)
{
	u8 status;

	do {
		udelay(10);
		status = ata_chk_status(ap);
		max--;
	} while ((status & bits) && (max > 0));

	return status;
}

static inline u8 ata_wait_idle(struct ata_port *ap)
{
	u8 status = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

	if (status & (ATA_BUSY | ATA_DRQ)) {
		unsigned long l = ap->ioaddr.cmdstat_addr;
		printk(KERN_WARNING
		       "ATA: abnormal status 0x%X on port 0x%lX\n",
		       status, l);
	}

	return status;
}

static inline struct ata_queued_cmd *ata_qc_from_tag (struct ata_port *ap,
						      unsigned int tag)
{
	if (likely(ata_tag_valid(tag)))
		return &ap->qcmd[tag];
	return NULL;
}

static inline void ata_tf_init(struct ata_port *ap, struct ata_taskfile *tf, unsigned int device)
{
	memset(tf, 0, sizeof(*tf));

	tf->ctl = ap->ctl;
	if (device == 0)
		tf->device = ATA_DEVICE_OBS;
	else
		tf->device = ATA_DEVICE_OBS | ATA_DEV1;
}

static inline u8 ata_irq_on(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	ap->ctl &= ~ATA_NIEN;
	ap->last_ctl = ap->ctl;

	if (ap->flags & ATA_FLAG_MMIO)
		writeb(ap->ctl, ioaddr->ctl_addr);
	else
		outb(ap->ctl, ioaddr->ctl_addr);
	return ata_wait_idle(ap);
}

static inline u8 ata_irq_ack(struct ata_port *ap, unsigned int chk_drq)
{
	unsigned int bits = chk_drq ? ATA_BUSY | ATA_DRQ : ATA_BUSY;
	u8 host_stat, post_stat, status;

	status = ata_busy_wait(ap, bits, 1000);
	if (status & bits)
		DPRINTK("abnormal status 0x%X\n", status);

	/* get controller status; clear intr, err bits */
	if (ap->flags & ATA_FLAG_MMIO) {
		void *mmio = (void *) ap->ioaddr.bmdma_addr;
		host_stat = readb(mmio + ATA_DMA_STATUS);
		writeb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
		       mmio + ATA_DMA_STATUS);

		post_stat = readb(mmio + ATA_DMA_STATUS);
	} else {
		host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
		outb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
		     ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

		post_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	}

	VPRINTK("irq ack: host_stat 0x%X, new host_stat 0x%X, drv_stat 0x%X\n",
		host_stat, post_stat, status);

	return status;
}

static inline u32 scr_read(struct ata_port *ap, unsigned int reg)
{
	return ap->ops->scr_read(ap, reg);
}

static inline void scr_write(struct ata_port *ap, unsigned int reg, u32 val)
{
	ap->ops->scr_write(ap, reg, val);
}

static inline unsigned int sata_dev_present(struct ata_port *ap)
{
	return ((scr_read(ap, SCR_STATUS) & 0xf) == 0x3) ? 1 : 0;
}

#endif /* __LINUX_LIBATA_H__ */
