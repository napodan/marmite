#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/kernel_stat.h>
#include <linux/err.h>

#include "sched.h"


struct cgroup_subsys cpuacct_subsys;

/*
 * CPU accounting code for task groups.
 *
 * Based on the work by Paul Menage (menage@google.com) and Balbir Singh
 * (balbir@in.ibm.com).
 */

/* track cpu usage of a group of tasks and its child groups */
struct cpuacct {
	struct cgroup_subsys_state css;
	/* cpuusage holds pointer to a u64-type object on every cpu */
	u64 __percpu *cpuusage;
	struct percpu_counter cpustat[CPUACCT_STAT_NSTATS];
	struct cpuacct *parent;
	void *cpuacct_data;
};

/* return cpu accounting group corresponding to this container */
static inline struct cpuacct *cgroup_ca(struct cgroup *cgrp)
{
	return container_of(cgroup_subsys_state(cgrp, cpuacct_subsys_id),
			    struct cpuacct, css);
}

/* return cpu accounting group to which this task belongs */
static inline struct cpuacct *task_ca(struct task_struct *tsk)
{
	return container_of(task_subsys_state(tsk, cpuacct_subsys_id),
			    struct cpuacct, css);
}


/* create a new cpu accounting group */
static struct cgroup_subsys_state *cpuacct_create(
	struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	struct cpuacct *ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	int i;

	if (!ca)
		goto out;

	ca->cpuusage = alloc_percpu(u64);
	if (!ca->cpuusage)
		goto out_free_ca;

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++)
		if (percpu_counter_init(&ca->cpustat[i], 0))
			goto out_free_counters;

	if (cgrp->parent)
		ca->parent = cgroup_ca(cgrp->parent);

	return &ca->css;

out_free_counters:
	while (--i >= 0)
		percpu_counter_destroy(&ca->cpustat[i]);
	free_percpu(ca->cpuusage);
out_free_ca:
	kfree(ca);
out:
	return ERR_PTR(-ENOMEM);
}

/* destroy an existing cpu accounting group */
static void
cpuacct_destroy(struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	int i;

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++)
		percpu_counter_destroy(&ca->cpustat[i]);
	free_percpu(ca->cpuusage);
	kfree(ca);
}

static int cpuacct_cpufreq_show(struct cgroup *cgrp, struct cftype *cft,
		struct cgroup_map_cb *cb)
{
	return 0;
}

/* return total cpu power usage (milliWatt second) of a group */
static u64 cpuacct_powerusage_read(struct cgroup *cgrp, struct cftype *cft)
{
	return 0;
}

static u64 cpuacct_cpuusage_read(struct cpuacct *ca, int cpu)
{
	u64 *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
	u64 data;

#ifndef CONFIG_64BIT
	/*
	 * Take rq->lock to make 64-bit read safe on 32-bit platforms.
	 */
	raw_spin_lock_irq(&cpu_rq(cpu)->lock);
	data = *cpuusage;
	raw_spin_unlock_irq(&cpu_rq(cpu)->lock);
#else
	data = *cpuusage;
#endif

	return data;
}

static void cpuacct_cpuusage_write(struct cpuacct *ca, int cpu, u64 val)
{
	u64 *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);

#ifndef CONFIG_64BIT
	/*
	 * Take rq->lock to make 64-bit write safe on 32-bit platforms.
	 */
	raw_spin_lock_irq(&cpu_rq(cpu)->lock);
	*cpuusage = val;
	raw_spin_unlock_irq(&cpu_rq(cpu)->lock);
#else
	*cpuusage = val;
#endif
}

/* return total cpu usage (in nanoseconds) of a group */
static u64 cpuusage_read(struct cgroup *cgrp, struct cftype *cft)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	u64 totalcpuusage = 0;
	int i;

	for_each_present_cpu(i)
		totalcpuusage += cpuacct_cpuusage_read(ca, i);

	return totalcpuusage;
}

