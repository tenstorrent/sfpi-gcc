// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-hoisted-prgm-reuse -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-stall-words -mtt-tensix-optimize-crossrow-shared-reload -fdump-rtl-rvtt_schedule-details" }
// Shared-reload RENAMED-EQUIVALENT / VARIED-CONSTANTS adversary twin
// (lane IP audit, GY recipe): the xrow-sharedreload-fire structure
// with every identifier renamed and EVERY coefficient replaced by
// arbitrary non-tanh values (owner-init constants and both in-loop
// reload constants).  The dedupe must key on the structural facts
// alone (both pairing halves materializing the same values through
// the same reload register in identical definition groups) -- if it
// only fired on the production tanh coefficients, this twin would
// catch the fingerprint.
// { dg-final { scan-rtl-dump {Crossrow shared-reload: reg \d+ epochs=2 removed=4 II \d+ -> \d+} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump {Crossrow pairing: bb \d+ rows=2} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=32->16" "rvtt_schedule" } }

void audit_ip_owner_init (void)
{
  auto w6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3c23d70a, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (w6, 12);
  auto w5 = __builtin_rvtt_sfpxloadi (nullptr, 0xbd4ccccd, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (w5, 13);
  auto w4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e6b851f, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (w4, 14);
}

void audit_ip_poly_row (void)
{
  auto w6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3c23d70a, 0, 0, 31);
  auto w5 = __builtin_rvtt_sfpxloadi (nullptr, 0xbd4ccccd, 0, 0, 31);
  auto w4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e6b851f, 0, 0, 31);
  auto w2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d0f5c29, 0, 0, 31);
  for (unsigned lane = 0; lane != 32; ++lane)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto p = __builtin_rvtt_sfpmad (w6, a, w5, 0);
      p = __builtin_rvtt_sfpmad (p, a, w4, 0);
      auto w3 = __builtin_rvtt_sfpxloadi (nullptr, 0xbe4a3d71, 0, 0, 31);
      p = __builtin_rvtt_sfpmad (p, a, w3, 0);
      p = __builtin_rvtt_sfpmad (p, a, w2, 0);
      auto w1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f75c28f, 0, 0, 31);
      p = __builtin_rvtt_sfpmad (p, a, w1, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      auto one = __builtin_rvtt_sfpreadlreg (10);
      auto sw = __builtin_rvtt_sfpswap (p, one, 1);
      auto mn = __builtin_rvtt_sfpselect2 (sw, 0);
      auto res = __builtin_rvtt_sfpsetsgn_v (mn, x, 0);
      __builtin_rvtt_sfpstore (nullptr, res, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
