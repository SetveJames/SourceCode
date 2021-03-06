/* -*- linux-c -*-
 *  drivers/cdrom/viocd.c
 *
 *  iSeries Virtual CD Rom
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *           Stephen Rothwell <sfr@au1.ibm.com>
 *
 * (C) Copyright 2000-2004 IBM Corporation
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * This routine provides access to CD ROM drives owned and managed by an
 * OS/400 partition running on the same box as this Linux partition.
 *
 * All operations are performed by sending messages back and forth to
 * the OS/400 partition.
 */

#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/completion.h>

#include <asm/bug.h>

#include <asm/scatterlist.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/vio.h>

#define VIOCD_DEVICE			"iseries/vcd"
#define VIOCD_DEVICE_DEVFS		"iseries/vcd"

#define VIOCD_VERS "1.06"

#define VIOCD_KERN_WARNING		KERN_WARNING "viocd: "
#define VIOCD_KERN_INFO			KERN_INFO "viocd: "

struct viocdlpevent {
	struct HvLpEvent	event;
	u32			reserved;
	u16			version;
	u16			sub_result;
	u16			disk;
	u16			flags;
	u32			token;
	u64			offset;		/* On open, max number of disks */
	u64			len;		/* On open, size of the disk */
	u32			block_size;	/* Only set on open */
	u32			media_size;	/* Only set on open */
};

enum viocdsubtype {
	viocdopen = 0x0001,
	viocdclose = 0x0002,
	viocdread = 0x0003,
	viocdwrite = 0x0004,
	viocdlockdoor = 0x0005,
	viocdgetinfo = 0x0006,
	viocdcheck = 0x0007
};

/*
 * Should probably make this a module parameter....sigh
 */
#define VIOCD_MAX_CD 8

static const struct vio_error_entry viocd_err_table[] = {
	{0x0201, EINVAL, "Invalid Range"},
	{0x0202, EINVAL, "Invalid Token"},
	{0x0203, EIO, "DMA Error"},
	{0x0204, EIO, "Use Error"},
	{0x0205, EIO, "Release Error"},
	{0x0206, EINVAL, "Invalid CD"},
	{0x020C, EROFS, "Read Only Device"},
	{0x020D, ENOMEDIUM, "Changed or Missing Volume (or Varied Off?)"},
	{0x020E, EIO, "Optical System Error (Varied Off?)"},
	{0x02FF, EIO, "Internal Error"},
	{0x3010, EIO, "Changed Volume"},
	{0xC100, EIO, "Optical System Error"},
	{0x0000, 0, NULL},
};

/*
 * This is the structure we use to exchange info between driver and interrupt
 * handler
 */
struct viocd_waitevent {
	struct completion	com;
	int			rc;
	u16			sub_result;
	int			changed;
};

/* this is a lookup table for the true capabilities of a device */
struct capability_entry {
	char	*type;
	int	capability;
};

static struct capability_entry capability_table[] __initdata = {
	{ "6330", CDC_LOCK | CDC_DVD_RAM },
	{ "6321", CDC_LOCK },
	{ "632B", 0 },
	{ NULL  , CDC_LOCK },
};

/* These are our internal structures for keeping track of devices */
static int viocd_numdev;

struct cdrom_info {
	char	rsrcname[10];
	char	type[4];
	char	model[3];
};
static struct cdrom_info viocd_unitinfo[VIOCD_MAX_CD];

struct disk_info {
	struct gendisk			*viocd_disk;
	struct cdrom_device_info	viocd_info;
};
static struct disk_info viocd_diskinfo[VIOCD_MAX_CD];

#define DEVICE_NR(di)	((di) - &viocd_diskinfo[0])
#define VIOCDI		viocd_diskinfo[deviceno].viocd_info

static request_queue_t *viocd_queue;
static spinlock_t viocd_reqlock;

#define MAX_CD_REQ	1

static int viocd_blk_open(struct inode *inode, struct file *file)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_open(&di->viocd_info, inode, file);
}

static int viocd_blk_release(struct inode *inode, struct file *file)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_release(&di->viocd_info, file);
}

static int viocd_blk_ioctl(struct inode *inode, struct file *file,
		unsigned cmd, unsigned long arg)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_ioctl(&di->viocd_info, inode, cmd, arg);
}

static int viocd_blk_media_changed(struct gendisk *disk)
{
	struct disk_info *di = disk->private_data;
	return cdrom_media_changed(&di->viocd_info);
}

struct block_device_operations viocd_fops = {
	.owner =		THIS_MODULE,
	.open =			viocd_blk_open,
	.release =		viocd_blk_release,
	.ioctl =		viocd_blk_ioctl,
	.media_changed =	viocd_blk_media_changed,
};

