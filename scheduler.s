	.file	"scheduler.c"
# GNU C11 (GCC) version 10.3.0 (x86_64-elf)
#	compiled by GNU C version 14.2.0, GMP version 6.1.0, MPFR version 3.1.4, MPC version 1.0.3, isl version isl-0.18-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -fdiagnostics-color=always -fpreprocessed scheduler.i
# -m64 -mcmodel=large -mno-red-zone -mtune=generic -march=x86-64
# -auxbase-strip build/scheduler.o -O2 -Wall -Wextra -Werror
# -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wcast-align
# -Wno-unused-function -std=gnu11 -ffreestanding -fdiagnostics-show-option
# -fdebug-prefix-map=/home/kali/Desktop/Operating System=C:/Users/matanel/Desktop/Projects/KernelDevelopment
# -fno-pic -fno-omit-frame-pointer -fno-optimize-sibling-calls
# -fopt-info-optimized=sched-opt.log -fverbose-asm
# options enabled:  -faggressive-loop-optimizations -falign-functions
# -falign-jumps -falign-labels -falign-loops -fallocation-dce
# -fasynchronous-unwind-tables -fauto-inc-dec -fbranch-count-reg
# -fcaller-saves -fcode-hoisting -fcombine-stack-adjustments -fcompare-elim
# -fcprop-registers -fcrossjumping -fcse-follow-jumps -fdefer-pop
# -fdelete-null-pointer-checks -fdevirtualize -fdevirtualize-speculatively
# -fearly-inlining -feliminate-unused-debug-symbols
# -feliminate-unused-debug-types -fexpensive-optimizations
# -fforward-propagate -ffp-int-builtin-inexact -ffunction-cse -fgcse
# -fgcse-lm -fgnu-unique -fguess-branch-probability -fhoist-adjacent-loads
# -fident -fif-conversion -fif-conversion2 -findirect-inlining -finline
# -finline-atomics -finline-functions -finline-functions-called-once
# -finline-small-functions -fipa-bit-cp -fipa-cp -fipa-icf
# -fipa-icf-functions -fipa-icf-variables -fipa-profile -fipa-pure-const
# -fipa-ra -fipa-reference -fipa-reference-addressable -fipa-sra
# -fipa-stack-alignment -fipa-vrp -fira-hoist-pressure
# -fira-share-save-slots -fira-share-spill-slots
# -fisolate-erroneous-paths-dereference -fivopts -fkeep-static-consts
# -fleading-underscore -flifetime-dse -flra-remat -fmath-errno
# -fmerge-constants -fmerge-debug-strings -fmove-loop-invariants
# -foptimize-strlen -fpartial-inlining -fpeephole -fpeephole2 -fplt
# -fprefetch-loop-arrays -free -freg-struct-return -freorder-blocks
# -freorder-blocks-and-partition -freorder-functions -frerun-cse-after-loop
# -fsched-critical-path-heuristic -fsched-dep-count-heuristic
# -fsched-group-heuristic -fsched-interblock -fsched-last-insn-heuristic
# -fsched-rank-heuristic -fsched-spec -fsched-spec-insn-heuristic
# -fsched-stalled-insns-dep -fschedule-fusion -fschedule-insns2
# -fsemantic-interposition -fshow-column -fshrink-wrap
# -fshrink-wrap-separate -fsigned-zeros -fsplit-ivs-in-unroller
# -fsplit-wide-types -fssa-backprop -fssa-phiopt -fstdarg-opt
# -fstore-merging -fstrict-aliasing -fstrict-volatile-bitfields
# -fsync-libcalls -fthread-jumps -ftoplevel-reorder -ftrapping-math
# -ftree-bit-ccp -ftree-builtin-call-dce -ftree-ccp -ftree-ch
# -ftree-coalesce-vars -ftree-copy-prop -ftree-cselim -ftree-dce
# -ftree-dominator-opts -ftree-dse -ftree-forwprop -ftree-fre
# -ftree-loop-if-convert -ftree-loop-im -ftree-loop-ivcanon
# -ftree-loop-optimize -ftree-parallelize-loops= -ftree-phiprop -ftree-pre
# -ftree-pta -ftree-reassoc -ftree-scev-cprop -ftree-sink -ftree-slsr
# -ftree-sra -ftree-switch-conversion -ftree-tail-merge -ftree-ter
# -ftree-vrp -funit-at-a-time -funwind-tables -fverbose-asm
# -fzero-initialized-in-bss -m128bit-long-double -m64 -m80387
# -malign-stringops -mavx256-split-unaligned-load
# -mavx256-split-unaligned-store -mfancy-math-387 -mfp-ret-in-387 -mfxsr
# -mieee-fp -mlong-double-80 -mmmx -mno-red-zone -mno-sse4 -mpush-args
# -msse -msse2 -mstv -mvzeroupper

	.text
	.section	.rodata
