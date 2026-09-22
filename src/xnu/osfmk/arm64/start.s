/*
 * Copyright (c) 2007-2022 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#include "assym.s"
#include <arm64/asm.h>
#include <arm64/proc_reg.h>
#include <arm64/machine_machdep.h>
#include <arm64/proc_reg.h>
#include <pexpert/arm64/board_config.h>
#include <mach_assert.h>
#include <machine/asm.h>
#include <arm64/tunables/tunables.s>
#include <arm64/exception_asm.h>

#if __ARM_KERNEL_PROTECT__
#include <arm/pmap.h>
#endif /* __ARM_KERNEL_PROTECT__ */



.macro MSR_VBAR_EL1_X0
#if defined(KERNEL_INTEGRITY_KTRR)
	mov	x1, lr
	bl		EXT(pinst_set_vbar)
	mov	lr, x1
#else
	msr		VBAR_EL1, x0
#endif
.endmacro

.macro MSR_TCR_EL1_X1
#if defined(KERNEL_INTEGRITY_KTRR)
	mov		x0, x1
	mov		x1, lr
	bl		EXT(pinst_set_tcr)
	mov		lr, x1
#else
	msr		TCR_EL1, x1
#endif
.endmacro

.macro MSR_TTBR1_EL1_X0
#if defined(KERNEL_INTEGRITY_KTRR)
	mov		x1, lr
	bl		EXT(pinst_set_ttbr1)
	mov		lr, x1
#else
	msr		TTBR1_EL1, x0
#endif
.endmacro

.macro MSR_SCTLR_EL1_X0
#if defined(KERNEL_INTEGRITY_KTRR)
	mov		x1, lr

	// This may abort, do so on SP1
	bl		EXT(pinst_spsel_1)

	bl		EXT(pinst_set_sctlr)
	msr		SPSel, #0									// Back to SP0
	mov		lr, x1
#else
	msr		SCTLR_EL1, x0
#endif /* defined(KERNEL_INTEGRITY_KTRR) */
.endmacro

/*
 * Checks the reset handler for global and CPU-specific reset-assist functions,
 * then jumps to the reset handler with boot args and cpu data. This is copied
 * to the first physical page during CPU bootstrap (see cpu.c).
 *
 * Variables:
 *	x19 - Reset handler data pointer
 *	x20 - Boot args pointer
 *	x21 - CPU data pointer
 */
	.text
	.align 12
	.globl EXT(LowResetVectorBase)
LEXT(LowResetVectorBase)
	/*
	 * On reset, both RVBAR_EL1 and VBAR_EL1 point here.  SPSel.SP is 1,
	 * so on reset the CPU will jump to offset 0x0 and on exceptions
	 * the CPU will jump to offset 0x200, 0x280, 0x300, or 0x380.
	 * In order for both the reset vector and exception vectors to
	 * coexist in the same space, the reset code is moved to the end
	 * of the exception vector area.
	 */
	b		EXT(reset_vector)

	/* EL1 SP1: These vectors trap errors during early startup on non-boot CPUs. */
	.align	9
	b		xzs_exc_sync_sp1
	.align	7
	b		xzs_exc_irq_sp1
	.align	7
	b		xzs_exc_fiq_sp1
	.align	7
	b		xzs_exc_serror_sp1

	.align	7
	.globl EXT(reset_vector)
LEXT(reset_vector)
	// Preserve x0 for start_first_cpu, if called
	// Unlock the core for debugging
	msr		OSLAR_EL1, xzr
	msr		DAIFSet, #(DAIFSC_ALL)				// Disable all interrupts

#if !(defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR))
	// Set low reset vector before attempting any loads
	adrp    x0, EXT(LowExceptionVectorBase)@page
	add     x0, x0, EXT(LowExceptionVectorBase)@pageoff
	msr     VBAR_EL1, x0
#endif



	// Process reset handlers
	adrp	x19, EXT(ResetHandlerData)@page			// Get address of the reset handler data
	add		x19, x19, EXT(ResetHandlerData)@pageoff
	mrs		x15, MPIDR_EL1						// Load MPIDR to get CPU number
#if HAS_CLUSTER
	and		x0, x15, #0xFFFF					// CPU number in Affinity0, cluster ID in Affinity1
#else
	and		x0, x15, #0xFF						// CPU number is in MPIDR Affinity Level 0
#endif
	ldr		x1, [x19, CPU_DATA_ENTRIES]			// Load start of data entries
	add		x3, x1, MAX_CPUS * 16				// end addr of data entries = start + (16 * MAX_CPUS)
Lcheck_cpu_data_entry:
	ldr		x21, [x1, CPU_DATA_PADDR]			// Load physical CPU data address
	cbz		x21, Lnext_cpu_data_entry
	ldr		w2, [x21, CPU_PHYS_ID]				// Load ccc cpu phys id
	cmp		x0, x2						// Compare cpu data phys cpu and MPIDR_EL1 phys cpu
	b.eq	Lfound_cpu_data_entry				// Branch if match
Lnext_cpu_data_entry:
	add		x1, x1, #16					// Increment to the next cpu data entry
	cmp		x1, x3
	b.eq	Lskip_cpu_reset_handler				// Not found
	b		Lcheck_cpu_data_entry	// loop
Lfound_cpu_data_entry:

#ifdef APPLEEVEREST
	/*
	 * On H15, we need to configure PIO-only tunables and to apply
	 * PIO lockdown as early as possible.
	 */
	SET_PIO_ONLY_REGISTERS x21, x2, x3, x4, x5, x6
#endif /* APPLEEVEREST */

	adrp	x20, EXT(const_boot_args)@page
	add		x20, x20, EXT(const_boot_args)@pageoff
	ldr		x0, [x21, CPU_RESET_HANDLER]		// Call CPU reset handler
	cbz		x0, Lskip_cpu_reset_handler

	// Validate that our handler is one of the two expected handlers
	adrp	x2, EXT(resume_idle_cpu)@page
	add		x2, x2, EXT(resume_idle_cpu)@pageoff
	cmp		x0, x2
	beq		1f
	adrp	x2, EXT(start_cpu)@page
	add		x2, x2, EXT(start_cpu)@pageoff
	cmp		x0, x2
	bne		Lskip_cpu_reset_handler
1:

#if HAS_BP_RET
	bl		EXT(set_bp_ret)
#endif

#if __ARM_KERNEL_PROTECT__ && defined(KERNEL_INTEGRITY_KTRR)
	/*
	 * Populate TPIDR_EL1 (in case the CPU takes an exception while
	 * turning on the MMU).
	 */
	ldr		x13, [x21, CPU_ACTIVE_THREAD]
	msr		TPIDR_EL1, x13
#endif /* __ARM_KERNEL_PROTECT__ */

	blr		x0
Lskip_cpu_reset_handler:
	b		.									// Hang if the handler is NULL or returns

	.align 3
	.global EXT(LowResetVectorEnd)
LEXT(LowResetVectorEnd)
	.global	EXT(SleepToken)
#if WITH_CLASSIC_S2R
LEXT(SleepToken)
	.space	(stSize_NUM),0
#endif

	.section __DATA_CONST,__const
	.align	3
	.globl  EXT(ResetHandlerData)
LEXT(ResetHandlerData)
	.space  (rhdSize_NUM),0		// (filled with 0s)
	.text


/*
 * __start trampoline is located at a position relative to LowResetVectorBase
 * so that iBoot can compute the reset vector position to set IORVBAR using
 * only the kernel entry point.  Reset vector = (__start & ~0xfff)
 */
	.align	3
	.globl EXT(_start)
LEXT(_start)
	ARM64_PROLOG
	b	EXT(start_first_cpu)


/*
 * Provides an early-boot exception vector so that the processor will spin
 * and preserve exception information (e.g., ELR_EL1) when early CPU bootstrap
 * code triggers an exception. This is copied to the second physical page
 * during CPU bootstrap (see cpu.c).
 */
	.align 12, 0
	.global	EXT(LowExceptionVectorBase)
LEXT(LowExceptionVectorBase)
	/* EL1 SP 0 */
	b		xzs_exc_sync_sp0
	.align	7
	b		xzs_exc_irq_sp0
	.align	7
	b		xzs_exc_fiq_sp0
	.align	7
	b		xzs_exc_serror_sp0
	/* EL1 SP1 */
	.align	7
	b		xzs_exc_sync_sp1
	.align	7
	b		xzs_exc_irq_sp1
	.align	7
	b		xzs_exc_fiq_sp1
	.align	7
	b		xzs_exc_serror_sp1
	/* EL0 64 */
	.align	7
	b		xzs_exc_sync_el0
	.align	7
	b		xzs_exc_irq_el0
	.align	7
	b		xzs_exc_fiq_el0
	.align	7
	b		xzs_exc_serror_el0
	/* EL0 32 */
	.align	7
	b		xzs_exc_sync_el0_32
	.align	7
	b		xzs_exc_irq_el0_32
	.align	7
	b		xzs_exc_fiq_el0_32
	.align	7
	b		xzs_exc_serror_el0_32
	.align 12, 0

