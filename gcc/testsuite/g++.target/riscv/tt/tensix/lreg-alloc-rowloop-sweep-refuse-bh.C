// DP-11 witness: in a Dst-stepping row loop the scratch row SWEEPS the
// file (base advances per iteration) while earlier iterations' output
// rows stay live for pack.  With 64 proven trips at step 2 the swept
// alias window covers every candidate offset, so NO scratch row is
// provably free: the allocator refuses by name instead of silently
// clobbering iteration 0's output from iteration ~63 (the
// pre-bounded-sweep miscompile DU-DP-11 describes).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "lreg-spill-no-free-dst" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "colorability=proven" "rvtt_lp_alloc" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void lreg_alloc_rowloop_sweep_refuse (void)
{
  for (unsigned d = 0; d != 64; ++d)
    {
      auto a0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto a1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);
      auto a2 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
      auto a3 = __builtin_rvtt_sfpload (nullptr, 6, 0, 0, 4, 7);
      auto a4 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
      auto a5 = __builtin_rvtt_sfpload (nullptr, 10, 0, 0, 4, 7);
      auto a6 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 4, 7);
      auto a7 = __builtin_rvtt_sfpload (nullptr, 14, 0, 0, 4, 7);
      auto a8 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 4, 7);
      for (unsigned t = 0; t != 8; ++t)
	{
	  a0 = __builtin_rvtt_sfpxor (a0, a1);
	  a1 = __builtin_rvtt_sfpxor (a1, a2);
	  a2 = __builtin_rvtt_sfpxor (a2, a3);
	  a3 = __builtin_rvtt_sfpxor (a3, a4);
	  a4 = __builtin_rvtt_sfpxor (a4, a5);
	  a5 = __builtin_rvtt_sfpxor (a5, a6);
	  a6 = __builtin_rvtt_sfpxor (a6, a7);
	  a7 = __builtin_rvtt_sfpxor (a7, a8);
	  a8 = __builtin_rvtt_sfpxor (a8, a0);
	}
      __builtin_rvtt_sfpstore (nullptr, a8, 192, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
