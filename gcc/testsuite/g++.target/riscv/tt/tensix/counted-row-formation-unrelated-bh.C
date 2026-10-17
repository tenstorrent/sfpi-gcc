// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Check that an aperiodic chain with no clone family forms no counted row.
// { dg-final { scan-rtl-dump-not "counted-row candidate" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Canonicalized" "rvtt_replay" } }
void unrelated_chain ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpadd (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c11, 0, 0, 0);
  x = __builtin_rvtt_sfpadd (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
