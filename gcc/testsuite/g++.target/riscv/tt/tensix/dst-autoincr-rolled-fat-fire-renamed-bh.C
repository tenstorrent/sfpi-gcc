// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed-equivalent, varied-constant twin of the fat-body covered fire:
// different names, different trip count, different stride, a longer body
// -- the covering walk keys on the iteration's slot-occupying words,
// never on kernel identity, coefficients, or a particular body length.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 4 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing covered .rows 1, iteration slot words 12 >= drain window 7, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-dominates-rolled-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 1 } }

using lane_vec = __xtt_vector;

void
wide_body_stream (unsigned trips)
{
  for (unsigned row = 0; row != 12; ++row)
    {
      lane_vec src = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      lane_vec acc = __builtin_rvtt_sfpmul (src, src, 0);
      acc = __builtin_rvtt_sfpmul (acc, src, 0);
      acc = __builtin_rvtt_sfpmul (acc, acc, 0);
      acc = __builtin_rvtt_sfpmul (acc, src, 0);
      acc = __builtin_rvtt_sfpmul (acc, acc, 0);
      acc = __builtin_rvtt_sfpmul (acc, src, 0);
      acc = __builtin_rvtt_sfpmul (acc, acc, 0);
      acc = __builtin_rvtt_sfpmul (acc, src, 0);
      __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
}
