/*
 *  arch/arm/include/asm/ptrace.h
 *
 *  Copyright (C) 1996-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_PTRACE_H
#define __ASM_ARM_PTRACE_H

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__
struct pt_regs {
	unsigned long uregs[18];
};


#ifdef __KERNEL__

#define arch_has_single_step()	(1)

#define user_mode(regs)	\
	(((regs)->ARM_cpsr & 0xf) == 0)

#ifdef CONFIG_ARM_THUMB
#define thumb_mode(regs) \
	(((regs)->ARM_cpsr & PSR_T_BIT))
#else
#define thumb_mode(regs) (0)
#endif

#define isa_mode(regs) \
	((((regs)->ARM_cpsr & PSR_J_BIT) >> 23) | \
	 (((regs)->ARM_cpsr & PSR_T_BIT) >> 5))

#define processor_mode(regs) \
	((regs)->ARM_cpsr & MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_F_BIT))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	unsigned long mode = regs->ARM_cpsr & MODE_MASK;

	/*
	 * Always clear the F (FIQ) and A (delayed abort) bits
	 */
	regs->ARM_cpsr &= ~(PSR_F_BIT | PSR_A_BIT);

	if ((regs->ARM_cpsr & PSR_I_BIT) == 0) {
		if (mode == USR_MODE)
			return 1;
		if (elf_hwcap & HWCAP_26BIT && mode == USR26_MODE)
			return 1;
	}

	/*
	 * Force CPSR to something logical...
	 */
	regs->ARM_cpsr &= PSR_f | PSR_s | PSR_x | PSR_T_BIT | MODE32_BIT;
	if (!(elf_hwcap & HWCAP_26BIT))
		regs->ARM_cpsr |= USR_MODE;

	return 0;
}

#define instruction_pointer(regs)	(regs)->ARM_pc

#ifdef CONFIG_SMP
extern unsigned long profile_pc(struct pt_regs *regs);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif

#define predicate(x)		((x) & 0xf0000000)
#define PREDICATE_ALWAYS	0xe0000000

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#endif

