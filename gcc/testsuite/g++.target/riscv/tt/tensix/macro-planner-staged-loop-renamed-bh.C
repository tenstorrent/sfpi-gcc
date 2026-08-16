// Renamed-equivalent proof (WP8 genericity self-check): identical
// dataflow under different function and value names forms the identical
// descriptor and launch words.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#if __riscv_xtttensixwh
constexpr unsigned keep = 3;
#else
constexpr unsigned keep = 7;
#endif

__attribute__((noinline)) void completely_unrelated_name (unsigned twilight)
{
  for (unsigned zebra = 0; zebra < twilight; ++zebra)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto aardvark = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, keep);
      auto banana = __builtin_rvtt_sfpshft_i (nullptr, aardvark, -31, 0, 0, 0);
      auto cactus = __builtin_rvtt_sfpcast (banana, 0);
      __builtin_rvtt_sfpstore (nullptr, cactus, 0, 0, 0, 0, keep);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
