/*
 * arch/s390/appldata/appldata_base.c
 *
 * Base infrastructure for Linux-z/VM Monitor Stream, Stage 1.
 * Exports appldata_register_ops() and appldata_unregister_ops() for the
 * data gathering modules.
 *
 * Copyright (C) 2003 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <geraldsc@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/page-flags.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>
#include <asm/timer.h>
//#include <linux/kernel_stat.h>

#include "appldata.h"


#define MY_PRINT_NAME	"appldata"		/* for debug messages, etc. */
#define APPLDATA_CPU_INTERVAL	10000		/* default (CPU) time for
						   sampling interval in
						   milliseconds */

#define TOD_MICRO	0x01000			/* nr. of TOD clock units
						   for 1 microsecond */
#ifndef CONFIG_ARCH_S390X

#define APPLDATA_START_INTERVAL_REC 0x00   	/* Function codes for */
#define APPLDATA_STOP_REC	    0x01	/* DIAG 0xDC	  */
#define APPLDATA_GEN_EVENT_RECORD   0x02
#define APPLDATA_START_CONFIG_REC   0x03

#else

#define APPLDATA_START_INTERVAL_REC 0x80
#define APPLDATA_STOP_REC   	    0x81
#define APPLDATA_GEN_EVENT_RECORD   0x82
#define APPLDATA_START_CONFIG_REC   0x83

#endif /* CONFIG_ARCH_S390X */


/*
 * Parameter list for DIAGNOSE X'DC'
 */
#ifndef CONFIG_ARCH_S390X
struct appldata_parameter_list {
	u16 diag;		/* The DIAGNOSE code X'00DC'          */
	u8  function;		/* The function code for the DIAGNOSE */
	u8  parlist_length;	/* Length of the parameter list       */
	u32 product_id_addr;	/* Address of the 16-byte product ID  */
	u16 reserved;
	u16 buffer_length;	/* Length of the application data buffer  */
	u32 buffer_addr;	/* Address of the application data buffer */
};
#else
struct appldata_parameter_list {
	u16 diag;
	u8  function;
	u8  parlist_length;
	u32 unused01;
	u16 reserved;
	u16 buffer_length;
	u32 unused02;
	u64 product_id_addr;
	u64 buffer_addr;
};
#endif /* CONFIG_ARCH_S390X */

/*
 * /proc entries (sysctl)
 */
static const char appldata_proc_name[APPLDATA_PROC_NAME_LENGTH] = "appldata";
static int appldata_timer_handler(ctl_table *ctl, int write, struct file *filp,
		   		  void *buffer, size_t *lenp);
static int appldata_interval_handler(ctl_table *ctl, int write,
					 struct file *filp, void *buffer,
					 size_t *lenp);

static struct ctl_table_header *appldata_sysctl_header;
static struct ctl_table appldata_table[] = {
	{
		.ctl_name	= CTL_APPLDATA_TIMER,
		.procname	= "timer",
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= &appldata_timer_handler,
	},
	{
		.ctl_name	= CTL_APPLDATA_INTERVAL,
		.procname	= "interval",
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= &appldata_interval_handler,
	},
	{ .ctl_name = 0 }
};

static struct ctl_table appldata_dir_table[] = {
	{
		.ctl_name	= CTL_APPLDATA,
		.procname	= appldata_proc_name,
		.maxlen		= 0,
		.mode		= S_IRUGO | S_IXUGO,
		.child		= appldata_table,
	},
	{ .ctl_name = 0 }
};

/*
 * Timer
 */
DEFINE_PER_CPU(struct vtimer_list, appldata_timer);
static atomic_t appldata_expire_count = ATOMIC_INIT(0);
static struct appldata_mod_vtimer_args {
	struct vtimer_list *timer;
	u64    expires;
} appldata_mod_vtimer_args;

static spinlock_t appldata_timer_lock = SPIN_LOCK_UNLOCKED;
static int appldata_interval = APPLDATA_CPU_INTERVAL;
static int appldata_timer_active;

/*
 * Tasklet
 */
static struct tasklet_struct appldata_tasklet_struct;

/*
 * Ops list
 */
static spinlock_t appldata_ops_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(appldata_ops_list);


/************************* timer, tasklet, DIAG ******************************/
/*
 * appldata_timer_function()
 *
 * schedule tasklet and reschedule timer
 */
