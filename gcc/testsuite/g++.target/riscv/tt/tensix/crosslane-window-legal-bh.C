// the ENABLE_DEST_INDEX write-restriction erratum positive controls: (a) the exempt opcodes (SFPLOADI, the
// indexed SFPSWAP, SFPTRANSP) writing the companion bank inside the
// window are legal; (b) the same non-exempt computation OUTSIDE the
// window is legal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

void window_content_legal ()
{
  auto k0 = __builtin_rvtt_sfpreadlreg (0);
  auto k1 = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  auto p0 = __builtin_rvtt_sfploadi (nullptr, 0x11, 0, 0, 4);
  auto p1 = __builtin_rvtt_sfploadi (nullptr, 0x22, 0, 0, 4);
  auto r = __builtin_rvtt_sfpswap_indexed (k0, k1, p0, p1, 1);
  auto ka = __builtin_rvtt_sfpselect4 (r, 0);
  auto pa = __builtin_rvtt_sfpselect4 (r, 2);
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
  auto y = __builtin_rvtt_sfpand (pa, pa);
  __builtin_rvtt_sfpwritelreg (y, 5);	// window closed: legal
  __builtin_rvtt_sfpwritelreg (ka, 1);
}

// { dg-final { scan-assembler {SFPCONFIG\t15, 4, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 0, 1} } }
