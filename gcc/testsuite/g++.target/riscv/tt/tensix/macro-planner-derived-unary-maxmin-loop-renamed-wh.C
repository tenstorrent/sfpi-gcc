// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// WH renamed/varied twin of the derived unary max/min loop: different
// function name, a different constant register (L11), a different Dst
// address (64), the opposite operand layout, and the other CPU.  The
// derived calendar's stride absorption WAS a per-CPU refusal here
// (derived-stride-absorption-unproven): the earlier WH-formed
// absorbed-stride calendar returned position-shuffled tiles after the
// first while every latched launch dst_row/mask was correct.  The WH
// dst-autoincr adjudication (sfpi-gcc 2a0ba1e6602;
// hardware-adjudicated) convicted the DUAL-SLOT SETC16 program --
// its base-0 bank words clobbered LLK's live ADDR_MOD_2 and corrupted
// the NEXT tile's datacopy -- so the machinery was wrong, not
// unproven.  With the corrected single-slot Base=1 program the WH
// derived calendar FORMS, mirroring the BH twin
// (macro-planner-derived-unary-maxmin-loop-bh.C).
//
// Word derivation (launch: 0x93<<24 | lreg_ind<<20 | mod0<<16
// | addr_mode<<14 | address; WH no-inc mode 3, Dst+=2 auto-inc mode 2,
// address 64):
//   value carrier   macro=0 vd=0/1  0x9300c040/0x9310c040
//                   = 2466299968 / 2467348544 (4 each, alternating)
//   store carrier   macro=1 vd=2    0x93608040 = 2472575040 (8; the
//                   absorbed Dst += 2 stride rides this launch's
//                   auto-increment mode)
//   SETC16 (single Base=1 slot, phys 6): 0xb2130000/0xb21d0002/
//                   0xb2360000 = 2987589632/2988244994/2989883392
// { dg-final { scan-rtl-dump "sequence-derivation-hazard" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "derived-stride-absorption-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor: derived-calendar events=2 staging=copy drain=3 kind-mask=0x0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor: templates=2 seq=2 misc=0x00000020 setc16=3 launches=2 drain=3 planned-lregs=0x7 prefix=all-lanes" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=0: 0x92000bc1" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=1: 0x940000d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=4: 0x00d50084" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=5: 0x53000000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=8: 0x00000020" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-launch: macro=0 vd=0 word=0x9300c040 alt-vd=1 alt-word=0x9310c040" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-launch: macro=1 vd=2 word=0x93608040" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987065344" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987982850" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2989621248" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466299968" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348544" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2472575040" 8 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "SFPLOAD\\t" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

__attribute__((noinline)) void pelican_floor_rows ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto v = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 3);
      auto cst = __builtin_rvtt_sfpreadlreg (11);
      auto pair = __builtin_rvtt_sfpswap (v, cst, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 64, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
