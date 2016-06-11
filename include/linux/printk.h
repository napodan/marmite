#ifndef __KERNEL_PRINTK__
#define __KERNEL_PRINTK__

#ifdef CONFIG_PRINTK
void dump_stack_print_info(const char *log_lvl);
#else
static inline __printf(1, 0)
int vprintk(const char *s, va_list args)
{
	return 0;
}
static inline __printf(1, 2) __cold
int printk(const char *s, ...)
{
	return 0;
}
static inline __printf(1, 2) __cold
int printk_deferred(const char *s, ...)
{
	return 0;
}
static inline int printk_ratelimit(void)
{
	return 0;
}
static inline bool printk_timed_ratelimit(unsigned long *caller_jiffies,
					  unsigned int interval_msec)
{
	return false;
}

static inline void wake_up_klogd(void)
{
}

static inline void log_buf_kexec_setup(void)
{
}

static inline void setup_log_buf(int early)
{
}

static inline void dump_stack_set_arch_desc(const char *fmt, ...)
{
}

static inline void dump_stack_print_info(const char *log_lvl)
{
}

static inline void show_regs_print_info(const char *log_lvl)
{
}
#endif

extern void dump_stack(void) __cold;


#endif
