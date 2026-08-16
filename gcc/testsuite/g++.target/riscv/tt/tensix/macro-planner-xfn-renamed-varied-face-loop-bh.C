// Renamed-equivalent + varied-constant proof for the region-scoped
// window (genericity self-check): the same opaque-caller face-loop
// dataflow under unrelated names, with a different round instr_mod1 (2),
// forms through the scoped window and packs the varied operand into the
// round template's field (word low byte 0xd2, loadi 210) -- the field
// derivation, never a name or the value 1, is the capability.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner config-ownership: loop-scoped window" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "SFPLOADI\\tL0, 210, 2" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }

#if __riscv_xtttensixwh
constexpr unsigned hold_still = 3;
#else
constexpr unsigned hold_still = 7;
#endif

#define GLIMMER_ROW()                                                         \
  do                                                                          \
    {                                                                         \
      auto petal = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6,               \
					   hold_still);                       \
      auto ember = __builtin_rvtt_sfpcast (petal, 0);                         \
      auto quartz                                                             \
	= __builtin_rvtt_sfpstochrnd_i (nullptr, ember, 0, 0, 0, 2, 0);       \
      __builtin_rvtt_sfpstore (nullptr, quartz, 0, 0, 0, 2, hold_still);      \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

#define GLIMMER_FACE()                                                        \
  do                                                                          \
    {                                                                         \
      GLIMMER_ROW (); GLIMMER_ROW (); GLIMMER_ROW (); GLIMMER_ROW ();         \
      GLIMMER_ROW (); GLIMMER_ROW (); GLIMMER_ROW (); GLIMMER_ROW ();         \
      __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);                             \
      __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);                             \
    }                                                                         \
  while (0)

volatile unsigned lantern_mmio;

__attribute__((noinline)) void wandering_dune (unsigned tumbleweed)
{
  lantern_mmio = 0x00570000;		/* raw MMIO instruction push */
  asm volatile (".ttinsn\t%0" :: "i" (4));	/* opaque raw issue */
  for (unsigned brook = 0; brook < tumbleweed; ++brook)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      GLIMMER_FACE ();
    }
}
