/* A runtime immediate needs an instruction buffer to hold the synthesised
   instruction word, so these dynamic subjects provide one.  */
namespace ckernel { volatile unsigned long *instrn_buffer; }
// Check that an SFPIADD with a runtime immediate refuses as an opaque row and keeps the explicit sequence.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner refusal: row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPIADD" 8 } }

__attribute__((noinline)) void runtime_imm_rows (int k)
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpiadd_i (ckernel::instrn_buffer, a, k, 0, 0, 4);                  \
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

int main ()
{
  runtime_imm_rows (3);
  return 0;
}
