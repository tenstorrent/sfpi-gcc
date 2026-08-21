// Isomorphic sub-regions defer by name: a Dst load parked mid-stream
// splits the ten-live knot into two regions with IDENTICAL insn-code
// signatures, which defer to replay capture formation (row isomorphism
// is the post-allocation re-rolls' contract), and the named pressure
// refusal is preserved -- fail closed, never a partial guess.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump-times "Prera-pressure-schedule deferred: repeated-row shape at uid=\\d+ in bb \\d+ \\(replay capture formation owns row isomorphism\\)" 2 "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump-not "Prera-pressure-schedule: bb" "rvtt_lp_schedule_prera" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void bait_dst_split (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);
  auto c = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
  auto d = __builtin_rvtt_sfpload (nullptr, 6, 0, 0, 4, 7);
  auto e = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  auto u0 = __builtin_rvtt_sfpxor (a, b);
  auto u1 = __builtin_rvtt_sfpxor (b, c);
  auto u2 = __builtin_rvtt_sfpxor (c, d);
  auto u3 = __builtin_rvtt_sfpxor (d, e);
  auto u4 = __builtin_rvtt_sfpxor (e, a);
  auto u5 = __builtin_rvtt_sfpxor (a, c);
  auto u6 = __builtin_rvtt_sfpxor (b, d);
  auto u7 = __builtin_rvtt_sfpxor (c, e);
  auto g = __builtin_rvtt_sfpload (nullptr, 10, 0, 0, 4, 7);
  auto r0 = __builtin_rvtt_sfpxor (u0, u1);
  auto r1 = __builtin_rvtt_sfpxor (u2, u3);
  auto r2 = __builtin_rvtt_sfpxor (u4, u5);
  auto r3 = __builtin_rvtt_sfpxor (u6, u7);
  auto s0 = __builtin_rvtt_sfpxor (r0, r1);
  auto s1 = __builtin_rvtt_sfpxor (r2, r3);
  auto s2 = __builtin_rvtt_sfpxor (s0, s1);
  auto out = __builtin_rvtt_sfpxor (s2, g);
  __builtin_rvtt_sfpstore (nullptr, out, 192, 0, 0, 4, 7);
}
