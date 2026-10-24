#include "rtl-lane-reset.h"
#include "ambient-walk-mop-template.h"

// An exact scalar fence and a completely programmed, audited NOP MOP preserve
// the caller's physical lane facts. A clean TU alone is not an entry ABI.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-prgm-const -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "entry-ambient all-lanes derived .flags and ROW_MASK proved." 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "ambient-entry-unproven" "rvtt_macro_planner" } }
// Both actual instructions remain; neither is an invented metadata marker.
// { dg-final { scan-assembler-times {(?n)^[ \t]*fence[ \t]*$} 1 } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+25165824} 1 } }

#define TTI(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

__attribute__((noinline)) void reed_tulean_fire ()
{
  program_ambient_nop_mop ();
  RVTT_TEST_LANE_RESET ();
  /* Residency candidate: forces the TU scan in this function's own
     prgm-const execution.  */
  auto x = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto prod = __builtin_rvtt_sfpmul (x, k, 0);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpadd (prod, b, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, x, 96, 0, 0, 0, 7);

  /* Shapes only the TU audit can classify: a scalar template and a MOP
     run word whose content is in template registers.  */
  __asm__ __volatile__ ("fence");
  TTI (0x01800000);		/* MOP */

#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