.LC0:
	.string	"InitScheduler"
	.text
	.align 16
	.globl	InitScheduler
	.type	InitScheduler, @function
InitScheduler:
.LFB23:
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	movabsq	$isBugChecking, %rax	#, tmp99
# kernel/cpu/scheduler/scheduler.c:33: void InitScheduler(void) {
	pushq	%rbp	#
.LCFI0:
	movq	%rsp, %rbp	#,
.LCFI1:
	subq	$144, %rsp	#,
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	cmpb	$0, (%rax)	#, isBugChecking
	jne	.L4	#,
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsl	lastfunc_history+1280, %eax	# lastfunc_history.current_index, lastfunc_history.current_index
	leal	1(%rax), %edx	#, tmp122
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%edx, %rax	# tmp122, tmp124
	movl	%edx, %ecx	# tmp122, tmp128
	imulq	$1717986919, %rax, %rax	#, tmp124, tmp125
	sarl	$31, %ecx	#, tmp128
	sarq	$34, %rax	#, tmp127
	subl	%ecx, %eax	# tmp128, _20
	leal	(%rax,%rax,4), %ecx	#, tmp131
	movl	%edx, %eax	# tmp122, tmp122
	addl	%ecx, %ecx	# tmp132
	subl	%ecx, %eax	# tmp132, tmp122
	movabsq	$lastfunc_history, %rcx	#, tmp121
# kernel/cpu/scheduler/../irql/../../trace.h:27:     lastfunc_history.current_index =
	movabsl	%eax, lastfunc_history+1280	# _20, lastfunc_history.current_index
	movslq	%eax, %rdx	# _20, _20
	movq	%rdx, %rsi	# _20, _20
	movabsq	$lastfunc_history+128, %rdx	#, tmp136
	salq	$7, %rsi	#, _20
	addq	%rsi, %rcx	# _44, _45
	addq	%rsi, %rdx	# _44, _51
	movq	%rcx, %rax	# _45, ivtmp.29
	.align 16
.L3:
# kernel/cpu/scheduler/../irql/../../trace.h:32:         lastfunc_history.names[lastfunc_history.current_index][j] = 0;
	movb	$0, (%rax)	#, MEM[base: _46, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:31:     for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
	addq	$1, %rax	#, ivtmp.29
	cmpq	%rdx, %rax	# _51, ivtmp.29
	jne	.L3	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	xorl	%eax, %eax	# i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movl	$73, %edx	#, _23
	movabsq	$.LC0, %rsi	#, tmp140
	.align 16
.L5:
# kernel/cpu/scheduler/../irql/../../trace.h:37:         lastfunc_history.names[lastfunc_history.current_index][i] =
	movb	%dl, (%rcx,%rax)	# _23, MEM[base: _45, index: i_25, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	addq	$1, %rax	#, i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	cmpq	$127, %rax	#, i
	je	.L4	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movzbl	(%rsi,%rax), %edx	# MEM[symbol: "InitScheduler", index: i_26, offset: 0B], _23
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	testb	%dl, %dl	# _23
	jne	.L5	#,
.L4:
# kernel/cpu/scheduler/scheduler.c:41:     kmemset(&cfm, 0, sizeof(cfm)); // Start with a clean, all-zero context
	leaq	-144(%rbp), %rdi	#, tmp142
	movl	$136, %edx	#,
	xorl	%esi, %esi	#
# kernel/cpu/scheduler/scheduler.c:35:     cpu.schedulerEnabled = true;
	movabsq	$cpu+4, %rax	#, tmp141
	movb	$1, (%rax)	#, cpu.schedulerEnabled
# kernel/cpu/scheduler/scheduler.c:41:     kmemset(&cfm, 0, sizeof(cfm)); // Start with a clean, all-zero context
	movabsq	$kmemset, %rax	#, tmp102
	call	*%rax	# tmp102
# kernel/cpu/scheduler/scheduler.c:48:     idleThread.registers = cfm;
	movdqu	-144(%rbp), %xmm0	# cfm, tmp144
	movdqu	-128(%rbp), %xmm1	# cfm, tmp145
# kernel/cpu/scheduler/scheduler.c:44:     cfm.rsp = (uint64_t)(idleStack + IDLE_STACK_SIZE);
	movabsq	$idleStack+4096, %rax	#, tmp143
# kernel/cpu/scheduler/scheduler.c:48:     idleThread.registers = cfm;
	movdqu	-112(%rbp), %xmm2	# cfm, tmp146
	movdqu	-96(%rbp), %xmm3	# cfm, tmp147
