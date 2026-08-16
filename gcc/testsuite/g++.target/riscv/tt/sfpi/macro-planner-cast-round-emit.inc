__attribute__((noinline)) void cast_round_pipeline_emittable ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#if __riscv_xtttensixwh
  constexpr unsigned no_increment = 3;
#else
  constexpr unsigned no_increment = 7;
#endif
#pragma GCC unroll 8
  for (int row = 0; row < 8; ++row)
    {
      auto input = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6,
					   no_increment);
      auto fp32 = __builtin_rvtt_sfpcast (input, 0);
      auto bf16 = __builtin_rvtt_sfpstochrnd_i (nullptr, fp32, 0, 0, 0,
						 1, 0);
      __builtin_rvtt_sfpstore (nullptr, bf16, 0, 0, 0, 2,
			       no_increment);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