#if defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR)
/*
 * Provide a global symbol so that we can narrow the V=P mapping to cover
 * this page during arm_vm_init.
 */
.align ARM_PGSHIFT
.globl EXT(bootstrap_instructions)
LEXT(bootstrap_instructions)

#endif /* defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR) */
	.align 2
	.globl EXT(resume_idle_cpu)
LEXT(resume_idle_cpu)
	adrp	lr, EXT(arm_init_idle_cpu)@page
	add		lr, lr, EXT(arm_init_idle_cpu)@pageoff
	b		start_cpu

	.align 2
	.globl EXT(start_cpu)
LEXT(start_cpu)
	adrp	lr, EXT(arm_init_cpu)@page
	add		lr, lr, EXT(arm_init_cpu)@pageoff
	b		start_cpu

	.align 2
start_cpu:
#if defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR)
	// This is done right away in reset vector for pre-KTRR devices
	// Set low reset vector now that we are in the KTRR-free zone
	adrp	x0, EXT(LowExceptionVectorBase)@page
	add		x0, x0, EXT(LowExceptionVectorBase)@pageoff
	MSR_VBAR_EL1_X0
#endif /* defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR) */

	// x20 set to BootArgs phys address
	// x21 set to cpu data phys address

	// Get the kernel memory parameters from the boot args
	ldr		x22, [x20, BA_VIRT_BASE]			// Get the kernel virt base
	ldr		x23, [x20, BA_PHYS_BASE]			// Get the kernel phys base
	ldr		x24, [x20, BA_MEM_SIZE]				// Get the physical memory size
	adrp	x25, EXT(bootstrap_pagetables)@page	// Get the start of the page tables
	ldr		x26, [x20, BA_BOOT_FLAGS]			// Get the kernel boot flags


	// Set TPIDR_EL0 with cached CPU info
	ldr		x0, [x21, CPU_TPIDR_EL0]
	msr		TPIDR_EL0, x0

	// Set TPIDRRO_EL0 to 0
	msr		TPIDRRO_EL0, xzr


	// Set the exception stack pointer
	ldr		x0, [x21, CPU_EXCEPSTACK_TOP]


	// Set SP_EL1 to exception stack
#if defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR)
	mov		x1, lr
	bl		EXT(pinst_spsel_1)
	mov		lr, x1
#else
	msr		SPSel, #1
#endif
	mov		sp, x0

	// Set the interrupt stack pointer
	ldr		x0, [x21, CPU_INTSTACK_TOP]
	msr		SPSel, #0
	mov		sp, x0

	// Convert lr to KVA
	add		lr, lr, x22
	sub		lr, lr, x23

	b		common_start

/*
 * create_l1_table_entry
 *
 * Given a virtual address, creates a table entry in an L1 translation table
 * to point to an L2 translation table.
 *   arg0 - Virtual address
 *   arg1 - L1 table address
 *   arg2 - L2 table address
 *   arg3 - Scratch register
 *   arg4 - Scratch register
 *   arg5 - Scratch register
 */
.macro create_l1_table_entry
	and		$3,	$0, #(ARM_PTE_T1_REGION_MASK(TCR_EL1_BOOT))
	lsr		$3, $3, #(ARM_TT_L1_SHIFT)			// Get index in L1 table for L2 table
	lsl		$3, $3, #(TTE_SHIFT)				// Convert index into pointer offset
	add		$3, $1, $3							// Get L1 entry pointer
	mov		$4, #(ARM_TTE_BOOT_TABLE)			// Get L1 table entry template
	and		$5, $2, #(ARM_TTE_TABLE_MASK)		// Get address bits of L2 table
	orr		$5, $4, $5 							// Create table entry for L2 table
	str		$5, [$3]							// Write entry to L1 table
.endmacro

/*
 * create_l2_block_entries
 *
 * Given base virtual and physical addresses, creates consecutive block entries
 * in an L2 translation table.
 *   arg0 - Virtual address
 *   arg1 - Physical address
 *   arg2 - L2 table address
 *   arg3 - Number of entries
 *   arg4 - Scratch register
 *   arg5 - Scratch register
 *   arg6 - Scratch register
 *   arg7 - Scratch register
 */
.macro create_l2_block_entries
	and		$4,	$0, #(ARM_TT_L2_INDEX_MASK)
	lsr		$4, $4, #(ARM_TTE_BLOCK_L2_SHIFT)	// Get index in L2 table for block entry
	lsl		$4, $4, #(TTE_SHIFT)				// Convert index into pointer offset
	add		$4, $2, $4							// Get L2 entry pointer
	mov		$5, #(ARM_TTE_BOOT_BLOCK_LOWER)		// Get L2 block entry template
	orr		$5, $5, #(ARM_TTE_BOOT_BLOCK_UPPER)
	and		$6, $1, #(ARM_TTE_BLOCK_L2_MASK)	// Get address bits of block mapping
	orr		$6, $5, $6
	mov		$5, $3
	mov		$7, #(ARM_TT_L2_SIZE)
1:
	str		$6, [$4], #(1 << TTE_SHIFT)			// Write entry to L2 table and advance
	add		$6, $6, $7							// Increment the output address
	subs	$5, $5, #1							// Decrement the number of entries
	b.ne	1b
.endmacro

/*
 *  arg0 - virtual start address
 *  arg1 - physical start address
 *  arg2 - number of entries to map
 *  arg3 - L1 table address
 *  arg4 - free space pointer
 *  arg5 - scratch (entries mapped per loop)
 *  arg6 - scratch
 *  arg7 - scratch
 *  arg8 - scratch
 *  arg9 - scratch
 */
.macro create_bootstrap_mapping
	/* calculate entries left in this page */
	and	$5, $0, #(ARM_TT_L2_INDEX_MASK)
	lsr	$5, $5, #(ARM_TT_L2_SHIFT)
	mov	$6, #(TTE_PGENTRIES)
	sub	$5, $6, $5

	/* allocate an L2 table */
3:	add	$4, $4, PGBYTES

	/* create_l1_table_entry(virt_base, L1 table, L2 table, scratch1, scratch2, scratch3) */
	create_l1_table_entry	$0, $3, $4, $6, $7, $8

	/* determine how many entries to map this loop - the smaller of entries
	 * remaining in page and total entries left */
	cmp	$2, $5
	csel	$5, $2, $5, lt

	/* create_l2_block_entries(virt_base, phys_base, L2 table, num_ents, scratch1, scratch2, scratch3) */
	create_l2_block_entries	$0, $1, $4, $5, $6, $7, $8, $9

	/* subtract entries just mapped and bail out if we're done */
	subs	$2, $2, $5
	beq	2f

	/* entries left to map - advance base pointers */
	add 	$0, $0, $5, lsl #(ARM_TT_L2_SHIFT)
	add 	$1, $1, $5, lsl #(ARM_TT_L2_SHIFT)

	mov	$5, #(TTE_PGENTRIES)  /* subsequent loops map (up to) a whole L2 page */
	b	3b
2:
.endmacro

.macro XZS_RECORD_STAGE stage
	MOV64	x16, \stage
	MOV64	x17, 0x80060014
	str		w16, [x17]
.endmacro

/*
 * _start_first_cpu
 * Cold boot init routine.  Called from __start
 *   x0 - Boot args
 */
	.align 2
	.globl EXT(start_first_cpu)
LEXT(start_first_cpu)

	// Unlock the core for debugging
	msr		OSLAR_EL1, xzr
	msr		DAIFSet, #(DAIFSC_ALL)				// Disable all interrupts

	mov		x20, x0
	mov		x21, #0

	// Checkpoint K0: kernel entry
	XZS_RECORD_STAGE 0x200
	adrp	x0, str_k0@page
	add		x0, x0, str_k0@pageoff
	bl		EXT(xzs_early_puts)

	// Set low reset vector before attempting any loads
	adrp	x0, EXT(LowExceptionVectorBase)@page
	add		x0, x0, EXT(LowExceptionVectorBase)@pageoff
	MSR_VBAR_EL1_X0

	// Checkpoint K1: exception vectors installed
	XZS_RECORD_STAGE 0x201
	adrp	x0, str_k1@page
	add		x0, x0, str_k1@pageoff
	bl		EXT(xzs_early_puts)


	// Get the kernel memory parameters from the boot args
	ldr		x22, [x20, BA_VIRT_BASE]			// Get the kernel virt base
	ldr		x23, [x20, BA_PHYS_BASE]			// Get the kernel phys base
	ldr		x24, [x20, BA_MEM_SIZE]				// Get the physical memory size
	adrp	x25, EXT(bootstrap_pagetables)@page	// Get the start of the page tables
	ldr		x26, [x20, BA_BOOT_FLAGS]			// Get the kernel boot flags

	// Clear the registers that will be used to store the userspace thread pointer and CPU number.
	// We may not actually be booting from ordinal CPU 0, so this register will be updated
	// in ml_parse_cpu_topology(), which happens later in bootstrap.
	msr		TPIDRRO_EL0, xzr
	msr		TPIDR_EL0, xzr

	// Set up exception stack pointer in physical RAM before MMU
	adrp	x0, EXT(excepstack_top)@page		// Load top of exception stack
	add		x0, x0, EXT(excepstack_top)@pageoff

	// Set SP_EL1 to exception stack
