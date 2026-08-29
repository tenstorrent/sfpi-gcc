// Audited-TU walk transparency, per-word arm (lane IV, typecast
// recovery): the entry-ambient walk DERIVES through raw `.ttinsn'
// constant words by decoding them against the audited lane-enable
// table (rvtt_raw_cc_word_class) instead of refusing on asm shape.
// The fire functions carry the production LLK-init anatomy ahead of a
// marker-free formable loop: thread-config SETC16, sync STALLWAIT,
// SFPCONFIG to a non-LaneConfig destination, the SFPU load-macro
// template-capture words (SFPCAST VD=12 / SFPSTOCHRND VD=13 -- the
// exact typecast blockers), an SFPLOADI, the audited LaneConfig
// default-reset, and the word-exact canonical all-lanes SFPENCC
// (transparent, NEVER a kill -- record-window swallowing).  An empty
// asm is a pure barrier.  No TU audit runs here (no prgm-const flag):
// every fire is carried by per-word decode alone.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 3 "rvtt_macro_planner" } }
// (heron's pure barrier admits silently -- no decoded words, no lean)
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk: derived through opaque init" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 6 "rvtt_macro_planner" } }

// NEAR-MISSES (each MUST stay dirty; fail-closed derivation):
// 1. a real CC writer -- a raw SFPSETCC word -- ahead of the loop;
// 2. a NON-canonical SFPENCC word (lanes-relevant fields differ from
//    the word-exact all-lanes encoding);
// 3. an SFPCONFIG LaneConfig write outside the audited default-reset
//    class (imm16 != 0 can set ROW_MASK).
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing \\(ambient-entry-unproven\\)" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk dirty: insn \\d+ bb \\d+ \\(tu-audit-not-run\\) word=0x7b000001" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk dirty: insn \\d+ bb \\d+ \\(tu-audit-not-run\\) word=0x8a003008" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk dirty: insn \\d+ bb \\d+ \\(tu-audit-not-run\\) word=0x910100f1" 1 "rvtt_macro_planner" } }

#define TTI(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

static inline void formable_loop (int base)
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, base, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, base, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void wren_walk_llk_init_fire ()
{
  /* The typecast LLK-init anatomy: every word decodes CC-inert.  */
  TTI (0xb20f0000);		/* SETC16 (thread config) */
  TTI (0xa2800010);		/* STALLWAIT (sync family) */
  TTI (0x91000040);		/* SFPCONFIG dest 4 (LoadMacroConfig) */
  TTI (0x900000c0);		/* SFPCAST VD=12: template capture */
  TTI (0x8e0000d1);		/* SFPSTOCHRND VD=13: template capture */
  TTI (0x710a0004);		/* SFPLOADI */
  formable_loop (0);
}

__attribute__((noinline)) void sable_walk_encc_reset_fire ()
{
  /* Ambient-ESTABLISHING words are transparent, never kills: the
     canonical all-lanes SFPENCC and the audited LaneConfig
     default-reset.  */
  TTI (0x8a00300a);		/* word-exact canonical all-lanes SFPENCC */
  TTI (0x910000f1);		/* SFPCONFIG LaneConfig default-reset */
  formable_loop (32);
}

__attribute__((noinline)) void heron_walk_barrier_fire ()
{
  __asm__ __volatile__ ("" ::: "memory");	/* pure barrier */
  formable_loop (64);
}

__attribute__((noinline)) void marsh_walk_ccwriter_refuse ()
{
  TTI (0xb20f0000);		/* inert neighbour: dirt is the SETCC */
  TTI (0x7b000001);		/* SFPSETCC: a real CC writer */
  formable_loop (0);
}

__attribute__((noinline)) void tarn_walk_encc_nonword_refuse ()
{
  TTI (0x8a003008);		/* SFPENCC, NOT the canonical word */
  formable_loop (0);
}

__attribute__((noinline)) void fjord_walk_laneconfig_refuse ()
{
  TTI (0x910100f1);		/* SFPCONFIG dest 15, imm16 != 0 */
  formable_loop (0);
}
