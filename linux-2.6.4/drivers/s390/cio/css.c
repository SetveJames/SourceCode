/*
 *  drivers/s390/cio/css.c
 *  driver for channel subsystem
 *   $Revision: 1.69 $
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>

#include "css.h"
#include "cio.h"
#include "cio_debug.h"
#include "ioasm.h"

unsigned int highest_subchannel;
int need_rescan = 0;
int css_init_done = 0;

struct device css_bus_device = {
	.bus_id = "css0",
};

static struct subchannel *
css_alloc_subchannel(int irq)
{
	struct subchannel *sch;
	int ret;

	sch = kmalloc (sizeof (*sch), GFP_KERNEL | GFP_DMA);
	if (sch == NULL)
		return ERR_PTR(-ENOMEM);
	ret = cio_validate_subchannel (sch, irq);
	if (ret < 0) {
		kfree(sch);
		return ERR_PTR(ret);
	}
	if (irq > highest_subchannel)
		highest_subchannel = irq;

	if (sch->st != SUBCHANNEL_TYPE_IO) {
		/* For now we ignore all non-io subchannels. */
		kfree(sch);
		return ERR_PTR(-EINVAL);
	}

	/* 
	 * Set intparm to subchannel address.
	 * This is fine even on 64bit since the subchannel is always located
	 * under 2G.
	 */
	sch->schib.pmcw.intparm = (__u32)(unsigned long)sch;
	ret = cio_modify(sch);
	if (ret) {
		kfree(sch);
		return ERR_PTR(ret);
	}
	return sch;
}

static void
css_free_subchannel(struct subchannel *sch)
{
	if (sch) {
		/* Reset intparm to zeroes. */
		sch->schib.pmcw.intparm = 0;
		cio_modify(sch);
		kfree(sch);
	}
	
}

static void
css_subchannel_release(struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	if (!cio_is_console(sch->irq))
		kfree(sch);
}

extern int css_get_ssd_info(struct subchannel *sch);

static int
css_register_subchannel(struct subchannel *sch)
{
	int ret;

	/* Initialize the subchannel structure */
	sch->dev.parent = &css_bus_device;
	sch->dev.bus = &css_bus_type;
	sch->dev.release = &css_subchannel_release;
	
	/* make it known to the system */
	ret = device_register(&sch->dev);
	if (ret)
		printk (KERN_WARNING "%s: could not register %s\n",
			__func__, sch->dev.bus_id);
	else
		css_get_ssd_info(sch);
	return ret;
}

int
css_probe_device(int irq)
{
	int ret;
	struct subchannel *sch;

	sch = css_alloc_subchannel(irq);
	if (IS_ERR(sch))
		return PTR_ERR(sch);
	ret = css_register_subchannel(sch);
	if (ret)
		css_free_subchannel(sch);
	return ret;
}

static struct subchannel *
__get_subchannel_by_stsch(int irq)
{
	struct subchannel *sch;
	int cc;
	struct schib schib;

	cc = stsch(irq, &schib);
	if (cc || !schib.pmcw.dnv)
		return NULL;
	sch = (struct subchannel *)(unsigned long)schib.pmcw.intparm;
	if (!sch)
		return NULL;
	if (get_device(&sch->dev))
		return sch;
	return NULL;
}

struct subchannel *
get_subchannel_by_schid(int irq)
{
	struct subchannel *sch;
	struct list_head *entry;
	struct device *dev;

	if (!get_bus(&css_bus_type))
		return NULL;

	/* Try to get subchannel from pmcw first. */ 
	sch = __get_subchannel_by_stsch(irq);
	if (sch)
		goto out;
	down_read(&css_bus_type.subsys.rwsem);

	list_for_each(entry, &css_bus_type.devices.list) {
		dev = get_device(container_of(entry,
					      struct device, bus_list));
		if (!dev)
			continue;
		/* Skip channel paths. */
		if (dev->release != &css_subchannel_release) {
			put_device(dev);
			continue;
		}
		sch = to_subchannel(dev);
		if (sch->irq == irq)
			break;
		put_device(dev);
		sch = NULL;
	}
	up_read(&css_bus_type.subsys.rwsem);
out:
	put_bus(&css_bus_type);

	return sch;
}

