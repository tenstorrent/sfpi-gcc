// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Varied-architecture counterpart to the Blackhole call-boundary refusal:
// seven rows, stride four, a different Dst address, and Wormhole modifier
// encodings.  The verdict depends only on target configuration economics.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config 8 >= removed 7" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 7 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using renamed_vector_t = __xtt_vector;

static inline void
move_segment ()
{
  renamed_vector_t x = __builtin_rvtt_sfpload (nullptr, 3, 0, 0, 0, 3);
  renamed_vector_t y = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (nullptr, y, 3, 0, 0, 0, 3);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

__attribute__((noinline, noclone)) void
translate_segment ()
{
  move_segment ();
  move_segment ();
  move_segment ();
  move_segment ();
  move_segment ();
  move_segment ();
  move_segment ();
}

void
dispatch_segment ()
{
  for (unsigned i = 0; i != 3; ++i)
    translate_segment ();
}
