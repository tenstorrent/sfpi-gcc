// { dg-options "-mcpu=tt-qsr64 -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

unsigned udiv3(unsigned n)  { return n / 3; }
/*
**_Z5udiv3j:
**	li	a5,954437632
**	zext.w	a0,a0
**	sh1add	a5,a5,a5
**	addi	a5,a5,-1365
**	mul	a0,a0,a5
**	srli	a0,a0,33
**	ret
*/

unsigned udiv7(unsigned n)  { return n / 7; }
/*
**_Z5udiv7j:
**	li	a5,7
**	divuw	a0,a0,a5
**	ret
*/

unsigned udiv10(unsigned n) { return n / 10; }
/*
**_Z6udiv10j:
**	li	a5,1288491008
**	zext.w	a0,a0
**	bseti	a5,a5,31
**	addi	a5,a5,-819
**	mul	a0,a0,a5
**	srli	a0,a0,35
**	ret
*/

unsigned udiv100(unsigned n){ return n / 100; }
/*
**_Z7udiv100j:
**	li	a5,1374388224
**	zext.w	a0,a0
**	addi	a5,a5,1311
**	mul	a0,a0,a5
**	srli	a0,a0,37
**	ret
*/

int sdiv3(int n)  { return n / 3; }
/*
**_Z5sdiv3i:
**	li	a5,1431654400
**	addi	a5,a5,1366
**	mul	a5,a0,a5
**	sraiw	a0,a0,31
**	srli	a5,a5,32
**	subw	a0,a5,a0
**	ret
*/

int sdiv7(int n)  { return n / 7; }
/*
**_Z5sdiv7i:
**	li	a5,7
**	divw	a0,a0,a5
**	ret
*/

unsigned umod7(unsigned n) { return n % 7; }
/*
**_Z5umod7j:
**	li	a5,7
**	remuw	a0,a0,a5
**	andi	a0,a0,7
**	ret
*/

int smod7(int n)           { return n % 7; }
/*
**_Z5smod7i:
**	li	a5,7
**	remw	a0,a0,a5
**	ret
*/
