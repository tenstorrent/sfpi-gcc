// { dg-do compile }
// { dg-additional-options "-mcpu=tt-wh -mabi=ilp32 -fno-inline -O2 -fno-exceptions" }
// { dg-final { check-function-bodies "**" "" } }

// Read after write race with different sized accesses and different
// effective addresses.

template<typename T> union u {
  unsigned i[2];
  struct {
    T volatile a, b;
  } s;
};

// These are ordered nicely, we don't need the workaround (but emit it anyway).
// It'd be nice to do better.
template<typename T>
unsigned ok (unsigned x, unsigned y) {
    u<T> u;

    u.s.b = y;
    u.s.a = x;
    return u.i[0];
}

template<typename T>
unsigned bad (unsigned x, unsigned y) {
  u<T> u;

  u.s.a = x;
  u.s.b = y;
  return u.i[0];
}


int foo (unsigned x, unsigned y) {
  return ok<short> (x, y) + ok<char> (x, y)
    + bad<short> (x, y) + bad<char> (x, y);
}

/*
**_Z2okIsEjjj:
**	addi	sp,sp,-16
**	sh	a1,10\(sp\)
**	lhu	zero,10\(sp\)
**	sh	a0,8\(sp\)
**	lw	a0,8\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

/*
**_Z2okIcEjjj:
**	addi	sp,sp,-16
**	sb	a1,9\(sp\)
**	lbu	zero,9\(sp\)
**	sb	a0,8\(sp\)
**	lw	a0,8\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

/*
**_Z3badIsEjjj:
**	addi	sp,sp,-16
**	sh	a0,8\(sp\)
**	sh	a1,10\(sp\)
**	lhu	zero,10\(sp\)
**	lw	a0,8\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/

/*
**_Z3badIcEjjj:
**	addi	sp,sp,-16
**	sb	a0,8\(sp\)
**	sb	a1,9\(sp\)
**	lbu	zero,9\(sp\)
**	lw	a0,8\(sp\)
**	addi	sp,sp,16
**	jr	ra
*/
