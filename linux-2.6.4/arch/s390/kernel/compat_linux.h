#ifndef _ASM_S390X_S390_H
#define _ASM_S390X_S390_H

#include <linux/config.h>
#include <linux/compat.h>
#include <linux/socket.h>
#include <linux/syscalls.h>
#include <linux/nfs_fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/export.h>

/* Macro that masks the high order bit of an 32 bit pointer and converts it*/
/*       to a 64 bit pointer */
#define A(__x) ((unsigned long)((__x) & 0x7FFFFFFFUL))
#define AA(__x)				\
	((unsigned long)(__x))

/* Now 32bit compatibility types */
struct ipc_kludge_32 {
        __u32   msgp;                           /* pointer              */
        __s32   msgtyp;
};

struct old_sigaction32 {
       __u32			sa_handler;	/* Really a pointer, but need to deal with 32 bits */
       compat_old_sigset_t	sa_mask;	/* A 32 bit mask */
       __u32			sa_flags;
       __u32			sa_restorer;	/* Another 32 bit pointer */
};
 
typedef union sigval32 {
        int     sival_int;
        __u32   sival_ptr;
} sigval_t32;
                 
typedef struct siginfo32 {
	int	si_signo;
	int	si_errno;
	int	si_code;

	union {
		int _pad[((128/sizeof(int)) - 3)];

		/* kill() */
		struct {
			pid_t	_pid;	/* sender's pid */
			uid_t	_uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int	_timer1;
			unsigned int	_timer2;
                
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t			_pid;	/* sender's pid */
			uid_t			_uid;	/* sender's uid */
			sigval_t32		_sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t			_pid;	/* which child */
			uid_t			_uid;	/* sender's uid */
			int			_status;/* exit code */
			compat_clock_t		_utime;
			compat_clock_t		_stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			__u32	_addr;	/* faulting insn/memory ref. - pointer */
		} _sigfault;
                          
		/* SIGPOLL */
		struct {
			int	_band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int	_fd;
		} _sigpoll;
	} _sifields;
} siginfo_t32;  

/*
 * How these fields are to be accessed.
 */
#define si_pid		_sifields._kill._pid
#define si_uid		_sifields._kill._uid
#define si_status	_sifields._sigchld._status
#define si_utime	_sifields._sigchld._utime
#define si_stime	_sifields._sigchld._stime
#define si_value	_sifields._rt._sigval
#define si_int		_sifields._rt._sigval.sival_int
#define si_ptr		_sifields._rt._sigval.sival_ptr
#define si_addr		_sifields._sigfault._addr
#define si_band		_sifields._sigpoll._band
#define si_fd		_sifields._sigpoll._fd    

/* asm/sigcontext.h */
typedef union
{
	__u64   d;
	__u32   f; 
} freg_t32;

typedef struct
{
	unsigned int	fpc;
	freg_t32	fprs[__NUM_FPRS];              
} _s390_fp_regs32;

typedef struct 
{
        __u32   mask;
        __u32	addr;
} _psw_t32 __attribute__ ((aligned(8)));

#define PSW32_MASK_PER		0x40000000UL
#define PSW32_MASK_DAT		0x04000000UL
#define PSW32_MASK_IO		0x02000000UL
#define PSW32_MASK_EXT		0x01000000UL
#define PSW32_MASK_KEY		0x00F00000UL
#define PSW32_MASK_MCHECK	0x00040000UL
#define PSW32_MASK_WAIT		0x00020000UL
#define PSW32_MASK_PSTATE	0x00010000UL
#define PSW32_MASK_ASC		0x0000C000UL
#define PSW32_MASK_CC		0x00003000UL
#define PSW32_MASK_PM		0x00000f00UL

#define PSW32_ADDR_AMODE31	0x80000000UL
#define PSW32_ADDR_INSN		0x7FFFFFFFUL

#define PSW32_BASE_BITS		0x00080000UL

#define PSW32_ASC_PRIMARY	0x00000000UL
#define PSW32_ASC_ACCREG	0x00004000UL
#define PSW32_ASC_SECONDARY	0x00008000UL
#define PSW32_ASC_HOME		0x0000C000UL

#define PSW32_USER_BITS	(PSW32_BASE_BITS | PSW32_MASK_DAT | PSW32_ASC_HOME | \
			 PSW32_MASK_IO | PSW32_MASK_EXT | PSW32_MASK_MCHECK | \
			 PSW32_MASK_PSTATE)

typedef struct
{
	_psw_t32	psw;
	__u32		gprs[__NUM_GPRS];
	__u32		acrs[__NUM_ACRS];
} _s390_regs_common32;

typedef struct
{
	_s390_regs_common32 regs;
	_s390_fp_regs32     fpregs;
} _sigregs32;

#define _SIGCONTEXT_NSIG32	64
#define _SIGCONTEXT_NSIG_BPW32	32
#define __SIGNAL_FRAMESIZE32	96
#define _SIGMASK_COPY_SIZE32	(sizeof(u32)*2)

struct sigcontext32
{
	__u32	oldmask[_COMPAT_NSIG_WORDS];
	__u32	sregs;				/* pointer */
};

/* asm/signal.h */
struct sigaction32 {
	__u32		sa_handler;		/* pointer */
	__u32		sa_flags;
        __u32		sa_restorer;		/* pointer */
	compat_sigset_t	sa_mask;        /* mask last for extensibility */
};

typedef struct {
	__u32			ss_sp;		/* pointer */
	int			ss_flags;
	compat_size_t		ss_size;
} stack_t32;

/* asm/ucontext.h */
struct ucontext32 {
	__u32			uc_flags;
	__u32			uc_link;	/* pointer */	
	stack_t32		uc_stack;
	_sigregs32		uc_mcontext;
	compat_sigset_t		uc_sigmask;	/* mask last for extensibility */
};

#define SIGEV_PAD_SIZE32 ((SIGEV_MAX_SIZE/sizeof(int)) - 3)
struct sigevent32 {
	union {
		int sival_int;
		u32 sival_ptr;
	} sigev_value;
	int sigev_signo;
	int sigev_notify;
	union {
		int _pad[SIGEV_PAD_SIZE32];
		int _tid;
		struct {
			u32 *_function;
			u32 *_attribute;
		} _sigev_thread;
	} _sigev_un;
};

#endif /* _ASM_S390X_S390_H */
