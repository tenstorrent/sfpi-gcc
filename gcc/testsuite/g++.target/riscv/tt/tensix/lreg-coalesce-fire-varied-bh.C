// Renamed/varied-constant generality twin of lreg-coalesce-fire-bh.C:
// every variable renamed, the fold row and every store row moved, the
// precolored anchor set shifted from L0..L5 to L2..L7 (the heavy chain
// then colors L0/L1 and blocks the same way).  Same verdicts as the
// primary fire twin: the mechanism keys on graph structure, not on
// names, rows, or which six anchors are pinned.  TODAY: 1 spill.
// FUTURE-VERDICT: as lreg-coalesce-fire-bh.C.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-lreg-alloc -mtt-tensix-optimize-lreg-coalesce -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details -fdump-rtl-rvtt_dst_ownership" }
// { dg-final { scan-rtl-dump "1 reload.s. folded" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "lreg-alloc coalesce: merged web r\\d+ into r\\d+ .briggs test" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-times "spilling web" 1 "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 8 } }
// { dg-final { scan-assembler-times {\mSFPSTORE\t} 25 } }

#define KEEP(x, n) __builtin_rvtt_sfpwritelreg ((x), (n))
#define EMIT(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void coalesce_fire_varied (void)
{
  auto seed  = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
  auto blend = __builtin_rvtt_sfpxor (seed, seed);
  auto echo  = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);

  auto ka = __builtin_rvtt_sfpxor (blend, blend);
  auto kb = __builtin_rvtt_sfpxor (ka, ka);
  auto kc = __builtin_rvtt_sfpxor (kb, kb);
  EMIT (echo, 72);

  auto q2 = __builtin_rvtt_sfpreadlreg (2);
  auto q3 = __builtin_rvtt_sfpreadlreg (3);
  auto q4 = __builtin_rvtt_sfpreadlreg (4);
  auto q5 = __builtin_rvtt_sfpreadlreg (5);
  auto q6 = __builtin_rvtt_sfpreadlreg (6);
  auto q7 = __builtin_rvtt_sfpreadlreg (7);

  KEEP (q2, 2); KEEP (q2, 2); KEEP (q2, 2); KEEP (q2, 2); KEEP (q2, 2);
  KEEP (q2, 2); KEEP (q2, 2); KEEP (q2, 2); KEEP (q2, 2); KEEP (q2, 2);
  KEEP (q3, 3); KEEP (q3, 3); KEEP (q3, 3); KEEP (q3, 3); KEEP (q3, 3);
  KEEP (q3, 3); KEEP (q3, 3); KEEP (q3, 3); KEEP (q3, 3); KEEP (q3, 3);
  KEEP (q4, 4); KEEP (q4, 4); KEEP (q4, 4); KEEP (q4, 4); KEEP (q4, 4);
  KEEP (q4, 4); KEEP (q4, 4); KEEP (q4, 4); KEEP (q4, 4); KEEP (q4, 4);
  KEEP (q5, 5); KEEP (q5, 5); KEEP (q5, 5); KEEP (q5, 5); KEEP (q5, 5);
  KEEP (q5, 5); KEEP (q5, 5); KEEP (q5, 5); KEEP (q5, 5); KEEP (q5, 5);
  KEEP (q6, 6); KEEP (q6, 6); KEEP (q6, 6); KEEP (q6, 6); KEEP (q6, 6);
  KEEP (q6, 6); KEEP (q6, 6); KEEP (q6, 6); KEEP (q6, 6); KEEP (q6, 6);
  KEEP (q7, 7); KEEP (q7, 7); KEEP (q7, 7); KEEP (q7, 7); KEEP (q7, 7);
  KEEP (q7, 7); KEEP (q7, 7); KEEP (q7, 7); KEEP (q7, 7); KEEP (q7, 7);

  EMIT (ka, 80); EMIT (ka, 88); EMIT (ka, 96); EMIT (ka, 104);
  EMIT (ka, 112); EMIT (ka, 120); EMIT (ka, 128); EMIT (ka, 136);
  EMIT (kb, 144); EMIT (kb, 152); EMIT (kb, 160); EMIT (kb, 168);
  EMIT (kb, 176); EMIT (kb, 184); EMIT (kb, 192); EMIT (kb, 200);
  EMIT (kc, 208); EMIT (kc, 216); EMIT (kc, 224); EMIT (kc, 232);
  EMIT (kc, 240); EMIT (kc, 248); EMIT (kc, 256);
}
