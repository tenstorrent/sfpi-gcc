// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void negate () {
  auto a = vInt (1);
  auto b = -a;
  __builtin_rvtt_sfpwritelreg (b.get (), 1);

  auto c = ~a + 1;
  __builtin_rvtt_sfpwritelreg (c.get (), 2);

  auto d = 4u + c;
  __builtin_rvtt_sfpwritelreg (d.get (), 3);
}
/*
**_Z6negatev:
**	SFPLOADI	L2, 1, 4
**	SFPLOADI	L0, 0, 4
**	SFPMOV	L1, L2, 2
**	SFPIADD	L1, L0, 0, 6
**	# WRITE L1
**	SFPNOT	L2, L2
**	SFPIADD	L2, L2, 1, 5
**	# WRITE L2
**	SFPIADD	L3, L2, 4, 5
**	# WRITE L3
**	ret
*/

void slength () {
  sfpi::vInt v = __builtin_rvtt_sfpreadlreg (0);
  sfpi::vUInt u = __builtin_rvtt_sfpreadlreg (7);

  // Mixed compares
  v_if (v == 0) {
    v = __builtin_rvtt_sfpreadlreg (1);
  } v_endif;

  v_if (v < 0) {
    v = __builtin_rvtt_sfpreadlreg (2);
  } v_endif;

  v_if (v > 0) {
    v = __builtin_rvtt_sfpreadlreg (3);
  } v_endif;

  v_if (v >= 0) {
    v = __builtin_rvtt_sfpreadlreg (4);
  } v_endif;

  v_if (u == 0) {
    u = __builtin_rvtt_sfpreadlreg (1);
  } v_endif;

  v_if (u < 0) {
    u = __builtin_rvtt_sfpreadlreg (2);
  } v_endif;

  v_if (u > 0) {
    u = __builtin_rvtt_sfpreadlreg (3);
  } v_endif;

  v_if (u >= 0) {
    u = __builtin_rvtt_sfpreadlreg (4);
  } v_endif;
}
/*
**_Z7slengthv:
**	# READ L0
**	# READ L7
**	SFPSETCC	L0, 0, 6
**	# READ L1
**	SFPMOV	L0, L1, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSETCC	L0, 0, 0
**	# READ L2
**	SFPMOV	L0, L2, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSETCC	L0, 0, 4
**	SFPSETCC	L0, 0, 2
**	# READ L3
**	SFPMOV	L0, L3, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSETCC	L0, 0, 4
**	# READ L4
**	SFPMOV	L0, L4, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSETCC	L7, 0, 6
**	# READ L1
**	SFPMOV	L7, L1, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPSETCC	L7, 0, 0
**	# READ L2
**	SFPMOV	L7, L2, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPSETCC	L7, 0, 4
**	SFPSETCC	L7, 0, 2
**	# READ L3
**	SFPMOV	L7, L3, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPSETCC	L7, 0, 4
**	# READ L4
**	SFPMOV	L7, L4, 0	# LV:L7
**	SFPENCC	3, 10
**	ret
*/

void ulength () {
  sfpi::vInt v = __builtin_rvtt_sfpreadlreg (0);
  sfpi::vUInt u = __builtin_rvtt_sfpreadlreg (7);

  // Mixed compares
  v_if (v == 0x10u) {
    v = __builtin_rvtt_sfpreadlreg (1);
  } v_endif;

  v_if (v < 0x10u) {
    v = __builtin_rvtt_sfpreadlreg (2);
  } v_endif;

  v_if (v > 0x10u) {
    v = __builtin_rvtt_sfpreadlreg (3);
  } v_endif;

  v_if (v >= 0x10u) {
    v = __builtin_rvtt_sfpreadlreg (4);
  } v_endif;

  v_if (u == 0x10u) {
    u = __builtin_rvtt_sfpreadlreg (1);
  } v_endif;

  v_if (u < 0x10u) {
    u = __builtin_rvtt_sfpreadlreg (2);
  } v_endif;

  v_if (u > 0x10u) {
    u = __builtin_rvtt_sfpreadlreg (3);
  } v_endif;

  v_if (u >= 0x10u) {
    u = __builtin_rvtt_sfpreadlreg (4);
  } v_endif;
}
/*
**_Z7ulengthv:
**	# READ L0
**	# READ L7
**	SFPIADD	L1, L0, -16, 5
**	SFPSETCC	L1, 0, 6
**	# READ L1
**	SFPMOV	L0, L1, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPIADD	L1, L0, -16, 1
**	# READ L2
**	SFPMOV	L0, L2, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPIADD	L1, L0, -16, 9
**	SFPSETCC	L1, 0, 2
**	# READ L3
**	SFPMOV	L0, L3, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPIADD	L1, L0, -16, 9
**	# READ L4
**	SFPMOV	L0, L4, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPIADD	L0, L7, -16, 5
**	SFPSETCC	L0, 0, 6
**	# READ L1
**	SFPMOV	L7, L1, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPIADD	L0, L7, -16, 1
**	# READ L2
**	SFPMOV	L7, L2, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPIADD	L0, L7, -16, 9
**	SFPSETCC	L0, 0, 2
**	# READ L3
**	SFPMOV	L7, L3, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPIADD	L0, L7, -16, 9
**	# READ L4
**	SFPMOV	L7, L4, 0	# LV:L7
**	SFPENCC	3, 10
**	ret
*/

