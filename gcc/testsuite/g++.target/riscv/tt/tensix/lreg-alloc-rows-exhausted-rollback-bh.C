// Transactionality witness: ~80 mutually live values need more
// spills than the 64 scratch-row groups can carry; the allocator
// exhausts rows AFTER mutating, rolls the stream back, and the named
// pressure error reports on the exact pre-allocation stream.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "lreg-spill-no-free-dst" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "rolling back" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "colorability=proven" "rvtt_lp_alloc" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void lreg_alloc_rows_exhausted (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0000u, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001u, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0002u, 0, 0, 31);
  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0003u, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0004u, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0005u, 0, 0, 31);
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0006u, 0, 0, 31);
  auto c7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0007u, 0, 0, 31);
  auto c8 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0008u, 0, 0, 31);
  auto c9 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0009u, 0, 0, 31);
  auto c10 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b000au, 0, 0, 31);
  auto c11 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b000bu, 0, 0, 31);
  auto c12 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b000cu, 0, 0, 31);
  auto c13 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b000du, 0, 0, 31);
  auto c14 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b000eu, 0, 0, 31);
  auto c15 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b000fu, 0, 0, 31);
  auto c16 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0010u, 0, 0, 31);
  auto c17 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0011u, 0, 0, 31);
  auto c18 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0012u, 0, 0, 31);
  auto c19 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0013u, 0, 0, 31);
  auto c20 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0014u, 0, 0, 31);
  auto c21 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0015u, 0, 0, 31);
  auto c22 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0016u, 0, 0, 31);
  auto c23 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0017u, 0, 0, 31);
  auto c24 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0018u, 0, 0, 31);
  auto c25 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0019u, 0, 0, 31);
  auto c26 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b001au, 0, 0, 31);
  auto c27 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b001bu, 0, 0, 31);
  auto c28 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b001cu, 0, 0, 31);
  auto c29 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b001du, 0, 0, 31);
  auto c30 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b001eu, 0, 0, 31);
  auto c31 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b001fu, 0, 0, 31);
  auto c32 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0020u, 0, 0, 31);
  auto c33 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0021u, 0, 0, 31);
  auto c34 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0022u, 0, 0, 31);
  auto c35 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0023u, 0, 0, 31);
  auto c36 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0024u, 0, 0, 31);
  auto c37 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0025u, 0, 0, 31);
  auto c38 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0026u, 0, 0, 31);
  auto c39 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0027u, 0, 0, 31);
  auto c40 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0028u, 0, 0, 31);
  auto c41 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0029u, 0, 0, 31);
  auto c42 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b002au, 0, 0, 31);
  auto c43 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b002bu, 0, 0, 31);
  auto c44 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b002cu, 0, 0, 31);
  auto c45 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b002du, 0, 0, 31);
  auto c46 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b002eu, 0, 0, 31);
  auto c47 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b002fu, 0, 0, 31);
  auto c48 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0030u, 0, 0, 31);
  auto c49 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0031u, 0, 0, 31);
  auto c50 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0032u, 0, 0, 31);
  auto c51 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0033u, 0, 0, 31);
  auto c52 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0034u, 0, 0, 31);
  auto c53 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0035u, 0, 0, 31);
  auto c54 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0036u, 0, 0, 31);
  auto c55 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0037u, 0, 0, 31);
  auto c56 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0038u, 0, 0, 31);
  auto c57 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0039u, 0, 0, 31);
  auto c58 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b003au, 0, 0, 31);
  auto c59 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b003bu, 0, 0, 31);
  auto c60 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b003cu, 0, 0, 31);
  auto c61 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b003du, 0, 0, 31);
  auto c62 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b003eu, 0, 0, 31);
  auto c63 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b003fu, 0, 0, 31);
  auto c64 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0040u, 0, 0, 31);
  auto c65 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0041u, 0, 0, 31);
  auto c66 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0042u, 0, 0, 31);
  auto c67 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0043u, 0, 0, 31);
  auto c68 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0044u, 0, 0, 31);
  auto c69 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0045u, 0, 0, 31);
  auto c70 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0046u, 0, 0, 31);
  auto c71 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0047u, 0, 0, 31);
  auto c72 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0048u, 0, 0, 31);
  auto c73 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0049u, 0, 0, 31);
  auto c74 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b004au, 0, 0, 31);
  auto c75 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b004bu, 0, 0, 31);
  auto c76 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b004cu, 0, 0, 31);
  auto c77 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b004du, 0, 0, 31);
  auto c78 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b004eu, 0, 0, 31);
  auto c79 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b004fu, 0, 0, 31);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      x = __builtin_rvtt_sfpmad (x, c2, c3, 0);
      x = __builtin_rvtt_sfpmad (x, c4, c5, 0);
      x = __builtin_rvtt_sfpmad (x, c6, c7, 0);
      x = __builtin_rvtt_sfpmad (x, c8, c9, 0);
      x = __builtin_rvtt_sfpmad (x, c10, c11, 0);
      x = __builtin_rvtt_sfpmad (x, c12, c13, 0);
      x = __builtin_rvtt_sfpmad (x, c14, c15, 0);
      x = __builtin_rvtt_sfpmad (x, c16, c17, 0);
      x = __builtin_rvtt_sfpmad (x, c18, c19, 0);
      x = __builtin_rvtt_sfpmad (x, c20, c21, 0);
      x = __builtin_rvtt_sfpmad (x, c22, c23, 0);
      x = __builtin_rvtt_sfpmad (x, c24, c25, 0);
      x = __builtin_rvtt_sfpmad (x, c26, c27, 0);
      x = __builtin_rvtt_sfpmad (x, c28, c29, 0);
      x = __builtin_rvtt_sfpmad (x, c30, c31, 0);
      x = __builtin_rvtt_sfpmad (x, c32, c33, 0);
      x = __builtin_rvtt_sfpmad (x, c34, c35, 0);
      x = __builtin_rvtt_sfpmad (x, c36, c37, 0);
      x = __builtin_rvtt_sfpmad (x, c38, c39, 0);
      x = __builtin_rvtt_sfpmad (x, c40, c41, 0);
      x = __builtin_rvtt_sfpmad (x, c42, c43, 0);
      x = __builtin_rvtt_sfpmad (x, c44, c45, 0);
      x = __builtin_rvtt_sfpmad (x, c46, c47, 0);
      x = __builtin_rvtt_sfpmad (x, c48, c49, 0);
      x = __builtin_rvtt_sfpmad (x, c50, c51, 0);
      x = __builtin_rvtt_sfpmad (x, c52, c53, 0);
      x = __builtin_rvtt_sfpmad (x, c54, c55, 0);
      x = __builtin_rvtt_sfpmad (x, c56, c57, 0);
      x = __builtin_rvtt_sfpmad (x, c58, c59, 0);
      x = __builtin_rvtt_sfpmad (x, c60, c61, 0);
      x = __builtin_rvtt_sfpmad (x, c62, c63, 0);
      x = __builtin_rvtt_sfpmad (x, c64, c65, 0);
      x = __builtin_rvtt_sfpmad (x, c66, c67, 0);
      x = __builtin_rvtt_sfpmad (x, c68, c69, 0);
      x = __builtin_rvtt_sfpmad (x, c70, c71, 0);
      x = __builtin_rvtt_sfpmad (x, c72, c73, 0);
      x = __builtin_rvtt_sfpmad (x, c74, c75, 0);
      x = __builtin_rvtt_sfpmad (x, c76, c77, 0);
      x = __builtin_rvtt_sfpmad (x, c78, c79, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
