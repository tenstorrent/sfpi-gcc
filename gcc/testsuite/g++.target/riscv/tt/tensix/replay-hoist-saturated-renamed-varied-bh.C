// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 6 exec surplus 2262 >= record delivery 738" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -1038 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }

// Check that a varied saturated sibling-launch body hides its record delivery and refuses the hoist.
void renamed_saturated_kernel_variant ()
{
  auto v = __builtin_rvtt_sfpreadlreg (1);
  auto w = __builtin_rvtt_sfpreadlreg (2);
  auto p = __builtin_rvtt_sfpreadlreg (3);
  auto q = __builtin_rvtt_sfpreadlreg (4);
  auto r = __builtin_rvtt_sfpreadlreg (5);
  for (unsigned trip = 0; trip != 7; ++trip)
    {
      v = __builtin_rvtt_sfpadd (v, v, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      p = __builtin_rvtt_sfpadd (p, p, 0);
      q = __builtin_rvtt_sfpadd (q, q, 0);
      r = __builtin_rvtt_sfpadd (r, r, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      p = __builtin_rvtt_sfpadd (p, p, 0);
      q = __builtin_rvtt_sfpadd (q, q, 0);
      r = __builtin_rvtt_sfpadd (r, r, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      p = __builtin_rvtt_sfpadd (p, p, 0);
      q = __builtin_rvtt_sfpadd (q, q, 0);
      r = __builtin_rvtt_sfpadd (r, r, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      p = __builtin_rvtt_sfpadd (p, p, 0);
      q = __builtin_rvtt_sfpadd (q, q, 0);
      r = __builtin_rvtt_sfpadd (r, r, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      p = __builtin_rvtt_sfpadd (p, p, 0);
      q = __builtin_rvtt_sfpadd (q, q, 0);
      r = __builtin_rvtt_sfpadd (r, r, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      p = __builtin_rvtt_sfpadd (p, p, 0);
      q = __builtin_rvtt_sfpadd (q, q, 0);
      r = __builtin_rvtt_sfpadd (r, r, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (v, 1);
  __builtin_rvtt_sfpwritelreg (w, 2);
  __builtin_rvtt_sfpwritelreg (p, 3);
  __builtin_rvtt_sfpwritelreg (q, 4);
  __builtin_rvtt_sfpwritelreg (r, 5);
}