# kernel/cpu/scheduler/scheduler.c:44:     cfm.rsp = (uint64_t)(idleStack + IDLE_STACK_SIZE);
	movq	%rax, -24(%rbp)	# tmp143, cfm.rsp
# kernel/cpu/scheduler/scheduler.c:48:     idleThread.registers = cfm;
	movabsq	$idleThread, %rdi	#, tmp105
	movdqu	-80(%rbp), %xmm4	# cfm, tmp148
	movdqu	-64(%rbp), %xmm5	# cfm, tmp149
	movaps	%xmm0, (%rdi)	# tmp144, idleThread.registers
	movabsq	$kernel_idle_checks, %rax	#, tmp152
	movdqu	-48(%rbp), %xmm6	# cfm, tmp150
	movdqu	-32(%rbp), %xmm7	# cfm, tmp151
	movabsq	%rax, idleThread+128	# tmp152, idleThread.registers
# kernel/cpu/scheduler/scheduler.c:53:     cpu.currentThread = &idleThread;
	movq	%rdi, %rax	# tmp105, tmp105
	movabsq	%rax, cpu+8	# tmp105, cpu.currentThread
# kernel/cpu/scheduler/scheduler.c:56:     cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
	movabsq	$cpu+24, %rax	#, tmp155
# kernel/cpu/scheduler/scheduler.c:49:     idleThread.threadState = RUNNING;
	movl	$0, 136(%rdi)	#, idleThread.threadState
# kernel/cpu/scheduler/scheduler.c:50:     idleThread.nextThread = NULL;
	movq	$0, 144(%rdi)	#, idleThread.nextThread
# kernel/cpu/scheduler/scheduler.c:56:     cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
	movq	$0, (%rax)	#, cpu.readyQueue.tail
# kernel/cpu/scheduler/scheduler.c:56:     cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
	movq	$0, -8(%rax)	#, cpu.readyQueue.head
# kernel/cpu/scheduler/scheduler.c:48:     idleThread.registers = cfm;
	movaps	%xmm1, 16(%rdi)	# tmp145, idleThread.registers
	movaps	%xmm2, 32(%rdi)	# tmp146, idleThread.registers
	movaps	%xmm3, 48(%rdi)	# tmp147, idleThread.registers
	movaps	%xmm4, 64(%rdi)	# tmp148, idleThread.registers
	movaps	%xmm5, 80(%rdi)	# tmp149, idleThread.registers
	movaps	%xmm6, 96(%rdi)	# tmp150, idleThread.registers
	movaps	%xmm7, 112(%rdi)	# tmp151, idleThread.registers
# kernel/cpu/scheduler/scheduler.c:57: }
	leave	
.LCFI2:
	ret	
.LFE23:
	.size	InitScheduler, .-InitScheduler
	.section	.rodata
.LC1:
	.string	"Schedule"
.LC2:
	.string	"enqueue_runnable"
.LC3:
	.string	"enqueue"
.LC4:
	.string	"dequeue"
	.text
	.align 16
	.globl	Schedule
	.type	Schedule, @function
Schedule:
.LFB25:
	pushq	%rbp	#
.LCFI3:
	movq	%rsp, %rbp	#,
.LCFI4:
	pushq	%r13	#
	pushq	%r12	#
	pushq	%rbx	#
.LCFI5:
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	movabsq	$isBugChecking, %rbx	#, tmp242
# kernel/cpu/scheduler/scheduler.c:68: void Schedule(void) {
	subq	$24, %rsp	#,
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	cmpb	$0, (%rbx)	#, isBugChecking
	jne	.L19	#,
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsl	lastfunc_history+1280, %eax	# lastfunc_history.current_index, lastfunc_history.current_index
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsq	$lastfunc_history, %rcx	#, tmp241
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	leal	1(%rax), %edx	#, tmp150
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%edx, %rax	# tmp150, tmp152
	movl	%edx, %esi	# tmp150, tmp156
	imulq	$1717986919, %rax, %rax	#, tmp152, tmp153
	sarl	$31, %esi	#, tmp156
	sarq	$34, %rax	#, tmp155
	subl	%esi, %eax	# tmp156, _26
	leal	(%rax,%rax,4), %esi	#, tmp159
	movl	%edx, %eax	# tmp150, tmp150
	addl	%esi, %esi	# tmp160
	subl	%esi, %eax	# tmp160, tmp150
	movabsq	$lastfunc_history+128, %rsi	#, tmp245
# kernel/cpu/scheduler/../irql/../../trace.h:27:     lastfunc_history.current_index =
	movabsl	%eax, lastfunc_history+1280	# _26, lastfunc_history.current_index
	movslq	%eax, %rdx	# _26, _26
	salq	$7, %rdx	#, _180
	addq	%rdx, %rcx	# _180, _181
	addq	%rsi, %rdx	# tmp245, _187
	movq	%rcx, %rax	# _181, ivtmp.96
	.align 16
