.section __TEXT,__text,regular,pure_instructions
.p2align 4
.globl _start
_start:
	mov	x0, #1
	adr	x1, msg
	adr	x2, msg_end
	sub	x2, x2, x1
	mov	x16, #4
	svc	#0x80
	mov	x0, #0
	mov	x16, #1
	svc	#0x80
msg:
	.ascii "hello from XNU-XZS userland\n"
msg_end:
