// Architectural multi-result coverage for TopK-style index tracking.

void indexed_swap_02 ()
{
  auto v0 = __builtin_rvtt_sfpreadlreg (0);
  auto v2 = __builtin_rvtt_sfpreadlreg (2);
  auto i0 = __builtin_rvtt_sfpreadlreg (4);
  auto i2 = __builtin_rvtt_sfpreadlreg (6);
  auto r = __builtin_rvtt_sfpswap_indexed (v0, v2, i0, i2, 8);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 2), 4);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 3), 6);
}
/*
**_Z15indexed_swap_02v:
**	# READ L0
**	# READ L2
**	# READ L4
**	# READ L6
**	SFPSWAP	L0, L2, 8	# INDEXED R:L4,L6
**	# WRITE L0
**	# WRITE L2
**	# WRITE L4
**	# WRITE L6
**	ret
*/

void transpose_both_banks ()
{
  auto v0 = __builtin_rvtt_sfpreadlreg (0);
  auto v1 = __builtin_rvtt_sfpreadlreg (1);
  auto v2 = __builtin_rvtt_sfpreadlreg (2);
  auto v3 = __builtin_rvtt_sfpreadlreg (3);
  auto i0 = __builtin_rvtt_sfpreadlreg (4);
  auto i1 = __builtin_rvtt_sfpreadlreg (5);
  auto i2 = __builtin_rvtt_sfpreadlreg (6);
  auto i3 = __builtin_rvtt_sfpreadlreg (7);
  auto r = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, i0, i1, i2, i3);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 2), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 3), 3);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (4), 4);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (5), 5);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (6), 6);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (7), 7);
}
/*
**_Z20transpose_both_banksv:
**	# READ L0
**	# READ L1
**	# READ L2
**	# READ L3
**	# READ L4
**	# READ L5
**	# READ L6
**	# READ L7
**	SFPTRANSP	# R:L0,L1,L2,L3,L4,L5,L6,L7
**	# WRITE L0
**	# WRITE L1
**	# WRITE L2
**	# WRITE L3
**	# READ L4
**	# WRITE L4
**	# READ L5
**	# WRITE L5
**	# READ L6
**	# WRITE L6
**	# READ L7
**	# WRITE L7
**	ret
*/

// Exercise allocation from unconstrained load pseudos.  The four selected
// output registers must always be a legal value pair and its +4 companions;
// any independently colored tuple produces an impossible alternative.
void adversarial_allocator ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto ia = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto ib = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto r = __builtin_rvtt_sfpswap_indexed (a, b, ia, ib, 8);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (r, 0), 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (r, 1), 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (r, 2), 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (r, 3), 0, 0, 0, 0, 0);
}