.L18:
# kernel/cpu/scheduler/../irql/../../trace.h:32:         lastfunc_history.names[lastfunc_history.current_index][j] = 0;
	movb	$0, (%rax)	#, MEM[base: _182, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:31:     for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
	addq	$1, %rax	#, ivtmp.96
	cmpq	%rdx, %rax	# _187, ivtmp.96
	jne	.L18	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	xorl	%eax, %eax	# i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movl	$83, %edx	#, _29
	movabsq	$.LC1, %rsi	#, tmp251
	.align 16
.L20:
# kernel/cpu/scheduler/../irql/../../trace.h:37:         lastfunc_history.names[lastfunc_history.current_index][i] =
	movb	%dl, (%rcx,%rax)	# _29, MEM[base: _181, index: i_135, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	addq	$1, %rax	#, i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	cmpq	$127, %rax	#, i
	je	.L19	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movzbl	(%rsi,%rax), %edx	# MEM[symbol: "Schedule", index: i_32, offset: 0B], _29
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	testb	%dl, %dl	# _29
	jne	.L20	#,
.L19:
# kernel/cpu/scheduler/scheduler.c:70:     if (isScheduleDpcQueued) {
	movabsq	$isScheduleDpcQueued, %rax	#, tmp148
# kernel/cpu/scheduler/scheduler.c:70:     if (isScheduleDpcQueued) {
	cmpb	$0, (%rax)	#, isScheduleDpcQueued
	je	.L17	#,
# kernel/cpu/scheduler/scheduler.c:71:         isScheduleDpcQueued = false;
	movabsq	$isScheduleDpcQueued, %rax	#, tmp256
	movb	$0, (%rax)	#, isScheduleDpcQueued
.L17:
# kernel/cpu/scheduler/scheduler.c:74:     MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);
	movabsq	$MtRaiseIRQL, %rax	#, tmp170
	leaq	-36(%rbp), %rsi	#, tmp257
	movl	$2, %edi	#,
	call	*%rax	# tmp170
# kernel/cpu/scheduler/scheduler.c:76:     Thread* prev = cpu.currentThread;
	movabsq	$cpu+8, %rax	#, tmp258
	movq	(%rax), %r13	# cpu.currentThread, prev
# kernel/cpu/scheduler/scheduler.c:77:     if (prev != &idleThread) {
	movabsq	$idleThread, %rax	#, next
	cmpq	%rax, %r13	# next, prev
	je	.L63	#,
# kernel/cpu/scheduler/scheduler.c:78:         save_context(&prev->registers);
	movabsq	$save_context, %rax	#, tmp175
	movq	%r13, %rdi	# prev,
	call	*%rax	# tmp175
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	movzbl	(%rbx), %edi	# isBugChecking, isBugChecking.1_34
	testb	%dil, %dil	# isBugChecking.1_34
	jne	.L24	#,
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsl	lastfunc_history+1280, %eax	# lastfunc_history.current_index, lastfunc_history.current_index
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsq	$lastfunc_history, %rcx	#, tmp241
	leaq	128(%rcx), %rsi	#, tmp245
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	addl	$1, %eax	#, tmp178
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%eax, %r8	# tmp178, tmp180
	cltd
	imulq	$1717986919, %r8, %r8	#, tmp180, tmp181
	sarq	$34, %r8	#, tmp183
	subl	%edx, %r8d	# tmp184, prephitmp_133
	leal	(%r8,%r8,4), %edx	#, tmp187
	addl	%edx, %edx	# tmp188
	subl	%edx, %eax	# tmp188, tmp178
# kernel/cpu/scheduler/../irql/../../trace.h:27:     lastfunc_history.current_index =
	movabsl	%eax, lastfunc_history+1280	# prephitmp_133, lastfunc_history.current_index
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%eax, %rdx	# tmp178,
	movq	%rdx, %r8	#,
	salq	$7, %rdx	#, _165
	leaq	(%rcx,%rdx), %r9	#, _166
	addq	%rsi, %rdx	# tmp245, _172
	movq	%r9, %rax	# _166, ivtmp.80
	.align 16
.L25:
# kernel/cpu/scheduler/../irql/../../trace.h:32:         lastfunc_history.names[lastfunc_history.current_index][j] = 0;
	movb	$0, (%rax)	#, MEM[base: _167, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:31:     for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
	addq	$1, %rax	#, ivtmp.80
	cmpq	%rdx, %rax	# _172, ivtmp.80
	jne	.L25	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	xorl	%eax, %eax	# i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movl	$101, %edx	#, _41
	movabsq	$.LC2, %r10	#, tmp250
	.align 16
