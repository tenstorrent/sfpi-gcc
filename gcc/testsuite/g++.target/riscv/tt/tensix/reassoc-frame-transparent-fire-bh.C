// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_reassoc" }
// R2 window fire: the accumulation chain lives inside an enclosing
// structured region (whose outermost pushc/popc the rvtt_cc pass
// rewrites to an ambient SETCC and a trailing all-lanes reset, both
// OUTSIDE the chain window), and a BALANCED NESTED frame -- a
// surviving plain-PUSHC/plain-POPC pair with a narrowing refinement
// and a predicated consumer -- sits BETWEEN the links.  Historically
// any CC event in the window refused the rebalance by name; under the
// stage-B flag the CC-region tree proves the frame is strictly inside
// the window's own frame (its popc restores the saved enable state,
// so the mask at every link and at the rewrite point is identical)
// and the licensed rebalance fires.  The FP license keys are
// unchanged (both still required).
// { dg-final { scan-tree-dump "licensed rebalance" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "reassoc-cc-region-boundary" "rvtt_reassoc" } }

void
ra_frame_transparent (void)
{
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  /* Enclosing structured region: rvtt_cc removes this outermost pushc
     and turns its popc into the canonical all-lanes reset -- both
     land OUTSIDE the chain window below.  */
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (x0, 0);
  auto s1 = __builtin_rvtt_sfpadd (x0, x1, 0);
  auto s2 = __builtin_rvtt_sfpadd (s1, x2, 0);
  /* The nested frame between the links: survives rvtt_cc, carries a
     narrowing refinement and a predicated live-value merge.  */
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (x1, 0);
  auto zz = __builtin_rvtt_sfpassign_lv (x2, x1);
  __builtin_rvtt_sfpstore (nullptr, zz, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfppopc (0);
  auto s3 = __builtin_rvtt_sfpadd (s2, x3, 0);
  __builtin_rvtt_sfpstore (nullptr, s3, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfppopc (0);
}
