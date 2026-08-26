// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Function-level budget: each loop fits the per-loop bound (220 words
// flattened), but a vehicle instantiating many admissible delivery
// loops must not grow past the TRISC code region (the topk_xl K=2048
// overflow) -- the fifth loop refuses by name once the accumulated
// flattened total would exceed the function budget.
// { dg-final { scan-tree-dump "refused .launch-flatten-function-budget." "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump "fires=4 refusals=" "rvtt_launch_flatten" } }

#define LF_TEN_LAUNCHES \
      { auto a = __builtin_rvtt_sfpreadlreg (0); \
	auto b = __builtin_rvtt_sfpreadlreg (1); \
	__builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpmul (a, b, 0), 2); } \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0); \
      __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 0);

#define LF_LOOP(NAME) \
  for (int NAME = 0; NAME < 20; ++NAME) \
    { \
      LF_TEN_LAUNCHES \
    }

void lf_function_budget ()
{
  LF_LOOP (a)
  LF_LOOP (b)
  LF_LOOP (c)
  LF_LOOP (d)
  LF_LOOP (e)
  LF_LOOP (f)
}
