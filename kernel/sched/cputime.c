#include <linux/export.h>
#include <linux/sched.h>
#include <linux/tsacct_kern.h>
#include <linux/kernel_stat.h>
//#include <linux/static_key.h>
//#include <linux/context_tracking.h>
#include "sched.h"


#ifdef CONFIG_IRQ_TIME_ACCOUNTING

/*
 * There are no locks covering percpu hardirq/softirq time.
 * They are only modified in vtime_account, on corresponding CPU
 * with interrupts disabled. So, writes are safe.
 * They are read and saved off onto struct rq in update_rq_clock().
 * This may result in other CPU reading this CPU's irq time and can
 * race with irq/vtime_account on this CPU. We would either get old
 * or new value with a side effect of accounting a slice of irq time to wrong
 * task when irq is in progress while we read rq->clock. That is a worthy
 * compromise in place of having locks on each irq in account_system_time.
 */
DEFINE_PER_CPU(u64, cpu_hardirq_time);
DEFINE_PER_CPU(u64, cpu_softirq_time);

static DEFINE_PER_CPU(u64, irq_start_time);
static int sched_clock_irqtime;

void enable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 1;
}

void disable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 0;
}

#ifndef CONFIG_64BIT
DEFINE_PER_CPU(seqcount_t, irq_time_seq);
#endif /* CONFIG_64BIT */

/*
 * Called before incrementing preempt_count on {soft,}irq_enter
 * and before decrementing preempt_count on {soft,}irq_exit.
 */
void account_system_vtime(struct task_struct *curr)
{
	unsigned long flags;
	s64 delta;
	int cpu;

	if (!sched_clock_irqtime)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();
	delta = sched_clock_cpu(cpu) - __this_cpu_read(irq_start_time);
	__this_cpu_add(irq_start_time, delta);

	irq_time_write_begin();
	/*
	 * We do not account for softirq time from ksoftirqd here.
	 * We want to continue accounting softirq time to ksoftirqd thread
	 * in that case, so as not to confuse scheduler with a special task
	 * that do not consume any time, but still wants to run.
	 */
	if (hardirq_count())
		__this_cpu_add(cpu_hardirq_time, delta);
	else if (in_serving_softirq() && !(curr->flags & PF_KSOFTIRQD))
		__this_cpu_add(cpu_softirq_time, delta);

	irq_time_write_end();
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(account_system_vtime);

#endif /* CONFIG_IRQ_TIME_ACCOUNTING */
/*
 * Account user cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in user space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_user_time(struct task_struct *p, cputime_t cputime,
		       cputime_t cputime_scaled)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t tmp;

	/* Add user time to process. */
	p->utime = cputime_add(p->utime, cputime);
	p->utimescaled = cputime_add(p->utimescaled, cputime_scaled);
	account_group_user_time(p, cputime);

	/* Add user time to cpustat. */
	tmp = cputime_to_cputime64(cputime);
	if (TASK_NICE(p) > 0)
		cpustat->nice = cputime64_add(cpustat->nice, tmp);
	else
		cpustat->user = cputime64_add(cpustat->user, tmp);

	cpuacct_update_stats(p, CPUACCT_STAT_USER, cputime);
	/* Account for user time used */
	acct_update_integrals(p);
}

/*
 * Account guest cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in virtual machine since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
static void account_guest_time(struct task_struct *p, cputime_t cputime,
			       cputime_t cputime_scaled)
{
	cputime64_t tmp;
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;

	tmp = cputime_to_cputime64(cputime);

	/* Add guest time to process. */
	p->utime = cputime_add(p->utime, cputime);
	p->utimescaled = cputime_add(p->utimescaled, cputime_scaled);
	account_group_user_time(p, cputime);
	p->gtime = cputime_add(p->gtime, cputime);

	/* Add guest time to cpustat. */
	if (TASK_NICE(p) > 0) {
		cpustat->nice = cputime64_add(cpustat->nice, tmp);
		cpustat->guest_nice = cputime64_add(cpustat->guest_nice, tmp);
	} else {
		cpustat->user = cputime64_add(cpustat->user, tmp);
		cpustat->guest = cputime64_add(cpustat->guest, tmp);
	}
}

/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_system_time(struct task_struct *p, int hardirq_offset,
			 cputime_t cputime, cputime_t cputime_scaled)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t tmp;

	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0)) {
		account_guest_time(p, cputime, cputime_scaled);
		return;
	}

	/* Add system time to process. */
	p->stime = cputime_add(p->stime, cputime);
	p->stimescaled = cputime_add(p->stimescaled, cputime_scaled);
	account_group_system_time(p, cputime);

	/* Add system time to cpustat. */
	tmp = cputime_to_cputime64(cputime);
	if (hardirq_count() - hardirq_offset)
		cpustat->irq = cputime64_add(cpustat->irq, tmp);
	else if (in_serving_softirq())
		cpustat->softirq = cputime64_add(cpustat->softirq, tmp);
	else
		cpustat->system = cputime64_add(cpustat->system, tmp);

	cpuacct_update_stats(p, CPUACCT_STAT_SYSTEM, cputime);

	/* Account for system time used */
	acct_update_integrals(p);
}

/*
 * Account for involuntary wait time.
 * @cputime: the cpu time spent in involuntary wait
 */
