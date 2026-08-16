	.file	"invariant-loadi-barrier-bh.C"
	.option nopic
	.text
	.align	2
	.globl	_Z14memory_barrierv
	.type	_Z14memory_barrierv, @function
_Z14memory_barrierv:
.LFB0:
	# READ L0
	li	a5,0
	lui	a3,%hi(ordinary_memory)
	li	a4,8
.L2:
	sw	a5,%lo(ordinary_memory)(a3)
	SFPLOADI	L1, 6717, 2
	SFPLOADI	L1, 15947, 8	# LV:L1
	SFPMUL	L0, L0, L1, 0
	addi	a5,a5,1
	bne	a5,a4,.L2
	# WRITE L0
	ret
.LFE0:
	.size	_Z14memory_barrierv, .-_Z14memory_barrierv
	.align	2
	.globl	_Z10cc_barrierv
	.type	_Z10cc_barrierv, @function
_Z10cc_barrierv:
.LFB1:
	# READ L0
	li	a5,8
.L6:
	SFPENCC	10, 3
	SFPLOADI	L1, 6717, 2
	SFPLOADI	L1, 15947, 8	# LV:L1
	SFPMUL	L0, L0, L1, 0
	addi	a5,a5,-1
	bne	a5,zero,.L6
	# WRITE L0
	ret
.LFE1:
	.size	_Z10cc_barrierv, .-_Z10cc_barrierv
	.align	2
	.globl	_Z14config_barrierv
	.type	_Z14config_barrierv, @function
_Z14config_barrierv:
.LFB2:
	# READ L0
	li	a5,8
.L9:
	SFPCONFIG	0, 0, 0	# R:L0 CFG:0
	SFPLOADI	L1, 6717, 2
	SFPLOADI	L1, 15947, 8	# LV:L1
	SFPMUL	L0, L0, L1, 0
	SFPNOP
	addi	a5,a5,-1
	bne	a5,zero,.L9
	# WRITE L0
	ret
.LFE2:
	.size	_Z14config_barrierv, .-_Z14config_barrierv
	.align	2
	.globl	_Z11gpr_barrierv
	.type	_Z11gpr_barrierv, @function
_Z11gpr_barrierv:
.LFB3:
	# READ L0
	li	a5,8
.L12:
	SFPLOADI	L1, 6717, 2
	SFPLOADI	L1, 15947, 8	# LV:L1
	SFPMUL	L0, L0, L1, 0
	addi	a5,a5,-1
	bne	a5,zero,.L12
	# WRITE L0
	ret
.LFE3:
	.size	_Z11gpr_barrierv, .-_Z11gpr_barrierv
	.globl	ordinary_memory
	.section	.sbss,"aw",@nobits
	.align	2
	.type	ordinary_memory, @object
	.size	ordinary_memory, 4
ordinary_memory:
	.zero	4
	.ident	"GCC: (tenstorrent/sfpi-macro-planner) 15.1.0"
	.section	.note.GNU-stack,"",@progbits