/* Get info on CD devices from OS/400 */
static void __init get_viocd_info(void)
{
	dma_addr_t dmaaddr;
	HvLpEvent_Rc hvrc;
	int i;
	struct viocd_waitevent we;

	dmaaddr = dma_map_single(iSeries_vio_dev, viocd_unitinfo,
			sizeof(viocd_unitinfo), DMA_FROM_DEVICE);
	if (dmaaddr == (dma_addr_t)-1) {
		printk(VIOCD_KERN_WARNING "error allocating tce\n");
		return;
	}

	init_completion(&we.com);

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdgetinfo,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, dmaaddr, 0,
			sizeof(viocd_unitinfo), 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(VIOCD_KERN_WARNING "cdrom error sending event. rc %d\n",
				(int)hvrc);
		return;
	}

	wait_for_completion(&we.com);

	dma_unmap_single(iSeries_vio_dev, dmaaddr, sizeof(viocd_unitinfo),
			DMA_FROM_DEVICE);

	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.sub_result);
		printk(VIOCD_KERN_WARNING "bad rc %d:0x%04X on getinfo: %s\n",
				we.rc, we.sub_result, err->msg);
		return;
	}

	for (i = 0; (i < VIOCD_MAX_CD) && viocd_unitinfo[i].rsrcname[0]; i++)
		viocd_numdev++;
}

static int viocd_open(struct cdrom_device_info *cdi, int purpose)
{
        struct disk_info *diskinfo = cdi->handle;
	int device_no = DEVICE_NR(diskinfo);
	HvLpEvent_Rc hvrc;
	struct viocd_waitevent we;

	init_completion(&we.com);
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, ((u64)device_no << 48),
			0, 0, 0);
	if (hvrc != 0) {
		printk(VIOCD_KERN_WARNING
				"bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	wait_for_completion(&we.com);

	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.sub_result);
		printk(VIOCD_KERN_WARNING "bad rc %d:0x%04X on open: %s\n",
				we.rc, we.sub_result, err->msg);
		return -err->errno;
	}

	return 0;
}

static void viocd_release(struct cdrom_device_info *cdi)
{
	int device_no = DEVICE_NR((struct disk_info *)cdi->handle);
	HvLpEvent_Rc hvrc;

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdclose,
			HvLpEvent_AckInd_NoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp), 0,
			VIOVERSION << 16, ((u64)device_no << 48), 0, 0, 0);
	if (hvrc != 0)
		printk(VIOCD_KERN_WARNING
				"bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
}

/* Send a read or write request to OS/400 */
static int send_request(struct request *req)
{
	HvLpEvent_Rc hvrc;
	struct disk_info *diskinfo = req->rq_disk->private_data;
	u64 len;
	dma_addr_t dmaaddr;
	struct scatterlist sg;

	BUG_ON(req->nr_phys_segments > 1);
	BUG_ON(rq_data_dir(req) != READ);

        if (blk_rq_map_sg(req->q, req, &sg) == 0) {
		printk(VIOCD_KERN_WARNING
				"error setting up scatter/gather list\n");
		return -1;
	}

	if (dma_map_sg(iSeries_vio_dev, &sg, 1, DMA_FROM_DEVICE) == 0) {
		printk(VIOCD_KERN_WARNING "error allocating sg tce\n");
		return -1;
	}
	dmaaddr = sg_dma_address(&sg);
	len = sg_dma_len(&sg);
	if (dmaaddr == (dma_addr_t)-1) {
		printk(VIOCD_KERN_WARNING "error allocating tce\n");
		return -1;
	}

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdread,
			HvLpEvent_AckInd_DoAck,
			HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)req, VIOVERSION << 16,
			((u64)DEVICE_NR(diskinfo) << 48) | dmaaddr,
			(u64)req->sector * 512, len, 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(VIOCD_KERN_WARNING "hv error on op %d\n", (int)hvrc);
		return -1;
	}

	return 0;
}


static int rwreq;

static void do_viocd_request(request_queue_t *q)
{
	struct request *req;

	while ((rwreq == 0) && ((req = elv_next_request(q)) != NULL)) {
		/* check for any kind of error */
		if (send_request(req) < 0) {
			printk(VIOCD_KERN_WARNING
					"unable to send message to OS/400!");
			end_request(req, 0);
		} else
			rwreq++;
	}
}

