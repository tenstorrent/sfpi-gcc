// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

void *buf;

void cst ()
{
  auto c = __builtin_rvtt_sfpxloadi (buf, 0x12345678, 0, 0, 16);
  __builtin_rvtt_sfpstore (0, c, 0, 0, 0, 0, 0);
}
/*
**_Z3cstv:
**	SFPLOADI	L0, 4660, 8
**	SFPLOADI	L0, 22136, 10	# LV:L0
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/

void var (int x)
{
  auto v = __builtin_rvtt_sfpxloadi (buf, x, 0, 0, 16);
  __builtin_rvtt_sfpstore (0, v, 0, 0, 0, 0, 0);
}
/*
**_Z3vari:
**	lui	a3,%hi\(buf\)
**	zext.h	a4,a0
**	li	a5, 1896480768	# 2:710a0000
**	lw	a3,%lo\(buf\)\(a3\)
**	add	a4,a4,a5
**	li	a5, 1896349696	# 3:71080000
**	sw	a4, 0\(a3\)	# 2:SFPLOADI	L0, a4, 10
**	srli	a0,a0,16
**	add	a0,a0,a5
**	sw	a0, 0\(a3\)	# 3:SFPLOADI	L0, a0, 8	# LV:L0
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/
