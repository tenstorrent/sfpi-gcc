// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// Varied WH twin, opposite swap sense: constant-in-VC max
// (swap (cst, v) + select2 (pair, 1) -> template word 0x92000bc9, the
// mod-9 routing flip of the renamed twin's 0x92000bc1), a different
// Dst address (32).  Forms on WH with the corrected single-slot Base=1
// SETC16 program (sfpi-gcc 2a0ba1e6602 -- the per-CPU absorption
// refusal is discharged; see
// macro-planner-derived-unary-maxmin-loop-renamed-wh.C).
//
// Launch words (0x93<<24 | lreg_ind<<20 | mod0<<16 | addr_mode<<14
// | address; address 32): value macro=0 vd=0/1 0x9300c020/0x9310c020 =
// 2466299936/2467348512; store macro=1 vd=2 auto-inc 0x93608020 =
// 2472575008.
// { dg-final { scan-rtl-dump "descriptor: derived-calendar events=2 staging=copy drain=3 kind-mask=0x0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor: templates=2 seq=2 misc=0x00000020 setc16=3 launches=2 drain=3 planned-lregs=0x7 prefix=all-lanes" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=0: 0x92000bc9" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=4: 0x00d50084" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=5: 0x53000000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987065344" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987982850" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2989621248" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466299936" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348512" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2472575008" 8 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

__attribute__((noinline)) void heron_ceiling_rows ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto v = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 0, 3);
      auto cst = __builtin_rvtt_sfpreadlreg (11);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 32, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
