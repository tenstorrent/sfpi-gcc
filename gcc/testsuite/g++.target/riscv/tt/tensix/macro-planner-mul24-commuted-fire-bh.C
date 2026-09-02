// Derived-template SFPMUL24 commuted-operand admission: both audited
// mods are symmetric in VA/VB (the reference simulator sfpmul24_result; SFPMUL24.md),
// so an in-place product the register allocator tied onto the VA-side
// factor is realizable through the commuted word — the launch VD
// supplies that factor through the VB:=VD route at execution and the
// OTHER factor's register is named in the template's VA field.  Here
// the product is written args-(b, a) with its destination on b's web;
// a stays live in the shift chain below, so the allocator cannot
// coalesce the product onto a.  Before the commuted admission this
// product was outside the admitted class (the whole row refused
// derivation); with it the row forms and the derived MUL24 word names
// a's register through the VA field.  The two shift words between the
// product and its accumulate are load-bearing slack: they hold the
// explicit consumer far enough past the MAD's two-cycle latency for
// the derived-calendar visibility deadline.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: derived-calendar events=5 staging=none drain=2 kind-mask=0x3" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=3 seq=2 misc=0x00000310 setc16=3 launches=2 drain=2 planned-lregs=0x7 prefix=all-lanes" 1 "rvtt_macro_planner" } }
// The commuted product's derived word: SFPMUL24 LOWER, VC pinned to the
// zero constant L9, the surviving factor named through the VA field
// (L1), VB supplied by the launch VD at execution.
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x980109d0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The hosted product vanishes into the calendar; the accumulate, one
// slack shift, and one cast remain the row's explicit issues.
// { dg-final { scan-assembler-not "SFPMUL24" } }
// Adjudicated fact: this row's launches are fixed-VD value
// carriers (drain=2), so the inter-row drain applies -- the old
// back-to-back expectation pinned a stream whose next-row events land
// ON the pending-writeback cycle (a staged-event same-cycle race by
// the established retire-before-issue model; the boundary proof
// refuses it).  With the per-row drain the eight rows become uniform
// and the always-on replay former folds the explicit tail: one
// execute-while-record copy plus seven playbacks -- the launch words
// still issue once per row.
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPIADD" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 2 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, \[0-9\]+, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, \[0-9\]+, 0, 0" 7 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467618816" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2470756416" 8 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      b = __builtin_rvtt_sfpmul24 (b, a, 0);                                  \
      auto t = __builtin_rvtt_sfpshft_i (nullptr, a, 1, 0, 0, 0);             \
      t = __builtin_rvtt_sfpshft_i (nullptr, t, 2, 0, 0, 0);                  \
      b = __builtin_rvtt_sfpiadd_v (b, t, 4);                                 \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      __builtin_rvtt_sfpstore (nullptr, b, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void commuted_product_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  commuted_product_rows ();
  return 0;
}