.L27:
# kernel/cpu/scheduler/../irql/../../trace.h:37:         lastfunc_history.names[lastfunc_history.current_index][i] =
	movb	%dl, (%r9,%rax)	# _41, MEM[base: _166, index: i_134, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	addq	$1, %rax	#, i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	cmpq	$127, %rax	#, i
	je	.L26	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movzbl	(%r10,%rax), %edx	# MEM[symbol: "enqueue_runnable", index: i_44, offset: 0B], _41
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	testb	%dl, %dl	# _41
	jne	.L27	#,
.L26:
# kernel/cpu/scheduler/scheduler.c:62:     if (t->threadState == RUNNING) {
	movl	136(%r13), %edx	# prev_11->threadState,
	testl	%edx, %edx	#
	je	.L37	#,
# kernel/cpu/scheduler/../cpu.h:113: 	if (!queue->head) queue->head = thread;
	movabsq	$cpu+16, %rax	#, tmp272
	movq	(%rax), %r12	# MEM[(struct Queue *)&cpu + 16B].head, next
.L38:
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	addl	$1, %r8d	#, tmp203
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%r8d, %rax	# tmp203, tmp204
	movl	%r8d, %edx	# tmp203, tmp208
	imulq	$1717986919, %rax, %rax	#, tmp204, tmp205
	sarl	$31, %edx	#, tmp208
	sarq	$34, %rax	#, tmp207
	subl	%edx, %eax	# tmp208, _65
	leal	(%rax,%rax,4), %edx	#, tmp211
	movl	%r8d, %eax	# tmp203, tmp203
	addl	%edx, %edx	# tmp212
	subl	%edx, %eax	# tmp212, tmp203
# kernel/cpu/scheduler/../irql/../../trace.h:27:     lastfunc_history.current_index =
	movabsl	%eax, lastfunc_history+1280	# _65, lastfunc_history.current_index
	movslq	%eax, %rdx	# _65, _65
	salq	$7, %rdx	#, _60
	addq	%rdx, %rcx	# _60, _147
	addq	%rsi, %rdx	# tmp245, _35
	movq	%rcx, %rax	# _147, ivtmp.48
	.align 16
.L34:
# kernel/cpu/scheduler/../irql/../../trace.h:32:         lastfunc_history.names[lastfunc_history.current_index][j] = 0;
	movb	$0, (%rax)	#, MEM[base: _146, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:31:     for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
	addq	$1, %rax	#, ivtmp.48
	cmpq	%rax, %rdx	# ivtmp.48, _35
	jne	.L34	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	xorl	%eax, %eax	# i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movl	$100, %edx	#, _68
	movabsq	$.LC4, %rsi	#, tmp248
	.align 16
.L35:
# kernel/cpu/scheduler/../irql/../../trace.h:37:         lastfunc_history.names[lastfunc_history.current_index][i] =
	movb	%dl, (%rcx,%rax)	# _68, MEM[base: _147, index: i_56, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	addq	$1, %rax	#, i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	cmpq	$127, %rax	#, i
	je	.L22	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movzbl	(%rsi,%rax), %edx	# MEM[symbol: "dequeue", index: i_71, offset: 0B], _68
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	testb	%dl, %dl	# _68
	jne	.L35	#,
.L22:
# kernel/cpu/scheduler/../cpu.h:121: 	if (!thread) return NULL;
	testq	%r12, %r12	# next
	je	.L40	#,
.L33:
# kernel/cpu/scheduler/../cpu.h:123: 	queue->head = thread->nextThread;
	movq	144(%r12), %rax	# prephitmp_149->nextThread, _61
# kernel/cpu/scheduler/../cpu.h:123: 	queue->head = thread->nextThread;
	movabsq	%rax, cpu+16	# _61, MEM[(struct Queue *)&cpu + 16B].head
# kernel/cpu/scheduler/../cpu.h:124: 	if (!queue->head) queue->tail = NULL;
	testq	%rax, %rax	# _61
	je	.L64	#,
.L36:
# kernel/cpu/scheduler/scheduler.c:89:     cpu.currentThread = next;
	movq	%r12, %rax	# next, next
# kernel/cpu/scheduler/scheduler.c:90:     MtLowerIRQL(oldIrql);
	movl	-36(%rbp), %edi	# oldIrql,
# kernel/cpu/scheduler/scheduler.c:88:     next->threadState = RUNNING;
	movl	$0, 136(%r12)	#, next_5->threadState
# kernel/cpu/scheduler/scheduler.c:89:     cpu.currentThread = next;
	movabsq	%rax, cpu+8	# next, cpu.currentThread
