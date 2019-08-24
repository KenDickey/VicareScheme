/* Ikarus Scheme -- A compiler for R6RS Scheme.
 * Copyright (C) 2006,2007,2008  Abdulaziz Ghuloum
 * Modified by Marco Maggi <marco.maggi-ipsu@poste.it>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

.text
.globl  ik_asm_enter
.globl _ik_asm_enter
.globl  ik_foreign_call
.globl _ik_foreign_call
.globl  ik_asm_reenter
.globl _ik_asm_reenter
.globl ik_underflow_handler
.globl _ik_underflow_handler

/* For Scheme call/stack details see "doc/vicare-scheme.pdf":
 *
 *	"Chapter 16: Introduction to execution machinery"
 */

/** ------------------------------------------------------------
 ** 64-bit code -- ARM64/AARCH64
 ** ----------------------------------------------------------*/
// #if __aarch64__

// Nota Bene:
//   This code MUST correspond with defs in file
//     "ikarus.compiler.aarch64-assembler.sl"

// Scheme and C Stacks are disjoint
	
// c integer parameters come in registers:
//     %x0..%x7
// return value registers are %x0..%x7 and %x8
// CalleR-save regs x9..x15 used by Vicare
// CalleE-save registers:
//     %x19..%x28 are calleE-save
// Frame-Pointer: %x29
// Link Register: %x30	
// Stack Pointer: 31 -- Stack is 16 byte aligned [4 lower bits zero]
// PC is NOT a named register
// SP is used by C but NOT by Scheme [FPR]

//       Intel  ARM64
// AAR = %eax =  x9	accumulator and arguments count
// CPR = %edi = x10	pointer to closure
// APR = %ebp = x11	allocation pointer
// FPR = %esp = x12	frame pointer register = Scheme stack frame pointer
// PCR = %esi = x13	pointer to PCB: Process Control Block
//       %ecx = x14
//       %edx = x15      [code pointer reg]

// Process Control Block: see "src/internals.h"
// typedef struct ikpcb_t { 
//  ikptr_t  allocation_pointer; /* offset =  0 * wordsize =  0
//  ikptr_t  allocation_redline; /* offset =  1 * wordsize =  8
//  ikptr_t  frame_pointer;	 /* offset =  2 * wordsize = 16
//  ikptr_t  frame_base;	 /* offset =  3 * wordsize = 24
//  ikptr_t  frame_redline;	 /* offset =  4 * wordsize = 32
//  ikptr_t  next_k;		 /* offset =  5 * wordsize = 40
//  ikptr_t  system_stack;	 /* offset =  6 * wordsize = 48
//  ikptr_t  dirty_vector;	 /* offset =  7 * wordsize = 56
//  ikptr_t  arg_list;		 /* offset =  8 * wordsize = 64
//  ikptr_t  engine_counter;	 /* offset =  9 * wordsize = 72
//  ikptr_t  interrupted;	 /* offset = 10 * wordsize = 80
//  ikptr_t  base_rtd;		 /* offset = 11 * wordsize = 88
//  ikptr_t  collect_key;	 /* offset = 12 * wordsize = 96
//   ...                            ...
//   ... }

// Note: stack grows down toward lower memory, so	
//   stack slot 2         -> [%sp, #-16] (neg offset)
// But structures are UP from base, so	
//   ikpcb->frame_pointer -> [%pcb, #16] (pos offset) 	