void mixed () {
  // These comparisons mix signed/unsigned
  // we should deprecate and remove the functionality
  constexpr uint32_t ZERO = 0;
  sfpi::vInt v = __builtin_rvtt_sfpreadlreg (0);
  sfpi::vUInt u = __builtin_rvtt_sfpreadlreg (7);

  // Mixed compares
  v_if (v == ZERO) {
    v = __builtin_rvtt_sfpreadlreg (1);
  } v_endif;

  v_if (v < ZERO) {
    v = __builtin_rvtt_sfpreadlreg (2);
  } v_endif;

  v_if (v > ZERO) {
    v = __builtin_rvtt_sfpreadlreg (3);
  } v_endif;

  v_if (v >= ZERO) {
    v = __builtin_rvtt_sfpreadlreg (4);
  } v_endif;

  v_if (v == u) {
    u = __builtin_rvtt_sfpreadlreg (1);
  } v_endif;

  v_if (v < u) {
    u = __builtin_rvtt_sfpreadlreg (2);
  } v_endif;

  v_if (v > u) {
    u = __builtin_rvtt_sfpreadlreg (3);
  } v_endif;

  v_if (v >= u) {
    u = __builtin_rvtt_sfpreadlreg (4);
  } v_endif;

  // Mixed math
  auto r = u - v;
  static_assert (std::is_same<vUInt,decltype (r)>::value);
  __builtin_rvtt_sfpwritelreg (r.get (), 5);
}
/*
**_Z5mixedv:
**	# READ L0
**	# READ L7
**	SFPSETCC	L0, 0, 6
**	# READ L1
**	SFPMOV	L0, L1, 0	# LV:L0
**	SFPMOV	L5, L0, 2
**	SFPENCC	3, 10
**	SFPSETCC	L0, 0, 0
**	# READ L2
**	SFPMOV	L5, L2, 0	# LV:L5
**	SFPENCC	3, 10
**	SFPSETCC	L5, 0, 4
**	SFPSETCC	L5, 0, 2
**	# READ L3
**	SFPMOV	L5, L3, 0	# LV:L5
**	SFPENCC	3, 10
**	SFPSETCC	L5, 0, 4
**	# READ L4
**	SFPMOV	L5, L4, 0	# LV:L5
**	SFPENCC	3, 10
**	SFPMOV	L0, L7, 2
**	SFPIADD	L0, L5, 0, 6
**	SFPSETCC	L0, 0, 6
**	# READ L1
**	SFPMOV	L7, L1, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPMOV	L0, L7, 2
**	SFPIADD	L0, L5, 0, 2
**	# READ L2
**	SFPMOV	L7, L2, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPMOV	L0, L7, 2
**	SFPIADD	L0, L5, 0, 10
**	SFPSETCC	L0, 0, 2
**	# READ L3
**	SFPMOV	L7, L3, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPMOV	L0, L7, 2
**	SFPIADD	L0, L5, 0, 10
**	# READ L4
**	SFPMOV	L7, L4, 0	# LV:L7
**	SFPENCC	3, 10
**	SFPIADD	L5, L7, 0, 6
**	# WRITE L5
**	ret
*/
