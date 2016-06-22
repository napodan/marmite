/*
 * Deadline Scheduling Class (SCHED_DEADLINE)
 *
 * Earliest Deadline First (EDF) + Constant Bandwidth Server (CBS).
 *
 * Tasks that periodically executes their instances for less than their
 * runtime won't miss any of their deadlines.
 * Tasks that are not periodic or sporadic or that tries to execute more
 * than their reserved bandwidth will be slowed down (and may potentially
 * miss some of their deadlines), and won't affect any other task.
 *
 * Copyright (C) 2012 Dario Faggioli <raistlin@linux.it>,
 *                    Juri Lelli <juri.lelli@gmail.com>,
 *                    Michael Trimarchi <michael@amarulasolutions.com>,
 *                    Fabio Checconi <fchecconi@gmail.com>
 */
#include "sched.h"

#include <linux/slab.h>

struct dl_bandwidth def_dl_bandwidth;

static inline struct task_struct *dl_task_of(struct sched_dl_entity *dl_se)
{
	return container_of(dl_se, struct task_struct, dl);
}

static inline struct rq *rq_of_dl_rq(struct dl_rq *dl_rq)
{
	return container_of(dl_rq, struct rq, dl);
}

static inline struct dl_rq *dl_rq_of_se(struct sched_dl_entity *dl_se)
{
	struct task_struct *p = dl_task_of(dl_se);
	struct rq *rq = task_rq(p);

	return &rq->dl;
}

static inline int on_dl_rq(struct sched_dl_entity *dl_se)
{
	return !RB_EMPTY_NODE(&dl_se->rb_node);
}

static inline int is_leftmost(struct task_struct *p, struct dl_rq *dl_rq)
{
	struct sched_dl_entity *dl_se = &p->dl;

	return dl_rq->rb_leftmost == &dl_se->rb_node;
}

void init_dl_bandwidth(struct dl_bandwidth *dl_b, u64 period, u64 runtime)
{
	raw_spin_lock_init(&dl_b->dl_runtime_lock);
	dl_b->dl_period = period;
	dl_b->dl_runtime = runtime;
}

void init_dl_bw(struct dl_bw *dl_b)
{
	raw_spin_lock_init(&dl_b->lock);
	raw_spin_lock(&def_dl_bandwidth.dl_runtime_lock);
	if (global_rt_runtime() == RUNTIME_INF)
		dl_b->bw = -1;
	else
		dl_b->bw = to_ratio(global_rt_period(), global_rt_runtime());
	raw_spin_unlock(&def_dl_bandwidth.dl_runtime_lock);
	dl_b->total_bw = 0;
}

void init_dl_rq(struct dl_rq *dl_rq, struct rq *rq)
{
	dl_rq->rb_root = RB_ROOT;

#ifdef CONFIG_SMP
	/* zero means no -deadline tasks */
	dl_rq->earliest_dl.curr = dl_rq->earliest_dl.next = 0;

	dl_rq->dl_nr_migratory = 0;
	dl_rq->overloaded = 0;
	dl_rq->pushable_dl_tasks_root = RB_ROOT;
#else
	init_dl_bw(&dl_rq->dl_bw);
#endif
}

#ifdef CONFIG_SMP

static inline int dl_overloaded(struct rq *rq)
{
	return atomic_read(&rq->rd->dlo_count);
}

static inline void dl_set_overload(struct rq *rq)
{
	if (!rq->online)
		return;

	cpumask_set_cpu(rq->cpu, rq->rd->dlo_mask);
	/*
	 * Must be visible before the overload count is
	 * set (as in sched_rt.c).
	 *
	 * Matched by the barrier in pull_dl_task().
	 */
	smp_wmb();
	atomic_inc(&rq->rd->dlo_count);
}

static inline void dl_clear_overload(struct rq *rq)
{
	if (!rq->online)
		return;

	atomic_dec(&rq->rd->dlo_count);
	cpumask_clear_cpu(rq->cpu, rq->rd->dlo_mask);
}

static void update_dl_migration(struct dl_rq *dl_rq)
{
	if (dl_rq->dl_nr_migratory && dl_rq->dl_nr_running > 1) {
		if (!dl_rq->overloaded) {
			dl_set_overload(rq_of_dl_rq(dl_rq));
			dl_rq->overloaded = 1;
		}
	} else if (dl_rq->overloaded) {
		dl_clear_overload(rq_of_dl_rq(dl_rq));
		dl_rq->overloaded = 0;
	}
}

