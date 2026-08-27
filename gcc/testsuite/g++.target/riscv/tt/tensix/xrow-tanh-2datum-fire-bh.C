// The tanh 2-datum composition (lane IC): the distilled tanh row --
// load, abs, Horner mads over three CReg-resident coefficients plus a
// local and an in-loop reload, mul, min-vs-creg SFPSWAP clamp, setsgn,
// store -- forms the two-datum pipelined replay body the hand kernel
// writes, under -mtt-tensix-optimize-hoisted-prgm-reuse (the three
// preheader constants take the init's value-identical PRGM slots,
// releasing their LREGs) composed with
// -mtt-tensix-optimize-crossrow-pairing-stall-words (the SFPSWAP joins
// the pairing vocabulary at its priced acceptance stall; copy-half
// rename priority; critical-path tail interleave).  The paired body
// carries no SFPNOP: both SFPMUL->SFPSWAP erratum shadows fill with
// the other datum's real words.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-hoisted-prgm-reuse -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-stall-words -fdump-tree-rvtt_prgm_const-details -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .hoisted-reuse class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=32->16" "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler "SFPMAD\tL\[0-7\], L12," } }

void owner_init (void)
{
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3bc0919e, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c6, 12);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0xbd887f48, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c5, 13);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e905782, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c4, 14);
}

void tanh_row (void)
{
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3bc0919e, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0xbd887f48, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e905782, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3cfd1ca0, 0, 0, 31);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto p = __builtin_rvtt_sfpmad (c6, a, c5, 0);
      p = __builtin_rvtt_sfpmad (p, a, c4, 0);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0xbefa66db, 0, 0, 31);
      p = __builtin_rvtt_sfpmad (p, a, c3, 0);
      p = __builtin_rvtt_sfpmad (p, a, c2, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f7fbec0, 0, 0, 31);
      p = __builtin_rvtt_sfpmad (p, a, c1, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      auto one = __builtin_rvtt_sfpreadlreg (10);
      auto sw = __builtin_rvtt_sfpswap (p, one, 1);
      auto mn = __builtin_rvtt_sfpselect2 (sw, 0);
      auto res = __builtin_rvtt_sfpsetsgn_v (mn, x, 0);
      __builtin_rvtt_sfpstore (nullptr, res, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