# kernel/cpu/scheduler/scheduler.c:90:     MtLowerIRQL(oldIrql);
	movabsq	$MtLowerIRQL, %rax	#, tmp224
	call	*%rax	# tmp224
# kernel/cpu/scheduler/scheduler.c:91:     restore_context(&next->registers);
	movq	%r12, %rdi	# next,
	movabsq	$restore_context, %rax	#, tmp225
	call	*%rax	# tmp225
# kernel/cpu/scheduler/scheduler.c:92: }
	addq	$24, %rsp	#,
	popq	%rbx	#
.LCFI6:
	popq	%r12	#
.LCFI7:
	popq	%r13	#
.LCFI8:
	popq	%rbp	#
.LCFI9:
	ret	
.L63:
.LCFI10:
# kernel/cpu/scheduler/../cpu.h:120: 	Thread* thread = queue->head;
	movabsq	$cpu+16, %rax	#, tmp259
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	cmpb	$0, (%rbx)	#, isBugChecking
# kernel/cpu/scheduler/../cpu.h:120: 	Thread* thread = queue->head;
	movq	(%rax), %r12	# MEM[(struct Queue *)&cpu + 16B].head, next
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	jne	.L22	#,
.L23:
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsq	$lastfunc_history+1280, %rax	#, tmp265
	movl	(%rax), %r8d	# lastfunc_history.current_index, prephitmp_133
	leaq	-1280(%rax), %rcx	#, tmp241
	leaq	-1152(%rax), %rsi	#, tmp245
	jmp	.L38	#
.L64:
# kernel/cpu/scheduler/../cpu.h:124: 	if (!queue->head) queue->tail = NULL;
	movabsq	$cpu+24, %rax	#, tmp268
	movq	$0, (%rax)	#, MEM[(struct Queue *)&cpu + 16B].tail
	jmp	.L36	#
.L24:
# kernel/cpu/scheduler/scheduler.c:62:     if (t->threadState == RUNNING) {
	movl	136(%r13), %eax	# prev_11->threadState,
	testl	%eax, %eax	#
	je	.L39	#,
# kernel/cpu/scheduler/../cpu.h:113: 	if (!queue->head) queue->head = thread;
	movabsq	$cpu+16, %rax	#, tmp273
	movq	(%rax), %r12	# MEM[(struct Queue *)&cpu + 16B].head, next
	jmp	.L22	#
.L37:
# kernel/cpu/scheduler/scheduler.c:63:         t->threadState = READY;
	movl	$1, 136(%r13)	#, prev_11->threadState
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	addl	$1, %r8d	#, tmp226
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%r8d, %rax	# tmp226, tmp227
	movl	%r8d, %edx	# tmp226, tmp231
	imulq	$1717986919, %rax, %rax	#, tmp227, tmp228
	sarl	$31, %edx	#, tmp231
	sarq	$34, %rax	#, tmp230
	subl	%edx, %eax	# tmp231, _74
	leal	(%rax,%rax,4), %edx	#, tmp234
	movl	%r8d, %eax	# tmp226, tmp226
	addl	%edx, %edx	# tmp235
	subl	%edx, %eax	# tmp235, tmp226
# kernel/cpu/scheduler/../irql/../../trace.h:27:     lastfunc_history.current_index =
	movabsl	%eax, lastfunc_history+1280	# _74, lastfunc_history.current_index
	movslq	%eax, %rdx	# _74, _74
	salq	$7, %rdx	#, _117
	addq	%rdx, %rcx	# _117, _116
	addq	%rsi, %rdx	# tmp245, _110
	movq	%rcx, %rax	# _116, ivtmp.64
	.align 16
.L28:
# kernel/cpu/scheduler/../irql/../../trace.h:32:         lastfunc_history.names[lastfunc_history.current_index][j] = 0;
	movb	$0, (%rax)	#, MEM[base: _115, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:31:     for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
	addq	$1, %rax	#, ivtmp.64
	cmpq	%rax, %rdx	# ivtmp.64, _110
	jne	.L28	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	xorl	%eax, %eax	# i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movl	$101, %edx	#, _54
	movabsq	$.LC3, %rsi	#, tmp249
	.align 16
.L30:
# kernel/cpu/scheduler/../irql/../../trace.h:37:         lastfunc_history.names[lastfunc_history.current_index][i] =
	movb	%dl, (%rcx,%rax)	# _54, MEM[base: _116, index: i_82, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	addq	$1, %rax	#, i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	cmpq	$127, %rax	#, i
	je	.L29	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movzbl	(%rsi,%rax), %edx	# MEM[symbol: "enqueue", index: i_57, offset: 0B], _54
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	testb	%dl, %dl	# _54
	jne	.L30	#,
