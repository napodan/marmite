#ifndef _SCHED_RT_H
#define _SCHED_RT_H

/*
 * default timeslice is 100 msecs (used only for SCHED_RR tasks).
 * Timeslices get refilled after they expire.
 */
#define RR_TIMESLICE		(100 * HZ / 1000)

#endif /* _SCHED_RT_H */
