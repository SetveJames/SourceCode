/*
 *
 * linux/drivers/s390/scsi/zfcp_aux.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * (C) Copyright IBM Corp. 2002, 2004
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *            Aron Zeh
 *            Wolfgang Taphorn
 *            Stefan Bader <stefan.bader@de.ibm.com>
 *            Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_AUX_REVISION "$Revision: 1.98 $"

/********************** INCLUDES *********************************************/

#include <linux/init.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/syscalls.h>

#include "zfcp_ext.h"

#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ebcdic.h>
#include <asm/cpcmd.h>		/* Debugging only */
#include <asm/processor.h>	/* Debugging only */

#include <linux/miscdevice.h>
#include <linux/major.h>

/* accumulated log level (module parameter) */
static u32 loglevel = ZFCP_LOG_LEVEL_DEFAULTS;
static char *device;
/*********************** FUNCTION PROTOTYPES *********************************/

/* written against the module interface */
static int __init  zfcp_module_init(void);
static void __exit zfcp_module_exit(void);

int zfcp_reboot_handler(struct notifier_block *, unsigned long, void *);

/* FCP related */
static void zfcp_ns_gid_pn_handler(unsigned long);

/* miscellaneous */
#ifdef ZFCP_STAT_REQSIZES
static int zfcp_statistics_init_all(void);
static int zfcp_statistics_clear_all(void);
static int zfcp_statistics_clear(struct list_head *);
static int zfcp_statistics_new(struct list_head *, u32);
#endif

static inline int zfcp_sg_list_alloc(struct zfcp_sg_list *, size_t);
static inline int zfcp_sg_list_free(struct zfcp_sg_list *);
static inline int zfcp_sg_list_copy_from_user(struct zfcp_sg_list *, void *,
					      size_t);
static inline int zfcp_sg_list_copy_to_user(void *, struct zfcp_sg_list *,
					    size_t);

static int zfcp_cfdc_dev_ioctl(struct inode *, struct file *,
	unsigned int, unsigned long);

#define ZFCP_CFDC_IOC_MAGIC                     0xDD
#define ZFCP_CFDC_IOC \
	_IOWR(ZFCP_CFDC_IOC_MAGIC, 0, struct zfcp_cfdc_sense_data)

#ifdef CONFIG_S390_SUPPORT
static struct ioctl_trans zfcp_ioctl_trans = {ZFCP_CFDC_IOC, (void*) sys_ioctl};
#endif

static struct file_operations zfcp_cfdc_fops = {
	.ioctl = zfcp_cfdc_dev_ioctl
};

static struct miscdevice zfcp_cfdc_misc = {
	.minor = ZFCP_CFDC_DEV_MINOR,
	.name = ZFCP_CFDC_DEV_NAME,
	.fops = &zfcp_cfdc_fops
};

/*********************** KERNEL/MODULE PARAMETERS  ***************************/

/* declare driver module init/cleanup functions */
module_init(zfcp_module_init);
module_exit(zfcp_module_exit);

MODULE_AUTHOR("Heiko Carstens <heiko.carstens@de.ibm.com>, "
	      "Martin Peschke <mpeschke@de.ibm.com>, "
	      "Raimund Schroeder <raimund.schroeder@de.ibm.com>, "
	      "Wolfgang Taphorn <taphorn@de.ibm.com>, "
	      "Aron Zeh <arzeh@de.ibm.com>, "
	      "IBM Deutschland Entwicklung GmbH");
MODULE_DESCRIPTION
    ("FCP (SCSI over Fibre Channel) HBA driver for IBM eServer zSeries");
MODULE_LICENSE("GPL");

module_param(device, charp, 0);
MODULE_PARM_DESC(device, "specify initial device");

module_param(loglevel, uint, 0);
MODULE_PARM_DESC(loglevel,
		 "log levels, 8 nibbles: "
		 "(unassigned) ERP QDIO DIO Config FSF SCSI Other, "
		 "levels: 0=none 1=normal 2=devel 3=trace");

#ifdef ZFCP_PRINT_FLAGS
u32 flags_dump = 0;
module_param(flags_dump, uint, 0);
#endif

/****************************************************************/
/************** Functions without logging ***********************/
/****************************************************************/

void
_zfcp_hex_dump(char *addr, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		printk("%02x", addr[i]);
		if ((i % 4) == 3)
			printk(" ");
		if ((i % 32) == 31)
			printk("\n");
	}
	if (((i-1) % 32) != 31)
		printk("\n");
}

/****************************************************************/
/************** Uncategorised Functions *************************/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER

#ifdef ZFCP_STAT_REQSIZES

static int
zfcp_statistics_clear(struct list_head *head)
{
	int retval = 0;
	unsigned long flags;
	struct zfcp_statistics *stat, *tmp;

	write_lock_irqsave(&zfcp_data.stat_lock, flags);
	list_for_each_entry_safe(stat, tmp, head, list) {
		list_del(&stat->list);
		kfree(stat);
	}
	write_unlock_irqrestore(&zfcp_data.stat_lock, flags);

	return retval;
}

/* Add new statistics entry */
static int
zfcp_statistics_new(struct list_head *head, u32 num)
{
	int retval = 0;
	struct zfcp_statistics *stat;

	stat = kmalloc(sizeof (struct zfcp_statistics), GFP_ATOMIC);
	if (stat) {
		memset(stat, 0, sizeof (struct zfcp_statistics));
		stat->num = num;
		stat->occurrence = 1;
		list_add_tail(&stat->list, head);
	} else
		zfcp_data.stat_errors++;

	return retval;
}

int
zfcp_statistics_inc(struct list_head *head, u32 num)
{
	int retval = 0;
	unsigned long flags;
	struct zfcp_statistics *stat;

	write_lock_irqsave(&zfcp_data.stat_lock, flags);
	list_for_each_entry(stat, head, list) {
		if (stat->num == num) {
			stat->occurrence++;
			goto unlock;
		}
	}
	/* occurrence must be initialized to 1 */
	zfcp_statistics_new(head, num);
 unlock:
	write_unlock_irqrestore(&zfcp_data.stat_lock, flags);
	return retval;
}

static int
zfcp_statistics_init_all(void)
{
	int retval = 0;

	rwlock_init(&zfcp_data.stat_lock);
	INIT_LIST_HEAD(&zfcp_data.read_req_head);
	INIT_LIST_HEAD(&zfcp_data.write_req_head);
	INIT_LIST_HEAD(&zfcp_data.read_sg_head);
	INIT_LIST_HEAD(&zfcp_data.write_sg_head);
	INIT_LIST_HEAD(&zfcp_data.read_sguse_head);
	INIT_LIST_HEAD(&zfcp_data.write_sguse_head);
	return retval;
}

static int
zfcp_statistics_clear_all(void)
{
	int retval = 0;

	zfcp_statistics_clear(&zfcp_data.read_req_head);
	zfcp_statistics_clear(&zfcp_data.write_req_head);
	zfcp_statistics_clear(&zfcp_data.read_sg_head);
	zfcp_statistics_clear(&zfcp_data.write_sg_head);
	zfcp_statistics_clear(&zfcp_data.read_sguse_head);
	zfcp_statistics_clear(&zfcp_data.write_sguse_head);
	return retval;
}

#endif /* ZFCP_STAT_REQSIZES */

static inline int
zfcp_fsf_req_is_scsi_cmnd(struct zfcp_fsf_req *fsf_req)
{
	return ((fsf_req->fsf_command == FSF_QTCB_FCP_CMND) &&
		!(fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT));
}

