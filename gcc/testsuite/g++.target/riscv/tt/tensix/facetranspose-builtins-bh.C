// X6 FPU face-transpose builtins: emission forms on Blackhole (lane FV,
// 2026-08-22).  Each builtin must emit the architectural mnemonic with
// the operands in TTI order; word-identity vs the TT_OP macro encodings
// was proven with the .ttinsn pairwise oracle (laneFV evidence
// gas-probe/).  No LReg staging anywhere: the family is immediate-only.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }

// The topk_xl transpose_dest_face_32b pass-1 head: lo16 Dst rows into
// SrcB rows 16..31, four rows per word.
void movd2b_lo16 ()
{
  __builtin_rvtt_ttmovd2b (1, 16, 7, 2, 0);
}

void movd2b_hi16_varied ()
{
  __builtin_rvtt_ttmovd2b (0, 28, 7, 2, 112);
}

void movb2a_park ()
{
  __builtin_rvtt_ttmovb2a (12, 7, 2, 28);
}

void movb2d_writeback ()
{
  __builtin_rvtt_ttmovb2d (0, 20, 7, 4, 4);
}

void mova2d_lo16_restore ()
{
  __builtin_rvtt_ttmova2d (1, 8, 7, 2, 8);
}

void trnspsrcb ()
{
  __builtin_rvtt_tttrnspsrcb ();
}

// The face-transpose entry stall: block CFG ops until SFPU drained and
// both Src banks owned (p_stall STALL_CFG=0x80, WAIT_SFPU|SRCA_VLD|
// SRCB_VLD = 0x980).
void stallwait_entry ()
{
  __builtin_rvtt_ttstallwait (0x80, 0x980);
}

// Backend-config byte RMW, one builtin per byte lane.
void rmwcib_all_lanes ()
{
  __builtin_rvtt_ttrmwcib (0, 0x01, 0x01, 2);
  __builtin_rvtt_ttrmwcib (1, 0xff, 0x12, 3);
  __builtin_rvtt_ttrmwcib (2, 0x1e, 0x0a, 1);
  __builtin_rvtt_ttrmwcib (3, 0x20, 0x20, 1);
}

// { dg-final { scan-assembler {TTMOVD2B\t1, 16, 7, 2, 0} } }
// { dg-final { scan-assembler {TTMOVD2B\t0, 28, 7, 2, 112} } }
// { dg-final { scan-assembler {TTMOVB2A\t12, 7, 2, 28} } }
// { dg-final { scan-assembler {TTMOVB2D\t0, 20, 7, 4, 4} } }
// { dg-final { scan-assembler {TTMOVA2D\t1, 8, 7, 2, 8} } }
// { dg-final { scan-assembler {TTTRNSPSRCB} } }
// { dg-final { scan-assembler {TTSTALLWAIT\t128, 2432} } }
// { dg-final { scan-assembler {TTRMWCIB0\t1, 1, 2} } }
// { dg-final { scan-assembler {TTRMWCIB1\t255, 18, 3} } }
// { dg-final { scan-assembler {TTRMWCIB2\t30, 10, 1} } }
// { dg-final { scan-assembler {TTRMWCIB3\t32, 32, 1} } }
// { dg-final { scan-assembler-not {SFPLOADI|SFPMOV} } }