static void appldata_timer_function(unsigned long data, struct pt_regs *regs)
{
	P_DEBUG("   -= Timer =-\n");
	P_DEBUG("CPU: %i, expire: %i\n", smp_processor_id(),
		atomic_read(&appldata_expire_count));
	if (atomic_dec_and_test(&appldata_expire_count)) {
		atomic_set(&appldata_expire_count, num_online_cpus());
		tasklet_schedule((struct tasklet_struct *) data);
	}
}

/*
 * appldata_tasklet_function()
 *
 * call data gathering function for each (active) module
 */
static void appldata_tasklet_function(unsigned long data)
{
	struct list_head *lh;
	struct appldata_ops *ops;
	int i;

	P_DEBUG("  -= Tasklet =-\n");
	i = 0;
	spin_lock(&appldata_ops_lock);
	list_for_each(lh, &appldata_ops_list) {
		ops = list_entry(lh, struct appldata_ops, list);
		P_DEBUG("list_for_each loop: %i) active = %u, name = %s\n",
			++i, ops->active, ops->name);
		if (ops->active == 1) {
			ops->callback(ops->data);
		}
	}
	spin_unlock(&appldata_ops_lock);
}

/*
 * appldata_mod_vtimer_wrap()
 *
 * wrapper function for mod_virt_timer(), because smp_call_function_on()
 * accepts only one parameter.
 */
static void appldata_mod_vtimer_wrap(struct appldata_mod_vtimer_args *args) {
	mod_virt_timer(args->timer, args->expires);
}

/*
 * appldata_diag()
 *
 * prepare parameter list, issue DIAG 0xDC
 */
static int appldata_diag(char record_nr, u16 function, unsigned long buffer,
			u16 length)
{
	unsigned long ry;
	struct appldata_product_id {
		char prod_nr[7];			/* product nr. */
		char prod_fn[2];			/* product function */
		char record_nr;				/* record nr. */
		char version_nr[2];			/* version */
		char release_nr[2];			/* release */
		char mod_lvl[2];			/* modification lvl. */
	} appldata_product_id = {
	/* all strings are EBCDIC, record_nr is byte */
		.prod_nr    = {0xD3, 0xC9, 0xD5, 0xE4,
				0xE7, 0xD2, 0xD9},	/* "LINUXKR" */
		.prod_fn    = {0xD5, 0xD3},		/* "NL" */
		.record_nr  = record_nr,
		.version_nr = {0xF2, 0xF6},		/* "26" */
		.release_nr = {0xF0, 0xF1},		/* "01" */
		.mod_lvl    = {0xF0, 0xF0},		/* "00" */
	};
	struct appldata_parameter_list appldata_parameter_list = {
				.diag = 0xDC,
				.function = function,
				.parlist_length =
					sizeof(appldata_parameter_list),
				.buffer_length = length,
				.product_id_addr =
					(unsigned long) &appldata_product_id,
				.buffer_addr = virt_to_phys((void *) buffer)
	};

        if (!MACHINE_IS_VM)
                return -ENOSYS;
	ry = -1;
	asm volatile(
			"diag %1,%0,0xDC\n\t"
			: "=d" (ry) : "d" (&(appldata_parameter_list)) : "cc");
	return (int) ry;
}
/********************** timer, tasklet, DIAG <END> ***************************/


/****************************** /proc stuff **********************************/
/*
 * appldata_timer_handler()
 *
 * Start/Stop timer, show status of timer (0 = not active, 1 = active)
 */
static int
appldata_timer_handler(ctl_table *ctl, int write, struct file *filp,
			   void *buffer, size_t *lenp)
{
	int len, i;
	char buf[2];
	u64 per_cpu_interval;

	if (!*lenp || filp->f_pos) {
		*lenp = 0;
		return 0;
	}
	if (!write) {
		len = sprintf(buf, appldata_timer_active ? "1\n" : "0\n");
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		goto out;
	}
	per_cpu_interval = (u64) (appldata_interval*1000 /
				 num_online_cpus()) * TOD_MICRO;
	len = *lenp;
	if (copy_from_user(buf, buffer, len > sizeof(buf) ? sizeof(buf) : len))
		return -EFAULT;
	spin_lock(&appldata_timer_lock);
	per_cpu_interval = (u64) (appldata_interval*1000 /
				 num_online_cpus()) * TOD_MICRO;
	if ((buf[0] == '1') && (!appldata_timer_active)) {
		for (i = 0; i < num_online_cpus(); i++) {
			per_cpu(appldata_timer, i).expires = per_cpu_interval;
			smp_call_function_on(add_virt_timer_periodic,
						&per_cpu(appldata_timer, i),
						0, 1, i);
		}
		appldata_timer_active = 1;
		P_INFO("Monitoring timer started.\n");
	} else if ((buf[0] == '0') && (appldata_timer_active)) {
		for (i = 0; i < num_online_cpus(); i++) {
			del_virt_timer(&per_cpu(appldata_timer, i));
		}
		appldata_timer_active = 0;
		P_INFO("Monitoring timer stopped.\n");
	}
	spin_unlock(&appldata_timer_lock);
out:
	*lenp = len;
	filp->f_pos += len;
	return 0;
}

