/* include/linux/cpuacct.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _CPUACCT_H_
#define _CPUACCT_H_

#include <linux/cgroup.h>

/* Time spent by the tasks of the cpu accounting group executing in ... */
enum cpuacct_stat_index {
	CPUACCT_STAT_USER,	/* ... user mode */
	CPUACCT_STAT_SYSTEM,	/* ... kernel mode */

	CPUACCT_STAT_NSTATS,
};

#ifdef CONFIG_CGROUP_CPUACCT

extern void cpuacct_charge(struct task_struct *tsk, u64 cputime);
extern void cpuacct_update_stats(struct task_struct *tsk,
		enum cpuacct_stat_index idx, cputime_t val);

#else

static inline void cpuacct_charge(struct task_struct *tsk, u64 cputime)
{
}

static inline void
 cpuacct_update_stats(struct task_struct *tsk,
		enum cpuacct_stat_index idx, cputime_t val)
{
}

#endif

#ifdef CONFIG_CGROUP_CPUACCT

/*
 * Platform specific CPU frequency hooks for cpuacct. These functions are
 * called from the scheduler.
 */
struct cpuacct_charge_calls {
	/*
	 * Platforms can take advantage of this data and use
	 * per-cpu allocations if necessary.
	 */
	void (*init) (void **cpuacct_data);
	void (*charge) (void *cpuacct_data,  u64 cputime, unsigned int cpu);
	void (*cpufreq_show) (void *cpuacct_data, struct cgroup_map_cb *cb);
	/* Returns power consumed in milliWatt seconds */
	u64 (*power_usage) (void *cpuacct_data);
};

int cpuacct_charge_register(struct cpuacct_charge_calls *fn);

#endif /* CONFIG_CGROUP_CPUACCT */

#endif // _CPUACCT_H_
