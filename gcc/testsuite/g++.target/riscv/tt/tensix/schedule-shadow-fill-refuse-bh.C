// Near misses for the generalized shadow fill: a CC-mutating instruction in
// the crossed range is a barrier for a lane-predicated filler, and a
// dependent instruction is no filler at all.  Both bubbles keep their
// SFPNOP and no move fires.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-latency-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Shadow-fill moved" "rvtt_schedule" } }
// { dg-final { scan-assembler "SFPNOP" } }

void cc_write_blocks_crossing ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto r = __builtin_rvtt_sfpswap (p, b, 1);
  auto p1 = __builtin_rvtt_sfpselect2 (r, 0);
  auto b1 = __builtin_rvtt_sfpselect2 (r, 1);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b1, 0);
  __builtin_rvtt_sfppopc (0);
  auto f = __builtin_rvtt_sfpmul (c, c, 0);
  __builtin_rvtt_sfpwritelreg (p1, 0);
  __builtin_rvtt_sfpwritelreg (b1, 1);
  __builtin_rvtt_sfpwritelreg (f, 2);
}

void all_later_work_depends ()
{
  auto a = __builtin_rvtt_sfpreadlreg (4);
  auto b = __builtin_rvtt_sfpreadlreg (5);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto r = __builtin_rvtt_sfpswap (p, b, 1);
  auto p1 = __builtin_rvtt_sfpselect2 (r, 0);
  auto b1 = __builtin_rvtt_sfpselect2 (r, 1);
  auto g = __builtin_rvtt_sfpmul (p1, b1, 0);
  __builtin_rvtt_sfpwritelreg (p1, 4);
  __builtin_rvtt_sfpwritelreg (b1, 5);
  __builtin_rvtt_sfpwritelreg (g, 6);
}