#if defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR)
	bl		EXT(pinst_spsel_1)
#else
	msr		SPSel, #1
#endif

	mov		sp, x0

	// Set up interrupt stack pointer in physical RAM before MMU
	adrp	x0, EXT(intstack_top)@page			// Load top of irq stack
	add		x0, x0, EXT(intstack_top)@pageoff
	msr		SPSel, #0							// Set SP_EL0 to interrupt stack
	mov		sp, x0

	// Load address to the C init routine into link register
	adrp	lr, EXT(arm_init)@page
	add		lr, lr, EXT(arm_init)@pageoff
	add		lr, lr, x22							// Convert to KVA
	sub		lr, lr, x23

	/*
	 * Set up the bootstrap page tables with a single block entry for the V=P
	 * mapping, a single block entry for the trampolined kernel address (KVA),
	 * and all else invalid. This requires four pages:
	 *	Page 1 - V=P L1 table
	 *	Page 2 - V=P L2 table
	 *	Page 3 - KVA L1 table
	 *	Page 4 - KVA L2 table
	 */

	// Invalidate all entries in the bootstrap page tables (zero 32 pages / 128KB to cover all L1 & L2 tables on 4GB DRAM)
	mov		x0, #(ARM_TTE_EMPTY)				// Load invalid entry template
	mov		x1, x25								// Start at V=P pagetable root
	mov		x2, #(TTE_PGENTRIES)				// Load number of entries per page
	lsl		x2, x2, #5							// Shift by 5 for num entries on 32 pages (16384 entries = 128KB)