void
zfcp_cmd_dbf_event_fsf(const char *text, struct zfcp_fsf_req *fsf_req,
		       void *add_data, int add_length)
{
#ifdef ZFCP_DEBUG_COMMANDS
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct scsi_cmnd *scsi_cmnd;
	int level = 3;
	int i;
	unsigned long flags;

	write_lock_irqsave(&adapter->cmd_dbf_lock, flags);
	if (zfcp_fsf_req_is_scsi_cmnd(fsf_req)) {
		scsi_cmnd = fsf_req->data.send_fcp_command_task.scsi_cmnd;
		debug_text_event(adapter->cmd_dbf, level, "fsferror");
		debug_text_event(adapter->cmd_dbf, level, text);
		debug_event(adapter->cmd_dbf, level, &fsf_req,
			    sizeof (unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no,
			    sizeof (u32));
		debug_event(adapter->cmd_dbf, level, &scsi_cmnd,
			    sizeof (unsigned long));
		debug_event(adapter->cmd_dbf, level, &scsi_cmnd->cmnd,
			    min(ZFCP_CMD_DBF_LENGTH, (int)scsi_cmnd->cmd_len));
		for (i = 0; i < add_length; i += ZFCP_CMD_DBF_LENGTH)
			debug_event(adapter->cmd_dbf,
				    level,
				    (char *) add_data + i,
				    min(ZFCP_CMD_DBF_LENGTH, add_length - i));
	}
	write_unlock_irqrestore(&adapter->cmd_dbf_lock, flags);
#endif
}

/* XXX additionally log unit if available */
/* ---> introduce new parameter for unit, see 2.4 code */
void
zfcp_cmd_dbf_event_scsi(const char *text, struct scsi_cmnd *scsi_cmnd)
{
#ifdef ZFCP_DEBUG_COMMANDS
	struct zfcp_adapter *adapter;
	union zfcp_req_data *req_data;
	struct zfcp_fsf_req *fsf_req;
	int level = ((host_byte(scsi_cmnd->result) != 0) ? 1 : 5);
	unsigned long flags;

	adapter = (struct zfcp_adapter *) scsi_cmnd->device->host->hostdata[0];
	req_data = (union zfcp_req_data *) scsi_cmnd->host_scribble;
	fsf_req = (req_data ? req_data->send_fcp_command_task.fsf_req : NULL);
	write_lock_irqsave(&adapter->cmd_dbf_lock, flags);
	debug_text_event(adapter->cmd_dbf, level, "hostbyte");
	debug_text_event(adapter->cmd_dbf, level, text);
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd->result, sizeof (u32));
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd,
		    sizeof (unsigned long));
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd->cmnd,
		    min(ZFCP_CMD_DBF_LENGTH, (int)scsi_cmnd->cmd_len));
	if (likely(fsf_req)) {
		debug_event(adapter->cmd_dbf, level, &fsf_req,
			    sizeof (unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no,
			    sizeof (u32));
	} else {
		debug_text_event(adapter->cmd_dbf, level, "");
		debug_text_event(adapter->cmd_dbf, level, "");
	}
	write_unlock_irqrestore(&adapter->cmd_dbf_lock, flags);
#endif
}

void
zfcp_in_els_dbf_event(struct zfcp_adapter *adapter, const char *text,
		      struct fsf_status_read_buffer *status_buffer, int length)
{
#ifdef ZFCP_DEBUG_INCOMING_ELS
	int level = 1;
	int i;

	debug_text_event(adapter->in_els_dbf, level, text);
	debug_event(adapter->in_els_dbf, level, &status_buffer->d_id, 8);
	for (i = 0; i < length; i += ZFCP_IN_ELS_DBF_LENGTH)
		debug_event(adapter->in_els_dbf,
			    level,
			    (char *) status_buffer->payload + i,
			    min(ZFCP_IN_ELS_DBF_LENGTH, length - i));
#endif
}

/**
 * zfcp_device_setup - setup function
 * @str: pointer to parameter string
 *
 * Parse "device=..." parameter string.
 */
static int __init
zfcp_device_setup(char *str)
{
	char *tmp;

	if (!str)
		return 0;

	tmp = strchr(str, ',');
	if (!tmp)
		goto err_out;
	*tmp++ = '\0';
	strncpy(zfcp_data.init_busid, str, BUS_ID_SIZE);
	zfcp_data.init_busid[BUS_ID_SIZE-1] = '\0';

	zfcp_data.init_wwpn = simple_strtoull(tmp, &tmp, 0);
	if (*tmp++ != ',')
		goto err_out;
	if (*tmp == '\0')
		goto err_out;

	zfcp_data.init_fcp_lun = simple_strtoull(tmp, &tmp, 0);
	if (*tmp != '\0')
		goto err_out;
	return 1;

 err_out:
	ZFCP_LOG_NORMAL("Parse error for device parameter string %s\n", str);
	return 0;
}

static void __init
zfcp_init_device_configure(void)
{
	int found = 0;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	down(&zfcp_data.config_sema);
	read_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry(adapter, &zfcp_data.adapter_list_head, list)
		if (strcmp(zfcp_data.init_busid,
			   zfcp_get_busid_by_adapter(adapter)) == 0) {
			zfcp_adapter_get(adapter);
			found = 1;
			break;
		}
	read_unlock_irq(&zfcp_data.config_lock);
	if (!found)
		goto out_adapter;
	port = zfcp_port_enqueue(adapter, zfcp_data.init_wwpn, 0);
	if (!port)
		goto out_port;
	unit = zfcp_unit_enqueue(port, zfcp_data.init_fcp_lun);
	if (!unit)
		goto out_unit;
	up(&zfcp_data.config_sema);
	ccw_device_set_online(adapter->ccw_device);
	down(&zfcp_data.config_sema);
	wait_event(unit->scsi_add_wq, atomic_read(&unit->scsi_add_work) == 0);
	zfcp_unit_put(unit);
 out_unit:
	zfcp_port_put(port);
 out_port:
	zfcp_adapter_put(adapter);
 out_adapter:
	up(&zfcp_data.config_sema);
	return;
}

static int __init
zfcp_module_init(void)
{

	int retval = 0;

	atomic_set(&zfcp_data.loglevel, loglevel);

	ZFCP_LOG_DEBUG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	ZFCP_LOG_TRACE("Start Address of module: 0x%lx\n",
		       (unsigned long) &zfcp_module_init);

	/* initialize adapter list */
	INIT_LIST_HEAD(&zfcp_data.adapter_list_head);

	/* initialize adapters to be removed list head */
	INIT_LIST_HEAD(&zfcp_data.adapter_remove_lh);

#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_init_all();
#endif

#ifdef CONFIG_S390_SUPPORT
	retval = register_ioctl32_conversion(zfcp_ioctl_trans.cmd,
					     zfcp_ioctl_trans.handler);
	if (retval != 0) {
		ZFCP_LOG_INFO("Cannot register a 32-bit support of "
			      "the IOC handler\n");
		goto out_ioctl32;
	}
#endif
	retval = misc_register(&zfcp_cfdc_misc);
	if (retval != 0) {
		ZFCP_LOG_INFO(
			"Device file for the control file data channel "
			"cannot be registered\n");
		goto out_misc_register;
	} else {
		ZFCP_LOG_INFO(
			"Device file for the control file data channel "
			"has become MAJOR/MINOR numbers %d/%d\n",
			ZFCP_CFDC_DEV_MAJOR, zfcp_cfdc_misc.minor);
	}

	/* Initialise proc semaphores */
	sema_init(&zfcp_data.config_sema, 1);

	/* initialise configuration rw lock */
	rwlock_init(&zfcp_data.config_lock);

	zfcp_data.reboot_notifier.notifier_call = zfcp_reboot_handler;
	register_reboot_notifier(&zfcp_data.reboot_notifier);

	/* save address of data structure managing the driver module */
	zfcp_data.scsi_host_template.module = THIS_MODULE;

	/* setup dynamic I/O */
	retval = zfcp_ccw_register();
	if (retval) {
		ZFCP_LOG_NORMAL("Registering with common I/O layer failed.\n");
		goto out_ccw_register;
	}

	if (zfcp_device_setup(device))
		zfcp_init_device_configure();

	goto out;

 out_ccw_register:
	unregister_reboot_notifier(&zfcp_data.reboot_notifier);
	misc_deregister(&zfcp_cfdc_misc);
 out_misc_register:
#ifdef CONFIG_S390_SUPPORT
	unregister_ioctl32_conversion(zfcp_ioctl_trans.cmd);
 out_ioctl32:
#endif
#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear_all();
#endif

 out:
	return retval;
}

