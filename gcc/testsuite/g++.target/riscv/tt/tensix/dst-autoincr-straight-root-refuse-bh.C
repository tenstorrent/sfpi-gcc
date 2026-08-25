// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// An externally visible, address-taken straight function has no direct cgraph
// caller in this TU.  It nevertheless executes a fresh three-word slot program
// on every invocation.  Eight removed increments only tie the uniform issue
// plus settlement cost and must refuse, independent of call-graph visibility.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config 8 >= removed 8" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using root_vector_t = __xtt_vector;

static inline void
root_step ()
{
  root_vector_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  root_vector_t y = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

extern "C" __attribute__((noinline, noclone, used, externally_visible)) void
external_root ()
{
  root_step ();
  root_step ();
  root_step ();
  root_step ();
  root_step ();
  root_step ();
  root_step ();
  root_step ();
}

using root_pointer_t = void (*) ();
root_pointer_t exported_root_pointer = &external_root;