void account_steal_time(cputime_t cputime)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t cputime64 = cputime_to_cputime64(cputime);

	cpustat->steal = cputime64_add(cpustat->steal, cputime64);
}

/*
 * Account for idle time.
 * @cputime: the cpu time spent in idle wait
 */
void account_idle_time(cputime_t cputime)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t cputime64 = cputime_to_cputime64(cputime);
	struct rq *rq = this_rq();

	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat->iowait = cputime64_add(cpustat->iowait, cputime64);
	else
		cpustat->idle = cputime64_add(cpustat->idle, cputime64);
}

/*
 * Accumulate raw cputime values of dead tasks (sig->[us]time) and live
 * tasks (sum on group iteration) belonging to @tsk's group.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times)
{
	struct signal_struct *sig = tsk->signal;
	struct sighand_struct *sighand;
	struct task_struct *t;

	*times = INIT_CPUTIME;

	rcu_read_lock();
	sighand = rcu_dereference(tsk->sighand);
	if (!sighand)
		goto out;

	t = tsk;
	do {
		times->utime = cputime_add(times->utime, t->utime);
		times->stime = cputime_add(times->stime, t->stime);
		times->sum_exec_runtime += t->se.sum_exec_runtime;

		t = next_thread(t);
	} while (t != tsk);

	times->utime = cputime_add(times->utime, sig->utime);
	times->stime = cputime_add(times->stime, sig->stime);
	times->sum_exec_runtime += sig->sum_sched_runtime;
out:
	rcu_read_unlock();
}

/*
 * Use precise platform statistics if available:
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING

#endif /* CONFIG_VIRT_CPU_ACCOUNTING */


#ifdef CONFIG_VIRT_CPU_ACCOUNTING
void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	*ut = p->utime;
	*st = p->stime;
}

void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);

	*ut = cputime.utime;
	*st = cputime.stime;
}
#else /* !CONFIG_VIRT_CPU_ACCOUNTING */
/*
 * Account a single tick of cpu time.
 * @p: the process that the cpu time gets accounted to
 * @user_tick: indicates if the tick is a user or a system tick
 */
void account_process_tick(struct task_struct *p, int user_tick)
{
	cputime_t one_jiffy_scaled = cputime_to_scaled(cputime_one_jiffy);
	struct rq *rq = this_rq();

	if (user_tick)
		account_user_time(p, cputime_one_jiffy, one_jiffy_scaled);
	else if ((p != rq->idle) || (irq_count() != HARDIRQ_OFFSET))
		account_system_time(p, HARDIRQ_OFFSET, cputime_one_jiffy,
				    one_jiffy_scaled);
	else
		account_idle_time(cputime_one_jiffy);
}

/*
 * Account multiple ticks of steal time.
 * @p: the process from which the cpu time has been stolen
 * @ticks: number of stolen ticks
 */
void account_steal_ticks(unsigned long ticks)
{
	account_steal_time(jiffies_to_cputime(ticks));
}

/*
 * Account multiple ticks of idle time.
 * @ticks: number of stolen ticks
 */
void account_idle_ticks(unsigned long ticks)
{
	account_idle_time(jiffies_to_cputime(ticks));
}


#ifndef nsecs_to_cputime
# define nsecs_to_cputime(__nsecs)	nsecs_to_jiffies(__nsecs)
#endif
/*
 * Adjust tick based cputime random precision against scheduler
 * runtime accounting.
 */
static void cputime_adjust(struct task_cputime *curr,
			   struct task_struct *p,
			   cputime_t *ut, cputime_t *st)
{
	cputime_t rtime, utime = p->utime, total = cputime_add(utime, p->stime);

	/*
	 * Use CFS's precise accounting:
	 */
	rtime = nsecs_to_cputime(curr->sum_exec_runtime);

	if (total) {
		u64 temp = rtime;

		temp *= utime;
		do_div(temp, total);
		utime = (cputime_t)temp;
	} else
		utime = rtime;

	/*
	 * Compare with previous values, to keep monotonicity:
	 */
	p->prev_cputime.utime = max(p->prev_cputime.utime, utime);
	p->prev_cputime.stime = max(p->prev_cputime.stime, cputime_sub(rtime, p->prev_cputime.utime));

	*ut = p->prev_cputime.utime;
	*st = p->prev_cputime.stime;
}

void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime = {
		.sum_exec_runtime = p->se.sum_exec_runtime,
	};


	cputime_adjust(&cputime, p, ut, st);
}

/*
 * Must be called with siglock held.
 */
void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct signal_struct *sig = p->signal;
	struct task_cputime cputime;
	cputime_t rtime, utime, total;

	thread_group_cputime(p, &cputime);

	total = cputime_add(cputime.utime, cputime.stime);
	rtime = nsecs_to_cputime(cputime.sum_exec_runtime);

	if (total) {
		u64 temp = rtime;

		temp *= cputime.utime;
		do_div(temp, total);
		utime = (cputime_t)temp;
	} else
		utime = rtime;

	sig->prev_cputime.utime = max(sig->prev_cputime.utime, utime);
	sig->prev_cputime.stime = max(sig->prev_cputime.stime,
			      cputime_sub(rtime, sig->prev_cputime.utime));

	*ut = sig->prev_cputime.utime;
	*st = sig->prev_cputime.stime;
}

#endif /* !CONFIG_VIRT_CPU_ACCOUNTING */
