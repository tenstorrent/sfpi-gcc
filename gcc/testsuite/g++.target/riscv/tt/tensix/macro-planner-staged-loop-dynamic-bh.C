// Encodability direction (re-adjudicated for the derived calendar): a run-time
// shift amount cannot pack the template imm12 field, so the SHIFT has
// no template realization and stays an EXPLICIT issue -- while the
// provable remainder of the row (the store-producer cast through
// LReg16 and the delayed store) still forms, with the configuration
// prefix hoisted to the loop preheader.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor: templates=1 seq=2 misc=0x00000120 setc16=3 launches=2 drain=1" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x900000c0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=1 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPSHFT" 1 } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

__attribute__((noinline)) void staged_loop_dynamic (unsigned iterations,
						    unsigned amount)
{
  for (unsigned row = 0; row < iterations; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					    no_increment);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, amount,
					       0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0,
			       no_increment);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