/*
 * appldata_interval_handler()
 *
 * Set (CPU) timer interval for collection of data (in milliseconds), show
 * current timer interval.
 */
static int
appldata_interval_handler(ctl_table *ctl, int write, struct file *filp,
			   void *buffer, size_t *lenp)
{
	int len, i, interval;
	char buf[16];
	u64 per_cpu_interval;

	if (!*lenp || filp->f_pos) {
		*lenp = 0;
		return 0;
	}
	if (!write) {
		len = sprintf(buf, "%i\n", appldata_interval);
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		goto out;
	}
	len = *lenp;
	if (copy_from_user(buf, buffer, len > sizeof(buf) ? sizeof(buf) : len)) {
		return -EFAULT;
	}
	sscanf(buf, "%i", &interval);
	if (interval <= 0) {
		P_ERROR("Timer CPU interval has to be > 0!\n");
		return -EINVAL;
	}
	per_cpu_interval = (u64) (interval*1000 / num_online_cpus()) * TOD_MICRO;

	spin_lock(&appldata_timer_lock);
	appldata_interval = interval;
	if (appldata_timer_active) {
		for (i = 0; i < num_online_cpus(); i++) {
			appldata_mod_vtimer_args.timer =
					&per_cpu(appldata_timer, i);
			appldata_mod_vtimer_args.expires =
					per_cpu_interval;
			smp_call_function_on(
				(void *) appldata_mod_vtimer_wrap,
				&appldata_mod_vtimer_args,
				0, 1, i);
		}
	}
	spin_unlock(&appldata_timer_lock);

	P_INFO("Monitoring CPU interval set to %u milliseconds.\n",
		 interval);
out:
	*lenp = len;
	filp->f_pos += len;
	return 0;
}

/*
 * appldata_generic_handler()
 *
 * Generic start/stop monitoring and DIAG, show status of
 * monitoring (0 = not in process, 1 = in process)
 */
static int
appldata_generic_handler(ctl_table *ctl, int write, struct file *filp,
			   void *buffer, size_t *lenp)
{
	struct appldata_ops *ops;
	int rc, len;
	char buf[2];

	ops = ctl->data;
	if (!*lenp || filp->f_pos) {
		*lenp = 0;
		return 0;
	}
	if (!write) {
		len = sprintf(buf, ops->active ? "1\n" : "0\n");
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		goto out;
	}
	len = *lenp;
	if (copy_from_user(buf, buffer, len > sizeof(buf) ? sizeof(buf) : len))
		return -EFAULT;

	spin_lock_bh(&appldata_ops_lock);
	if ((buf[0] == '1') && (ops->active == 0)) {
		ops->active = 1;
		ops->callback(ops->data);	// init record
		rc = appldata_diag(ops->record_nr,
					APPLDATA_START_INTERVAL_REC,
					(unsigned long) ops->data, ops->size);
		if (rc != 0) {
			P_ERROR("START DIAG 0xDC for %s failed, "
				"return code: %d\n", ops->name, rc);
			ops->active = 0;
		} else {
			P_INFO("Monitoring %s data enabled, "
				"DIAG 0xDC started.\n", ops->name);
		}
	} else if ((buf[0] == '0') && (ops->active == 1)) {
		ops->active = 0;
		rc = appldata_diag(ops->record_nr, APPLDATA_STOP_REC,
				(unsigned long) ops->data, ops->size);
		if (rc != 0) {
			P_ERROR("STOP DIAG 0xDC for %s failed, "
				"return code: %d\n", ops->name, rc);
		} else {
			P_INFO("Monitoring %s data disabled, "
				"DIAG 0xDC stopped.\n", ops->name);
		}
	}
	spin_unlock_bh(&appldata_ops_lock);
out:
	*lenp = len;
	filp->f_pos += len;
	return 0;
}

