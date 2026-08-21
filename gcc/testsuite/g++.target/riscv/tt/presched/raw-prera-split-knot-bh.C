// Barrier-crossing knot, fail closed with a committed monotone
// improvement: distinct-signature sub-regions around a mid-stream Dst
// load are each scheduled -- the first improves 10 -> 9 (its best;
// nine values are pinned live across the barrier), the second refuses
// at its live-in floor of 9 -- and the named pressure refusal is
// preserved: no reorder within a region can cross a dst-access
// barrier, and the pass never pretends otherwise.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=8 pressure 10 -> 9 makespan 8 -> 8 model-peak=9 candidate=ecc target=bh" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule refused: no joint pressure/makespan improvement in bb \\d+ region at uid=\\d+ \\(pressure 9 -> ecc 9 / model 9; makespan 9 -> ecc 9 / model 9\\)" "rvtt_lp_schedule_prera" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void bait_dst_knot (void)
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
  auto s3 = __builtin_rvtt_sfpxor (s2, s0);
  auto out = __builtin_rvtt_sfpxor (s3, g);
  __builtin_rvtt_sfpstore (nullptr, out, 192, 0, 0, 4, 7);
}
