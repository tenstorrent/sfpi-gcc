// Outward ownership refusals: the formed MOP template survives the
// callee's return in thread-shared, RISC-write-only registers, so a
// caller that launches its own (type-1, hoisted-program) template
// after calling the formed function would expand OUR template -- the
// deterministic silicon hang of the 2026-08-17 minmax perf-shape
// adjudication.  All three shapes here refuse by name and leave the
// delivery byte-identical (force is on: every refusal is structural):
// - direct: the caller launches per iteration around the call with its
//   template programming hoisted out of the loop (the perf harness
//   shape);
// - transitive: the same hazard one call level up;
// - partial re-arm: the caller rewrites only the A0 word between the
//   call and its launch -- not the full clobbered set.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-caller-template-live-unproven\\)" 3 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "is reachable after a call to this function without a full template re-arm" 3 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP\\t" } }

#define MOP_TYPE1_WORD 0x01800000 // raw MOP, mop_type 1, counts from cfg

__attribute__ ((noinline, noclone)) static void
formed_callee_direct ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
}

void
wrapper_hazard (unsigned n)
{
  // Caller template programmed ONCE, launched per iteration (the
  // hoisted program-once perf-harness shape).
  volatile unsigned *mop_cfg = (volatile unsigned *) 0xFFB80000;
  for (unsigned i = 0; i != 9; ++i)
    mop_cfg[i] = 0x02000000;
  for (unsigned i = 0; i != n; ++i)
    {
      asm volatile (".ttinsn %0" :: "i" (MOP_TYPE1_WORD));
      formed_callee_direct ();
    }
}

__attribute__ ((noinline, noclone)) static void
formed_callee_deep ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}

__attribute__ ((noinline, noclone)) static void
middle_layer ()
{
  formed_callee_deep ();
}

void
wrapper_hazard_transitive (unsigned n)
{
  for (unsigned i = 0; i != n; ++i)
    {
      asm volatile (".ttinsn %0" :: "i" (MOP_TYPE1_WORD));
      middle_layer ();
    }
}

__attribute__ ((noinline, noclone)) static void
formed_callee_partial ()
{
  // One extra launch: a distinct body, or IPA-ICF folds this into
  // formed_callee_deep and the two shapes share one refusal.
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}

void
wrapper_partial_rearm (unsigned n)
{
  volatile unsigned *mop_cfg = (volatile unsigned *) 0xFFB80000;
  for (unsigned i = 0; i != n; ++i)
    {
      formed_callee_partial ();
      mop_cfg[3] = 0x02000000; // A0 only: flags/step slots stay clobbered
      asm volatile (".ttinsn %0" :: "i" (MOP_TYPE1_WORD));
    }
}
