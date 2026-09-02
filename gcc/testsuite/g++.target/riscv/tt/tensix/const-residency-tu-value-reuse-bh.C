// TU value-identical PRGM reuse: the TU's own init function
// claims L12..L14, but every write to L12 stores the SAME 32-bit value
// the loop candidate materializes, so the candidate reuses the claimed
// register.  Soundness is value idempotence, not ordering: every write
// anywhere stores that value, and the candidate's own all-lanes
// programming (still emitted) puts it in every lane; an interleaved
// lane-predicated write of the same value preserves it -- no
// cross-function ordering or dominance proof is used or needed.  The
// near-miss twin materializes a value NO TU write stores: with all
// three registers claimed it refuses prgm-exhausted and the loop keeps
// its materialization.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3f317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L12 for constant 0x3f317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted" 1 "rvtt_prgm_const" } }
// The init's three SFPCONFIGs plus the reusing kernel's one.
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }

void owner_init (void)
{
  auto ln2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (ln2, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 14);
}

void kernel_reuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void kernel_no_match (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      y = __builtin_rvtt_sfpmul (y, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (y, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (y, 1);
}