/*************************** /proc stuff <END> *******************************/


/************************* module-ops management *****************************/
/*
 * appldata_register_ops()
 *
 * update ops list, register /proc/sys entries
 */
int appldata_register_ops(struct appldata_ops *ops)
{
	struct list_head *lh;
	struct appldata_ops *tmp_ops;
	int rc, i;

	rc = 0;
	i = 0;

	if ((ops->size > APPLDATA_MAX_REC_SIZE) ||
		(ops->size < 0)){
		P_ERROR("Invalid size of %s record = %i, maximum = %i!\n",
			ops->name, ops->size, APPLDATA_MAX_REC_SIZE);
		rc = -ENOMEM;
		goto out;
	}
	if ((ops->ctl_nr == CTL_APPLDATA) ||
	    (ops->ctl_nr == CTL_APPLDATA_TIMER) ||
	    (ops->ctl_nr == CTL_APPLDATA_INTERVAL)) {
		P_ERROR("ctl_nr %i already in use!\n", ops->ctl_nr);
		rc = -EBUSY;
		goto out;
	}
	ops->ctl_table = kmalloc(4*sizeof(struct ctl_table), GFP_KERNEL);
	if (ops->ctl_table == NULL) {
		P_ERROR("Not enough memory for %s ctl_table!\n", ops->name);
		rc = -ENOMEM;
		goto out;
	}
	memset(ops->ctl_table, 0, 4*sizeof(struct ctl_table));

	spin_lock_bh(&appldata_ops_lock);
	list_for_each(lh, &appldata_ops_list) {
		tmp_ops = list_entry(lh, struct appldata_ops, list);
		P_DEBUG("register_ops loop: %i) name = %s, ctl = %i\n",
			++i, tmp_ops->name, tmp_ops->ctl_nr);
		P_DEBUG("Comparing %s (ctl %i) with %s (ctl %i)\n",
			tmp_ops->name, tmp_ops->ctl_nr, ops->name,
			ops->ctl_nr);
		if (strncmp(tmp_ops->name, ops->name,
				APPLDATA_PROC_NAME_LENGTH) == 0) {
			spin_unlock_bh(&appldata_ops_lock);
			P_ERROR("Name \"%s\" already registered!\n", ops->name);
			kfree(ops->ctl_table);
			rc = -EBUSY;
			goto out;
		}
		if (tmp_ops->ctl_nr == ops->ctl_nr) {
			spin_unlock_bh(&appldata_ops_lock);
			P_ERROR("ctl_nr %i already registered!\n", ops->ctl_nr);
			kfree(ops->ctl_table);
			rc = -EBUSY;
			goto out;
		}
	}
	list_add(&ops->list, &appldata_ops_list);
	spin_unlock_bh(&appldata_ops_lock);

	ops->ctl_table[0].ctl_name = CTL_APPLDATA;
	ops->ctl_table[0].procname = appldata_proc_name;
	ops->ctl_table[0].maxlen   = 0;
	ops->ctl_table[0].mode     = S_IRUGO | S_IXUGO;
	ops->ctl_table[0].child    = &ops->ctl_table[2];

	ops->ctl_table[1].ctl_name = 0;

	ops->ctl_table[2].ctl_name = ops->ctl_nr;
	ops->ctl_table[2].procname = ops->name;
	ops->ctl_table[2].mode     = S_IRUGO | S_IWUSR;
	ops->ctl_table[2].proc_handler = appldata_generic_handler;
	ops->ctl_table[2].data = ops;

	ops->ctl_table[3].ctl_name = 0;

	ops->sysctl_header = register_sysctl_table(ops->ctl_table,1);
	ops->ctl_table[2].de->owner = ops->owner;
	P_INFO("%s-ops registered!\n", ops->name);
out:
	return rc;
}

/*
 * appldata_unregister_ops()
 *
 * update ops list, unregister /proc entries, stop DIAG if necessary
 */
void appldata_unregister_ops(struct appldata_ops *ops)
{
	int rc;

	unregister_sysctl_table(ops->sysctl_header);
	kfree(ops->ctl_table);
	if (ops->active == 1) {
		ops->active = 0;
		rc = appldata_diag(ops->record_nr, APPLDATA_STOP_REC,
				(unsigned long) ops->data, ops->size);
		if (rc != 0) {
			P_ERROR("STOP DIAG 0xDC for %s failed, "
				"return code: %d\n", ops->name, rc);
		} else {
			P_INFO("Monitoring %s data disabled, "
				"DIAG 0xDC stopped.\n", ops->name);
		}

	}
	spin_lock_bh(&appldata_ops_lock);
	list_del(&ops->list);
	spin_unlock_bh(&appldata_ops_lock);
	P_INFO("%s-ops unregistered!\n", ops->name);
}
/********************** module-ops management <END> **************************/


