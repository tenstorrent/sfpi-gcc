// A shortened one-word materialization cannot profit from LOOP-class
// residency: its resident read is itself one SFPMOV issue, while staging and
// SFPCONFIG add entry-edge work.  The varied twin prevents value/name matching.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "no-net-loop-issue-saving: 1-word materialization requires a 1-word resident read" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM .*loop class" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void single_word_store_refuses ()
{
  for (unsigned row = 0; row != 32; ++row)
    {
      auto fraction = __builtin_rvtt_sfpxloadi (nullptr, 0x3f400000, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fraction, 0, 0, 0, 0, 7);
    }
}

void renamed_varied_store_refuses ()
{
  for (unsigned stripe = 0; stripe != 19; ++stripe)
    {
      auto minus_two = __builtin_rvtt_sfpxloadi (nullptr, 0xc0000000, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, minus_two, 0, 0, 0, 0, 7);
    }
}
