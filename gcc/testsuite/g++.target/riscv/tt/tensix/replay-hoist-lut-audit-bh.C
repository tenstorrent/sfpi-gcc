// SFPLUT effect/latency audit (lane DL; rvtt-cost.md D3-follow-up row):
// SFPLUT carries audited effects for mod0 in {0, SGN_RETAIN=4} (reads
// LReg[0..2] + the tied LReg[3] destination, lane-predicated write, no
// lane-flag effect, MAD-unit result latency 1 "as per SFPMAD"), so a
// counted LUT payload is priceable.  Before this audit the identical
// body refused as effect-opaque ("reissue-unproved: payload insn ... is
// effect-opaque") -- the blocker for the whole LUT corpus family.  The
// second function varies names, trip count, and uses the SGN_RETAIN
// mod within the audited envelope.  The third uses SFPLUTFP32, whose
// audit is DELIBERATELY deferred (rvtt-cost.md: the Mod1/Mod1Mirror
// scheduling split and per-mode register envelopes need their own
// audit): it keeps refusing effect-opaque by name.  SFPLUT's
// INDIRECT_VD mod is unreachable here -- the builtin checker rejects
// it by name at the front end ("invalid mod1 value"); the rvtt.md
// refusing default remains as defense in depth.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoist pricing .loop \\d+.: trips 32, words 8, exec_ilk 8 slots, deliver_body 984, deliver_record 1107, record 1407, before 984, after 870, benefit 2241 .min 60." 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoist pricing .loop \\d+.: trips 24, words 8, exec_ilk 8 slots, deliver_body 984, deliver_record 1107, record 1407, before 984, after 870, benefit 1329 .min 60." 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "reissue-unproved: payload insn \\d+ is effect-opaque" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 58 } }

void lut_fire ()
{
  auto c0 = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto x = __builtin_rvtt_sfpreadlreg (3);
  auto d = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfplut (c0, c1, c2, x, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplut (c0, c1, c2, x, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplut (c0, c1, c2, x, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplut (c0, c1, c2, x, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 3);
  __builtin_rvtt_sfpwritelreg (d, 4);
}

void lut_fire_retain ()
{
  auto k0 = __builtin_rvtt_sfpreadlreg (0);
  auto k1 = __builtin_rvtt_sfpreadlreg (1);
  auto k2 = __builtin_rvtt_sfpreadlreg (2);
  auto y = __builtin_rvtt_sfpreadlreg (3);
  auto e = __builtin_rvtt_sfpreadlreg (5);
  for (unsigned trip = 0; trip != 24; ++trip)
    {
      y = __builtin_rvtt_sfplut (k0, k1, k2, y, 4);
      e = __builtin_rvtt_sfpmul (e, e, 0);
      y = __builtin_rvtt_sfplut (k0, k1, k2, y, 4);
      e = __builtin_rvtt_sfpmul (e, e, 0);
      y = __builtin_rvtt_sfplut (k0, k1, k2, y, 4);
      e = __builtin_rvtt_sfpmul (e, e, 0);
      y = __builtin_rvtt_sfplut (k0, k1, k2, y, 4);
      e = __builtin_rvtt_sfpmul (e, e, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 3);
  __builtin_rvtt_sfpwritelreg (e, 5);
}

void lutfp32_still_refuses ()
{
  auto c0 = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto x = __builtin_rvtt_sfpreadlreg (3);
  auto d = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      // SFPLUTFP32: audit deliberately deferred -- the loop stays
      // effect-opaque by name.
      x = __builtin_rvtt_sfplutfp32_3r (c0, c1, c2, x, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplutfp32_3r (c0, c1, c2, x, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 3);
  __builtin_rvtt_sfpwritelreg (d, 4);
}
