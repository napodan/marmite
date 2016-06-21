#include "sched.h"

/*
 * stop-task scheduling class.
 *
 * The stop task is the highest priority task in the system, it preempts
 * everything and will be preempted by nothing.
 *
 * See kernel/stop_machine.c
 */

#ifdef CONFIG_SMP
static int
select_task_rq_stop(struct task_struct *p, int sd_flag, int flags)
{
	return task_cpu(p); /* stop tasks as never migrate */
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_SCHED_HMP

static void
inc_hmp_sched_stats_stop(struct rq *rq, struct task_struct *p)
{
	inc_cumulative_runnable_avg(&rq->hmp_stats, p);
}

static void
dec_hmp_sched_stats_stop(struct rq *rq, struct task_struct *p)
{
	dec_cumulative_runnable_avg(&rq->hmp_stats, p);
}

#else	/* CONFIG_SCHED_HMP */

static inline void
inc_hmp_sched_stats_stop(struct rq *rq, struct task_struct *p) { }

static inline void
dec_hmp_sched_stats_stop(struct rq *rq, struct task_struct *p) { }

#endif	/* CONFIG_SCHED_HMP */

