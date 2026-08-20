// Scratch-row derivation witness: the kernel's own typed 32-bit Dst
// accesses claim rows 0 and 252, so the allocator's proven-free
// scratch row moves off the default 252 to 248 (the +/-3 mod 256
// aliasing window around every used immediate is excluded).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 248, 4, 7} } }
// { dg-final { scan-assembler-not {\mSFPSTORE\tL[0-7], 252, 4, 7} } }

extern volatile unsigned __instrn_buffer[];

void lreg_alloc_dst32 (void)
{
  auto a0 = __builtin_rvtt_sfpload (__instrn_buffer, 0, 0, 0, 4, 7);
  auto a1 = __builtin_rvtt_sfpload (__instrn_buffer, 252, 0, 0, 4, 7);
  auto a2 = __builtin_rvtt_sfpreadlreg (2);
  auto a3 = __builtin_rvtt_sfpreadlreg (3);
  auto a4 = __builtin_rvtt_sfpreadlreg (4);
  auto a5 = __builtin_rvtt_sfpreadlreg (5);
  auto a6 = __builtin_rvtt_sfpreadlreg (6);
  auto a7 = __builtin_rvtt_sfpreadlreg (7);
  auto a8 = __builtin_rvtt_sfpmul (a0, a1, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto t = a0;
      a0 = __builtin_rvtt_sfpmad (a1, a2, a3, 0);
      a1 = __builtin_rvtt_sfpmad (a2, a3, a4, 0);
      a2 = __builtin_rvtt_sfpmad (a3, a4, a5, 0);
      a3 = __builtin_rvtt_sfpmad (a4, a5, a6, 0);
      a4 = __builtin_rvtt_sfpmad (a5, a6, a7, 0);
      a5 = __builtin_rvtt_sfpmad (a6, a7, a8, 0);
      a6 = __builtin_rvtt_sfpmad (a7, a8, t, 0);
      a7 = __builtin_rvtt_sfpmad (a8, t, a0, 0);
      a8 = __builtin_rvtt_sfpmad (t, a0, a1, 0);
    }
  __builtin_rvtt_sfpwritelreg (a0, 0);
  __builtin_rvtt_sfpwritelreg (a8, 1);
}