.L29:
# kernel/cpu/scheduler/../cpu.h:113: 	if (!queue->head) queue->head = thread;
	movabsq	$cpu+16, %rax	#, tmp262
	movq	(%rax), %r12	# MEM[(struct Queue *)&cpu + 16B].head, next
# kernel/cpu/scheduler/../cpu.h:112: 	thread->nextThread = NULL;
	movq	$0, 144(%r13)	#, prev_11->nextThread
# kernel/cpu/scheduler/../cpu.h:113: 	if (!queue->head) queue->head = thread;
	testq	%r12, %r12	# next
	je	.L65	#,
# kernel/cpu/scheduler/../cpu.h:114: 	else queue->tail->nextThread = thread;
	movabsq	cpu+24, %rax	# MEM[(struct Queue *)&cpu + 16B].tail, MEM[(struct Queue *)&cpu + 16B].tail
	movq	%r13, 144(%rax)	# prev, _48->nextThread
.L32:
# kernel/cpu/scheduler/../cpu.h:115: 	queue->tail = thread;
	movq	%r13, %rax	# prev, prev
	movabsq	%rax, cpu+24	# prev, MEM[(struct Queue *)&cpu + 16B].tail
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	testb	%dil, %dil	# isBugChecking.1_34
	je	.L23	#,
	jmp	.L33	#
.L40:
# kernel/cpu/scheduler/scheduler.c:85:         next = &idleThread;
	movabsq	$idleThread, %r12	#, next
	jmp	.L36	#
.L39:
# kernel/cpu/scheduler/scheduler.c:63:         t->threadState = READY;
	movl	$1, 136(%r13)	#, prev_11->threadState
	jmp	.L29	#
.L65:
# kernel/cpu/scheduler/../cpu.h:113: 	if (!queue->head) queue->head = thread;
	movq	%r13, %rax	# prev, prev
	movq	%r13, %r12	# prev, next
	movabsq	%rax, cpu+16	# prev, MEM[(struct Queue *)&cpu + 16B].head
	jmp	.L32	#
.LFE25:
	.size	Schedule, .-Schedule
	.section	.rodata
.LC5:
	.string	"Yield"
	.text
	.align 16
	.globl	Yield
	.type	Yield, @function
Yield:
.LFB26:
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	movabsq	$isBugChecking, %rax	#, tmp97
# kernel/cpu/scheduler/scheduler.c:94: void Yield(void) {
	pushq	%rbp	#
.LCFI11:
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	cmpb	$0, (%rax)	#, isBugChecking
# kernel/cpu/scheduler/scheduler.c:94: void Yield(void) {
	movq	%rsp, %rbp	#,
.LCFI12:
# kernel/cpu/scheduler/../irql/../../trace.h:25:     if (!function_name || isBugChecking) return;
	jne	.L69	#,
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movabsl	lastfunc_history+1280, %eax	# lastfunc_history.current_index, lastfunc_history.current_index
	leal	1(%rax), %edx	#, tmp100
# kernel/cpu/scheduler/../irql/../../trace.h:28:         (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;
	movslq	%edx, %rax	# tmp100, tmp102
	movl	%edx, %ecx	# tmp100, tmp106
	imulq	$1717986919, %rax, %rax	#, tmp102, tmp103
	sarl	$31, %ecx	#, tmp106
	sarq	$34, %rax	#, tmp105
	subl	%ecx, %eax	# tmp106, _8
	leal	(%rax,%rax,4), %ecx	#, tmp109
	movl	%edx, %eax	# tmp100, tmp100
	addl	%ecx, %ecx	# tmp110
	subl	%ecx, %eax	# tmp110, tmp100
	movabsq	$lastfunc_history, %rcx	#, tmp99
# kernel/cpu/scheduler/../irql/../../trace.h:27:     lastfunc_history.current_index =
	movabsl	%eax, lastfunc_history+1280	# _8, lastfunc_history.current_index
	movslq	%eax, %rdx	# _8, _8
	movq	%rdx, %rsi	# _8, _8
	movabsq	$lastfunc_history+128, %rdx	#, tmp114
	salq	$7, %rsi	#, _8
	addq	%rsi, %rcx	# _32, _33
	addq	%rsi, %rdx	# _32, _39
	movq	%rcx, %rax	# _33, ivtmp.115
	.align 16
.L68:
# kernel/cpu/scheduler/../irql/../../trace.h:32:         lastfunc_history.names[lastfunc_history.current_index][j] = 0;
	movb	$0, (%rax)	#, MEM[base: _34, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:31:     for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
	addq	$1, %rax	#, ivtmp.115
	cmpq	%rdx, %rax	# _39, ivtmp.115
	jne	.L68	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	xorl	%eax, %eax	# i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movl	$89, %edx	#, _11
	movabsq	$.LC5, %rsi	#, tmp118
	.align 16