static int cpuusage_write(struct cgroup *cgrp, struct cftype *cftype,
								u64 reset)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	int err = 0;
	int i;

	if (reset) {
		err = -EINVAL;
		goto out;
	}

	for_each_present_cpu(i)
		cpuacct_cpuusage_write(ca, i, 0);

out:
	return err;
}

static int cpuacct_percpu_seq_read(struct cgroup *cgroup, struct cftype *cft,
				   struct seq_file *m)
{
	struct cpuacct *ca = cgroup_ca(cgroup);
	u64 percpu;
	int i;

	for_each_present_cpu(i) {
		percpu = cpuacct_cpuusage_read(ca, i);
		seq_printf(m, "%llu ", (unsigned long long) percpu);
	}
	seq_printf(m, "\n");
	return 0;
}

static const char *cpuacct_stat_desc[] = {
	[CPUACCT_STAT_USER] = "user",
	[CPUACCT_STAT_SYSTEM] = "system",
};

static int cpuacct_stats_show(struct cgroup *cgrp, struct cftype *cft,
			      struct cgroup_map_cb *cb)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	int i;

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++) {
		s64 val = percpu_counter_read(&ca->cpustat[i]);
		val = cputime64_to_clock_t(val);
		cb->fill(cb, cpuacct_stat_desc[i], val);
	}
	return 0;
}

static struct cftype files[] = {
	{
		.name = "usage",
		.read_u64 = cpuusage_read,
		.write_u64 = cpuusage_write,
	},
	{
		.name = "usage_percpu",
		.read_seq_string = cpuacct_percpu_seq_read,
	},
	{
		.name = "stat",
		.read_map = cpuacct_stats_show,
	},
	{
		.name =  "cpufreq",
		.read_map = cpuacct_cpufreq_show,
	},
	{
		.name = "power",
		.read_u64 = cpuacct_powerusage_read
	},
};

/*
 * charge this task's execution time to its accounting group.
 *
 * called with rq->lock held.
 */
void cpuacct_charge(struct task_struct *tsk, u64 cputime)
{
	struct cpuacct *ca;
	int cpu;

	if (unlikely(!cpuacct_subsys.active))
		return;

	cpu = task_cpu(tsk);

	rcu_read_lock();

	ca = task_ca(tsk);

	for (; ca; ca = ca->parent) {
		u64 *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
		*cpuusage += cputime;

	}

	rcu_read_unlock();
}

/*
 * When CONFIG_VIRT_CPU_ACCOUNTING is enabled one jiffy can be very large
 * in cputime_t units. As a result, cpuacct_update_stats calls
 * percpu_counter_add with values large enough to always overflow the
 * per cpu batch limit causing bad SMP scalability.
 *
 * To fix this we scale percpu_counter_batch by cputime_one_jiffy so we
 * batch the same amount of time with CONFIG_VIRT_CPU_ACCOUNTING disabled
 * and enabled. We cap it at INT_MAX which is the largest allowed batch value.
 */
#ifdef CONFIG_SMP
#define CPUACCT_BATCH	\
	min_t(long, percpu_counter_batch * cputime_one_jiffy, INT_MAX)
#else
#define CPUACCT_BATCH	0
#endif

/*
 * Charge the system/user time to the task's accounting group.
 */
void cpuacct_update_stats(struct task_struct *tsk,
		enum cpuacct_stat_index idx, cputime_t val)
{
	struct cpuacct *ca;
	int batch = CPUACCT_BATCH;

	if (unlikely(!cpuacct_subsys.active))
		return;

	rcu_read_lock();
	ca = task_ca(tsk);

	do {
		__percpu_counter_add(&ca->cpustat[idx], val, batch);
		ca = ca->parent;
	} while (ca);
	rcu_read_unlock();
}

static int cpuacct_populate(struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	return cgroup_add_files(cgrp, ss, files, ARRAY_SIZE(files));
}

struct cgroup_subsys cpuacct_subsys = {
	.name = "cpuacct",
	.create = cpuacct_create,
	.destroy = cpuacct_destroy,
	.populate = cpuacct_populate,
	.subsys_id = cpuacct_subsys_id,
};