Linvalidate_bootstrap:							// do {
	str		x0, [x1], #(1 << TTE_SHIFT)			//   Invalidate and advance
	subs	x2, x2, #1							//   entries--
	b.ne	Linvalidate_bootstrap				// } while (entries != 0)

	/*
	 * In order to reclaim memory on targets where TZ0 (or some other entity)
	 * must be located at the base of memory, iBoot may set the virtual and
	 * physical base addresses to immediately follow whatever lies at the
	 * base of physical memory.
	 *
	 * If the base address belongs to TZ0, it may be dangerous for xnu to map
	 * it (as it may be prefetched, despite being technically inaccessible).
	 * In order to avoid this issue while keeping the mapping code simple, we
	 * may continue to use block mappings, but we will only map the kernelcache
	 * mach header to the end of memory.
	 *
	 * Given that iBoot guarantees that the unslid kernelcache base address
	 * will begin on an L2 boundary, this should prevent us from accidentally
	 * mapping TZ0.
	 */
	adrp	x0, EXT(_mh_execute_header)@page	// address of kernel mach header
	add		x0, x0, EXT(_mh_execute_header)@pageoff
	ldr		w1, [x0, #0x18]						// load mach_header->flags
	tbz		w1, #0x1f, Lkernelcache_base_found	// if MH_DYLIB_IN_CACHE unset, base is kernel mach header
	ldr		w1, [x0, #0x20]						// load first segment cmd (offset sizeof(kernel_mach_header_t))
	cmp		w1, #0x19							// must be LC_SEGMENT_64
	bne		.
	ldr		x1, [x0, #0x38]						// load first segment vmaddr
	sub		x1, x0, x1							// compute slide
	MOV64	x0, VM_KERNEL_LINK_ADDRESS
	add		x0, x0, x1							// base is kernel link address + slide

Lkernelcache_base_found:
	/*
	 * Adjust physical and virtual base addresses to account for physical
	 * memory preceeding xnu Mach-O header
	 * x22 - Kernel virtual base
	 * x23 - Kernel physical base
	 * x24 - Physical memory size
	 */
	sub		x18, x0, x23
	sub		x24, x24, x18
	add		x22, x22, x18
	add		x23, x23, x18

	/*
	 * x0  - V=P virtual cursor
	 * x4  - V=P physical cursor
	 * x14 - KVA virtual cursor
	 * x15 - KVA physical cursor
	 */
	mov		x4, x0
	mov		x14, x22
	mov		x15, x23

	/*
	 * Allocate L1 tables
	 * x1 - V=P L1 page
	 * x3 - KVA L1 page
	 * x2 - free mem pointer from which we allocate a variable number of L2
	 * pages. The maximum number of bootstrap page table pages is limited to
	 * BOOTSTRAP_TABLE_SIZE. For a 2G 4k page device, assuming the worst-case
	 * slide, we need 1xL1 and up to 3xL2 pages (1GB mapped per L1 entry), so
	 * 8 total pages for V=P and KVA.
	 */
	mov		x1, x25
	add		x3, x1, PGBYTES
	mov		x2, x3

	/*
	 * Setup the V=P bootstrap mapping
	 * x5 - total number of L2 entries to allocate
	 */
	lsr		x5,  x24, #(ARM_TT_L2_SHIFT)
	/* create_bootstrap_mapping(vbase, pbase, num_ents, L1 table, freeptr) */
	create_bootstrap_mapping x0,  x4,  x5, x1, x2, x6, x10, x11, x12, x13

	/* Setup the KVA bootstrap mapping */
	lsr		x5,  x24, #(ARM_TT_L2_SHIFT)
	create_bootstrap_mapping x14, x15, x5, x3, x2, x9, x10, x11, x12, x13
	/*
	 * In 16KB granule with 47-bit VA, L1 entry 0 covers 0x0 - 0x0fffffffff (first 64GB).
	 * Entire DRAM (0x80000000 - 0x180000000) and low MMIO (0x0 - 0x09830000) reside in L1 entry 0.
	 * L2 entries are 32MB blocks (2^25 = 0x02000000 bytes).
	 * Retrieve the L2 table pointer from L1 entry 0:
	 */
	ldr		x6, [x1, #0]
	and		x6, x6, #(ARM_TTE_TABLE_MASK)

	/* Map 0x80000000 - 0x81ffffff (entry 64: 0x80000000 >> 25 = 64) covering 0x80060000 and boot_args */
	MOV64	x7, (ARM_TTE_BOOT_BLOCK_LOWER | ARM_TTE_BOOT_BLOCK_UPPER | 0x80000000)
	str		x7, [x6, #(64 * 8)]

	/* Map 0x06000000 - 0x07ffffff (entry 3: 0x06000000 >> 25 = 3) covering IMEM restart 0x066bf65c & UART 0x075b0000 */
	MOV64	x7, (ARM_TTE_TYPE_BLOCK | ARM_TTE_VALID | ARM_TTE_BLOCK_AF | ARM_TTE_BLOCK_ATTRINDX(CACHE_ATTRINDX_DISABLE) | ARM_TTE_BLOCK_NX | ARM_TTE_BLOCK_PNX | 0x06000000)
	str		x7, [x6, #(3 * 8)]

	/* Map 0x08000000 - 0x09ffffff (entry 4: 0x08000000 >> 25 = 4) covering APCS watchdog 0x09830000 */
	MOV64	x7, (ARM_TTE_TYPE_BLOCK | ARM_TTE_VALID | ARM_TTE_BLOCK_AF | ARM_TTE_BLOCK_ATTRINDX(CACHE_ATTRINDX_DISABLE) | ARM_TTE_BLOCK_NX | ARM_TTE_BLOCK_PNX | 0x08000000)
	str		x7, [x6, #(4 * 8)]

	/* Map 0x00000000 - 0x01ffffff (entry 0: 0x00000000 >> 25 = 0) covering MPM PS_HOLD 0x004ab000 */
	MOV64	x7, (ARM_TTE_TYPE_BLOCK | ARM_TTE_VALID | ARM_TTE_BLOCK_AF | ARM_TTE_BLOCK_ATTRINDX(CACHE_ATTRINDX_DISABLE) | ARM_TTE_BLOCK_NX | ARM_TTE_BLOCK_PNX | 0x00000000)
	str		x7, [x6, #(0 * 8)]

	/* Map 0xa6000000 - 0xa7ffffff (entry 83: 0xa6000000 >> 25 = 83) covering pstore ramoops 0xa7f00000 & console 0xa7fbe000 as Normal Writeback RAM */
	MOV64	x7, (ARM_TTE_BOOT_BLOCK_LOWER | ARM_TTE_BOOT_BLOCK_UPPER | 0xa6000000)
	str		x7, [x6, #(83 * 8)]

	/* Ensure TTEs are visible */
	dsb		ish
	isb

	// Checkpoint K2: early mappings prepared
	XZS_RECORD_STAGE 0x202
	adrp	x0, str_k2@page
	add		x0, x0, str_k2@pageoff
	bl		EXT(xzs_early_puts)

	b		common_start

/*
 * Begin common CPU initialization
 *
 * Regster state:
 *	x20 - PA of boot args
 *	x21 - zero on cold boot, PA of cpu data on warm reset
 *	x22 - Kernel virtual base
 *	x23 - Kernel physical base
 *	x25 - PA of the V=P pagetable root
 *	 lr - KVA of C init routine
 *	 sp - SP_EL0 selected
 *
 *	SP_EL0 - KVA of CPU's interrupt stack
 *	SP_EL1 - KVA of CPU's exception stack
 *	TPIDRRO_EL0 - CPU number
 */
common_start:

#if HAS_NEX_PG
	mov x19, lr
	bl		EXT(set_nex_pg)
	mov lr, x19
#endif

	// Set the translation control register.
	MOV64	x1, TCR_EL1_BOOT

	adrp	x0, str_tcr_cand@page
	add		x0, x0, str_tcr_cand@pageoff
	bl		EXT(xzs_early_puts)
	mov		x0, x1
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	MSR_TCR_EL1_X1

	adrp	x0, str_tcr_actual@page
	add		x0, x0, str_tcr_actual@pageoff
	bl		EXT(xzs_early_puts)
	mrs		x0, TCR_EL1
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	/* Set up translation table base registers.
	 *	TTBR0 - V=P table @ top of kernel
	 *	TTBR1 - KVA table @ top of kernel + 1 page
	 */
#if defined(KERNEL_INTEGRITY_KTRR) || defined(KERNEL_INTEGRITY_CTRR) || defined(KERNEL_INTEGRITY_PV_CTRR)
	/* Note that for KTRR configurations, the V=P map will be modified by
	 * arm_vm_init.c.
	 */
#endif
	and		x0, x25, #(TTBR_BADDR_MASK)
	mov		x19, lr
	bl		EXT(set_mmu_ttb)
	mov		lr, x19

	adrp	x0, str_ttbr0_actual@page
	add		x0, x0, str_ttbr0_actual@pageoff
	bl		EXT(xzs_early_puts)
	mrs		x0, TTBR0_EL1
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	add		x0, x25, PGBYTES
	and		x0, x0, #(TTBR_BADDR_MASK)
	MSR_TTBR1_EL1_X0

	adrp	x0, str_ttbr1_actual@page
	add		x0, x0, str_ttbr1_actual@pageoff
	bl		EXT(xzs_early_puts)
	mrs		x0, TTBR1_EL1
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	// Set up MAIR attr0 for normal memory, attr1 for device memory
	mov		x0, xzr
	mov		x1, #(MAIR_WRITEBACK << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_WRITEBACK))
	orr		x0, x0, x1
	mov		x1, #(MAIR_WRITETHRU << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_WRITETHRU))
	orr		x0, x0, x1
	mov		x1, #(MAIR_WRITECOMB << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_WRITECOMB))
	orr		x0, x0, x1
	mov		x1, #(MAIR_WRITEBACK << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_RESERVED))
	orr		x0, x0, x1
	mov		x1, #(MAIR_POSTED_COMBINED_REORDERED << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_POSTED_COMBINED_REORDERED))
	orr		x0, x0, x1
	mov		x1, #(MAIR_DISABLE << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_DISABLE))
	orr		x0, x0, x1
#if HAS_FEAT_XS
	mov		x1, #(MAIR_DISABLE_XS << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_DISABLE_XS))
	orr		x0, x0, x1
	mov		x1, #(MAIR_POSTED_COMBINED_REORDERED_XS << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_POSTED_COMBINED_REORDERED_XS))
	orr		x0, x0, x1
#else
	mov		x1, #(MAIR_POSTED << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_POSTED))
	orr		x0, x0, x1
	mov		x1, #(MAIR_POSTED_REORDERED << MAIR_ATTR_SHIFT(CACHE_ATTRINDX_POSTED_REORDERED))
	orr		x0, x0, x1
#endif /* HAS_FEAT_XS */
	msr		MAIR_EL1, x0
	isb
	tlbi	vmalle1
	dsb		ish

	adrp	x0, str_mair_actual@page
	add		x0, x0, str_mair_actual@pageoff
	bl		EXT(xzs_early_puts)
	mrs		x0, MAIR_EL1
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	// Checkpoint K3: TCR_EL1 configured
	XZS_RECORD_STAGE 0x203
	adrp	x0, str_k3@page
	add		x0, x0, str_k3@pageoff
	bl		EXT(xzs_early_puts)


#ifndef __ARM_IC_NOALIAS_ICACHE__
	/* Invalidate the TLB and icache on systems that do not guarantee that the
	 * caches are invalidated on reset.
	 */
	tlbi	vmalle1
	ic		iallu
#endif

	/* If x21 is not 0, then this is either the start_cpu path or
	 * the resume_idle_cpu path.  cpu_ttep should already be
	 * populated, so just switch to the kernel_pmap now.
	 */

	cbz		x21, 1f
	adrp	x0, EXT(cpu_ttep)@page
	add		x0, x0, EXT(cpu_ttep)@pageoff
	ldr		x0, [x0]
	MSR_TTBR1_EL1_X0
1:

	// Set up the exception vectors
#if __ARM_KERNEL_PROTECT__
	/* If this is not the first reset of the boot CPU, the alternate mapping
	 * for the exception vectors will be set up, so use it.  Otherwise, we
	 * should use the mapping located in the kernelcache mapping.
	 */
	MOV64	x0, ARM_KERNEL_PROTECT_EXCEPTION_START

	cbnz		x21, 1f
#endif /* __ARM_KERNEL_PROTECT__ */
	// Keep diagnostic exception vector active at physical address (mapped in V=P)
	adrp	x0, EXT(LowExceptionVectorBase)@page
	add		x0, x0, EXT(LowExceptionVectorBase)@pageoff
1:
	MSR_VBAR_EL1_X0

#if HAS_APPLE_PAC
	PAC_INIT_KEY_STATE tmp=x0, tmp2=x1
#endif /* HAS_APPLE_PAC */

	// Checkpoint K4: before SCTLR.M
	XZS_RECORD_STAGE 0x204
	adrp	x0, str_k4@page
	add		x0, x0, str_k4@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_sctlr_cand@page
	add		x0, x0, str_sctlr_cand@pageoff
	bl		EXT(xzs_early_puts)
	mrs		x0, SCTLR_EL1
	orr		x0, x0, #(1 << 0)		// M=1: Enable MMU
	bic		x0, x0, #(1 << 2)		// C=0: Data Cache OFF (all DRAM stores uncached & persistent)
	orr		x0, x0, #(1 << 12)		// I=1: Enable Instruction Cache
	bic		x0, x0, #(1 << 1)		// A=0: Disable Alignment Faults
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	// Enable caches, MMU
	mrs		x0, SCTLR_EL1
	orr		x0, x0, #(1 << 0)		// M=1
	bic		x0, x0, #(1 << 2)		// C=0
	orr		x0, x0, #(1 << 12)		// I=1
	bic		x0, x0, #(1 << 1)		// A=0
	MSR_SCTLR_EL1_X0
	isb		sy

	// Checkpoint K5: after SCTLR.M
	XZS_RECORD_STAGE 0x205
	adrp	x0, str_k5@page
	add		x0, x0, str_k5@pageoff
	bl		EXT(xzs_early_puts)

#if (!CONFIG_KERNEL_INTEGRITY || (CONFIG_KERNEL_INTEGRITY && !defined(KERNEL_INTEGRITY_WT)))
	/* Watchtower
	 *
	 * If we have a Watchtower monitor it will setup CPACR_EL1 for us, touching
	 * it here would trap to EL3.
	 */

	// Enable NEON
	mov		x0, #(CPACR_FPEN_ENABLE)
	msr		CPACR_EL1, x0
#endif

	// Clear thread pointer
	msr		TPIDR_EL1, xzr						// Set thread register


#if defined(APPLE_ARM64_ARCH_FAMILY)
	mrs		x12, MDSCR_EL1
	orr		x12, x12, MDSCR_TDCC
	msr		MDSCR_EL1, x12
	// Initialization common to all non-virtual Apple targets
#endif  // APPLE_ARM64_ARCH_FAMILY

	// Read MIDR before start of per-SoC tunables
	mrs x12, MIDR_EL1

	APPLY_TUNABLES x12, x13, x14

#if HAS_CLUSTER && !NO_CPU_OVRD
	// Unmask external IRQs if we're restarting from non-retention WFI
	mrs		x9, CPU_OVRD
	and		x9, x9, #(~(ARM64_REG_CYC_OVRD_irq_mask | ARM64_REG_CYC_OVRD_fiq_mask))
	msr		CPU_OVRD, x9
#endif

	// If x21 != 0, we're doing a warm reset, so we need to trampoline to the kernel pmap.
	cbnz	x21, Ltrampoline

	// Set physical pointer of boot args (accessible via TTBR0) as first arg
	mov		x0, x20

#if KASAN
	mov	x20, x0
	mov	x21, lr

	// x0: boot args
	// x1: KVA page table phys base
	mrs	x1, TTBR1_EL1
	bl	EXT(kasan_bootstrap)

	mov	x0, x20
	mov	lr, x21
#endif

	// Compute KVA of arm_init into x1
	adrp	x1, EXT(arm_init)@page
	add		x1, x1, EXT(arm_init)@pageoff
	add		x1, x1, x22							// Convert to KVA
	sub		x1, x1, x23

	// Set return address to xzs_spin_halt in KVA in case arm_init returns
	adrp	lr, EXT(xzs_spin_halt)@page
	add		lr, lr, EXT(xzs_spin_halt)@pageoff
	add		lr, lr, x22
	sub		lr, lr, x23

	// Branch directly to arm_init(boot_args) in KVA!
	br		x1


Ltrampoline:
	// Load VA of the trampoline
	adrp	x0, arm_init_tramp@page
	add		x0, x0, arm_init_tramp@pageoff
	add		x0, x0, x22
	sub		x0, x0, x23

	// Branch to the trampoline
	br		x0

/*
 * V=P to KVA trampoline.
 *	x0 - KVA of cpu data pointer
 */
	.text
	.align 2
arm_init_tramp:
	ARM64_JUMP_TARGET
	/* On a warm boot, the full kernel translation table is initialized in
	 * addition to the bootstrap tables. The layout is as follows:
	 *
	 *  +--Top of Memory--+
	 *         ...
	 *  |                 |
	 *  |  Primary Kernel |
	 *  |   Trans. Table  |
	 *  |                 |
	 *  +--Top + 5 pages--+
	 *  |                 |
	 *  |  Invalid Table  |
	 *  |                 |
	 *  +--Top + 4 pages--+
	 *  |                 |
	 *  |    KVA Table    |
	 *  |                 |
	 *  +--Top + 2 pages--+
	 *  |                 |
	 *  |    V=P Table    |
	 *  |                 |
	 *  +--Top of Kernel--+
	 *  |                 |
	 *  |  Kernel Mach-O  |
	 *  |                 |
	 *         ...
	 *  +---Kernel Base---+
	 */


	mov		x19, lr
	// Convert CPU data PA to VA and set as first argument
	mov		x0, x21
	bl		EXT(phystokv)

	mov		lr, x19

	/* Return to arm_init() */
	ret

/*
 * ============================================================================
 * Xperia XZs (MSM8996) Early Diagnostic Telemetry & Exception Handlers
 * ============================================================================
 */
	.text
	.align 2
	.globl EXT(xzs_early_putc)
	.globl EXT(xzs_early_puts)
	.globl EXT(xzs_early_puthex64)

/*
 * LowExceptionVectorBase Trampolines
 */
xzs_exc_sync_sp0:
	msr		TPIDRRO_EL0, x0
	mov		x0, #0
	b		xzs_exc_common

xzs_exc_irq_sp0:
	msr		TPIDRRO_EL0, x0
	mov		x0, #1
	b		xzs_exc_common

xzs_exc_fiq_sp0:
	msr		TPIDRRO_EL0, x0
	mov		x0, #2
	b		xzs_exc_common

xzs_exc_serror_sp0:
	msr		TPIDRRO_EL0, x0
	mov		x0, #3
	b		xzs_exc_common

xzs_exc_sync_sp1:
	msr		TPIDRRO_EL0, x0
	mov		x0, #0
	b		xzs_exc_common

xzs_exc_irq_sp1:
	msr		TPIDRRO_EL0, x0
	mov		x0, #1
	b		xzs_exc_common

xzs_exc_fiq_sp1:
	msr		TPIDRRO_EL0, x0
	mov		x0, #2
	b		xzs_exc_common

xzs_exc_serror_sp1:
	msr		TPIDRRO_EL0, x0
	mov		x0, #3
	b		xzs_exc_common

xzs_exc_sync_el0:
xzs_exc_sync_el0_32:
	msr		TPIDRRO_EL0, x0
	mov		x0, #0
	b		xzs_exc_common

xzs_exc_irq_el0:
xzs_exc_irq_el0_32:
	msr		TPIDRRO_EL0, x0
	mov		x0, #1
	b		xzs_exc_common

xzs_exc_fiq_el0:
xzs_exc_fiq_el0_32:
	msr		TPIDRRO_EL0, x0
	mov		x0, #2
	b		xzs_exc_common

xzs_exc_serror_el0:
xzs_exc_serror_el0_32:
	msr		TPIDRRO_EL0, x0
	mov		x0, #3
	b		xzs_exc_common

/*
 * xzs_exc_common:
 *   Saves complete CPU state (x0-x30, SP, ESR, ELR, FAR, SPSR, SCTLR, TCR, TTBR, MAIR, VBAR)
 *   Disables MMU to ensure flat physical MMIO access, then dumps state via UART & DRAM buffer.
 */
xzs_exc_common:
	msr		DAIFSet, #0xf

	// Save x1 to TPIDR_EL1
	msr		TPIDR_EL1, x1

	// Stage 0: Direct Atomic DRAM Commit to 0x80060000 header
	// Accessible via TTBR0 (when MMU is ON) or physical DRAM (when MMU is OFF)
	MOV64	x1, 0x80060000
	ldr		w16, [x1]
	MOV64	x17, 0x585a5344				// "XZSD"
	cmp		w16, w17
	bne		1f
	// Increment exception_count at offset 0x10 (16)
	ldr		w16, [x1, #16]
	add		w16, w16, #1
	str		w16, [x1, #16]
	// Store last_elr at offset 0x18 (24)
	mrs		x16, ELR_EL1
	str		x16, [x1, #24]
	// Store last_esr at offset 0x20 (32)
	mrs		x16, ESR_EL1
	str		x16, [x1, #32]
	// Store last_far at offset 0x28 (40)
	mrs		x16, FAR_EL1
	str		x16, [x1, #40]
	// Store last_spsr at offset 0x30 (48)
	mrs		x16, SPSR_EL1
	str		x16, [x1, #48]
1:

	adrp	x1, xzs_exc_scratch@page
	add		x1, x1, xzs_exc_scratch@pageoff

	// Store type ID at offset 8
	str		x0, [x1, #8]

	// Restore original x1 and store at offset 112+8
	mrs		x0, TPIDR_EL1
	str		x0, [x1, #(112 + 8)]

	// Restore original x0 from TPIDRRO_EL0 and store at offset 112
	mrs		x0, TPIDRRO_EL0
	str		x0, [x1, #112]

	// Save remaining general purpose registers x2-x30
	stp		x2, x3, [x1, #(112 + 16)]
	stp		x4, x5, [x1, #(112 + 32)]
	stp		x6, x7, [x1, #(112 + 48)]
	stp		x8, x9, [x1, #(112 + 64)]
	stp		x10, x11, [x1, #(112 + 80)]
	stp		x12, x13, [x1, #(112 + 96)]
	stp		x14, x15, [x1, #(112 + 112)]
	stp		x16, x17, [x1, #(112 + 128)]
	stp		x18, x19, [x1, #(112 + 144)]
	stp		x20, x21, [x1, #(112 + 160)]
	stp		x22, x23, [x1, #(112 + 176)]
	stp		x24, x25, [x1, #(112 + 192)]
	stp		x26, x27, [x1, #(112 + 208)]
	stp		x28, x29, [x1, #(112 + 224)]
	str		x30, [x1, #(112 + 240)]

	// Save SP
	mov		x0, sp
	str		x0, [x1, #104]

	// Save exception and system registers
	mrs		x0, CurrentEL
	str		x0, [x1, #16]
	mrs		x0, ESR_EL1
	str		x0, [x1, #24]
	mrs		x0, ELR_EL1
	str		x0, [x1, #32]
	mrs		x0, FAR_EL1
	str		x0, [x1, #40]
	mrs		x0, SPSR_EL1
	str		x0, [x1, #48]
	mrs		x0, SCTLR_EL1
	str		x0, [x1, #56]
	mrs		x0, TCR_EL1
	str		x0, [x1, #64]
	mrs		x0, TTBR0_EL1
	str		x0, [x1, #72]
	mrs		x0, TTBR1_EL1
	str		x0, [x1, #80]
	mrs		x0, MAIR_EL1
	str		x0, [x1, #88]
	mrs		x0, VBAR_EL1
	str		x0, [x1, #96]

	// Do NOT disable MMU! Handler continues with active address space.
	// Dump diagnostics
	adrp	x19, xzs_exc_scratch@page
	add		x19, x19, xzs_exc_scratch@pageoff

	adrp	x0, str_exc_header@page
	add		x0, x0, str_exc_header@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_exc_type@page
	add		x0, x0, str_exc_type@pageoff
	bl		EXT(xzs_early_puts)

	ldr		w1, [x19, #8]
	cmp		w1, #0
	beq		1f
	cmp		w1, #1
	beq		2f
	cmp		w1, #2
	beq		3f
	adrp	x0, str_type_serror@page
	add		x0, x0, str_type_serror@pageoff
	b		4f
1:	adrp	x0, str_type_sync@page
	add		x0, x0, str_type_sync@pageoff
	b		4f
2:	adrp	x0, str_type_irq@page
	add		x0, x0, str_type_irq@pageoff
	b		4f
3:	adrp	x0, str_type_fiq@page
	add		x0, x0, str_type_fiq@pageoff
4:	bl		EXT(xzs_early_puts)

	adrp	x0, str_currel@page
	add		x0, x0, str_currel@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #16]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_esr@page
	add		x0, x0, str_esr@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #24]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_elr@page
	add		x0, x0, str_elr@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #32]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_far@page
	add		x0, x0, str_far@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #40]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_spsr@page
	add		x0, x0, str_spsr@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #48]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_sctlr@page
	add		x0, x0, str_sctlr@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #56]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_tcr@page
	add		x0, x0, str_tcr@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #64]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_ttbr0@page
	add		x0, x0, str_ttbr0@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #72]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_ttbr1@page
	add		x0, x0, str_ttbr1@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #80]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_mair@page
	add		x0, x0, str_mair@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #88]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_vbar@page
	add		x0, x0, str_vbar@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #96]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	adrp	x0, str_sp@page
	add		x0, x0, str_sp@pageoff
	bl		EXT(xzs_early_puts)
	ldr		x0, [x19, #104]
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)

	// Dump x0-x30
	mov		w20, #0
10:
	adrp	x0, str_reg_x@page
	add		x0, x0, str_reg_x@pageoff
	bl		EXT(xzs_early_puts)

	cmp		w20, #10
	blt		11f
	mov		w0, #'1'
	cmp		w20, #20
	blt		12f
	mov		w0, #'2'
	cmp		w20, #30
	blt		12f
	mov		w0, #'3'
12:
	bl		EXT(xzs_early_putc)
11:
	mov		w1, w20
	mov		w3, #10
	udiv	w2, w1, w3
	msub	w2, w2, w3, w1
	add		w0, w2, #'0'
	bl		EXT(xzs_early_putc)
	mov		w0, #'='
	bl		EXT(xzs_early_putc)

	add		x1, x19, #112
	ldr		x0, [x1, x20, lsl #3]
	bl		EXT(xzs_early_puthex64)
	mov		w0, #' '
	bl		EXT(xzs_early_putc)

	and		w1, w20, #3
	cmp		w1, #3
	bne		13f
	adrp	x0, str_newline@page
	add		x0, x0, str_newline@pageoff
	bl		EXT(xzs_early_puts)
13:
	add		w20, w20, #1
	cmp		w20, #31
	blt		10b

	adrp	x0, str_exc_footer@page
	add		x0, x0, str_exc_footer@pageoff
	bl		EXT(xzs_early_puts)

	.globl EXT(xzs_spin_halt)
LEXT(xzs_spin_halt)
xzs_spin_halt:
	adrp		x0, str_spin_halt@page
	add		x0, x0, str_spin_halt@pageoff
	bl		EXT(xzs_early_puts)
	// Flush entire persistent RAM buffers to physical DRAM (Point of Coherency)
	MOV64	x0, 0x80060000
	MOV64	x1, 0x80070000
10:	dc		civac, x0
	add		x0, x0, #64
	cmp		x0, x1
	b.lo	10b

	MOV64	x0, 0xa7f00000
	MOV64	x1, 0xa8000000
20:	dc		civac, x0
	add		x0, x0, #64
	cmp		x0, x1
	b.lo	20b

	dsb		sy
	isb		sy
	// 1. Write Fastboot restart reason (0x77665500 matches Linux "bootloader" for aboot)
	MOV64	x1, 0x066bf65c
	MOV64	x2, 0x77665500
	str		w2, [x1]
	dsb		sy
	isb		sy

	// 2. Short delay for memory/UART flush (~5ms)
	MOV64	x0, 100000
1:	subs	x0, x0, #1
	b.ne	1b

	// 3. Trigger Qualcomm APCS Watchdog Bite (WDT_BITE_TIME=1, WDT_EN=1, WDT_RST=1)
	// Audited MSM8996 APCS WDT register map (0x09830000 base from twrp-kernel.bin):
	// +0x04: WDT_RST (reload counter)
	// +0x08: WDT_EN  (bit 0 = 1 enable watchdog countdown)
	// +0x14: WDT_BITE_TIME (ticks @ 32765 Hz)
	MOV64	x1, 0x09830014
	mov		w2, #1
	str		w2, [x1]			// Bite in 1 tick (~30us)
	MOV64	x1, 0x09830008
	str		w2, [x1]			// WDT_EN = 1
	MOV64	x1, 0x09830004
	str		w2, [x1]			// WDT_RST = 1 (reload counter to bite time)
	dsb		sy
	isb		sy

	// 4. MPM PS_HOLD pull-down fallback
	MOV64	x1, 0x004ab000
	str		wzr, [x1]
	dsb		sy
	isb		sy

2:	wfe
	b		2b

/*
 * ============================================================================
 * Qualcomm MSM8996 Hardware Breadcrumb & Watchdog Interface
 * ============================================================================
 */
	.globl EXT(xzs_breadcrumb)
LEXT(xzs_breadcrumb)
xzs_breadcrumb:
	// x0 = checkpoint, w1 = errno
	stp		x29, x30, [sp, #-32]!
	stp		x19, x20, [sp, #16]
	mov		x19, x0
	mov		w20, w1

	// 0. Pre-seed Fastboot restart reason (0x77665500) so any reset boots to Fastboot
	MOV64	x2, 0x066bf65c
	MOV64	x3, 0x77665500
	str		w3, [x2]

	// 1. Write to on-chip IMEM SRAM (0x066bf660) - completely non-volatile across reset
	MOV64	x2, 0x066bf660
	MOV64	x3, 0x585a5344		// Magic: 'XZSD'
	str		w3, [x2, #0]		// magic
	str		w19, [x2, #4]		// checkpoint
	str		w20, [x2, #8]		// status_errno
	ldr		w4, [x2, #12]		// monotonic counter
	add		w4, w4, #1
	str		w4, [x2, #12]
	str		wzr, [x2, #16]		// fault_type = 0 (NORMAL)
	dsb		sy

	// 2. Pet hardware watchdog (reload counter on genuine forward progress)
	bl		EXT(xzs_watchdog_pet)

	// 3. Mirror to DRAM persistent buffer (0x80060020)
	MOV64	x2, 0x80060020
	MOV64	x3, 0x585a5344
	str		w3, [x2, #0]
	str		w19, [x2, #4]
	str		w20, [x2, #8]
	str		w4, [x2, #12]
	dc		cvac, x2
	dsb		sy

	// 4. Output concise breadcrumb line to console
	adrp	x0, str_bc_hdr@page
	add		x0, x0, str_bc_hdr@pageoff
	bl		EXT(xzs_early_puts)
	mov		x0, x19
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_bc_err@page
	add		x0, x0, str_bc_err@pageoff
	bl		EXT(xzs_early_puts)
	mov		w0, w20
	bl		EXT(xzs_early_puthex64)
	adrp	x0, str_bc_end@page
	add		x0, x0, str_bc_end@pageoff
	bl		EXT(xzs_early_puts)

	ldp		x19, x20, [sp, #16]
	ldp		x29, x30, [sp], #32
	ret

	.globl EXT(xzs_watchdog_arm)
LEXT(xzs_watchdog_arm)
xzs_watchdog_arm:
	cbz		w0, 1f
	b		2f
1:	MOV64	x0, 0xF0000			// ~30 seconds (983040 ticks at 32765 Hz)
2:	// 1. Pre-seed Fastboot restart reason in IMEM SRAM
	MOV64	x1, 0x066bf65c
	MOV64	x2, 0x77665500
	str		w2, [x1]
	// 2. Disable watchdog countdown before programming
	MOV64	x1, 0x09830008
	str		wzr, [x1]			// WDT_EN = 0
	// 3. Set bite time countdown
	MOV64	x1, 0x09830014
	str		w0, [x1]			// WDT_BITE_TIME
	// 4. Reload counter
	MOV64	x1, 0x09830004
	mov		w2, #1
	str		w2, [x1]			// WDT_RST = 1
	// 5. Enable watchdog countdown
	MOV64	x1, 0x09830008
	str		w2, [x1]			// WDT_EN = 1
	dsb		sy
	isb
	ret

	.globl EXT(xzs_watchdog_pet)
LEXT(xzs_watchdog_pet)
xzs_watchdog_pet:
	// Re-arm watchdog with full 30-second budget (~0xF0000 ticks @ 32765 Hz)
	MOV64	x0, 0xF0000
	b		xzs_watchdog_arm


	.globl EXT(xzs_record_fault)
LEXT(xzs_record_fault)
xzs_record_fault:
	// Pre-seed Fastboot restart reason
	MOV64	x4, 0x066bf65c
	MOV64	x5, 0x77665500
	str		w5, [x4]

	// Write fault details to IMEM SRAM
	MOV64	x4, 0x066bf660
	str		w0, [x4, #16]		// fault_type
	str		w1, [x4, #20]		// fault_esr
	str		x2, [x4, #24]		// fault_elr
	dsb		sy

	// Mirror to DRAM persistent buffer
	MOV64	x4, 0x80060030
	str		w0, [x4, #0]
	str		w1, [x4, #4]
	str		x2, [x4, #8]
	str		x3, [x4, #16]
	dc		civac, x4
	dsb		sy
	ret


/*
 * ============================================================================
 * Qualcomm MSM8996 Secondary Core Entry (PSCI CPU_ON Trampoline)
 * ============================================================================
 */
	.data
	.align 14
xzs_secondary_stacks:
	.space 4 * 4096
xzs_secondary_stacks_end:

	.globl EXT(g_xzs_ttbr0)
LEXT(g_xzs_ttbr0)
	.quad 0

xzs_secondary_kva_target:
	.quad xzs_secondary_kva_entry

	.text
	.align 2
	.globl EXT(xzs_secondary_entry)
LEXT(xzs_secondary_entry)
xzs_secondary_entry:
	// Disable all interrupts & debug exceptions (D=1, A=1, I=1, F=1)
	msr		DAIFSet, #(DAIFSC_ALL)

	// Set early physical SP_EL1: xzs_secondary_stacks_end - (cpu_id * 4096)
	msr		SPSel, #1
	adrp	x1, xzs_secondary_stacks_end@page
	add		x1, x1, xzs_secondary_stacks_end@pageoff
	sub		x1, x1, x0, lsl #12
	mov		sp, x1

	// Set early physical SP_EL0: 2048 bytes below SP_EL1
	sub		x1, x1, #2048
	msr		SPSel, #0
	mov		sp, x1

	// Save cpu_id in x19
	mov		x19, x0

	// 1. MAIR_EL1
	MOV64	x1, 0x0c0804ff00bb44ff
	msr		MAIR_EL1, x1

	// 2. TCR_EL1
	MOV64	x1, 0x000000226511a511
	msr		TCR_EL1, x1
	isb		sy

	// 3. TTBR1_EL1 (Kernel translation table from cpu_ttep)
	adrp	x1, EXT(cpu_ttep)@page
	add		x1, x1, EXT(cpu_ttep)@pageoff
	ldr		x1, [x1]
	msr		TTBR1_EL1, x1

	// 4. TTBR0_EL1 (Boot/Identity translation table from g_xzs_ttbr0)
	adrp	x1, EXT(g_xzs_ttbr0)@page
	add		x1, x1, EXT(g_xzs_ttbr0)@pageoff
	ldr		x1, [x1]
	msr		TTBR0_EL1, x1
	isb		sy

	// 5. Invalidate local TLB
	tlbi	vmalle1
	dsb		nsh
	isb		sy

	// 6. Set VBAR_EL1 to LowExceptionVectorBase before MMU enable
	adrp	x1, EXT(LowExceptionVectorBase)@page
	add		x1, x1, EXT(LowExceptionVectorBase)@pageoff
	msr		VBAR_EL1, x1

	// 7. Enable MMU & I-cache: SCTLR_EL1 = 0x10c01801 (M=1, I=1, C=0)
	MOV64	x1, 0x10c01801
	msr		SCTLR_EL1, x1
	isb		sy

	// 8. Long branch to High KVA!
	adrp	x1, xzs_secondary_kva_target@page
	add		x1, x1, xzs_secondary_kva_target@pageoff
	ldr		x1, [x1]
	br		x1

	.text
	.align 2
xzs_secondary_kva_entry:
	// Running in High KVA!
	// Update VBAR_EL1 to High KVA ExceptionVectorsBase
	adrp	x1, EXT(ExceptionVectorsBase)@page
	add		x1, x1, EXT(ExceptionVectorsBase)@pageoff
	msr		VBAR_EL1, x1
	isb		sy

	// Enable NEON / Floating Point (CPACR_EL1.FPEN = 0b11)
	mov		x1, #(CPACR_FPEN_ENABLE)
	msr		CPACR_EL1, x1
	isb		sy

	// Setup thread and stacks in High KVA from CpuDataEntries[cpu_id]
	adrp	x1, EXT(CpuDataEntries)@page
	add		x1, x1, EXT(CpuDataEntries)@pageoff
	add		x1, x1, x19, lsl #4
	ldr		x20, [x1, #8]				// x20 = cdp = CpuDataEntries[cpu_id].cpu_data_vaddr

	ldr		x21, [x20, #48]				// x21 = thread = cdp->cpu_active_thread
	msr		TPIDR_EL1, x21
	isb		sy

	// Set SP_EL1 to cdp->excepstackptr
	ldr		x1, [x20, #32]
	msr		SPSel, #1
	mov		sp, x1

	// Set SP_EL0 to thread->machine.kstackptr
	ldr		x1, [x21, #336]
	msr		SPSel, #0
	mov		sp, x1

	// Clear frame pointer
	mov		x29, #0

	// Clean & Invalidate local D-cache
	bl		EXT(Flush_Dcache)
	dsb		sy
	isb		sy

	// Enable D-cache: SCTLR_EL1.C = 1 (0x10c01805)
	mrs		x1, SCTLR_EL1
	orr		x1, x1, #(1 << 2)
	msr		SCTLR_EL1, x1
	dsb		sy
	isb		sy

	// Pass cpu_id to C entry
	mov		x0, x19
	bl		EXT(xzs_secondary_c_entry)

100:
	wfe
	b		100b


/*
 * xzs_early_putc:
 *   Transmits w0 via BLSP2 UART2 (0x075b0000) and writes to DRAM buffer (0x80060000).
 *   Preserves ALL caller registers. Fully SMP-safe and reentrant.
 */
LEXT(xzs_early_putc)
	cmp		sp, #0
	b.eq	.Lputc_early_nosp
	stp		x29, x30, [sp, #-48]!
	stp		x19, x20, [sp, #16]
	stp		x21, x22, [sp, #32]
	mrs		x22, DAIF
	msr		DAIFSet, #0xf			// Mask interrupts during character output
	mov		w19, w0

	cmp		w19, #'\n'
	bne		1f
	mov		w0, #'\r'
	bl		xzs_raw_tx
	mov		w0, w19
1:
	bl		xzs_raw_tx

	msr		DAIF, x22			// Restore interrupts
	ldp		x21, x22, [sp, #32]
	ldp		x19, x20, [sp, #16]
	ldp		x29, x30, [sp], #48
	ret

.Lputc_early_nosp:
	msr		TPIDRRO_EL0, x1
	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_putc_save@page
	add		x1, x1, xzs_putc_save@pageoff
	add		x1, x1, x2
	stp		x2, x3, [x1, #0]
	stp		x4, x5, [x1, #16]
	stp		x6, lr, [x1, #32]
	str		x0, [x1, #48]
	mrs		x0, TPIDRRO_EL0
	str		x0, [x1, #56]

	ldr		x0, [x1, #48]
	cmp		w0, #'\n'
	bne		1f
	mov		w0, #'\r'
	bl		xzs_raw_tx
	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_putc_save@page
	add		x1, x1, xzs_putc_save@pageoff
	add		x1, x1, x2
	ldr		x0, [x1, #48]
1:
	bl		xzs_raw_tx

	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_putc_save@page
	add		x1, x1, xzs_putc_save@pageoff
	add		x1, x1, x2
	ldp		x2, x3, [x1, #0]
	ldp		x4, x5, [x1, #16]
	ldp		x6, lr, [x1, #32]
	ldr		x0, [x1, #48]
	ldr		x1, [x1, #56]
	ret

/*
 * xzs_raw_tx: internal transmission of char in w0
 */
xzs_raw_tx:
	// Write to persistent DRAM log at 0x80060000
	MOV64	x2, 0x80060000
	ldr		w3, [x2]
	MOV64	x4, 0x585a5344			// "XZSD"
	cmp		w3, w4
	bne		7f
	add		x3, x2, #8				// pointer to write_offset
101:
	ldxr	w4, [x3]				// atomic fetch write_offset
	mov		w5, #65400
	cmp		w4, w5
	bge		7f
	add		w6, w4, #1
	stxr	w7, w6, [x3]			// atomic increment write_offset
	cbnz	w7, 101b
	add		x5, x2, #64				// 64-byte aligned header; data[] starts at offset 64 (0x40)
	add		x5, x5, w4, uxtw
	strb	w0, [x5]
	strb	wzr, [x5, #1]
	dc		cvac, x5
	dc		cvac, x3
7:
	// Write to persistent RAM pstore console buffer at 0xa7fbe000
	MOV64	x2, 0xa7fbe000
	ldr		w3, [x2]
	MOV64	x4, 0x43474244			// "DBGC" (PERSISTENT_RAM_SIG)
	cmp		w3, w4
	beq		80f
	// Initialize pstore console buffer header if not yet initialized
	str		w4, [x2]				// sig = 0x43474244
	str		wzr, [x2, #4]			// start = 0
	str		wzr, [x2, #8]			// size = 0
	dc		cvac, x2
80:
	ldr		w4, [x2, #8]			// size
	MOV64	x5, 262000				// max pstore console size
	cmp		w4, w5
	blo		81f
	mov		w4, #0					// buffer full: wrap around to beginning
81:
	add		x5, x2, #12				// header is 12 bytes: sig (4), start (4), size (4)
	add		x5, x5, w4, uxtw
	strb	w0, [x5]
	add		w4, w4, #1
	str		w4, [x2, #8]
	dc		cvac, x5
	dc		cvac, x2
8:
	// Write to persistent RAM pstore dmesg zone 0 at 0xa7f00000
	MOV64	x2, 0xa7f00000
	ldr		w3, [x2]
	MOV64	x4, 0x43474244			// "DBGC" (PERSISTENT_RAM_SIG)
	cmp		w3, w4
	beq		10f

	// Initialize pstore dmesg zone 0 header
	str		w4, [x2]				// sig = 0x43474244 ("DBGC")
	str		wzr, [x2, #4]			// start = 0
	str		wzr, [x2, #8]			// size = 0
	dc		cvac, x2

10:
	ldr		w4, [x2, #8]			// size
	MOV64	x5, 4000				// max dmesg zone 0 size (4096-byte record, max prz buffer 4084)
	cmp		w4, w5
	blo		11f
	mov		w4, #0					// buffer full: wrap around to beginning
11:
	add		x5, x2, #12
	add		x5, x5, w4, uxtw
	strb	w0, [x5]
	add		w4, w4, #1
	str		w4, [x2, #8]
	dc		cvac, x5
	dc		cvac, x2
9:
	dsb		ish
	ret

	.globl EXT(xzs_uart_putc_phys)
LEXT(xzs_uart_putc_phys)
	b		EXT(xzs_early_putc)

	.globl EXT(xzs_uart_putc_virt)
LEXT(xzs_uart_putc_virt)
	b		EXT(xzs_early_putc)

/*
 * xzs_early_puts:
 *   Input: x0 = pointer to null-terminated ASCII string.
 *   Preserves ALL registers. Fully SMP-safe and reentrant.
 */
LEXT(xzs_early_puts)
	cmp		sp, #0
	b.eq	.Lputs_early_nosp
	stp		x29, x30, [sp, #-32]!
	stp		x19, x20, [sp, #16]
	adrp		x19, EXT(xzs_early_puts_suppress)@page
	add		x19, x19, EXT(xzs_early_puts_suppress)@pageoff
	ldr		w19, [x19]
	cbnz		w19, .Lputs_suppressed
	mrs		x20, DAIF
	msr		DAIFSet, #0xf			// Mask interrupts during string output
	mov		x19, x0
1:
	ldrb	w0, [x19], #1
	cbz		w0, 2f
	bl		EXT(xzs_early_putc)
	b		1b
2:
	msr		DAIF, x20			// Restore interrupts
.Lputs_suppressed:
	ldp		x19, x20, [sp, #16]
	ldp		x29, x30, [sp], #32
	ret

.Lputs_early_nosp:
	msr		TPIDRRO_EL0, x1
	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_puts_save@page
	add		x1, x1, xzs_puts_save@pageoff
	add		x1, x1, x2
	stp		x19, lr, [x1, #0]
	mrs		x19, TPIDRRO_EL0
	str		x19, [x1, #16]
	str		x0, [x1, #24]

	mov		x19, x0
1:
	ldrb	w0, [x19], #1
	cbz		w0, 2f
	bl		EXT(xzs_early_putc)
	b		1b
2:
	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_puts_save@page
	add		x1, x1, xzs_puts_save@pageoff
	add		x1, x1, x2
	ldp		x19, lr, [x1, #0]
	ldr		x0, [x1, #24]
	ldr		x1, [x1, #16]
	ret

/*
 * xzs_early_puthex64:
 *   Input: x0 = 64-bit value to print as 0x0123456789abcdef.
 *   Preserves ALL registers. Fully SMP-safe and reentrant.
 */
LEXT(xzs_early_puthex64)
	cmp		sp, #0
	b.eq	.Lputhex_early_nosp
	stp		x29, x30, [sp, #-48]!
	stp		x19, x20, [sp, #16]
	stp		x21, x22, [sp, #32]
	mrs		x22, DAIF
	msr		DAIFSet, #0xf			// Mask interrupts
	mov		x19, x0
	adrp	x20, xzs_hex_digits@page
	add		x20, x20, xzs_hex_digits@pageoff

	mov		w0, #'0'
	bl		EXT(xzs_early_putc)
	mov		w0, #'x'
	bl		EXT(xzs_early_putc)

	mov		w21, #60
1:
	lsr		x0, x19, x21
	and		x0, x0, #0xf
	ldrb	w0, [x20, x0]
	bl		EXT(xzs_early_putc)
	subs	w21, w21, #4
	bge		1b

	msr		DAIF, x22
	ldp		x21, x22, [sp, #32]
	ldp		x19, x20, [sp, #16]
	ldp		x29, x30, [sp], #48
	ret

.Lputhex_early_nosp:
	msr		TPIDRRO_EL0, x1
	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_hex_save@page
	add		x1, x1, xzs_hex_save@pageoff
	add		x1, x1, x2
	stp		x19, lr, [x1, #0]
	stp		x20, x21, [x1, #16]
	mrs		x19, TPIDRRO_EL0
	str		x19, [x1, #32]
	str		x0, [x1, #40]

	mov		x19, x0
	adrp	x20, xzs_hex_digits@page
	add		x20, x20, xzs_hex_digits@pageoff

	mov		w0, #'0'
	bl		EXT(xzs_early_putc)
	mov		w0, #'x'
	bl		EXT(xzs_early_putc)

	mov		w21, #60
1:
	lsr		x0, x19, x21
	and		x0, x0, #0xf
	ldrb	w0, [x20, x0]
	bl		EXT(xzs_early_putc)
	subs	w21, w21, #4
	bge		1b

	mrs		x1, MPIDR_EL1
	and		x2, x1, #0xff
	ubfx	x1, x1, #8, #1
	bfi		x2, x1, #1, #1
	lsl		x2, x2, #6
	adrp	x1, xzs_hex_save@page
	add		x1, x1, xzs_hex_save@pageoff
	add		x1, x1, x2
	ldp		x19, lr, [x1, #0]
	ldp		x20, x21, [x1, #16]
	ldr		x0, [x1, #40]
	ldr		x1, [x1, #32]
	ret

/*
 * Static buffers and string tables (4 CPUs * 64 bytes)
 */
	.section __DATA,__data
	.align 3
xzs_exc_scratch:
	.space 512, 0
xzs_putc_save:
	.space 256, 0
xzs_puts_save:
	.space 256, 0
xzs_hex_save:
	.space 256, 0

	.section __TEXT,__cstring,cstring_literals
	.align 3
xzs_hex_digits:
	.asciz "0123456789abcdef"
str_k0:
	.asciz "[XNU-XZS] K0: kernel entry\n"
str_k1:
	.asciz "[XNU-XZS] K1: exception vectors installed\n"
str_k2:
	.asciz "[XNU-XZS] K2: early mappings prepared\n"
str_k3:
	.asciz "[XNU-XZS] K3: TCR_EL1 configured\n"
str_k4:
	.asciz "[XNU-XZS] K4: before SCTLR.M\n"
str_k5:
	.asciz "[XNU-XZS] K5: after SCTLR.M\n"
str_tcr_cand:
	.asciz "TCR_EL1 candidate:   "
str_tcr_actual:
	.asciz "TCR_EL1 actual:      "
str_ttbr0_actual:
	.asciz "TTBR0_EL1 actual:    "
str_ttbr1_actual:
	.asciz "TTBR1_EL1 actual:    "
str_mair_actual:
	.asciz "MAIR_EL1 actual:     "
str_sctlr_cand:
	.asciz "SCTLR_EL1 candidate: "
str_exc_header:
	.asciz "\n\n*** XNU EARLY EXCEPTION ***\n\n"
str_exc_type:
	.asciz "TYPE      = "
str_type_sync:
	.asciz "SYNC\n"
str_type_irq:
	.asciz "IRQ\n"
str_type_fiq:
	.asciz "FIQ\n"
str_type_serror:
	.asciz "SERROR\n"
str_currel:
	.asciz "CurrentEL = "
str_esr:
	.asciz "ESR_EL1   = "
str_elr:
	.asciz "ELR_EL1   = "
str_far:
	.asciz "FAR_EL1   = "
str_spsr:
	.asciz "SPSR_EL1  = "
str_sctlr:
	.asciz "SCTLR_EL1 = "
str_tcr:
	.asciz "TCR_EL1   = "
str_ttbr0:
	.asciz "TTBR0_EL1 = "
str_ttbr1:
	.asciz "TTBR1_EL1 = "
str_mair:
	.asciz "MAIR_EL1  = "
str_vbar:
	.asciz "VBAR_EL1  = "
str_sp:
	.asciz "SP        = "
str_reg_x:
	.asciz "x"
str_newline:
	.asciz "\n"
str_exc_footer:
	.asciz "\n*** CPU HALTED IN EARLY EXCEPTION HANDLER ***\n"
str_spin_halt:
	.asciz "XZS_SPIN_HALT_CALLED=yes\n"
str_bc_hdr:
	.asciz "\n[BREADCRUMB] CP="
str_bc_err:
	.asciz " ERR="
str_bc_end:
	.asciz "\n"


/* vim: set ts=4: */