static void inc_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
	struct task_struct *p = dl_task_of(dl_se);

	if (p->nr_cpus_allowed > 1)
		dl_rq->dl_nr_migratory++;

	update_dl_migration(dl_rq);
}

static void dec_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
	struct task_struct *p = dl_task_of(dl_se);

	if (p->nr_cpus_allowed > 1)
		dl_rq->dl_nr_migratory--;

	update_dl_migration(dl_rq);
}

/*
 * The list of pushable -deadline task is not a plist, like in
 * sched_rt.c, it is an rb-tree with tasks ordered by deadline.
 */
static void enqueue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
	struct dl_rq *dl_rq = &rq->dl;
	struct rb_node **link = &dl_rq->pushable_dl_tasks_root.rb_node;
	struct rb_node *parent = NULL;
	struct task_struct *entry;
	int leftmost = 1;

	BUG_ON(!RB_EMPTY_NODE(&p->pushable_dl_tasks));

	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct task_struct,
				 pushable_dl_tasks);
		if (dl_entity_preempt(&p->dl, &entry->dl))
			link = &parent->rb_left;
		else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	if (leftmost)
		dl_rq->pushable_dl_tasks_leftmost = &p->pushable_dl_tasks;

	rb_link_node(&p->pushable_dl_tasks, parent, link);
	rb_insert_color(&p->pushable_dl_tasks, &dl_rq->pushable_dl_tasks_root);
}

static void dequeue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
	struct dl_rq *dl_rq = &rq->dl;

	if (RB_EMPTY_NODE(&p->pushable_dl_tasks))
		return;

	if (dl_rq->pushable_dl_tasks_leftmost == &p->pushable_dl_tasks) {
		struct rb_node *next_node;

		next_node = rb_next(&p->pushable_dl_tasks);
		dl_rq->pushable_dl_tasks_leftmost = next_node;
	}

	rb_erase(&p->pushable_dl_tasks, &dl_rq->pushable_dl_tasks_root);
	RB_CLEAR_NODE(&p->pushable_dl_tasks);
}

static inline int has_pushable_dl_tasks(struct rq *rq)
{
	return !RB_EMPTY_ROOT(&rq->dl.pushable_dl_tasks_root);
}

static int push_dl_task(struct rq *rq);

#else

static inline
void enqueue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
}

static inline
void dequeue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
}

static inline
void inc_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
}

static inline
void dec_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
}

#endif /* CONFIG_SMP */

static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags) {}
static void __dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags) {}
static void check_preempt_curr_dl(struct rq *rq, struct task_struct *p,
				  int flags) {}

/*
 * We are being explicitly informed that a new instance is starting,
 * and this means that:
 *  - the absolute deadline of the entity has to be placed at
 *    current time + relative deadline;
 *  - the runtime of the entity has to be set to the maximum value.
 *
 * The capability of specifying such event is useful whenever a -deadline
 * entity wants to (try to!) synchronize its behaviour with the scheduler's
 * one, and to (try to!) reconcile itself with its own scheduling
 * parameters.
 */
static inline void setup_new_dl_entity(struct sched_dl_entity *dl_se,
				       struct sched_dl_entity *pi_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	WARN_ON(!dl_se->dl_new || dl_se->dl_throttled);

	/*
	 * We use the regular wall clock time to set deadlines in the
	 * future; in fact, we must consider execution overheads (time
	 * spent on hardirq context, etc.).
	 */
	dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
	dl_se->runtime = pi_se->dl_runtime;
	dl_se->dl_new = 0;
}

/*
 * Pure Earliest Deadline First (EDF) scheduling does not deal with the
 * possibility of a entity lasting more than what it declared, and thus
 * exhausting its runtime.
 *
 * Here we are interested in making runtime overrun possible, but we do
 * not want a entity which is misbehaving to affect the scheduling of all
 * other entities.
 * Therefore, a budgeting strategy called Constant Bandwidth Server (CBS)
 * is used, in order to confine each entity within its own bandwidth.
 *
 * This function deals exactly with that, and ensures that when the runtime
 * of a entity is replenished, its deadline is also postponed. That ensures
 * the overrunning entity can't interfere with other entity in the system and
 * can't make them miss their deadlines. Reasons why this kind of overruns
 * could happen are, typically, a entity voluntarily trying to overcome its
 * runtime, or it just underestimated it during sched_setscheduler_ex().
 */
