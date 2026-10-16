// Check that the entry-ambient walk derives through raw `.ttinsn' words that decode as CC-inert.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 3 "rvtt_macro_planner" } }
// (the empty-asm barrier is admitted without a decoded word)
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk: derived through opaque init" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 6 "rvtt_macro_planner" } }

// Near misses: a raw SFPSETCC, a non-canonical SFPENCC word, and a
// LaneConfig SFPCONFIG with imm16 != 0 (can set ROW_MASK).
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
  /* Typical LLK init words: each decodes CC-inert.  */
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
  /* All-lanes-establishing words are transparent, never kills: the
     canonical all-lanes SFPENCC and the LaneConfig default-reset.  */
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
