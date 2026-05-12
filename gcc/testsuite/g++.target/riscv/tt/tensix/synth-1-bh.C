// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

extern volatile unsigned iptr[];

void zero (unsigned i)
{
  // no renumbering
  auto id = __builtin_rvtt_synth_opcode (0, 2);

  auto val1 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);
  auto val2 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);

  __builtin_rvtt_sfpwritelreg (val1, 0);
  __builtin_rvtt_sfpwritelreg (val2, 1);
}
/*
**_Z4zeroj:
**	li	a5, 1879048192	# 2:70000000
**	add	a0,a5,a0
**	lui	a5,%hi\(iptr\)
**	addi	a5,a5,%lo\(iptr\)
**	sw	a0, 0\(a5\)	# 2:SFPLOAD	L0, a0, 0, 0
**	li	a4,1048576
**	xor	a4,a4,a0
**	sw	a4, 0\(a5\)	# 2:SFPLOAD	L1, a4, 0, 0
**	# WRITE L0
**	# WRITE L1
**	ret
*/

void one (unsigned i, unsigned j)
{
  // renumbering
  auto id = __builtin_rvtt_synth_opcode (0, 2);

  auto val1 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);
  auto val2 = __builtin_rvtt_sfpload (iptr, j, id + j, 2, 0, 0);

  __builtin_rvtt_sfpwritelreg (val1, 0);
  __builtin_rvtt_sfpwritelreg (val2, 1);
}
/*
**_Z3onejj:
**	li	a5, 1879048192	# 2:70000000
**	add	a0,a5,a0
**	lui	a5,%hi\(iptr\)
**	addi	a5,a5,%lo\(iptr\)
**	sw	a0, 0\(a5\)	# 2:SFPLOAD	L0, a0, 0, 0
**	li	a4, 1880096768	# 4:70100000
**	add	a4,a4,a1
**	sw	a4, 0\(a5\)	# 4:SFPLOAD	L1, a4, 0, 0
**	# WRITE L0
**	# WRITE L1
**	ret
*/

void two (unsigned i, unsigned j)
{
  // renumbering
  auto id1 = __builtin_rvtt_synth_opcode (0, 2);
  auto val1 = __builtin_rvtt_sfpload (iptr, i, id1 + i, 2, 0, 0);
  auto id2 = __builtin_rvtt_synth_opcode (1, 2);
  auto val2 = __builtin_rvtt_sfpload (iptr, j, id2 + j, 2, 0, 0);

  __builtin_rvtt_sfpwritelreg (val1, 0);
  __builtin_rvtt_sfpwritelreg (val2, 1);
}
/*
**_Z3twojj:
**	li	a5, 1879048192	# 4:70000000
**	add	a0,a5,a0
**	lui	a5,%hi\(iptr\)
**	addi	a5,a5,%lo\(iptr\)
**	sw	a0, 0\(a5\)	# 4:SFPLOAD	L0, a0, 0, 0
**	li	a4, 1880096769	# 2:70100001
**	add	a4,a4,a1
**	sw	a4, 0\(a5\)	# 2:SFPLOAD	L1, a4, 0, 0
**	# WRITE L0
**	# WRITE L1
**	ret
*/

void three (unsigned i)
{
  // CSE
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
**_Z5threej:
**	li	a5, 1879048192	# 2:70000000
**	add	a5,a5,a0
**	lui	a4,%hi\(iptr\)
**	sw	a5, %lo\(iptr\)\(a4\)	# 2:SFPLOAD	L0, a5, 0, 0
**	# WRITE L0
**	ret
*/

void four (unsigned i)
{
  // CSE
  unsigned id;
  if (i & 1)
    id = __builtin_rvtt_synth_opcode (0, 2);
  else
    id = __builtin_rvtt_synth_opcode (1, 2);
  auto val1 = __builtin_rvtt_sfpload (iptr, i, id + i, 2, 0, 0);
  __builtin_rvtt_sfpwritelreg (val1, 0);
}
/*
**_Z4fourj:
**	li	a5, 1879048192	# 2:70000000
**	add	a5,a5,a0
**	lui	a4,%hi\(iptr\)
**	sw	a5, %lo\(iptr\)\(a4\)	# 2:SFPLOAD	L0, a5, 0, 0
**	# WRITE L0
**	ret
*/