static void replenish_dl_entity(struct sched_dl_entity *dl_se,
				struct sched_dl_entity *pi_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	BUG_ON(pi_se->dl_runtime <= 0);

	/*
	 * This could be the case for a !-dl task that is boosted.
	 * Just go with full inherited parameters.
	 */
	if (dl_se->dl_deadline == 0) {
		dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
		dl_se->runtime = pi_se->dl_runtime;
	}

	/*
	 * We keep moving the deadline away until we get some
	 * available runtime for the entity. This ensures correct
	 * handling of situations where the runtime overrun is
	 * arbitrary large.
	 */
	while (dl_se->runtime <= 0) {
		dl_se->deadline += pi_se->dl_period;
		dl_se->runtime += pi_se->dl_runtime;
	}

	/*
	 * At this point, the deadline really should be "in
	 * the future" with respect to rq->clock. If it's
	 * not, we are, for some reason, lagging too much!
	 * Anyway, after having warn userspace abut that,
	 * we still try to keep the things running by
	 * resetting the deadline and the budget of the
	 * entity.
	 */
	if (dl_time_before(dl_se->deadline, rq_clock(rq))) {
		static bool lag_once = false;

		if (!lag_once) {
			lag_once = true;
			printk_deferred("sched: DL replenish lagged to much\n");
		}
		dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
		dl_se->runtime = pi_se->dl_runtime;
	}
}

/*
 * Here we check if --at time t-- an entity (which is probably being
 * [re]activated or, in general, enqueued) can use its remaining runtime
 * and its current deadline _without_ exceeding the bandwidth it is
 * assigned (function returns true if it can't). We are in fact applying
 * one of the CBS rules: when a task wakes up, if the residual runtime
 * over residual deadline fits within the allocated bandwidth, then we
 * can keep the current (absolute) deadline and residual budget without
 * disrupting the schedulability of the system. Otherwise, we should
 * refill the runtime and set the deadline a period in the future,
 * because keeping the current (absolute) deadline of the task would
 * result in breaking guarantees promised to other tasks (refer to
 * Documentation/scheduler/sched-deadline.txt for more informations).
 *
 * This function returns true if:
 *
 *   runtime / (deadline - t) > dl_runtime / dl_period ,
 *
 * IOW we can't recycle current parameters.
 *
 * Notice that the bandwidth check is done against the period. For
 * task with deadline equal to period this is the same of using
 * dl_deadline instead of dl_period in the equation above.
 */
static bool dl_entity_overflow(struct sched_dl_entity *dl_se,
			       struct sched_dl_entity *pi_se, u64 t)
{
	u64 left, right;

	/*
	 * left and right are the two sides of the equation above,
	 * after a bit of shuffling to use multiplications instead
	 * of divisions.
	 *
	 * Note that none of the time values involved in the two
	 * multiplications are absolute: dl_deadline and dl_runtime
	 * are the relative deadline and the maximum runtime of each
	 * instance, runtime is the runtime left for the last instance
	 * and (deadline - t), since t is rq->clock, is the time left
	 * to the (absolute) deadline. Even if overflowing the u64 type
	 * is very unlikely to occur in both cases, here we scale down
	 * as we want to avoid that risk at all. Scaling down by 10
	 * means that we reduce granularity to 1us. We are fine with it,
	 * since this is only a true/false check and, anyway, thinking
	 * of anything below microseconds resolution is actually fiction
	 * (but still we want to give the user that illusion >;).
	 */
	left = (pi_se->dl_period >> DL_SCALE) * (dl_se->runtime >> DL_SCALE);
	right = ((dl_se->deadline - t) >> DL_SCALE) *
		(pi_se->dl_runtime >> DL_SCALE);

	return dl_time_before(right, left);
}

/*
 * When a -deadline entity is queued back on the runqueue, its runtime and
 * deadline might need updating.
 *
 * The policy here is that we update the deadline of the entity only if:
 *  - the current deadline is in the past,
 *  - using the remaining runtime with the current deadline would make
 *    the entity exceed its bandwidth.
 */
static void update_dl_entity(struct sched_dl_entity *dl_se,
			     struct sched_dl_entity *pi_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	/*
	 * The arrival of a new instance needs special treatment, i.e.,
	 * the actual scheduling parameters have to be "renewed".
	 */
	if (dl_se->dl_new) {
		setup_new_dl_entity(dl_se, pi_se);
		return;
	}

	if (dl_time_before(dl_se->deadline, rq_clock(rq)) ||
	    dl_entity_overflow(dl_se, pi_se, rq_clock(rq))) {
		dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
		dl_se->runtime = pi_se->dl_runtime;
	}
}

