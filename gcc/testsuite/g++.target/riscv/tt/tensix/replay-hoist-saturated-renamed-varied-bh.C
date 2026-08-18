// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 6 exec surplus 4662 >= record delivery 738" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -1038 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }

// Renamed and varied twin of the execution-saturated sibling shape:
// different operation (add, not mul), different payload length (5),
// different sibling count (6), different trip count (6), different
// increment stride.  Delivery-only benefit 6 * (738 - 500) - 738 = 690
// would fire; the contiguous final-stream launch run of 6 has execution
// surplus 6 * (500 - 123) = 2262 >= record delivery 738, so the record
// delivery is hidden and the modeled benefit degenerates to -738:
// refuse.  Proves the context term keys on the candidate's structure,
// not on any operation identity or constant fingerprint.
void renamed_saturated_kernel_variant ()
{
  auto v = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned trip = 0; trip != 6; ++trip)
    {
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (v, 1);
}
