// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

namespace impostor {
extern volatile unsigned __instrn_buffer[];
}

static volatile unsigned __instrn_buffer[1];

void invariant_loadi_wrong_namespace ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (&impostor::__instrn_buffer[0],
					0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void invariant_loadi_internal_linkage ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (&__instrn_buffer[0], 0xbf91c2e7,
					0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