/*
 * If the entity depleted all its runtime, and if we want it to sleep
 * while waiting for some new execution time to become available, we
 * set the bandwidth enforcement timer to the replenishment instant
 * and try to activate it.
 *
 * Notice that it is important for the caller to know if the timer
 * actually started or not (i.e., the replenishment instant is in
 * the future or in the past).
 */
static int start_dl_timer(struct sched_dl_entity *dl_se, bool boosted)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);
	ktime_t now, act;
	ktime_t soft, hard;
	unsigned long range;
	s64 delta;

	if (boosted)
		return 0;
	/*
	 * We want the timer to fire at the deadline, but considering
	 * that it is actually coming from rq->clock and not from
	 * hrtimer's time base reading.
	 */
	act = ns_to_ktime(dl_se->deadline);
	now = hrtimer_cb_get_time(&dl_se->dl_timer);
	delta = ktime_to_ns(now) - rq_clock(rq);
	act = ktime_add_ns(act, delta);

	/*
	 * If the expiry time already passed, e.g., because the value
	 * chosen as the deadline is too small, don't even try to
	 * start the timer in the past!
	 */
	if (ktime_us_delta(act, now) < 0)
		return 0;

	hrtimer_set_expires(&dl_se->dl_timer, act);

	soft = hrtimer_get_softexpires(&dl_se->dl_timer);
	hard = hrtimer_get_expires(&dl_se->dl_timer);
	range = ktime_to_ns(ktime_sub(hard, soft));
	__hrtimer_start_range_ns(&dl_se->dl_timer, soft,
				 range, HRTIMER_MODE_ABS, 0);

	return hrtimer_active(&dl_se->dl_timer);
}

/*
 * This is the bandwidth enforcement timer callback. If here, we know
 * a task is not on its dl_rq, since the fact that the timer was running
 * means the task is throttled and needs a runtime replenishment.
 *
 * However, what we actually do depends on the fact the task is active,
 * (it is on its rq) or has been removed from there by a call to
 * dequeue_task_dl(). In the former case we must issue the runtime
 * replenishment and add the task back to the dl_rq; in the latter, we just
 * do nothing but clearing dl_throttled, so that runtime and deadline
 * updating (and the queueing back to dl_rq) will be done by the
 * next call to enqueue_task_dl().
 */
static enum hrtimer_restart dl_task_timer(struct hrtimer *timer)
{
	struct sched_dl_entity *dl_se = container_of(timer,
						     struct sched_dl_entity,
						     dl_timer);
	struct task_struct *p = dl_task_of(dl_se);
	struct rq *rq;
again:
	rq = task_rq(p);
	raw_spin_lock(&rq->lock);

	if (rq != task_rq(p)) {
		/* Task was moved, retrying. */
		raw_spin_unlock(&rq->lock);
		goto again;
	}

	/*
	 * We need to take care of several possible races here:
	 *
	 *   - the task might have changed its scheduling policy
	 *     to something different than SCHED_DEADLINE
	 *   - the task might have changed its reservation parameters
	 *     (through sched_setattr())
	 *   - the task might have been boosted by someone else and
	 *     might be in the boosting/deboosting path
	 *
	 * In all this cases we bail out, as the task is already
	 * in the runqueue or is going to be enqueued back anyway.
	 */
	if (!dl_task(p) || dl_se->dl_new ||
	    dl_se->dl_boosted || !dl_se->dl_throttled)
		goto unlock;

	sched_clock_tick();
	update_rq_clock(rq);
	dl_se->dl_throttled = 0;
	dl_se->dl_yielded = 0;
	if (p->on_rq) {
		enqueue_task_dl(rq, p, ENQUEUE_REPLENISH);
		if (dl_task(rq->curr))
			check_preempt_curr_dl(rq, p, 0);
		else
			resched_task(rq->curr);
#ifdef CONFIG_SMP
		/*
		 * Queueing this task back might have overloaded rq,
		 * check if we need to kick someone away.
		 */
		if (has_pushable_dl_tasks(rq))
			push_dl_task(rq);
#endif
	}
unlock:
	raw_spin_unlock(&rq->lock);

	return HRTIMER_NORESTART;
}