static void __exit
zfcp_module_exit(void)
{
	unregister_reboot_notifier(&zfcp_data.reboot_notifier);
	zfcp_ccw_unregister();
	misc_deregister(&zfcp_cfdc_misc);
#ifdef CONFIG_S390_SUPPORT
	unregister_ioctl32_conversion(zfcp_ioctl_trans.cmd);
#endif
#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear_all();
#endif
	ZFCP_LOG_DEBUG("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

/*
 * This function is called automatically by the kernel whenever a reboot or a 
 * shut-down is initiated and zfcp is still loaded
 *
 * locks:       zfcp_data.config_sema is taken prior to shutting down the module
 *              and removing all structures
 * returns:     NOTIFY_DONE in all cases
 */
int
zfcp_reboot_handler(struct notifier_block *notifier, unsigned long code,
		    void *ptr)
{
	zfcp_ccw_unregister();
	return NOTIFY_DONE;
}


/*
 * function:    zfcp_cfdc_dev_ioctl
 *
 * purpose:     Handle control file upload/download transaction via IOCTL
 *		interface
 *
 * returns:     0           - Operation completed successfuly
 *              -ENOTTY     - Unknown IOCTL command
 *              -EINVAL     - Invalid sense data record
 *              -ENXIO      - The FCP adapter is not available
 *              -EOPNOTSUPP - The FCP adapter does not have CFDC support
 *              -ENOMEM     - Insufficient memory
 *              -EFAULT     - User space memory I/O operation fault
 *              -EPERM      - Cannot create or queue FSF request or create SBALs
 */
static int
zfcp_cfdc_dev_ioctl(struct inode *inode, struct file *file,
                    unsigned int command, unsigned long buffer)
{
	struct zfcp_cfdc_sense_data sense_data, *sense_data_user;
	struct zfcp_adapter *adapter = NULL;
	struct zfcp_fsf_req *fsf_req = NULL;
	struct zfcp_sg_list *sg_list = NULL;
	u32 fsf_command, option;
	char *bus_id = NULL;
	int retval = 0;

	ZFCP_LOG_NORMAL(
		"Control file data channel transaction opened\n");

	sg_list = kmalloc(sizeof(struct zfcp_sg_list), GFP_KERNEL);
	if (sg_list == NULL) {
		ZFCP_LOG_NORMAL(
			"Not enough memory for the scatter-gather list\n");
		retval = -ENOMEM;
		goto out;
	}
	sg_list->count = 0;

	if (command != ZFCP_CFDC_IOC) {
		ZFCP_LOG_NORMAL(
			"IOC request code 0x%x is not valid\n",
			command);
		retval = -ENOTTY;
		goto out;
	}

	if ((sense_data_user = (struct zfcp_cfdc_sense_data*)buffer) == NULL) {
		ZFCP_LOG_NORMAL(
			"Sense data record is required\n");
		retval = -EINVAL;
		goto out;
	}

	retval = copy_from_user(&sense_data, sense_data_user,
		sizeof(struct zfcp_cfdc_sense_data));
	if (retval) {
		ZFCP_LOG_NORMAL("Cannot copy sense data record from user space "
				"memory\n");
		retval = -EFAULT;
		goto out;
	}

	if (sense_data.signature != ZFCP_CFDC_SIGNATURE) {
		ZFCP_LOG_NORMAL(
			"No valid sense data request signature 0x%08x found\n",
			ZFCP_CFDC_SIGNATURE);
		retval = -EINVAL;
		goto out;
	}

	switch (sense_data.command) {

	case ZFCP_CFDC_CMND_DOWNLOAD_NORMAL:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_NORMAL_MODE;
		break;

	case ZFCP_CFDC_CMND_DOWNLOAD_FORCE:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_FORCE;
		break;

	case ZFCP_CFDC_CMND_FULL_ACCESS:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_FULL_ACCESS;
		break;

	case ZFCP_CFDC_CMND_RESTRICTED_ACCESS:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_RESTRICTED_ACCESS;
		break;

	case ZFCP_CFDC_CMND_UPLOAD:
		fsf_command = FSF_QTCB_UPLOAD_CONTROL_FILE;
		option = 0;
		break;

	default:
		ZFCP_LOG_NORMAL(
			"Command code 0x%08x is not valid\n",
			sense_data.command);
		retval = -EINVAL;
		goto out;
	}

	bus_id = kmalloc(BUS_ID_SIZE, GFP_KERNEL);
	if (bus_id == NULL) {
		ZFCP_LOG_NORMAL("Out of memory!\n");
		retval = -ENOMEM;
		goto out;
	}
	snprintf(bus_id, BUS_ID_SIZE, "%d.%d.%04x",
		(sense_data.devno >> 24),
		(sense_data.devno >> 16) & 0xFF,
		(sense_data.devno & 0xFFFF));

	retval = -ENXIO;
	read_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry(adapter, &zfcp_data.adapter_list_head, list) {
		if (strncmp(bus_id, zfcp_get_busid_by_adapter(adapter),
		    BUS_ID_SIZE) == 0) {
			zfcp_adapter_get(adapter);
			retval = 0;
			break;
		}
	}
	read_unlock_irq(&zfcp_data.config_lock);

	kfree(bus_id);

	if (retval != 0) {
		ZFCP_LOG_NORMAL("Specified adapter does not exist\n");
		goto out;
	}

	if (sense_data.command & ZFCP_CFDC_WITH_CONTROL_FILE) {
		retval = zfcp_sg_list_alloc(sg_list,
					    ZFCP_CFDC_MAX_CONTROL_FILE_SIZE);
		if (retval) {
			ZFCP_LOG_NORMAL("Not enough memory for the "
					"scatter-gather list\n");
			retval = -ENOMEM;
			goto out;
		}
	}

	if ((sense_data.command & ZFCP_CFDC_DOWNLOAD) &&
	    (sense_data.command & ZFCP_CFDC_WITH_CONTROL_FILE)) {
		retval = zfcp_sg_list_copy_from_user(
			sg_list, &sense_data_user->control_file,
			ZFCP_CFDC_MAX_CONTROL_FILE_SIZE);
		if (retval) {
			ZFCP_LOG_NORMAL("Cannot copy control file from user "
					"space memory\n");
			retval = -EFAULT;
			goto out;
		}
	}

	retval = zfcp_fsf_control_file(
		adapter, &fsf_req, fsf_command, option, sg_list);
	if (retval == -EOPNOTSUPP) {
		ZFCP_LOG_NORMAL(
			"Specified adapter does not support control file\n");
		goto out;
	} else if (retval != 0) {
		ZFCP_LOG_NORMAL(
			"Cannot create or queue FSF request or create SBALs\n");
		retval = -EPERM;
		goto out;
	}

	wait_event(fsf_req->completion_wq,
	           fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	sense_data.fsf_status = fsf_req->qtcb->header.fsf_status;
	memcpy(&sense_data.fsf_status_qual,
	       &fsf_req->qtcb->header.fsf_status_qual,
	       sizeof(union fsf_status_qual));
	memcpy(&sense_data.payloads, &fsf_req->qtcb->bottom.support.els, 256);

	retval = copy_to_user(sense_data_user, &sense_data,
		sizeof(struct zfcp_cfdc_sense_data));
	if (retval) {
		ZFCP_LOG_NORMAL(
			"Cannot copy sense data record to user space memory\n");
		retval = -EFAULT;
		goto out;
	}

	if (sense_data.command & ZFCP_CFDC_UPLOAD) {
		retval = zfcp_sg_list_copy_to_user(
			&sense_data_user->control_file, sg_list,
			ZFCP_CFDC_MAX_CONTROL_FILE_SIZE);
		if (retval) {
			ZFCP_LOG_NORMAL("Cannot copy control file to user "
					"space memory\n");
			retval = -EFAULT;
			goto out;
		}
	}

 out:
	if (fsf_req != NULL)
		zfcp_fsf_req_cleanup(fsf_req);

	if ((adapter != NULL) && (retval != -ENXIO))
		zfcp_adapter_put(adapter);

	if (sg_list != NULL) {
		zfcp_sg_list_free(sg_list);
		kfree(sg_list);
	}

	ZFCP_LOG_NORMAL(
		"Control file data channel transaction closed\n");

	return retval;
}


/*
 * function:    zfcp_sg_list_alloc
 *
 * purpose:     Create a scatter-gather list of the specified size
 *
 * returns:     0       - Scatter gather list is created
 *              -ENOMEM - Insufficient memory (*list_ptr is then set to NULL)
 */
static inline int
zfcp_sg_list_alloc(struct zfcp_sg_list *sg_list, size_t size)
{
	struct scatterlist *sg;
	int i;
	int retval = 0;

	sg_list->count = size >> PAGE_SHIFT;
	if (size & ~PAGE_MASK)
		sg_list->count++;
	sg_list->sg = kmalloc(sg_list->count * sizeof(struct scatterlist),
			      GFP_KERNEL);
	if (sg_list->sg == NULL) {
		retval = -ENOMEM;
		goto out;
	}

	for (i = 0, sg = sg_list->sg; i < sg_list->count; i++, sg++) {
		sg->length = min(size, PAGE_SIZE);
		sg->offset = 0;
		sg->page = alloc_pages(GFP_KERNEL, 0);
		if (sg->page == NULL) {
			sg_list->count = i;
			zfcp_sg_list_free(sg_list);
			retval = -ENOMEM;
			goto out;
		}
		size -= sg->length;
	}

 out:
	return retval;
}


/*
 * function:    zfcp_sg_list_free
 *
 * purpose:     Destroy a scatter-gather list and release memory
 *
 * returns:     Always 0
 */
static inline int
zfcp_sg_list_free(struct zfcp_sg_list *sg_list)
{
	struct scatterlist *sg;
	int i;
	int retval = 0;

	BUG_ON((sg_list->sg == NULL) || (sg_list == NULL));

	for (i = 0, sg = sg_list->sg; i < sg_list->count; i++, sg++)
		__free_pages(sg->page, 0);

	return retval;
}


/*
 * function:    zfcp_sg_list_copy_from_user
 *
 * purpose:     Copy data from user space memory to the scatter-gather list
 *
 * returns:     0       - The data has been copied from user
 *              -EFAULT - Memory I/O operation fault
 */
static inline int
zfcp_sg_list_copy_from_user(struct zfcp_sg_list *sg_list, void *user_buffer,
                            size_t size)
{
	struct scatterlist *sg;
	unsigned int length;
	void *zfcp_buffer;
	int retval = 0;

	for (sg = sg_list->sg; size > 0; sg++) {
		length = min((unsigned int)size, sg->length);
		zfcp_buffer = (void*)
			((page_to_pfn(sg->page) << PAGE_SHIFT) + sg->offset);
		if (copy_from_user(zfcp_buffer, user_buffer, length)) {
			ZFCP_LOG_INFO("Memory error (copy_from_user)\n");
			retval = -EFAULT;
			goto out;
		}
		user_buffer += length;
		size -= length;
	}

 out:
	return retval;
}


/*
 * function:    zfcp_sg_list_copy_to_user
 *
 * purpose:     Copy data from the scatter-gather list to user space memory
 *
 * returns:     0       - The data has been copied to user
 *              -EFAULT - Memory I/O operation fault
 */
static inline int
zfcp_sg_list_copy_to_user(void *user_buffer, struct zfcp_sg_list *sg_list,
                          size_t size)
{
	struct scatterlist *sg;
	unsigned int length;
	void *zfcp_buffer;
	int retval = 0;

	for (sg = sg_list->sg; size > 0; sg++) {
		length = min((unsigned int)size, sg->length);
		zfcp_buffer = (void*)
			((page_to_pfn(sg->page) << PAGE_SHIFT) + sg->offset);
		if (copy_to_user(user_buffer, zfcp_buffer, length)) {
			ZFCP_LOG_INFO("Memory error (copy_to_user)\n");
			retval = -EFAULT;
			goto out;
		}
		user_buffer += length;
		size -= length;
	}

 out:
	return retval;
}


#undef ZFCP_LOG_AREA

/****************************************************************/
/****** Functions for configuration/set-up of structures ********/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG

/**
 * zfcp_get_unit_by_lun - find unit in unit list of port by fcp lun
 * @port: pointer to port to search for unit
 * @fcp_lun: lun to search for
 * Traverses list of all units of a port and returns pointer to a unit
 * if lun of a unit matches.
 */

struct zfcp_unit *
zfcp_get_unit_by_lun(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;
	int found = 0;

	list_for_each_entry(unit, &port->unit_list_head, list) {
		if ((unit->fcp_lun == fcp_lun) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status))
		{
			found = 1;
			break;
		}
	}
	return found ? unit : NULL;
}

/**
 * zfcp_get_port_by_wwpn - find unit in unit list of port by fcp lun
 * @adapter: pointer to adapter to search for port
 * @wwpn: wwpn to search for
 * Traverses list of all ports of an adapter and returns a pointer to a port
 * if wwpn of a port matches.
 */

struct zfcp_port *
zfcp_get_port_by_wwpn(struct zfcp_adapter *adapter, wwn_t wwpn)
{
	struct zfcp_port *port;
	int found = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if ((port->wwpn == wwpn) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status))
		{
			found = 1;
			break;
		}
	}
	return found ? port : NULL;
}

