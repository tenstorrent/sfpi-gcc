// Transpose involution (X4): SFPTRANSP o SFPTRANSP is the
// identity on both banks (SFPTRANSP.md functional model; FB battery
// transpose inverse); an adjacent frame pair whose second frame's
// eight inputs are the first's eight outputs cancels entirely, under
// the proven all-lanes state (mixed-enable transposes are not
// involutions).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

void there_and_back ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  auto v0 = __builtin_rvtt_sfpreadlreg (0);
  auto v1 = __builtin_rvtt_sfpreadlreg (1);
  auto v2 = __builtin_rvtt_sfpreadlreg (2);
  auto v3 = __builtin_rvtt_sfpreadlreg (3);
  auto c0 = __builtin_rvtt_sfploadi (nullptr, 0x11, 0, 0, 4);
  auto c1 = __builtin_rvtt_sfploadi (nullptr, 0x22, 0, 0, 4);
  auto c2 = __builtin_rvtt_sfploadi (nullptr, 0x33, 0, 0, 4);
  auto c3 = __builtin_rvtt_sfploadi (nullptr, 0x44, 0, 0, 4);
  auto r1 = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, c0, c1, c2, c3);
  auto o0 = __builtin_rvtt_sfpselect4 (r1, 0);
  auto o1 = __builtin_rvtt_sfpselect4 (r1, 1);
  auto o2 = __builtin_rvtt_sfpselect4 (r1, 2);
  auto o3 = __builtin_rvtt_sfpselect4 (r1, 3);
  auto q0 = __builtin_rvtt_sfpreadlreg (4);
  auto q1 = __builtin_rvtt_sfpreadlreg (5);
  auto q2 = __builtin_rvtt_sfpreadlreg (6);
  auto q3 = __builtin_rvtt_sfpreadlreg (7);
  auto r2 = __builtin_rvtt_sfptransp8 (o0, o1, o2, o3, q0, q1, q2, q3);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 1), 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 2), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 3), 3);
}

// { dg-final { scan-tree-dump "transp8 involution cancel" "rvtt_crosslane" } }
// { dg-final { scan-assembler-not {SFPTRANSP} } }
