// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Symbolic trip count: refuse by name, bytes stay rolled.
// { dg-final { scan-tree-dump "refused .launch-flatten-trip-count-unproven." "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 2 } }

void lf_trip_unproven (int n)
{
  bool init = true;
  for (int d = 0; d < n; ++d)
    {
      if (init)
	{
	  __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 1, 1);
	  auto a = __builtin_rvtt_sfpreadlreg (0);
	  auto b = __builtin_rvtt_sfpreadlreg (1);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpadd (a, b, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpadd (a, b, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpadd (a, b, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpadd (a, b, 0);
	  __builtin_rvtt_sfpwritelreg (a, 2);
	  init = false;
	}
      else
	__builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
    }
}