/*
 * Enqueues a logical unit at the end of the unit list associated with the 
 * specified port. Also sets up some unit internal structures.
 *
 * returns:	pointer to unit with a usecount of 1 if a new unit was
 *              successfully enqueued
 *              NULL otherwise
 * locks:	config_sema must be held to serialise changes to the unit list
 */
struct zfcp_unit *
zfcp_unit_enqueue(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;

	/*
	 * check that there is no unit with this FCP_LUN already in list
	 * and enqueue it.
	 * Note: Unlike for the adapter and the port, this is an error
	 */
	read_lock_irq(&zfcp_data.config_lock);
	unit = zfcp_get_unit_by_lun(port, fcp_lun);
	read_unlock_irq(&zfcp_data.config_lock);
	if (unit)
		return NULL;

	unit = kmalloc(sizeof (struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return NULL;
	memset(unit, 0, sizeof (struct zfcp_unit));

	init_waitqueue_head(&unit->scsi_add_wq);
	/* initialise reference count stuff */
	atomic_set(&unit->refcount, 0);
	init_waitqueue_head(&unit->remove_wq);

	unit->port = port;
	/*
	 * FIXME: reuse of scsi_luns!
	 */
	unit->scsi_lun = port->max_scsi_lun + 1;
	unit->fcp_lun = fcp_lun;
	unit->common_magic = ZFCP_MAGIC;
	unit->specific_magic = ZFCP_MAGIC_UNIT;

	/* setup for sysfs registration */
	snprintf(unit->sysfs_device.bus_id, BUS_ID_SIZE, "0x%016llx", fcp_lun);
	unit->sysfs_device.parent = &port->sysfs_device;
	unit->sysfs_device.release = zfcp_sysfs_unit_release;
	dev_set_drvdata(&unit->sysfs_device, unit);

	/* mark unit unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);

	if (device_register(&unit->sysfs_device)) {
		kfree(unit);
		return NULL;
	}

	if (zfcp_sysfs_unit_create_files(&unit->sysfs_device)) {
		/*
		 * failed to create all sysfs attributes, therefore the unit
		 * must be put on the unit_remove listhead of the port where
		 * the release function expects it.
		 */
		write_lock_irq(&zfcp_data.config_lock);
		list_add_tail(&unit->list, &port->unit_remove_lh);
		write_unlock_irq(&zfcp_data.config_lock);
		device_unregister(&unit->sysfs_device);
		return NULL;
	}

	/*
	 * update max SCSI LUN of logical units attached to parent remote port
	 */
	port->max_scsi_lun++;

	/*
	 * update max SCSI LUN of logical units attached to parent adapter
	 */
	if (port->adapter->max_scsi_lun < port->max_scsi_lun)
		port->adapter->max_scsi_lun = port->max_scsi_lun;

	/*
	 * update max SCSI LUN of logical units attached to host (SCSI stack)
	 */
	if (port->adapter->scsi_host &&
	    (port->adapter->scsi_host->max_lun < port->max_scsi_lun))
		port->adapter->scsi_host->max_lun = port->max_scsi_lun + 1;

	zfcp_unit_get(unit);

	/* unit is new and needs to be added to list */
	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);
	list_add_tail(&unit->list, &port->unit_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	port->units++;
	zfcp_port_get(port);

	return unit;
}

/* locks:  config_sema must be held */
void
zfcp_unit_dequeue(struct zfcp_unit *unit)
{
	/* remove specified unit data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&unit->list);
	write_unlock_irq(&zfcp_data.config_lock);

	unit->port->units--;
	zfcp_port_put(unit->port);

	kfree(unit);

	return;
}

static void *
zfcp_mempool_alloc(int gfp_mask, void *size)
{
	return kmalloc((size_t) size, gfp_mask);
}

static void
zfcp_mempool_free(void *element, void *size)
{
	kfree(element);
}

/*
 * Allocates a combined QTCB/fsf_req buffer for erp actions and fcp/SCSI
 * commands.
 * It also genrates fcp-nameserver request/response buffer and unsolicited 
 * status read fsf_req buffers.
 *
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static int
zfcp_allocate_low_mem_buffers(struct zfcp_adapter *adapter)
{
	adapter->pool.fsf_req_erp =
		mempool_create(ZFCP_POOL_FSF_REQ_ERP_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_fsf_req_pool_element));

	if (NULL == adapter->pool.fsf_req_erp) {
		ZFCP_LOG_INFO("error: pool allocation failed (fsf_req_erp)\n");
		return -ENOMEM;
	}

	adapter->pool.fsf_req_scsi =
		mempool_create(ZFCP_POOL_FSF_REQ_SCSI_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_fsf_req_pool_element));

	if (NULL == adapter->pool.fsf_req_scsi) {
		ZFCP_LOG_INFO("error: pool allocation failed (fsf_req_scsi)\n");
		return -ENOMEM;
	}

	adapter->pool.fsf_req_abort =
		mempool_create(ZFCP_POOL_FSF_REQ_ABORT_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_fsf_req_pool_element));

	if (NULL == adapter->pool.fsf_req_abort) {
		ZFCP_LOG_INFO("error: pool allocation failed "
			      "(fsf_req_abort)\n");
		return -ENOMEM;
	}

	adapter->pool.fsf_req_status_read =
		mempool_create(ZFCP_POOL_STATUS_READ_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free,
			       (void *) sizeof(struct zfcp_fsf_req));

	if (NULL == adapter->pool.fsf_req_status_read) {
		ZFCP_LOG_INFO("error: pool allocation failed "
			      "(fsf_req_status_read\n");
		return -ENOMEM;
	}

	adapter->pool.data_status_read =
		mempool_create(ZFCP_POOL_STATUS_READ_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free,
			       (void *) sizeof(struct fsf_status_read_buffer));

	if (NULL == adapter->pool.data_status_read) {
		ZFCP_LOG_INFO("error: pool allocation failed "
			      "(data_status_read)\n");
		return -ENOMEM;
	}

	adapter->pool.data_gid_pn =
		mempool_create(ZFCP_POOL_DATA_GID_PN_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_gid_pn_data));

	if (NULL == adapter->pool.data_gid_pn) {
		ZFCP_LOG_INFO("error: pool allocation failed (data_gid_pn)\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * zfcp_free_low_mem_buffers - free memory pools of an adapter
 * @adapter: pointer to zfcp_adapter for which memory pools should be freed
 * locking:  zfcp_data.config_sema must be held
 */
static void
zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
	if (adapter->pool.fsf_req_erp)
		mempool_destroy(adapter->pool.fsf_req_erp);
	if (adapter->pool.fsf_req_scsi)
		mempool_destroy(adapter->pool.fsf_req_scsi);
	if (adapter->pool.fsf_req_abort)
		mempool_destroy(adapter->pool.fsf_req_abort);
	if (adapter->pool.fsf_req_status_read)
		mempool_destroy(adapter->pool.fsf_req_status_read);
	if (adapter->pool.data_status_read)
		mempool_destroy(adapter->pool.data_status_read);
	if (adapter->pool.data_gid_pn)
		mempool_destroy(adapter->pool.data_gid_pn);
}

/*
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 *
 * returns:	0             if a new adapter was successfully enqueued
 *              ZFCP_KNOWN    if an adapter with this devno was already present
 *		-ENOMEM       if alloc failed
 * locks:	config_sema must be held to serialise changes to the adapter list
 */
struct zfcp_adapter *
zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	int retval = 0;
	struct zfcp_adapter *adapter;
	char dbf_name[20];

	/*
	 * Note: It is safe to release the list_lock, as any list changes 
	 * are protected by the config_sema, which must be held to get here
	 */

	/* try to allocate new adapter data structure (zeroed) */
	adapter = kmalloc(sizeof (struct zfcp_adapter), GFP_KERNEL);
	if (!adapter) {
		ZFCP_LOG_INFO("error: Allocation of base adapter "
			      "structure failed\n");
		goto out;
	}
	memset(adapter, 0, sizeof (struct zfcp_adapter));

	ccw_device->handler = NULL;

	/* save ccw_device pointer */
	adapter->ccw_device = ccw_device;

	retval = zfcp_qdio_allocate_queues(adapter);
	if (retval)
		goto queues_alloc_failed;

	retval = zfcp_qdio_allocate(adapter);
	if (retval)
		goto qdio_allocate_failed;

	retval = zfcp_allocate_low_mem_buffers(adapter);
	if (retval)
		goto failed_low_mem_buffers;

	/* set magics */
	adapter->common_magic = ZFCP_MAGIC;
	adapter->specific_magic = ZFCP_MAGIC_ADAPTER;

	/* initialise reference count stuff */
	atomic_set(&adapter->refcount, 0);
	init_waitqueue_head(&adapter->remove_wq);

	/* initialise list of ports */
	INIT_LIST_HEAD(&adapter->port_list_head);

	/* initialise list of ports to be removed */
	INIT_LIST_HEAD(&adapter->port_remove_lh);

	/* initialize list of fsf requests */
	rwlock_init(&adapter->fsf_req_list_lock);
	INIT_LIST_HEAD(&adapter->fsf_req_list_head);

	/* initialize abort lock */
	rwlock_init(&adapter->abort_lock);

	/* initialise some erp stuff */
	init_waitqueue_head(&adapter->erp_thread_wqh);
	init_waitqueue_head(&adapter->erp_done_wqh);

	/* initialize lock of associated request queue */
	rwlock_init(&adapter->request_queue.queue_lock);

	/* intitialise SCSI ER timer */
	init_timer(&adapter->scsi_er_timer);

	/* set FC service class used per default */
	adapter->fc_service_class = ZFCP_FC_SERVICE_CLASS_DEFAULT;

	sprintf(adapter->name, "%s", zfcp_get_busid_by_adapter(adapter));
	ASCEBC(adapter->name, strlen(adapter->name));

	/* mark adapter unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	adapter->ccw_device = ccw_device;
	dev_set_drvdata(&ccw_device->dev, adapter);

	if (zfcp_sysfs_adapter_create_files(&ccw_device->dev))
		goto sysfs_failed;

#ifdef ZFCP_DEBUG_REQUESTS
	/* debug feature area which records fsf request sequence numbers */
	sprintf(dbf_name, ZFCP_REQ_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->req_dbf = debug_register(dbf_name,
					  ZFCP_REQ_DBF_INDEX,
					  ZFCP_REQ_DBF_AREAS,
					  ZFCP_REQ_DBF_LENGTH);
	if (!adapter->req_dbf) {
		ZFCP_LOG_INFO
		    ("error: Out of resources. Request debug feature for "
		     "adapter %s could not be generated.\n",
		     zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_req_dbf;
	}
	debug_register_view(adapter->req_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->req_dbf, ZFCP_REQ_DBF_LEVEL);
	debug_text_event(adapter->req_dbf, 1, "zzz");
#endif				/* ZFCP_DEBUG_REQUESTS */

#ifdef ZFCP_DEBUG_COMMANDS
	/* debug feature area which records SCSI command failures (hostbyte) */
	rwlock_init(&adapter->cmd_dbf_lock);
	sprintf(dbf_name, ZFCP_CMD_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->cmd_dbf = debug_register(dbf_name,
					  ZFCP_CMD_DBF_INDEX,
					  ZFCP_CMD_DBF_AREAS,
					  ZFCP_CMD_DBF_LENGTH);
	if (!adapter->cmd_dbf) {
		ZFCP_LOG_INFO
		    ("error: Out of resources. Command debug feature for "
		     "adapter %s could not be generated.\n",
		     zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_cmd_dbf;
	}
	debug_register_view(adapter->cmd_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->cmd_dbf, ZFCP_CMD_DBF_LEVEL);
#endif				/* ZFCP_DEBUG_COMMANDS */

#ifdef ZFCP_DEBUG_ABORTS
	/* debug feature area which records SCSI command aborts */
	sprintf(dbf_name, ZFCP_ABORT_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->abort_dbf = debug_register(dbf_name,
					    ZFCP_ABORT_DBF_INDEX,
					    ZFCP_ABORT_DBF_AREAS,
					    ZFCP_ABORT_DBF_LENGTH);
	if (!adapter->abort_dbf) {
		ZFCP_LOG_INFO
		    ("error: Out of resources. Abort debug feature for "
		     "adapter %s could not be generated.\n",
		     zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_abort_dbf;
	}
	debug_register_view(adapter->abort_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->abort_dbf, ZFCP_ABORT_DBF_LEVEL);
#endif				/* ZFCP_DEBUG_ABORTS */

#ifdef ZFCP_DEBUG_INCOMING_ELS
	/* debug feature area which records SCSI command aborts */
	sprintf(dbf_name, ZFCP_IN_ELS_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->in_els_dbf = debug_register(dbf_name,
					     ZFCP_IN_ELS_DBF_INDEX,
					     ZFCP_IN_ELS_DBF_AREAS,
					     ZFCP_IN_ELS_DBF_LENGTH);
	if (!adapter->in_els_dbf) {
		ZFCP_LOG_INFO("error: Out of resources. ELS debug feature for "
			      "adapter %s could not be generated.\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_in_els_dbf;
	}
	debug_register_view(adapter->in_els_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->in_els_dbf, ZFCP_IN_ELS_DBF_LEVEL);
#endif				/* ZFCP_DEBUG_INCOMING_ELS */

	sprintf(dbf_name, ZFCP_ERP_DBF_NAME "%s",
		zfcp_get_busid_by_adapter(adapter));
	adapter->erp_dbf = debug_register(dbf_name,
					  ZFCP_ERP_DBF_INDEX,
					  ZFCP_ERP_DBF_AREAS,
					  ZFCP_ERP_DBF_LENGTH);
	if (!adapter->erp_dbf) {
		ZFCP_LOG_INFO("error: Out of resources. ERP debug feature for "
			      "adapter %s could not be generated.\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto failed_erp_dbf;
	}
	debug_register_view(adapter->erp_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->erp_dbf, ZFCP_ERP_DBF_LEVEL);

	/* put allocated adapter at list tail */
	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_data.adapters++;

	goto out;

 failed_erp_dbf:
#ifdef ZFCP_DEBUG_INCOMING_ELS
	debug_unregister(adapter->in_els_dbf);
 failed_in_els_dbf:
#endif

#ifdef ZFCP_DEBUG_ABORTS
	debug_unregister(adapter->abort_dbf);
 failed_abort_dbf:
#endif

#ifdef ZFCP_DEBUG_COMMANDS
	debug_unregister(adapter->cmd_dbf);
 failed_cmd_dbf:
#endif

#ifdef ZFCP_DEBUG_REQUESTS
	debug_unregister(adapter->req_dbf);
 failed_req_dbf:
#endif
	zfcp_sysfs_adapter_remove_files(&ccw_device->dev);
 sysfs_failed:
	dev_set_drvdata(&ccw_device->dev, NULL);
 failed_low_mem_buffers:
	zfcp_free_low_mem_buffers(adapter);
	if (qdio_free(ccw_device) != 0)
		ZFCP_LOG_NORMAL
		    ("bug: could not free memory used by data transfer "
		     "mechanism for adapter %s\n",
		     zfcp_get_busid_by_adapter(adapter));
 qdio_allocate_failed:
	zfcp_qdio_free_queues(adapter);
 queues_alloc_failed:
	kfree(adapter);
	adapter = NULL;
 out:
	return adapter;
}

/*
 * returns:	0 - struct zfcp_adapter  data structure successfully removed
 *		!0 - struct zfcp_adapter  data structure could not be removed
 *			(e.g. still used)
 * locks:	adapter list write lock is assumed to be held by caller
 *              adapter->fsf_req_list_lock is taken and released within this 
 *              function and must not be held on entry
 */
void
zfcp_adapter_dequeue(struct zfcp_adapter *adapter)
{
	int retval = 0;
	unsigned long flags;

	zfcp_sysfs_adapter_remove_files(&adapter->ccw_device->dev);
	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	/* sanity check: no pending FSF requests */
	read_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	retval = !list_empty(&adapter->fsf_req_list_head);
	read_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
	if (retval) {
		ZFCP_LOG_NORMAL("bug: Adapter %s is still in use, "
				"%i requests are still outstanding "
				"(debug info 0x%lx)\n",
				zfcp_get_busid_by_adapter(adapter),
				atomic_read(&adapter->fsf_reqs_active),
				(unsigned long) adapter);
		retval = -EBUSY;
		goto out;
	}

	/* remove specified adapter data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&adapter->list);
	write_unlock_irq(&zfcp_data.config_lock);

	/* decrease number of adapters in list */
	zfcp_data.adapters--;

	ZFCP_LOG_TRACE("adapter 0x%lx removed from list, "
		       "%i adapters still in list\n",
		       (unsigned long) adapter, zfcp_data.adapters);

	retval = qdio_free(adapter->ccw_device);
	if (retval)
		ZFCP_LOG_NORMAL
		    ("bug: could not free memory used by data transfer "
		     "mechanism for adapter %s\n",
		     zfcp_get_busid_by_adapter(adapter));

	debug_unregister(adapter->erp_dbf);

#ifdef ZFCP_DEBUG_REQUESTS
	debug_unregister(adapter->req_dbf);
#endif

#ifdef ZFCP_DEBUG_COMMANDS
	debug_unregister(adapter->cmd_dbf);
#endif
#ifdef ZFCP_DEBUG_ABORTS
	debug_unregister(adapter->abort_dbf);
#endif

#ifdef ZFCP_DEBUG_INCOMING_ELS
	debug_unregister(adapter->in_els_dbf);
#endif

	zfcp_free_low_mem_buffers(adapter);
	/* free memory of adapter data structure and queues */
	zfcp_qdio_free_queues(adapter);
	ZFCP_LOG_TRACE("Freeing adapter structure.\n");
	kfree(adapter);
 out:
	return;
}

/*
 * Enqueues a remote port at the end of the port list.
 * All port internal structures are set-up and the proc-fs entry is also 
 * allocated. Some SCSI-stack structures are modified for the port.
 *
 * returns:	0            if a new port was successfully enqueued
 *              ZFCP_KNOWN   if a port with the requested wwpn already exists
 *              -ENOMEM      if allocation failed
 *              -EINVAL      if at least one of the specified parameters was wrong
 * locks:       config_sema must be held to serialise changes to the port list
 *              within this function (must not be held on entry)
 */
struct zfcp_port *
zfcp_port_enqueue(struct zfcp_adapter *adapter, wwn_t wwpn, u32 status)
{
	struct zfcp_port *port;
	int check_scsi_id;
	int check_wwpn;

	check_scsi_id = !(status & ZFCP_STATUS_PORT_NO_SCSI_ID);
	check_wwpn = !(status & ZFCP_STATUS_PORT_NO_WWPN);

	/*
	 * check that there is no port with this WWPN already in list
	 */
	if (check_wwpn) {
		read_lock_irq(&zfcp_data.config_lock);
		port = zfcp_get_port_by_wwpn(adapter, wwpn);
		read_unlock_irq(&zfcp_data.config_lock);
		if (port)
			return NULL;
	}

	port = kmalloc(sizeof (struct zfcp_port), GFP_KERNEL);
	if (!port)
		return NULL;
	memset(port, 0, sizeof (struct zfcp_port));

	/* initialise reference count stuff */
	atomic_set(&port->refcount, 0);
	init_waitqueue_head(&port->remove_wq);

	INIT_LIST_HEAD(&port->unit_list_head);
	INIT_LIST_HEAD(&port->unit_remove_lh);

	port->adapter = adapter;

	if (check_scsi_id)
		port->scsi_id = adapter->max_scsi_id + 1;

	if (check_wwpn)
		port->wwpn = wwpn;

	atomic_set_mask(status, &port->status);

	port->common_magic = ZFCP_MAGIC;
	port->specific_magic = ZFCP_MAGIC_PORT;

	/* setup for sysfs registration */
	if (status & ZFCP_STATUS_PORT_NAMESERVER)
		snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE, "nameserver");
	else
		snprintf(port->sysfs_device.bus_id,
			 BUS_ID_SIZE, "0x%016llx", wwpn);
	port->sysfs_device.parent = &adapter->ccw_device->dev;
	port->sysfs_device.release = zfcp_sysfs_port_release;
	dev_set_drvdata(&port->sysfs_device, port);

	/* mark port unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);

	if (device_register(&port->sysfs_device)) {
		kfree(port);
		return NULL;
	}

	if (zfcp_sysfs_port_create_files(&port->sysfs_device, status)) {
		/*
		 * failed to create all sysfs attributes, therefore the port
		 * must be put on the port_remove listhead of the adapter
		 * where the release function expects it.
		 */
		write_lock_irq(&zfcp_data.config_lock);
		list_add_tail(&port->list, &adapter->port_remove_lh);
		write_unlock_irq(&zfcp_data.config_lock);
		device_unregister(&port->sysfs_device);
		return NULL;
	}

	if (check_scsi_id) {
		/*
		 * update max. SCSI ID of remote ports attached to
		 * "parent" adapter if necessary
		 * (do not care about the adapters own SCSI ID)
		 */
		adapter->max_scsi_id++;

		/*
		 * update max. SCSI ID of remote ports attached to
		 * "parent" host (SCSI stack) if necessary
		 */
		if (adapter->scsi_host &&
		    (adapter->scsi_host->max_id < adapter->max_scsi_id + 1))
			adapter->scsi_host->max_id = adapter->max_scsi_id + 1;
	}

	zfcp_port_get(port);

	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);
	list_add_tail(&port->list, &adapter->port_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	adapter->ports++;
	zfcp_adapter_get(adapter);

	return port;
}

/*
 * returns:	0 - struct zfcp_port data structure successfully removed
 *		!0 - struct zfcp_port data structure could not be removed
 *			(e.g. still used)
 * locks :	port list write lock is assumed to be held by caller
 */
void
zfcp_port_dequeue(struct zfcp_port *port)
{
	/* remove specified port from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&port->list);
	write_unlock_irq(&zfcp_data.config_lock);

	port->adapter->ports--;
	zfcp_adapter_put(port->adapter);

	kfree(port);

	return;
}

/* Enqueues a nameserver port */
int
zfcp_nameserver_enqueue(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	/* generate port structure */
	port = zfcp_port_enqueue(adapter, 0, ZFCP_STATUS_PORT_NAMESERVER);
	if (!port) {
		ZFCP_LOG_INFO("error: Could not establish a connection to the "
			      "fabric name server connected to the "
			      "adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		return -ENXIO;
	}
	/* set special D_ID */
	port->d_id = ZFCP_DID_NAMESERVER;
	adapter->nameserver_port = port;
	zfcp_port_put(port);

	return 0;
}

#undef ZFCP_LOG_AREA

/****************************************************************/
/******* Fibre Channel Standard related Functions  **************/
/****************************************************************/

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FC

void
zfcp_fsf_incoming_els_rscn(struct zfcp_adapter *adapter,
			   struct fsf_status_read_buffer *status_buffer)
{
	struct fcp_rscn_head *fcp_rscn_head;
	struct fcp_rscn_element *fcp_rscn_element;
	struct zfcp_port *port;
	u16 i;
	u16 no_entries;
	u32 range_mask;
	unsigned long flags;

	fcp_rscn_head = (struct fcp_rscn_head *) status_buffer->payload;
	fcp_rscn_element = (struct fcp_rscn_element *) status_buffer->payload;

	/* see FC-FS */
	no_entries = (fcp_rscn_head->payload_len / 4);

	zfcp_in_els_dbf_event(adapter, "##rscn", status_buffer,
			      fcp_rscn_head->payload_len);

	debug_text_event(adapter->erp_dbf, 1, "unsol_els_rscn:");
	for (i = 1; i < no_entries; i++) {
		/* skip head and start with 1st element */
		fcp_rscn_element++;
		switch (fcp_rscn_element->addr_format) {
		case ZFCP_PORT_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_PORT_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_PORT;
			break;
		case ZFCP_AREA_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_AREA_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_AREA;
			break;
		case ZFCP_DOMAIN_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_DOMAIN_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_DOMAIN;
			break;
		case ZFCP_FABRIC_ADDRESS:
			ZFCP_LOG_FLAGS(1, "ZFCP_FABRIC_ADDRESS\n");
			range_mask = ZFCP_PORTS_RANGE_FABRIC;
			break;
		default:
			ZFCP_LOG_INFO("Received RSCN with unknown "
				      "address format.\n");
			continue;
		}
		read_lock_irqsave(&zfcp_data.config_lock, flags);
		list_for_each_entry(port, &adapter->port_list_head, list) {
			if (atomic_test_mask
			    (ZFCP_STATUS_PORT_NAMESERVER, &port->status))
				continue;
			/* Do we know this port? If not skip it. */
			if (!atomic_test_mask
			    (ZFCP_STATUS_PORT_DID_DID, &port->status)) {
				ZFCP_LOG_INFO
					("Received state change notification."
					 "Trying to open the port with wwpn "
					 "0x%Lx. Hope it's there now.\n",
					 port->wwpn);
				debug_text_event(adapter->erp_dbf, 1,
						 "unsol_els_rscnu:");
				zfcp_erp_port_reopen(port,
						     ZFCP_STATUS_COMMON_ERP_FAILED);
				continue;
			}

			/*
			 * FIXME: race: d_id might being invalidated
			 * (...DID_DID reset)
			 */
			if ((port->d_id & range_mask)
			    == (fcp_rscn_element->nport_did & range_mask)) {
				ZFCP_LOG_TRACE("reopen did 0x%x\n",
					       fcp_rscn_element->nport_did);
				/*
				 * Unfortunately, an RSCN does not specify the
				 * type of change a target underwent. We assume
				 * that it makes sense to reopen the link.
				 * FIXME: Shall we try to find out more about
				 * the target and link state before closing it?
				 * How to accomplish this? (nameserver?)
				 * Where would such code be put in?
				 * (inside or outside erp)
				 */
				ZFCP_LOG_INFO
				    ("Received state change notification."
				     "Trying to reopen the port with wwpn "
				     "0x%Lx.\n", port->wwpn);
				debug_text_event(adapter->erp_dbf, 1,
						 "unsol_els_rscnk:");
				zfcp_test_link(port);
			}
		}
		read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	}
}

static void
zfcp_fsf_incoming_els_plogi(struct zfcp_adapter *adapter,
			    struct fsf_status_read_buffer *status_buffer)
{
	logi *els_logi = (logi *) status_buffer->payload;
	struct zfcp_port *port;
	unsigned long flags;

	zfcp_in_els_dbf_event(adapter, "##plogi", status_buffer, 28);

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (port->wwpn == (*(wwn_t *) & els_logi->nport_wwn))
			break;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->wwpn != (*(wwn_t *) & els_logi->nport_wwn))) {
		ZFCP_LOG_DEBUG("Re-open port indication received "
			       "for the non-existing port with D_ID "
			       "0x%3.3x, on the adapter "
			       "%s. Ignored.\n",
			       status_buffer->d_id,
			       zfcp_get_busid_by_adapter(adapter));
	} else {
		debug_text_event(adapter->erp_dbf, 1, "unsol_els_plogi:");
		debug_event(adapter->erp_dbf, 1, &els_logi->nport_wwn, 8);
		zfcp_erp_port_forced_reopen(port, 0);
	}
}

static void
zfcp_fsf_incoming_els_logo(struct zfcp_adapter *adapter,
			   struct fsf_status_read_buffer *status_buffer)
{
	struct fcp_logo *els_logo = (struct fcp_logo *) status_buffer->payload;
	struct zfcp_port *port;
	unsigned long flags;

	zfcp_in_els_dbf_event(adapter, "##logo", status_buffer, 16);

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (port->wwpn == els_logo->nport_wwpn)
			break;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->wwpn != els_logo->nport_wwpn)) {
		ZFCP_LOG_DEBUG("Re-open port indication received "
			       "for the non-existing port with D_ID "
			       "0x%3.3x, on the adapter "
			       "%s. Ignored.\n",
			       status_buffer->d_id,
			       zfcp_get_busid_by_adapter(adapter));
	} else {
		debug_text_event(adapter->erp_dbf, 1, "unsol_els_logo:");
		debug_event(adapter->erp_dbf, 1, &els_logo->nport_wwpn, 8);
		zfcp_erp_port_forced_reopen(port, 0);
	}
}

static void
zfcp_fsf_incoming_els_unknown(struct zfcp_adapter *adapter,
			      struct fsf_status_read_buffer *status_buffer)
{
	zfcp_in_els_dbf_event(adapter, "##undef", status_buffer, 24);
	ZFCP_LOG_NORMAL("warning: Unknown incoming ELS (0x%x) received "
			"for the adapter %s\n",
			*(u32 *) (status_buffer->payload),
			zfcp_get_busid_by_adapter(adapter));

}

void
zfcp_fsf_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer;
	u32 els_type;
	struct zfcp_adapter *adapter;

	status_buffer = fsf_req->data.status_read.buffer;
	els_type = *(u32 *) (status_buffer->payload);
	adapter = fsf_req->adapter;

	if (els_type == LS_PLOGI)
		zfcp_fsf_incoming_els_plogi(adapter, status_buffer);
	else if (els_type == LS_LOGO)
		zfcp_fsf_incoming_els_logo(adapter, status_buffer);
	else if ((els_type & 0xffff0000) == LS_RSCN)
		/* we are only concerned with the command, not the length */
		zfcp_fsf_incoming_els_rscn(adapter, status_buffer);
	else
		zfcp_fsf_incoming_els_unknown(adapter, status_buffer);

}


/**
 * zfcp_gid_pn_buffers_alloc - allocate buffers for GID_PN nameserver request
 * @gid_pn: pointer to return pointer to struct zfcp_gid_pn_data
 * @pool: pointer to mempool_t if non-null memory pool is used for allocation
 */
static int
zfcp_gid_pn_buffers_alloc(struct zfcp_gid_pn_data **gid_pn, mempool_t *pool)
{
	struct zfcp_gid_pn_data *data;

	if (pool != NULL) {
		data = mempool_alloc(pool, GFP_ATOMIC);
		if (likely(data != NULL)) {
			data->ct.pool = pool;
		}
	} else {
		data = kmalloc(sizeof(struct zfcp_gid_pn_data), GFP_ATOMIC);
		}

        if (NULL == data){
		ZFCP_LOG_DEBUG("Out of memory.\n");
                return -ENOMEM;
	}

	memset(data, 0, sizeof(*data));
        data->ct.req = &data->req;
        data->ct.resp = &data->resp;
	data->ct.req_count = data->ct.resp_count = 1;
	zfcp_address_to_sg(&data->ct_iu_req, &data->req);
        zfcp_address_to_sg(&data->ct_iu_resp, &data->resp);
        data->req.length = sizeof(struct ct_iu_gid_pn_req);
        data->resp.length = sizeof(struct ct_iu_gid_pn_resp);

	*gid_pn = data;
	return 0;
}

/**
 * zfcp_gid_pn_buffers_free - free buffers for GID_PN nameserver request
 * @gid_pn: pointer to struct zfcp_gid_pn_data which has to be freed
 */
static void
zfcp_gid_pn_buffers_free(struct zfcp_gid_pn_data *gid_pn)
{
        if ((gid_pn->ct.pool != 0)) {
		mempool_free(gid_pn, gid_pn->ct.pool);
        } else {
                kfree(gid_pn);
	}

	return;
}

/**
 * zfcp_ns_gid_pn_request - initiate GID_PN nameserver request
 * @erp_action: pointer to zfcp_erp_action where GID_PN request is needed
 */
int
zfcp_ns_gid_pn_request(struct zfcp_erp_action *erp_action)
{
	int ret;
        struct ct_iu_gid_pn_req *ct_iu_req;
        struct zfcp_gid_pn_data *gid_pn;
        struct zfcp_adapter *adapter = erp_action->adapter;

	ret = zfcp_gid_pn_buffers_alloc(&gid_pn, adapter->pool.data_gid_pn);
	if (ret < 0) {
		ZFCP_LOG_INFO("error: Out of memory. Could not allocate "
                              "buffers for nameserver request GID_PN. "
                              "(adapter: %s)\n",
			      zfcp_get_busid_by_adapter(adapter));
	goto out;
	}

	/* setup nameserver request */
        ct_iu_req = zfcp_sg_to_address(gid_pn->ct.req);
        ct_iu_req->header.revision = ZFCP_CT_REVISION;
        ct_iu_req->header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        ct_iu_req->header.gs_subtype = ZFCP_CT_NAME_SERVER;
        ct_iu_req->header.options = ZFCP_CT_SYNCHRONOUS;
        ct_iu_req->header.cmd_rsp_code = ZFCP_CT_GID_PN;
        ct_iu_req->header.max_res_size = ZFCP_CT_MAX_SIZE;
	ct_iu_req->wwpn = erp_action->port->wwpn;

        /* setup parameters for send generic command */
        gid_pn->ct.port = adapter->nameserver_port;
	gid_pn->ct.handler = zfcp_ns_gid_pn_handler;
	gid_pn->ct.handler_data = (unsigned long) gid_pn;
        gid_pn->ct.timeout = ZFCP_NS_GID_PN_TIMEOUT;
        gid_pn->ct.timer = &erp_action->timer;
	gid_pn->port = erp_action->port;

	ret = zfcp_fsf_send_ct(&gid_pn->ct, adapter->pool.fsf_req_erp,
			       erp_action);
	if (ret) {
		ZFCP_LOG_INFO("error: Could not send nameserver request GID_PN."
                              "(adapter %s)\n",
			      zfcp_get_busid_by_adapter(adapter));

                zfcp_gid_pn_buffers_free(gid_pn);
	}

 out:
	return ret;
}

/**
 * zfcp_ns_gid_pn_handler - handler for GID_PN nameserver request
 * @data: unsigned long, contains pointer to struct zfcp_gid_pn_data
 */
static void zfcp_ns_gid_pn_handler(unsigned long data)
{
	struct zfcp_port *port;
        struct zfcp_send_ct *ct;
	struct ct_iu_gid_pn_req *ct_iu_req;
	struct ct_iu_gid_pn_resp *ct_iu_resp;
        struct zfcp_gid_pn_data *gid_pn;


	gid_pn = (struct zfcp_gid_pn_data *) data;
	port = gid_pn->port;
        ct = &gid_pn->ct;
	ct_iu_req = zfcp_sg_to_address(ct->req);
	ct_iu_resp = zfcp_sg_to_address(ct->resp);

        if (ct_iu_resp->header.revision != ZFCP_CT_REVISION)
		goto failed;
        if (ct_iu_resp->header.gs_type != ZFCP_CT_DIRECTORY_SERVICE)
		goto failed;
        if (ct_iu_resp->header.gs_subtype != ZFCP_CT_NAME_SERVER)
		goto failed;
        if (ct_iu_resp->header.options != ZFCP_CT_SYNCHRONOUS)
		goto failed;
        if (ct_iu_resp->header.cmd_rsp_code != ZFCP_CT_ACCEPT) {
		/* FIXME: do we need some specific erp entry points */
		atomic_set_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status);
		goto failed;
	}
	/* paranoia */
	if (ct_iu_req->wwpn != port->wwpn) {
		ZFCP_LOG_NORMAL(
			"bug: Port WWPN returned by nameserver lookup "
                        "does not correspond to the expected value "
			"(adapter: %s, debug info: 0x%016Lx, 0x%016Lx)\n",
			zfcp_get_busid_by_port(port), port->wwpn,
                        ct_iu_req->wwpn);
		goto failed;
	}

	/* looks like a valid d_id */
        port->d_id = ct_iu_resp->d_id & ZFCP_DID_MASK;
	atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
	ZFCP_LOG_DEBUG("busid %s:  WWPN=0x%Lx ---> D_ID=0x%6.6x\n",
		       zfcp_get_busid_by_port(port),
		       port->wwpn, (unsigned int) port->d_id);
	goto out;

failed:
	ZFCP_LOG_NORMAL("warning: WWPN 0x%Lx not found by nameserver lookup "
			"(adapter: %s)\n",
			port->wwpn, zfcp_get_busid_by_port(port));
	ZFCP_LOG_DEBUG("CT IUs do not match:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG, (char *) ct_iu_req,
		      sizeof(struct ct_iu_gid_pn_req));
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG, (char *) ct_iu_resp,
		      sizeof(struct ct_iu_gid_pn_resp));

 out:
        zfcp_gid_pn_buffers_free(gid_pn);
	return;
}

#undef ZFCP_LOG_AREA