/******************************* init / exit *********************************/
/*
 * appldata_init()
 *
 * init timer and tasklet, register /proc entries
 */
static int __init appldata_init(void)
{
	int i;

	P_DEBUG("sizeof(parameter_list) = %lu\n",
		sizeof(struct appldata_parameter_list));

	for (i = 0; i < num_online_cpus(); i++) {
		init_virt_timer(&per_cpu(appldata_timer, i));
		per_cpu(appldata_timer, i).function = appldata_timer_function;
		per_cpu(appldata_timer, i).data = (unsigned long)
						&appldata_tasklet_struct;
	}
	atomic_set(&appldata_expire_count, num_online_cpus());

	appldata_sysctl_header = register_sysctl_table(appldata_dir_table, 1);
#ifdef MODULE
	appldata_dir_table[0].de->owner = THIS_MODULE;
	appldata_table[0].de->owner = THIS_MODULE;
	appldata_table[1].de->owner = THIS_MODULE;
#endif

	tasklet_init(&appldata_tasklet_struct, appldata_tasklet_function, 0);
	P_DEBUG("Base interface initialized.\n");
	return 0;
}

/*
 * appldata_exit()
 *
 * stop timer and tasklet, unregister /proc entries
 */
static void __exit appldata_exit(void)
{
	struct list_head *lh;
	struct appldata_ops *ops;
	int rc, i;

	P_DEBUG("Unloading module ...\n");
	/*
	 * ops list should be empty, but just in case something went wrong...
	 */
	spin_lock_bh(&appldata_ops_lock);
	list_for_each(lh, &appldata_ops_list) {
		ops = list_entry(lh, struct appldata_ops, list);
		rc = appldata_diag(ops->record_nr, APPLDATA_STOP_REC,
				(unsigned long) ops->data, ops->size);
		if (rc != 0) {
			P_ERROR("STOP DIAG 0xDC for %s failed, "
				"return code: %d\n", ops->name, rc);
		}
	}
	spin_unlock_bh(&appldata_ops_lock);

	for (i = 0; i < num_online_cpus(); i++) {
		del_virt_timer(&per_cpu(appldata_timer, i));
	}
	appldata_timer_active = 0;

	unregister_sysctl_table(appldata_sysctl_header);

	tasklet_kill(&appldata_tasklet_struct);
	P_DEBUG("... module unloaded!\n");
}
/**************************** init / exit <END> ******************************/


module_init(appldata_init);
module_exit(appldata_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerald Schaefer");
MODULE_DESCRIPTION("Linux-VM Monitor Stream, base infrastructure");

EXPORT_SYMBOL_GPL(appldata_register_ops);
EXPORT_SYMBOL_GPL(appldata_unregister_ops);

#ifdef MODULE
/*
 * Kernel symbols needed by appldata_mem and appldata_os modules.
 * However, if this file is compiled as a module (for testing only), these
 * symbols are not exported. In this case, we define them locally and export
 * those.
 */
void si_swapinfo(struct sysinfo *val)
{
	val->freeswap = -1ul;
	val->totalswap = -1ul;
}

unsigned long avenrun[3] = {-1 - FIXED_1/200, -1 - FIXED_1/200,
				-1 - FIXED_1/200};
int nr_threads = -1;

void get_full_page_state(struct page_state *ps)
{
	memset(ps, -1, sizeof(struct page_state));
}

unsigned long nr_running(void)
{
	return -1;
}

unsigned long nr_iowait(void)
{
	return -1;
}

/*unsigned long nr_context_switches(void)
{
	return -1;
}*/
#endif /* MODULE */
EXPORT_SYMBOL_GPL(si_swapinfo);
EXPORT_SYMBOL_GPL(nr_threads);
EXPORT_SYMBOL_GPL(avenrun);
EXPORT_SYMBOL_GPL(get_full_page_state);
EXPORT_SYMBOL_GPL(nr_running);
EXPORT_SYMBOL_GPL(nr_iowait);
//EXPORT_SYMBOL_GPL(nr_context_switches);
