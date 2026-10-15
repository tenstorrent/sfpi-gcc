// The LREG pressure model counts an SFPSWAP against a constant register
// as one LREG when only the non-constant half is used: SFPSWAP drops its
// write to the constant register, so the pair needs no second register.
// Each body holds seven values across a bound against L10 (VC) or L9
// (VD), which peaks at exactly the eight-LREG file.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "const-residency: pressure 8 within the 8-LREG file" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPSWAP\tL\[0-7\], L10, 1" 1 } }
// { dg-final { scan-assembler-times "SFPSWAP\tL9, L\[0-7\], 1" 1 } }

#define LOADS							\
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);	\
  auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 2, 0, 0);	\
  auto c = __builtin_rvtt_sfpload (nullptr, 0, 0, 4, 0, 0);	\
  auto d = __builtin_rvtt_sfpload (nullptr, 0, 0, 6, 0, 0);	\
  auto e = __builtin_rvtt_sfpload (nullptr, 0, 0, 8, 0, 0);	\
  auto f = __builtin_rvtt_sfpload (nullptr, 0, 0, 10, 0, 0);	\
  auto g = __builtin_rvtt_sfpload (nullptr, 0, 0, 12, 0, 0);	\
  auto h = __builtin_rvtt_sfpload (nullptr, 0, 0, 14, 0, 0)

#define SUM(m)								\
  m = __builtin_rvtt_sfpadd (m, b, 0);					\
  m = __builtin_rvtt_sfpadd (m, c, 0);					\
  m = __builtin_rvtt_sfpadd (m, d, 0);					\
  m = __builtin_rvtt_sfpadd (m, e, 0);					\
  m = __builtin_rvtt_sfpadd (m, f, 0);					\
  m = __builtin_rvtt_sfpadd (m, g, 0);					\
  m = __builtin_rvtt_sfpadd (m, h, 0);					\
  __builtin_rvtt_sfpstore (nullptr, m, 0, 0, 0, 0, 0)

void swap_vc_bound_width (void)
{
  LOADS;
  auto r = __builtin_rvtt_sfpswap (a, __builtin_rvtt_sfpreadlreg (10), 1);
  auto m = __builtin_rvtt_sfpselect2 (r, 0);
  SUM (m);
}

void swap_vd_bound_width (void)
{
  LOADS;
  auto r = __builtin_rvtt_sfpswap (__builtin_rvtt_sfpreadlreg (9), a, 1);
  auto m = __builtin_rvtt_sfpselect2 (r, 1);
  SUM (m);
}