static inline int
css_get_subchannel_status(struct subchannel *sch, int schid)
{
	struct schib schib;
	int cc;

	cc = stsch(schid, &schib);
	if (cc)
		return CIO_GONE;
	if (!schib.pmcw.dnv)
		return CIO_GONE;
	if (sch && sch->schib.pmcw.dnv &&
	    (schib.pmcw.dev != sch->schib.pmcw.dev))
		return CIO_REVALIDATE;
	return CIO_OPER;
}
	
static inline int
css_evaluate_subchannel(int irq, int slow)
{
	int event, ret, disc;
	struct subchannel *sch;

	sch = get_subchannel_by_schid(irq);
	disc = sch ? device_is_disconnected(sch) : 0;
	if (disc && slow)
		return 0; /* Already processed. */
	if (!disc && !slow)
		return -EAGAIN; /* Will be done on the slow path. */
	event = css_get_subchannel_status(sch, irq);
	switch (event) {
	case CIO_GONE:
		if (!sch) {
			/* Never used this subchannel. Ignore. */
			ret = 0;
			break;
		}
		if (sch->driver && sch->driver->notify &&
		    sch->driver->notify(&sch->dev, CIO_GONE)) {
			device_set_disconnected(sch);
			ret = 0;
			break;
		}
		/*
		 * Unregister subchannel.
		 * The device will be killed automatically.
		 */
		device_unregister(&sch->dev);
		/* Reset intparm to zeroes. */
		sch->schib.pmcw.intparm = 0;
		cio_modify(sch);
		put_device(&sch->dev);
		ret = 0;
		break;
	case CIO_REVALIDATE:
		/* 
		 * Revalidation machine check. Sick.
		 * We don't notify the driver since we have to throw the device
		 * away in any case.
		 */
		device_unregister(&sch->dev);
		/* Reset intparm to zeroes. */
		sch->schib.pmcw.intparm = 0;
		cio_modify(sch);
		put_device(&sch->dev);
		ret = css_probe_device(irq);
		break;
	case CIO_OPER:
		if (disc)
			/* Get device operational again. */
			device_trigger_reprobe(sch);
		ret = sch ? 0 : css_probe_device(irq);
		break;
	default:
		BUG();
		ret = 0;
	}
	return ret;
}

static void
css_rescan_devices(void)
{
	int irq, ret;

	for (irq = 0; irq <= __MAX_SUBCHANNELS; irq++) {
		ret = css_evaluate_subchannel(irq, 1);
		/* No more memory. It doesn't make sense to continue. No
		 * panic because this can happen in midflight and just
		 * because we can't use a new device is no reason to crash
		 * the system. */
		if (ret == -ENOMEM)
			break;
		/* -ENXIO indicates that there are no more subchannels. */
		if (ret == -ENXIO)
			break;
	}
}

static void
css_evaluate_slow_subchannel(unsigned long schid)
{
	css_evaluate_subchannel(schid, 1);
}

void
css_trigger_slow_path(void)
{
	if (need_rescan) {
		need_rescan = 0;
		css_rescan_devices();
		return;
	}
	css_walk_subchannel_slow_list(css_evaluate_slow_subchannel);
}

/*
 * Rescan for new devices. FIXME: This is slow.
 * This function is called when we have lost CRWs due to overflows and we have
 * to do subchannel housekeeping.
 */
void
css_reiterate_subchannels(void)
{
	css_clear_subchannel_slow_list();
	need_rescan = 1;
}

/*
 * Called from the machine check handler for subchannel report words.
 */
int
css_process_crw(int irq)
{
	int ret;

	CIO_CRW_EVENT(2, "source is subchannel %04X\n", irq);

	if (need_rescan)
		/* We need to iterate all subchannels anyway. */
		return -EAGAIN;
	/* 
	 * Since we are always presented with IPI in the CRW, we have to
	 * use stsch() to find out if the subchannel in question has come
	 * or gone.
	 */
	ret = css_evaluate_subchannel(irq, 0);
	if (ret == -EAGAIN) {
		if (css_enqueue_subchannel_slow(irq)) {
			css_clear_subchannel_slow_list();
			need_rescan = 1;
		}
	}
	return ret;
}

/*
 * some of the initialization has already been done from init_IRQ(),
 * here we do the rest now that the driver core is running.
 * The struct subchannel's are created during probing (except for the
 * static console subchannel).
 */
