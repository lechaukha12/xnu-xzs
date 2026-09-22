.section __TEXT,__text,regular,pure_instructions
.p2align 2
.globl _xzs_svc
_xzs_svc:
	mov	x16, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	mov	x3, x4
	svc	#0x80
	b.cs	1f
	ret
1:
	neg	x0, x0
	ret