.align 16
ik_asm_enter:
_ik_asm_enter:
	
  // Called by C proc ik_exec_code() in "ikarus-exec.c"
  // closure pointer is the 4th arg, or x3
  // argcount is the third arg, or      x2
  // code is the second arg, or         x1
  // pcb is the first arg, or           x0
  // C return point is in         LR = x30
  // C stack pointer in x31	

  mov x10, x3     // CPR gets closure pointer
  mov x9,  x2	  // AAR gets arg count
  mov x15, x1     // capture code pointer
  mov x13, x0     // PCR gets pointer to pcb
  ldr x11, [x13]  // allocation pointer is at 0(pcb)
  str SP,  [x13, #48] // save C stack pointer in pcb->system_stack
  ldr x12, [x13, #16] // load scheme stack from  pcb->frame_pointer
  b   L_call
  .quad 8
  .quad 0
L_multivalue_label: // FIXME
  .quad  L_multivalue_underflow
  .quad 0
L_call:
  br x1     // goooooooo code pointer
  // now we're back
ik_underflow_handler:
_ik_underflow_handler:
  str  x9, [x12, #-16] // store return value from AAR (stack slot 2)
//@@CheckMe@@ orig code has #-8 as return value	(rvcount=1) ??
  mov  x9, x0          // C's x0 gets AAR as return value
L_do_underflow:
  str x12, [x13, #16]  // store Scheme stack pointer in pcb->frame_pointer
  str x11, [x13]       // store allocation pointer   in pcb->allocation_pointer
// assert SP = [x13, #48] as Scheme did not touch SP [until multi-proc => multi PCBs]
  ldr SP,  [x13, #48]   // restore system C stack from pcb->system_stack 

// LP still holds return address for C code, so
  ret      // back to C, which handled the underflow 
multivalue_underflow:
L_multivalue_underflow:
  addi x12, #8  // Pop 1 double from Scheme Stack ???@@@CHECKME@@@???
  b L_do_underflow

// ------------------------------------------------------------

.align 8
ik_asm_reenter:
_ik_asm_reenter:
  // C parameters come in registers: x0..x7, d0..d7
  //     [%rdi, %rsi, %rdx, %rcx, %r8 and %r9]
  // return value registers are x0 for scalars & d0 for floats
  //     [%rax and %rdi]
  // callee-save registers:
  //     %rbp, %rbx, %r12, r13, r14, %r15 are callee-save [UNUSED HERE]

  // called in "ikarus-exec.c"
  // argc is the third arg      x2
  // scheme stack is second arg x1
  // pcb is the first arg       x0
  // return point is at         LR

  mov x9,  x2      // third arg  -> argc         AAR
  mov x12, x1      // second arg -> scheme stack FPR
  mov x13, x0      // first arg  -> pcb addr     PCR
  ldr x11, [x13]   // allocation pointer is at 0(pcb)

  str SP, [x13, #48]    // save esp in pcb->system_stack
  cmp x0, #-8           // (-1 << 4) => 1 arg
  b.ne L_multi_value_reentry

L_single_value_reentry:
  ldr x0, [x12, #-8]   // Result from Scheme Stack
// Intel ret loads PC from FPR and increments FPR by 8,
// so on aarch64 this should be:	
  mov  IPO, x12
  addi x12, x12, #8
  b    IP0

L_multi_value_reentry:
  ldr IP0, [x12]  // code addr on top of Scheme Stack
// DISP_MULTIVALUE_RP = -(wordsize + CALL-INSTRUCTION-SIZE)
  ldr IP1, [IP0, #-18] // ?? WTF ?? @@@CHECKME@@@ 
  br  IP1


// ------------------------------------------------------------

// Call a C function From Scheme.   When entering this function:
//
// * %rsi/x13/PCR must contain the address of the PCB structure.
//
// * %rdi/x10/CPR must contain the address of the C function to call.
//
// * %rax/x9/AAR must contain a fixnum representing the encoded number of
//   arguments to the C function, EXcluding the last argument
//   (the "ikpcb_t * pcb"), such arguments are on the Scheme stack.
//
// NOTE When we enter this routine to call the C function
// "ik_stack_overflow()", we enter with a Scheme stack segment
// and come back with another Scheme stack segment.
//
// Note commentary in "ser/ikarus-runtime.c" proc "ik_stack_overflow()"
// "ik_foreign_call" label set in "scheme/ikarus.compiler.pass-flatten-codes.sls"
// Doc in Chapter 16 in "vicare-scheme.pdf"	
	
	
.align 8
ik_foreign_call:
_ik_foreign_call:
  // Store the Frame Pointer Register into "pcb->frame_pointer".
  str x12, [x13, #16]    // (movl fpr (pcb-ref 'frame-pointer))
  // Store the Allocation Pointer Register into "pcb->allocation_pointer".
  str x11, [x13]         // (movl apr (pcb-ref 'allocation-pointer))
  // Save the Frame Pointer Register (FPR, %esp) into RBX.  We will
  // need it later to transfer the arguments from the Scheme stack to
  // the C stack.
  mov x15, x12           // (movl fpr ebx)
  // Store in %rsp the C stack from "pcb->system_stack".
  // NB: Should be same number!! [assert]
  ldr SP, [x13, #48]     // (movl (pcb-ref 'system-stack) esp)

  // Here SP references the last used word on the C stack: the
  // "old top of C stack".

  // CalleR save regs are D0..D7, X0..X15 -- save on C stack
  stp  d6,  d7, [SP, #-16]! // push register pairs in reversed order
  stp  d4,  d5, [SP, #-16]!	
  stp  d2,  d3, [SP, #-16]!
  stp  d0,  d1, [SP, #-16]!		
  stp x14, x15, [SP, #-16]!
  stp x12, x13, [SP, #-16]! // x12=FPR, x13=PCR
  stp x10, x11, [SP, #-16]! // x10=CPR, x11=APR
  stp  x8,  x9, [SP, #-16]! // x9=AAR
  stp  x6,  x7, [SP, #-16]!
  stp  x4,  x5, [SP, #-16]!	
  stp  x2,  x3, [SP, #-16]!
  stp  x0,  x1, [SP, #-16]! // SP now -= 192		

  // If there are no arguments: jump to the appropriate label, the only
  // argument is the PCB addres in %rdi/CPR/x10
  cmpi x9, #0
  B.eq L_zero_args

  // There is one argument: jump to the appropriate label.
  cmpi x9, #-8
  B.eq L_one_arg

  // There are two arguments: jump to the appropriate label.
  cmpi x9, #-16
  B.eq L_two_args

  // There are three arguments: jump to the appropriate label.
  cmpi x9, #-24
  B.eq L_three_args

  // There are four arguments: jump to the appropriate label.
  cmpi x9, #-32
  B.eq L_four_args

  // There are five arguments: jump to the appropriate label.
  cmpi x9, #-40
  B.eq L_five_args

  // There are six arguments: jump to the appropriate label.
  cmpi x9, #-48
  B.eq L_six_args

  // There are seven arguments: jump to the appropriate label.
  cmpi x9, #-56
  B.eq L_seven_args

  // There are eight arguments: jump to the appropriate label.
  cmpi x9, #-64
  B.eq L_eight_args

;;; 	@@@@ intel->arm transition marker @@@@
	@@@@ Big assumption here is integer args @@@@ ??no flonums ?? @@@@

  // There are more than eight arguments: we push the 9th, 10th, ...
  // arguments on the C stack, and we put the 1st, 2nd, ..., 8th in
  // CPU registers.  Let's consider the example of a function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikptr_t arg2, ikptr_t arg3, ikptr_t arg4,
  //              ikptr_t arg5, ikptr_t arg6, ikptr_t arg7, ikptr_t arg8,
  //              ikpcb_t * pcb);
  //
  // When arriving here: FPR references the top of the Scheme stack;
  // AAR = -8 * wordsize = -64, the offset to add to FPR to
  // reference the topmost argument.  On the Scheme stack we have:
  //
  //         high memory
  //   |                      |
  //   |----------------------|
  //   |    return address    | <-- FPR
  //   |----------------------|
  //   |     C argument 1     | <-- FPR - 1 * wordsize = FPR -  8
  //   |----------------------|
  //   |     C argument 2     | <-- FPR - 2 * wordsize = FPR - 16
  //   |----------------------|
  //   |     C argument 3     | <-- FPR - 3 * wordsize = FPR - 24
  //   |----------------------|
  //   |     C argument 4     | <-- FPR - 4 * wordsize = FPR - 32
  //   |----------------------|
  //   |     C argument 5     | <-- FPR - 5 * wordsize = FPR - 40
  //   |----------------------|
  //   |     C argument 6     | <-- FPR - 6 * wordsize = FPR - 48
  //   |----------------------|
  //   |     C argument 7     | <-- FPR - 7 * wordsize = FPR - 56
  //   |----------------------|
  //   |     C argument 8     | <-- FPR - 8 * wordsize = FPR + AAR
  //   |----------------------|
  //   |                      |
  //          low memory
  //
  // On the C stack, right before performing the call instruction,
  // we want:
  //
  //         high memory
  //   |                      |
  //   |----------------------|
  //   |  old top of C stack  |
  //   |----------------------|
  //   |    pointer to PCB    | <-- SP + 2 * wordsize
  //   |----------------------|
  //   |     C argument 10    | <-- SP + 1 * wordsize
  //   |----------------------|
  //   |     C argument  9    | <-- SP
  //   |----------------------|
  //   |                      |
  //          low memory
  //
  // and in the CPU registers we want:
  //
  //   x0 == arg1, x1 == arg2, x2 == arg3, ...
  //
  // To keep %rsp with 16-bytes alignment we do this reasoning:
  //
  // 1. We know that now SP is 16-bytes aligned (hardware traps if not).
  //
  // 2. We know that we will push the PCB address on the stack, and it
  //    accounts for 8 bytes.
  //
  // 3. Every argument we will push on the stack accounts for 8 bytes.
  //
  // 4. We store 6 (even) argument in the CPU registers.
  //
  // 5. If we push an even number of machine words on the stack: the
  //    alignment is preserved.
  //
  // so:
  //
  // * If the number of arguments is odd: an odd number of arguments
  //   will be pushed on the stack; plus the PCB address: an even number
  //   of machine words will be pushed on the stack; we *do not* need to
  //   push additional words to keep the alignment.
  //
  // * If the number of arguments is even: an even number of arguments
  //   will be pushed on the stack; plus the PCB address: an odd number
  //   of machine words will be pushed on the stack; we *do* need to push
  //   an additional word on the stack to keep the alignment.
  //
  // If a machine word represents an odd number: its least significant
  // bit is set.  On 64-bit platforms: fixnums are encoded by left-shifting
  // the raw integer by 3 bits.  So a machine word represents an even
  // fixnum if its least significant bits are //b0000, and it represents
  // an odd fixnum if its least significant bits are //b1000.
  //
  // To isolate the least significant nibble in a machine word: we AND
  // it with #b1111 = 15.  If the result of such AND is not zero: we
  // *do not* need to push an additional word on the stack.
  //
  // (Here we use %rdx as a scratch register.)
  movq %rax, %rdx
  andq $15, %rdx
  cmpq $0, %rdx		// is this needed at all???
  jnz L_no_pad
  push $0
L_no_pad:

  // Push the PCB address on the C stack: it is the rightmost argument
  // to the C function.
  push %rdi		 // pcb
  // Copy the arguments from the Scheme stack to the C stack.
L_loop:                  // (label Lloop)
  movq (%rbx,%rax), %rcx // (movl (mem rbx rax) rcx)
  push %rcx              // (pushl rcx)
  addq $8, %rax          // (addl (int 8) rax)
  cmpq $-48, %rax        // (cmpl (int -48) rax)
  jne L_loop             // (jne (label Lloop))
  movq  -8(%rbx), %rdi		// arg1
  movq -16(%rbx), %rsi		// arg2
  movq -24(%rbx), %rdx		// arg3
  movq -32(%rbx), %rcx		// arg4
  movq -40(%rbx), %r8		// arg5
  movq -48(%rbx), %r9		// arg6
  jmp L_set

L_six_args:
  // There are six arguments.  A function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikptr_t arg2, ikptr_t arg3,
  //              ikptr_t arg4, ikptr_t arg5, ikptr_t arg6,
  //              ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == arg1, %rsi == arg2, %rdx == arg3,
  //   %rcx == arg4, %r8  == arg5, %r9  == arg6
  //
  // and pushing the PCB address on the stack.  We first push a word
  // on the stack to preserve 16-byte alignment of the value of %rsp.
  //
  push $0
  push %rdi			// pcb
  movq  -8(%rbx), %rdi		// arg1
  movq -16(%rbx), %rsi		// arg2
  movq -24(%rbx), %rdx		// arg3
  movq -32(%rbx), %rcx		// arg4
  movq -40(%rbx), %r8		// arg5
  movq -48(%rbx), %r9		// arg6
  jmp L_set

L_five_args:
  // There are five arguments.  A function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikptr_t arg2, ikptr_t arg3,
  //              ikptr_t arg4, ikptr_t arg5,
  //              ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == arg1, %rsi == arg2, %rdx == arg3,
  //   %rcx == arg4, %r8  == arg5, %r9  == pcb
  //
  movq %rdi, %r9		// pcb
  movq  -8(%rbx), %rdi		// arg1
  movq -16(%rbx), %rsi		// arg2
  movq -24(%rbx), %rdx		// arg3
  movq -32(%rbx), %rcx		// arg4
  movq -40(%rbx), %r8		// arg5
  jmp L_set

L_four_args:
  // There are four arguments.  A function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikptr_t arg2, ikptr_t arg3,
  //              ikptr_t arg4,
  //              ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == arg1, %rsi == arg2, %rdx == arg3,
  //   %rcx == arg4, %r8  == pcb
  //
  movq %rdi, %r8		// pcb
  movq  -8(%rbx), %rdi		// arg1
  movq -16(%rbx), %rsi		// arg2
  movq -24(%rbx), %rdx		// arg3
  movq -32(%rbx), %rcx		// arg4
  jmp L_set

L_three_args:
  // There are three arguments.  A function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikptr_t arg2, ikptr_t arg3,
  //              ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == arg1, %rsi == arg2, %rdx == arg3,
  //   %rcx == pcb
  //
  movq %rdi, %rcx		// pcb
  movq  -8(%rbx), %rdi		// arg1
  movq -16(%rbx), %rsi		// arg2
  movq -24(%rbx), %rdx		// arg3
  jmp L_set

L_two_args:
  // There are two arguments.  A function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikptr_t arg2,
  //              ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == arg1, %rsi == arg2, %rdx == pcb
  //
  movq %rdi, %rdx		// pcb
  movq  -8(%rbx), %rdi		// arg1
  movq -16(%rbx), %rsi		// arg2
  jmp L_set

L_one_arg:
  // There is one argument.  A function call to:
  //
  //   ikptr_t fun (ikptr_t arg1, ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == arg1, %rsi == pcb
  //
  movq %rdi, %rsi		// pcb
  movq -8(%rbx), %rdi		// arg1
  jmp L_set

L_zero_args:
  // There are zero arguments.  A function call to:
  //
  //   ikptr_t fun (ikpcb_t * pcb);
  //
  // is performed by setting:
  //
  //   %rdi == pcb
  //
  // and we already did it before.

L_set:                   // (label Lset)
  call *%r12             // (call cpr)
L_back:
// CalleR save regs are D0..D7, X0..X15 -- restore from C stack
  ldp  x0,  x1, [SP], #16 // pop in (reverse (reversed order)) = in-order
  ldp  x2,  x3, [SP], #16
  ldp  x4,  x5, [SP], #16	
  ldp  x6,  x7, [SP], #16
  ldp  x8,  x9, [SP], #16 //  x9=AAR
  ldp x10, x11, [SP], #16 // x10=CPR, x11=APR
  ldp x12, x13, [SP], #16 // x12=FPR, x13=PCR
  ldp x14, x15, [SP], #16
  ldp  d0,  d1, [SP], #16		
  ldp  d2,  d3, [SP], #16
  ldp  d4,  d5, [SP], #16	
  ldp  d6,  d7, [SP], #16 // SP now += 192
	
  // Restore PCB from %r13.
  movq %r13, %rsi
  // Retrieve the Frame Pointer Register from "pcb->frame_pointer".
  movq 16(%rsi), %rsp    // (movl (pcb-ref 'frame-pointer) fpr)
  // Retrieve the Allocation Pointer Register from "pcb->allocation_pointer".
  movq 0(%rsi), %rbp	 // (movl (pcb-ref 'allocation-pointer) apr)
  // Return to Scheme.
  ret
	
	

// end of file
// Local Variables:
//comment-start: "//"
//End:
