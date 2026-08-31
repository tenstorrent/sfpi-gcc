// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-loop-carried -fdump-tree-rvtt_reassoc" }
// THE LICENSED LOOP-CARRIED FIRE (FABLE item #8): with BOTH license
// keys (the effective -fassociative-math triple AND
// -mtt-tensix-optimize-reassoc-loop-carried) the 2-link loop-carried
// sfpadd accumulation splits into P=2 round-robin partial
// accumulators: a second header PHI initialized to the +0.0
// constant-register identity (sfpreadlreg 9) and a balanced reduction
// on the exit edge.  The value-change class is re-association order
// only (the ratified reassoc license lineage); the standing
// reassoc-loop-carried-underived refusal is discharged.
// { dg-final { scan-tree-dump-times "licensed loop-carried split P=2 over 2-link" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump "__builtin_rvtt_sfpreadlreg \\(9\\)" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "reassoc-loop-carried-underived" "rvtt_reassoc" } }

void
ra_loop_split_fire (int rows)
{
  auto acc = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, -32);
  for (int i = 0; i != rows; ++i)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      acc = __builtin_rvtt_sfpadd (acc, x, 0);
      acc = __builtin_rvtt_sfpadd (acc, y, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 6, 7);
}
