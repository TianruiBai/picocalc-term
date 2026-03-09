/****************************************************************************
 * pcterm/src/setjmp_stubs.S
 *
 * Minimal setjmp / longjmp for ARM Cortex-M33 (Thumb-2, ARMv8-M).
 * NuttX jmp_buf is an array of unsigned int — we save/restore the
 * callee-saved registers: r4-r11, sp, lr  (10 words = 40 bytes).
 *
 * This file is only needed when CONFIG_LIBC_ARCH_SETJMP is NOT enabled
 * in the NuttX config.
 ****************************************************************************/

#include <nuttx/config.h>

/* Only provide these if NuttX does not already supply them */

#ifndef CONFIG_LIBC_ARCH_SETJMP

	.syntax	unified
	.thumb
	.file	"setjmp_stubs.S"

/****************************************************************************
 * Name: setjmp
 *
 * int setjmp(jmp_buf env);
 *
 * Save callee-saved registers into env[0..9].
 * Returns 0 on direct call; non-zero when returning via longjmp.
 ****************************************************************************/

	.global	setjmp
	.type	setjmp, %function
	.thumb_func

setjmp:
	/* r0 = pointer to jmp_buf */

	mov	ip, sp			/* save sp into ip (r12) temporarily */
	stmia	r0!, {r4-r11}		/* save r4-r11 (8 words) */
	str	ip, [r0, #0]		/* save sp */
	str	lr, [r0, #4]		/* save lr (return address) */
	movs	r0, #0			/* return 0 */
	bx	lr

	.size	setjmp, . - setjmp

/****************************************************************************
 * Name: longjmp
 *
 * void longjmp(jmp_buf env, int val);
 *
 * Restore callee-saved registers from env[0..9].
 * Returns val (or 1 if val == 0) to the setjmp call site.
 ****************************************************************************/

	.global	longjmp
	.type	longjmp, %function
	.thumb_func

longjmp:
	/* r0 = pointer to jmp_buf, r1 = return value */

	ldmia	r0!, {r4-r11}		/* restore r4-r11 */
	ldr	r2, [r0, #0]		/* restore sp */
	ldr	r3, [r0, #4]		/* restore lr */
	mov	sp, r2
	/* if val == 0, force to 1 (POSIX requirement) */
	cmp	r1, #0
	it	eq
	moveq	r1, #1
	mov	r0, r1			/* return value */
	bx	r3			/* jump to saved lr */

	.size	longjmp, . - longjmp

#endif /* !CONFIG_LIBC_ARCH_SETJMP */
