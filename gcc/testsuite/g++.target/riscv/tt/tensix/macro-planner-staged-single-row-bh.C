// A single straight-line row is DISCOVERED (single-row extension)
// but never formed: one launch cannot amortize the configuration
// prefix, so Layer 6 refuses by name and the bytes stay identical to
// flags-off.  (The quarantined pass formed this shape unconditionally;
// the recorded oracle notes the divergence-by-design.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=1 row-len=4 runs=1 stride=2 loop=no" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "formation-refusal: unprofitable" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSHFT" 1 } }
// { dg-final { scan-assembler-times "SFPCAST" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }

#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

void single_row ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, no_increment);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, no_increment);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
