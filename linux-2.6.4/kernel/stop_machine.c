#include <linux/stop_machine.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>

/* Since we effect priority and affinity (both of which are visible
 * to, and settable by outside processes) we do indirection via a
 * kthread. */

/* Thread to stop each CPU in user context. */
enum stopmachine_state {
	STOPMACHINE_WAIT,
	STOPMACHINE_PREPARE,
	STOPMACHINE_DISABLE_IRQ,
	STOPMACHINE_EXIT,
};

static enum stopmachine_state stopmachine_state;
static unsigned int stopmachine_num_threads;
static atomic_t stopmachine_thread_ack;
static DECLARE_MUTEX(stopmachine_mutex);

static int stopmachine(void *cpu)
{
	int irqs_disabled = 0;
	int prepared = 0;

	set_cpus_allowed(current, cpumask_of_cpu((int)(long)cpu));

	/* Ack: we are alive */
	mb(); /* Theoretically the ack = 0 might not be on this CPU yet. */
	atomic_inc(&stopmachine_thread_ack);

	/* Simple state machine */
	while (stopmachine_state != STOPMACHINE_EXIT) {
		if (stopmachine_state == STOPMACHINE_DISABLE_IRQ 
		    && !irqs_disabled) {
			local_irq_disable();
			irqs_disabled = 1;
			/* Ack: irqs disabled. */
			mb(); /* Must read state first. */
			atomic_inc(&stopmachine_thread_ack);
		} else if (stopmachine_state == STOPMACHINE_PREPARE
			   && !prepared) {
			/* Everyone is in place, hold CPU. */
			preempt_disable();
			prepared = 1;
			mb(); /* Must read state first. */
			atomic_inc(&stopmachine_thread_ack);
		}
		cpu_relax();
	}

	/* Ack: we are exiting. */
	mb(); /* Must read state first. */
	atomic_inc(&stopmachine_thread_ack);

	if (irqs_disabled)
		local_irq_enable();
	if (prepared)
		preempt_enable();

	return 0;
}

/* Change the thread state */
static void stopmachine_set_state(enum stopmachine_state state)
{
	atomic_set(&stopmachine_thread_ack, 0);
	wmb();
	stopmachine_state = state;
	while (atomic_read(&stopmachine_thread_ack) != stopmachine_num_threads)
		cpu_relax();
}

static int stop_machine(void)
{
	int i, ret = 0;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	/* One high-prio thread per cpu.  We'll do this one. */
	sys_sched_setscheduler(current->pid, SCHED_FIFO, &param);

	atomic_set(&stopmachine_thread_ack, 0);
	stopmachine_num_threads = 0;
	stopmachine_state = STOPMACHINE_WAIT;

	for_each_online_cpu(i) {
		if (i == smp_processor_id())
			continue;
		ret = kernel_thread(stopmachine, (void *)(long)i,CLONE_KERNEL);
		if (ret < 0)
			break;
		stopmachine_num_threads++;
	}

	/* Wait for them all to come to life. */
	while (atomic_read(&stopmachine_thread_ack) != stopmachine_num_threads)
		yield();

	/* If some failed, kill them all. */
	if (ret < 0) {
		stopmachine_set_state(STOPMACHINE_EXIT);
		up(&stopmachine_mutex);
		return ret;
	}

	/* Don't schedule us away at this point, please. */
	local_irq_disable();

	/* Now they are all started, make them hold the CPUs, ready. */
	stopmachine_set_state(STOPMACHINE_PREPARE);

	/* Make them disable irqs. */
	stopmachine_set_state(STOPMACHINE_DISABLE_IRQ);

	return 0;
}

static void restart_machine(void)
{
	stopmachine_set_state(STOPMACHINE_EXIT);
	local_irq_enable();
}

struct stop_machine_data
{
	int (*fn)(void *);
	void *data;
	struct completion done;
};

static int do_stop(void *_smdata)
{
	struct stop_machine_data *smdata = _smdata;
	int ret;

	ret = stop_machine();
	if (ret == 0) {
		ret = smdata->fn(smdata->data);
		restart_machine();
	}

	/* We're done: you can kthread_stop us now */
	complete(&smdata->done);

	/* Wait for kthread_stop */
	while (!kthread_should_stop()) {
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return ret;
}

struct task_struct *__stop_machine_run(int (*fn)(void *), void *data,
				       unsigned int cpu)
{
	struct stop_machine_data smdata;
	struct task_struct *p;

	smdata.fn = fn;
	smdata.data = data;
	init_completion(&smdata.done);

	down(&stopmachine_mutex);

	/* If they don't care which CPU fn runs on, bind to any online one. */
	if (cpu == NR_CPUS)
		cpu = smp_processor_id();

	p = kthread_create(do_stop, &smdata, "kstopmachine");
	if (!IS_ERR(p)) {
		kthread_bind(p, cpu);
		wake_up_process(p);
		wait_for_completion(&smdata.done);
	}
	up(&stopmachine_mutex);
	return p;
}

int stop_machine_run(int (*fn)(void *), void *data, unsigned int cpu)
{
	struct task_struct *p;
	int ret;

	/* No CPUs can come up or down during this. */
	lock_cpu_hotplug();
	p = __stop_machine_run(fn, data, cpu);
	if (!IS_ERR(p))
		ret = kthread_stop(p);
	else
		ret = PTR_ERR(p);
	unlock_cpu_hotplug();

	return ret;
}