static int viocd_media_changed(struct cdrom_device_info *cdi, int disc_nr)
{
	struct viocd_waitevent we;
	HvLpEvent_Rc hvrc;
	int device_no = DEVICE_NR((struct disk_info *)cdi->handle);

	init_completion(&we.com);

	/* Send the open event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdcheck,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, ((u64)device_no << 48),
			0, 0, 0);
	if (hvrc != 0) {
		printk(VIOCD_KERN_WARNING "bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	wait_for_completion(&we.com);

	/* Check the return code.  If bad, assume no change */
	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.sub_result);
		printk(VIOCD_KERN_WARNING
				"bad rc %d:0x%04X on check_change: %s; Assuming no change\n",
				we.rc, we.sub_result, err->msg);
		return 0;
	}

	return we.changed;
}

static int viocd_lock_door(struct cdrom_device_info *cdi, int locking)
{
	HvLpEvent_Rc hvrc;
	u64 device_no = DEVICE_NR((struct disk_info *)cdi->handle);
	/* NOTE: flags is 1 or 0 so it won't overwrite the device_no */
	u64 flags = !!locking;
	struct viocd_waitevent we;

	init_completion(&we.com);

	/* Send the lockdoor event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdlockdoor,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16,
			(device_no << 48) | (flags << 32), 0, 0, 0);
	if (hvrc != 0) {
		printk(VIOCD_KERN_WARNING "bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	wait_for_completion(&we.com);

	if (we.rc != 0)
		return -EIO;
	return 0;
}

/* This routine handles incoming CD LP events */
static void vio_handle_cd_event(struct HvLpEvent *event)
{
	struct viocdlpevent *bevent;
	struct viocd_waitevent *pwe;
	struct disk_info *di;
	unsigned long flags;
	struct request *req;


	if (event == NULL)
		/* Notification that a partition went away! */
		return;
	/* First, we should NEVER get an int here...only acks */
	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		printk(VIOCD_KERN_WARNING
				"Yikes! got an int in viocd event handler!\n");
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}

	bevent = (struct viocdlpevent *)event;

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case viocdopen:
		if (event->xRc == 0) {
			di = &viocd_diskinfo[bevent->disk];
			blk_queue_hardsect_size(viocd_queue,
					bevent->block_size);
			set_capacity(di->viocd_disk,
					bevent->media_size *
					bevent->block_size / 512);
		}
		/* FALLTHROUGH !! */
	case viocdgetinfo:
	case viocdlockdoor:
		pwe = (struct viocd_waitevent *)event->xCorrelationToken;
return_complete:
		pwe->rc = event->xRc;
		pwe->sub_result = bevent->sub_result;
		complete(&pwe->com);
		break;

	case viocdcheck:
		pwe = (struct viocd_waitevent *)event->xCorrelationToken;
		pwe->changed = bevent->flags;
		goto return_complete;

	case viocdclose:
		break;

	case viocdread:
		/*
		 * Since this is running in interrupt mode, we need to
		 * make sure we're not stepping on any global I/O operations
		 */
		spin_lock_irqsave(&viocd_reqlock, flags);
		dma_unmap_single(iSeries_vio_dev, bevent->token, bevent->len,
				DMA_FROM_DEVICE);
		req = (struct request *)bevent->event.xCorrelationToken;
		rwreq--;

		if (event->xRc != HvLpEvent_Rc_Good) {
			const struct vio_error_entry *err =
				vio_lookup_rc(viocd_err_table,
						bevent->sub_result);
			printk(VIOCD_KERN_WARNING "request %p failed "
					"with rc %d:0x%04X: %s\n",
					req, event->xRc,
					bevent->sub_result, err->msg);
			end_request(req, 0);
		} else
			end_request(req, 1);

		/* restart handling of incoming requests */
		spin_unlock_irqrestore(&viocd_reqlock, flags);
		blk_run_queue(viocd_queue);
		break;

	default:
		printk(VIOCD_KERN_WARNING
				"message with invalid subtype %0x04X!\n",
				event->xSubtype & VIOMINOR_SUBTYPE_MASK);
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static struct cdrom_device_ops viocd_dops = {
	.open = viocd_open,
	.release = viocd_release,
	.media_changed = viocd_media_changed,
	.lock_door = viocd_lock_door,
	.capability = CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED | CDC_SELECT_DISC | CDC_MULTI_SESSION | CDC_MCN | CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO | CDC_RESET | CDC_IOCTLS | CDC_DRIVE_STATUS | CDC_GENERIC_PACKET | CDC_CD_R | CDC_CD_RW | CDC_DVD | CDC_DVD_R | CDC_DVD_RAM
};

static int __init find_capability(const char *type)
{
	struct capability_entry *entry;

	for(entry = capability_table; entry->type; ++entry)
		if(!strncmp(entry->type, type, 4))
			break;
	return entry->capability;
}

static int __init viocd_init(void)
{
	struct gendisk *gendisk;
	int deviceno;
	int ret = 0;

	if (viopath_hostLp == HvLpIndexInvalid) {
		vio_set_hostlp();
		/* If we don't have a host, bail out */
		if (viopath_hostLp == HvLpIndexInvalid)
			return -ENODEV;
	}

	printk(VIOCD_KERN_INFO "vers " VIOCD_VERS ", hosting partition %d\n",
			viopath_hostLp);

	if (register_blkdev(VIOCD_MAJOR, VIOCD_DEVICE) != 0) {
		printk(VIOCD_KERN_WARNING
				"Unable to get major %d for %s\n",
				VIOCD_MAJOR, VIOCD_DEVICE);
		return -EIO;
	}

	ret = viopath_open(viopath_hostLp, viomajorsubtype_cdio,
			MAX_CD_REQ + 2);
	if (ret) {
		printk(VIOCD_KERN_WARNING
				"error opening path to host partition %d\n",
				viopath_hostLp);
		goto out_unregister;
	}

	/* Initialize our request handler */
	vio_setHandler(viomajorsubtype_cdio, vio_handle_cd_event);

	get_viocd_info();
	if (viocd_numdev == 0)
		goto out_undo_vio;

	ret = -ENOMEM;
	spin_lock_init(&viocd_reqlock);
	viocd_queue = blk_init_queue(do_viocd_request, &viocd_reqlock);
	if (viocd_queue == NULL)
		goto out_unregister;
	blk_queue_max_hw_segments(viocd_queue, 1);
	blk_queue_max_phys_segments(viocd_queue, 1);
	blk_queue_max_sectors(viocd_queue, 4096 / 512);

	/* initialize units */
	for (deviceno = 0; deviceno < viocd_numdev; deviceno++) {
		struct disk_info *d = &viocd_diskinfo[deviceno];
		struct cdrom_device_info *c = &d->viocd_info;
		struct cdrom_info *ci = &viocd_unitinfo[deviceno];

		c->ops = &viocd_dops;
		c->speed = 4;
		c->capacity = 1;
		c->handle = d;
		c->mask = ~find_capability(ci->type);
		sprintf(c->name, VIOCD_DEVICE "%c", 'a' + deviceno);

		if (register_cdrom(c) != 0) {
			printk(VIOCD_KERN_WARNING
					"Cannot register viocd CD-ROM %s!\n",
					c->name);
			continue;
		}
		printk(VIOCD_KERN_INFO "cd %s is iSeries resource %10.10s "
				"type %4.4s, model %3.3s\n",
				c->name, ci->rsrcname, ci->type, ci->model);
		gendisk = alloc_disk(1);
		if (gendisk == NULL) {
			printk(VIOCD_KERN_WARNING
					"Cannot create gendisk for %s!\n",
					c->name);
			unregister_cdrom(&VIOCDI);
			continue;
		}
		gendisk->major = VIOCD_MAJOR;
		gendisk->first_minor = deviceno;
		strncpy(gendisk->disk_name, c->name,
				sizeof(gendisk->disk_name));
		snprintf(gendisk->devfs_name, sizeof(gendisk->devfs_name),
				VIOCD_DEVICE_DEVFS "%d", deviceno);
		gendisk->queue = viocd_queue;
		gendisk->fops = &viocd_fops;
		gendisk->flags = GENHD_FL_CD;
		set_capacity(gendisk, 0);
		gendisk->private_data = d;
		d->viocd_disk = gendisk;
		add_disk(gendisk);
	}

	return 0;

out_undo_vio:
	vio_clearHandler(viomajorsubtype_cdio);
	viopath_close(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ + 2);
out_unregister:
	unregister_blkdev(VIOCD_MAJOR, VIOCD_DEVICE);
	return ret;
}

static void __exit viocd_exit(void)
{
	int deviceno;

	for (deviceno = 0; deviceno < viocd_numdev; deviceno++) {
		struct disk_info *d = &viocd_diskinfo[deviceno];
		if (unregister_cdrom(&d->viocd_info) != 0)
			printk(VIOCD_KERN_WARNING
					"Cannot unregister viocd CD-ROM %s!\n",
					d->viocd_info.name);
		del_gendisk(d->viocd_disk);
		put_disk(d->viocd_disk);
	}
	blk_cleanup_queue(viocd_queue);

	viopath_close(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ + 2);
	vio_clearHandler(viomajorsubtype_cdio);
	unregister_blkdev(VIOCD_MAJOR, VIOCD_DEVICE);
}

module_init(viocd_init);
module_exit(viocd_exit);
MODULE_LICENSE("GPL");
