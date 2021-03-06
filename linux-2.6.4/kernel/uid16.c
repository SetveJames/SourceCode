/*
 *	Wrapper functions for 16bit uid back compatibility. All nicely tied
 *	together in the faint hope we can take the out in five years time.
 */

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>

asmlinkage long sys_chown16(const char * filename, old_uid_t user, old_gid_t group)
{
	return sys_chown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys_lchown16(const char * filename, old_uid_t user, old_gid_t group)
{
	return sys_lchown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys_fchown16(unsigned int fd, old_uid_t user, old_gid_t group)
{
	return sys_fchown(fd, low2highuid(user), low2highgid(group));
}

asmlinkage long sys_setregid16(old_gid_t rgid, old_gid_t egid)
{
	return sys_setregid(low2highgid(rgid), low2highgid(egid));
}

asmlinkage long sys_setgid16(old_gid_t gid)
{
	return sys_setgid((gid_t)gid);
}

asmlinkage long sys_setreuid16(old_uid_t ruid, old_uid_t euid)
{
	return sys_setreuid(low2highuid(ruid), low2highuid(euid));
}

asmlinkage long sys_setuid16(old_uid_t uid)
{
	return sys_setuid((uid_t)uid);
}

asmlinkage long sys_setresuid16(old_uid_t ruid, old_uid_t euid, old_uid_t suid)
{
	return sys_setresuid(low2highuid(ruid), low2highuid(euid),
		low2highuid(suid));
}

asmlinkage long sys_getresuid16(old_uid_t *ruid, old_uid_t *euid, old_uid_t *suid)
{
	int retval;

	if (!(retval = put_user(high2lowuid(current->uid), ruid)) &&
	    !(retval = put_user(high2lowuid(current->euid), euid)))
		retval = put_user(high2lowuid(current->suid), suid);

	return retval;
}

asmlinkage long sys_setresgid16(old_gid_t rgid, old_gid_t egid, old_gid_t sgid)
{
	return sys_setresgid(low2highgid(rgid), low2highgid(egid),
		low2highgid(sgid));
}

asmlinkage long sys_getresgid16(old_gid_t *rgid, old_gid_t *egid, old_gid_t *sgid)
{
	int retval;

	if (!(retval = put_user(high2lowgid(current->gid), rgid)) &&
	    !(retval = put_user(high2lowgid(current->egid), egid)))
		retval = put_user(high2lowgid(current->sgid), sgid);

	return retval;
}

asmlinkage long sys_setfsuid16(old_uid_t uid)
{
	return sys_setfsuid((uid_t)uid);
}

asmlinkage long sys_setfsgid16(old_gid_t gid)
{
	return sys_setfsgid((gid_t)gid);
}

static int groups16_to_user(old_gid_t __user *grouplist,
    struct group_info *group_info)
{
	int i;
	old_gid_t group;

	for (i = 0; i < group_info->ngroups; i++) {
		group = (old_gid_t)GROUP_AT(group_info, i);
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info,
    old_gid_t __user *grouplist)
{
	int i;
	old_gid_t group;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;
		GROUP_AT(group_info, i) = (gid_t)group;
	}

	return 0;
}

asmlinkage long sys_getgroups16(int gidsetsize, old_gid_t __user *grouplist)
{
	int i = 0;

	if (gidsetsize < 0)
		return -EINVAL;

	get_group_info(current->group_info);
	i = current->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups16_to_user(grouplist, current->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	put_group_info(current->group_info);
	return i;
}

asmlinkage long sys_setgroups16(int gidsetsize, old_gid_t __user *grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!capable(CAP_SETGID))
		return -EPERM;
	if ((unsigned)gidsetsize > NGROUPS_MAX)
		return -EINVAL;

	group_info = groups_alloc(gidsetsize);
	if (!group_info)
		return -ENOMEM;
	retval = groups16_from_user(group_info, grouplist);
	if (retval) {
		put_group_info(group_info);
		return retval;
	}

	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

asmlinkage long sys_getuid16(void)
{
	return high2lowuid(current->uid);
}

asmlinkage long sys_geteuid16(void)
{
	return high2lowuid(current->euid);
}

asmlinkage long sys_getgid16(void)
{
	return high2lowgid(current->gid);
}

asmlinkage long sys_getegid16(void)
{
	return high2lowgid(current->egid);
}
