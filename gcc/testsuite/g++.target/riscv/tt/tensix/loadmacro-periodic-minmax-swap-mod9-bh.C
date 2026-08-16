// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// Mod1 9 is a descriptor-only SFPSWAP routing (max into VD).  It is derived
// by emission from which SET reaches the store; the source form is pinned as
// a refusal so no pipeline can be handed the descriptor routing directly.

__attribute__((noinline)) void periodic_minmax_swap_mod9 ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto pair = __builtin_rvtt_sfpswap (a, b, 9); // { dg-error "invalid mod1 value" }
  auto result = __builtin_rvtt_sfpselect2 (pair, 0);
  __builtin_rvtt_sfpstore (nullptr, result, 128, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