.L70:
# kernel/cpu/scheduler/../irql/../../trace.h:37:         lastfunc_history.names[lastfunc_history.current_index][i] =
	movb	%dl, (%rcx,%rax)	# _11, MEM[base: _33, index: i_13, offset: 0B]
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	addq	$1, %rax	#, i
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	cmpq	$127, %rax	#, i
	je	.L69	#,
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	movzbl	(%rsi,%rax), %edx	# MEM[symbol: "Yield", index: i_14, offset: 0B], _11
# kernel/cpu/scheduler/../irql/../../trace.h:36:     for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
	testb	%dl, %dl	# _11
	jne	.L70	#,
.L69:
# kernel/cpu/scheduler/scheduler.c:96:     Schedule();
	movabsq	$Schedule, %rax	#, tmp98
	call	*%rax	# tmp98
# kernel/cpu/scheduler/scheduler.c:97: }
	popq	%rbp	#
.LCFI13:
	ret	
.LFE26:
	.size	Yield, .-Yield
	.local	idleStack
	.comm	idleStack,4096,16
	.globl	idleThread
	.section	.bss
	.align 32
	.type	idleThread, @object
	.size	idleThread, 152
idleThread:
	.zero	152
	.globl	isScheduleDpcQueued
	.type	isScheduleDpcQueued, @object
	.size	isScheduleDpcQueued, 1
isScheduleDpcQueued:
	.zero	1
	.section	.eh_frame,"aw",@progbits
.Lframe1:
	.long	.LECIE1-.LSCIE1
.LSCIE1:
	.long	0
	.byte	0x3
	.string	""
	.byte	0x1
	.byte	0x78
	.byte	0x10
	.byte	0xc
	.byte	0x7
	.byte	0x8
	.byte	0x90
	.byte	0x1
	.align 8
.LECIE1:
.LSFDE1:
	.long	.LEFDE1-.LASFDE1
.LASFDE1:
	.long	.LASFDE1-.Lframe1
	.quad	.LFB23
	.quad	.LFE23-.LFB23
	.byte	0x4
	.long	.LCFI0-.LFB23
	.byte	0xe
	.byte	0x10
	.byte	0x86
	.byte	0x2
	.byte	0x4
	.long	.LCFI1-.LCFI0
	.byte	0xd
	.byte	0x6
	.byte	0x4
	.long	.LCFI2-.LCFI1
	.byte	0xc6
	.byte	0xc
	.byte	0x7
	.byte	0x8
	.align 8
.LEFDE1:
.LSFDE3:
	.long	.LEFDE3-.LASFDE3
.LASFDE3:
	.long	.LASFDE3-.Lframe1
	.quad	.LFB25
	.quad	.LFE25-.LFB25
	.byte	0x4
	.long	.LCFI3-.LFB25
	.byte	0xe
	.byte	0x10
	.byte	0x86
	.byte	0x2
	.byte	0x4
	.long	.LCFI4-.LCFI3
	.byte	0xd
	.byte	0x6
	.byte	0x4
	.long	.LCFI5-.LCFI4
	.byte	0x8d
	.byte	0x3
	.byte	0x8c
	.byte	0x4
	.byte	0x83
	.byte	0x5
	.byte	0x4
	.long	.LCFI6-.LCFI5
	.byte	0xa
	.byte	0xc3
	.byte	0x4
	.long	.LCFI7-.LCFI6
	.byte	0xcc
	.byte	0x4
	.long	.LCFI8-.LCFI7
	.byte	0xcd
	.byte	0x4
	.long	.LCFI9-.LCFI8
	.byte	0xc6
	.byte	0xc
	.byte	0x7
	.byte	0x8
	.byte	0x4
	.long	.LCFI10-.LCFI9
	.byte	0xb
	.align 8
.LEFDE3:
.LSFDE5:
	.long	.LEFDE5-.LASFDE5
.LASFDE5:
	.long	.LASFDE5-.Lframe1
	.quad	.LFB26
	.quad	.LFE26-.LFB26
	.byte	0x4
	.long	.LCFI11-.LFB26
	.byte	0xe
	.byte	0x10
	.byte	0x86
	.byte	0x2
	.byte	0x4
	.long	.LCFI12-.LCFI11
	.byte	0xd
	.byte	0x6
	.byte	0x4
	.long	.LCFI13-.LCFI12
	.byte	0xc6
	.byte	0xc
	.byte	0x7
	.byte	0x8
	.align 8
.LEFDE5:
	.ident	"GCC: (GNU) 10.3.0"