static int __init
init_channel_subsystem (void)
{
	int ret, irq;

	if ((ret = bus_register(&css_bus_type)))
		goto out;
	if ((ret = device_register (&css_bus_device)))
		goto out_bus;

	css_init_done = 1;

	ctl_set_bit(6, 28);

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		struct subchannel *sch;

		if (cio_is_console(irq))
			sch = cio_get_console_subchannel();
		else {
			sch = css_alloc_subchannel(irq);
			if (IS_ERR(sch))
				ret = PTR_ERR(sch);
			else
				ret = 0;
			if (ret == -ENOMEM)
				panic("Out of memory in "
				      "init_channel_subsystem\n");
			/* -ENXIO: no more subchannels. */
			if (ret == -ENXIO)
				break;
			if (ret)
				continue;
		}
		/*
		 * We register ALL valid subchannels in ioinfo, even those
		 * that have been present before init_channel_subsystem.
		 * These subchannels can't have been registered yet (kmalloc
		 * not working) so we do it now. This is true e.g. for the
		 * console subchannel.
		 */
		css_register_subchannel(sch);
	}
	return 0;

out_bus:
	bus_unregister(&css_bus_type);
out:
	return ret;
}

/*
 * find a driver for a subchannel. They identify by the subchannel
 * type with the exception that the console subchannel driver has its own
 * subchannel type although the device is an i/o subchannel
 */
static int
css_bus_match (struct device *dev, struct device_driver *drv)
{
	struct subchannel *sch = container_of (dev, struct subchannel, dev);
	struct css_driver *driver = container_of (drv, struct css_driver, drv);

	if (sch->st == driver->subchannel_type)
		return 1;

	return 0;
}

struct bus_type css_bus_type = {
	.name  = "css",
	.match = &css_bus_match,
};

subsys_initcall(init_channel_subsystem);

/*
 * Register root devices for some drivers. The release function must not be
 * in the device drivers, so we do it here.
 */
static void
s390_root_dev_release(struct device *dev)
{
	kfree(dev);
}

struct device *
s390_root_dev_register(const char *name)
{
	struct device *dev;
	int ret;

	if (!strlen(name))
		return ERR_PTR(-EINVAL);
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	memset(dev, 0, sizeof(struct device));
	strncpy(dev->bus_id, name, min(strlen(name), (size_t)BUS_ID_SIZE));
	dev->release = s390_root_dev_release;
	ret = device_register(dev);
	if (ret) {
		kfree(dev);
		return ERR_PTR(ret);
	}
	return dev;
}

void
s390_root_dev_unregister(struct device *dev)
{
	if (dev)
		device_unregister(dev);
}

struct slow_subchannel {
	struct list_head slow_list;
	unsigned long schid;
};

static LIST_HEAD(slow_subchannels_head);
static spinlock_t slow_subchannel_lock = SPIN_LOCK_UNLOCKED;

int
css_enqueue_subchannel_slow(unsigned long schid)
{
	struct slow_subchannel *new_slow_sch;
	unsigned long flags;

	new_slow_sch = kmalloc(sizeof(struct slow_subchannel), GFP_ATOMIC);
	if (!new_slow_sch)
		return -ENOMEM;
	new_slow_sch->schid = schid;
	spin_lock_irqsave(&slow_subchannel_lock, flags);
	list_add_tail(&new_slow_sch->slow_list, &slow_subchannels_head);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
	return 0;
}

void
css_clear_subchannel_slow_list(void)
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	while (!list_empty(&slow_subchannels_head)) {
		struct slow_subchannel *slow_sch =
			list_entry(slow_subchannels_head.next,
				   struct slow_subchannel, slow_list);

		list_del_init(slow_subchannels_head.next);
		kfree(slow_sch);
	}
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

void
css_walk_subchannel_slow_list(void (*fn)(unsigned long))
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	while (!list_empty(&slow_subchannels_head)) {
		struct slow_subchannel *slow_sch =
			list_entry(slow_subchannels_head.next,
				   struct slow_subchannel, slow_list);

		list_del_init(slow_subchannels_head.next);
		spin_unlock_irqrestore(&slow_subchannel_lock, flags);
		fn(slow_sch->schid);
		spin_lock_irqsave(&slow_subchannel_lock, flags);
		kfree(slow_sch);
	}
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

int
css_slow_subchannels_exist(void)
{
	return (!list_empty(&slow_subchannels_head));
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(css_bus_type);
EXPORT_SYMBOL(s390_root_dev_register);
EXPORT_SYMBOL(s390_root_dev_unregister);
