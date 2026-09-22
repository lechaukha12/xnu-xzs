.section __TEXT,__text,regular,pure_instructions
.p2align 4
.globl _start
_start:
	ldr	x19, [sp]
	add	x20, sp, #8
	adr	x21, prefix
	mov	x0, #1
	mov	x1, x21
	mov	x2, #5
	mov	x16, #4
	svc	#0x80
	mov	x0, x19
	bl	_put_u
	mov	x0, #1
	adr	x1, nl
	mov	x2, #1
	mov	x16, #4
	svc	#0x80
	mov	x22, #0
1:
	cmp	x22, x19
	b.hs	9f
	adr	x1, argvlab
	mov	x0, #1
	mov	x2, #5
	mov	x16, #4
	svc	#0x80
	mov	x0, x22
	bl	_put_u
	mov	x0, #1
	adr	x1, eq
	mov	x2, #1
	mov	x16, #4
	svc	#0x80
	ldr	x1, [x20, x22, lsl #3]
	mov	x2, #0
2:
	ldrb	w3, [x1, x2]
	cbz	w3, 3f
	add	x2, x2, #1
	b	2b
3:
	mov	x0, #1
	mov	x16, #4
	svc	#0x80
	mov	x0, #1
	adr	x1, nl
	mov	x2, #1
	mov	x16, #4
	svc	#0x80
	add	x22, x22, #1
	b	1b
9:
	mov	x0, #0
	mov	x16, #1
	svc	#0x80

_put_u:
	stp	x29, x30, [sp, #-16]!
	sub	sp, sp, #16
	mov	x9, sp
	add	x10, x9, #15
	mov	w11, #10
	strb	wzr, [x10]
1:
	udiv	x12, x0, x11
	msub	x13, x12, x11, x0
	add	w13, w13, #'0'
	sub	x10, x10, #1
	strb	w13, [x10]
	mov	x0, x12
	cbnz	x0, 1b
	mov	x1, x10
	add	x2, sp, #15
	sub	x2, x2, x10
	mov	x0, #1
	mov	x16, #4
	svc	#0x80
	add	sp, sp, #16
	ldp	x29, x30, [sp], #16
	ret

prefix:
	.ascii "argc="
argvlab:
	.ascii "argv["
eq:
	.ascii "="
nl:
	.ascii "\n"
