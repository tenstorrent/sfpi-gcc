// { dg-options "-mcpu=tt-wh -O2 -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

struct Shape { unsigned char a, b, c, d; };

inline void force_stack(const Shape& s)
{
    asm("" ::"m"(s));
}

unsigned char synthetic(Shape s)
{
    force_stack(s);
    return s.c;
}
/*
**_Z9synthetic5Shape:
**	addi	sp,sp,-16
**	sw	a0,12\(sp\)
**	lw	zero,12\(sp\)
**	lbu	a0,14\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

void force_stack_realistic(const Shape&);

char realistic(Shape s)
{
    force_stack_realistic(s);
    return s.c;
}
/*
**_Z9realistic5Shape:
**	addi	sp,sp,-32
**	sw	a0,12\(sp\)
**	addi	a0,sp,12
**	sw	ra,28\(sp\)
**	call	_Z21force_stack_realisticRK5Shape
**	lw	ra,28\(sp\)
**	lbu	a0,14\(sp\)
**	addi	sp,sp,32
**	jr	ra
*/

Shape foo (int a, int b) {
  Shape s {char (a), char (b), 0, 0};
  force_stack (s);
  return s;
}
/*
**_Z3fooii:
**	addi	sp,sp,-16
**	sb	a0,8\(sp\)
**	sb	a1,9\(sp\)
**	lbu	zero,9\(sp\)
**	sh	zero,10\(sp\)
**	lhu	zero,10\(sp\)
**	lw	a5,8\(sp\)
**	li	a3,16711680
**	srli	a4,a5,8
**	andi	a4,a4,0xff
**	slli	a4,a4,8
**	andi	a0,a5,0xff
**	or	a0,a0,a4
**	and	a4,a5,a3
**	srli	a5,a5,24
**	or	a0,a0,a4
**	slli	a5,a5,24
**	or	a0,a0,a5
**	addi	sp,sp,16
**	jr	ra
*/
