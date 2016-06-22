#ifndef _SCHED_RT_H
#define _SCHED_RT_H

#ifdef CONFIG_RT_MUTEXES
extern struct task_struct *rt_mutex_get_top_task(struct task_struct *task);
#else
static inline int rt_mutex_getprio(struct task_struct *p)
{
	return p->normal_prio;
}
static inline struct task_struct *rt_mutex_get_top_task(struct task_struct *task)
{
	return NULL;
}
# define rt_mutex_adjust_pi(p)		do { } while (0)
static inline bool tsk_is_pi_blocked(struct task_struct *tsk)
{
	return false;
}
#endif
/*
 * default timeslice is 100 msecs (used only for SCHED_RR tasks).
 * Timeslices get refilled after they expire.
 */
#define RR_TIMESLICE		(100 * HZ / 1000)

#endif /* _SCHED_RT_H */
