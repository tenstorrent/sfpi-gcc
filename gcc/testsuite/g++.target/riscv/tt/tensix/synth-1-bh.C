// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

extern volatile unsigned iptr[];
unsigned frob ();

void zero ()
{
  unsigned i = frob ();
  // no renumbering
  auto id = __builtin_rvtt_synth_opcode (0, 2);

  auto val1 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);
  auto val2 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);

  __builtin_rvtt_sfpwritelreg (val1, 0);
  __builtin_rvtt_sfpwritelreg (val2, 1);
}
/*
**_Z4zerov:
**	addi	sp,sp,-16
**	sw	ra,12\(sp\)
**	call	_Z4frobv
**	li	a5, 1879048192	# 2:70000000
**	add	a0,a0,a5
**	lui	a5,%hi\(iptr\)
**	addi	a5,a5,%lo\(iptr\)
**	sw	a0, 0\(a5\)	# 2:SFPLOAD	L0, a0, 0, 0
**	li	a4,1048576
**	xor	a4,a4,a0
**	sw	a4, 0\(a5\)	# 2:SFPLOAD	L1, a4, 0, 0
**	# WRITE L0
**	# WRITE L1
**	lw	ra,12\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

void one ()
{
  unsigned i = frob ();
  unsigned j = frob ();
  // renumbering
  auto id = __builtin_rvtt_synth_opcode (0, 2);

  auto val1 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);
  auto val2 = __builtin_rvtt_sfpload (iptr, j, id + j, 2, 0, 0);

  __builtin_rvtt_sfpwritelreg (val1, 0);
  __builtin_rvtt_sfpwritelreg (val2, 1);
}
/*
**_Z3onev:
**	addi	sp,sp,-16
**	sw	ra,12\(sp\)
**	sw	s0,8\(sp\)
**	call	_Z4frobv
**	mv	s0,a0
**	call	_Z4frobv
**	li	a5, 1879048192	# 2:70000000
**	add	a4,s0,a5
**	lui	a5,%hi\(iptr\)
**	addi	a5,a5,%lo\(iptr\)
**	sw	a4, 0\(a5\)	# 2:SFPLOAD	L0, a4, 0, 0
**	li	a4, 1880096768	# 4:70100000
**	add	a0,a0,a4
**	sw	a0, 0\(a5\)	# 4:SFPLOAD	L1, a0, 0, 0
**	# WRITE L0
**	# WRITE L1
**	lw	ra,12\(sp\)
**	lw	s0,8\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

void two ()
{
  unsigned i = frob ();
  unsigned j = frob ();
  // renumbering
  auto id1 = __builtin_rvtt_synth_opcode (0, 2);
  auto val1 = __builtin_rvtt_sfpload (iptr, i, id1 + i, 2, 0, 0);
  auto id2 = __builtin_rvtt_synth_opcode (1, 2);
  auto val2 = __builtin_rvtt_sfpload (iptr, j, id2 + j, 2, 0, 0);

  __builtin_rvtt_sfpwritelreg (val1, 0);
  __builtin_rvtt_sfpwritelreg (val2, 1);
}
/*
**_Z3twov:
**	addi	sp,sp,-16
**	sw	ra,12\(sp\)
**	sw	s0,8\(sp\)
**	call	_Z4frobv
**	mv	s0,a0
**	call	_Z4frobv
**	li	a5, 1879048192	# 4:70000000
**	add	a4,s0,a5
**	lui	a5,%hi\(iptr\)
**	addi	a5,a5,%lo\(iptr\)
**	sw	a4, 0\(a5\)	# 4:SFPLOAD	L0, a4, 0, 0
**	li	a4, 1880096769	# 2:70100001
**	add	a0,a0,a4
**	sw	a0, 0\(a5\)	# 2:SFPLOAD	L1, a0, 0, 0
**	# WRITE L0
**	# WRITE L1
**	lw	ra,12\(sp\)
**	lw	s0,8\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

void three ()
{
  // CSE
  unsigned i = frob ();
  unsigned arg;
  if (i & 1)
    {
      auto id = __builtin_rvtt_synth_opcode (0, 2);
      arg = id + i;
    }
  else
    {
      auto id = __builtin_rvtt_synth_opcode (1, 2);
      arg = id + i;
    }
  auto val1 = __builtin_rvtt_sfpload (iptr, i, arg, 2, 0, 0);
  __builtin_rvtt_sfpwritelreg (val1, 0);
}
/*
**_Z5threev:
**	addi	sp,sp,-16
**	sw	ra,12\(sp\)
**	call	_Z4frobv
**	li	a5, 1879048192	# 2:70000000
**	add	a5,a5,a0
**	lui	a4,%hi\(iptr\)
**	sw	a5, %lo\(iptr\)\(a4\)	# 2:SFPLOAD	L0, a5, 0, 0
**	# WRITE L0
**	lw	ra,12\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

void four ()
{
  // CSE
  unsigned i = frob ();
  unsigned id;
  if (i & 1)
    id = __builtin_rvtt_synth_opcode (0, 2);
  else
    id = __builtin_rvtt_synth_opcode (1, 2);
  auto val1 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);
  __builtin_rvtt_sfpwritelreg (val1, 0);
}
/*
**_Z4fourv:
**	addi	sp,sp,-16
**	sw	ra,12\(sp\)
**	call	_Z4frobv
**	li	a5, 1879048192	# 2:70000000
**	add	a5,a5,a0
**	lui	a4,%hi\(iptr\)
**	sw	a5, %lo\(iptr\)\(a4\)	# 2:SFPLOAD	L0, a5, 0, 0
**	# WRITE L0
**	lw	ra,12\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/
