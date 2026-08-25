// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// A straight noinline callee repays eight removed row steps with a fresh
// three-SETC16 program on every call.  Configuration issue occupancy plus
// the required settlement window reaches the same cost, so the conservative
// profitability rule refuses without depending on the function's name or on
// its caller's particular loop shape.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config 8 >= removed 8" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using packet_t = __xtt_vector;

static inline void
step_packet ()
{
  packet_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  packet_t y = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

__attribute__((noinline, noclone)) void
process_packet ()
{
  step_packet ();
  step_packet ();
  step_packet ();
  step_packet ();
  step_packet ();
  step_packet ();
  step_packet ();
  step_packet ();
}

void
repeat_packet ()
{
  for (unsigned i = 0; i != 4; ++i)
    process_packet ();
}
