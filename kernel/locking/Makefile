ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_rtmutex-debug.o = -pg
endif

obj-$(CONFIG_RT_MUTEXES) += rtmutex.o
obj-$(CONFIG_DEBUG_RT_MUTEXES) += rtmutex-debug.o
obj-$(CONFIG_RT_MUTEX_TESTER) += rtmutex-tester.o

