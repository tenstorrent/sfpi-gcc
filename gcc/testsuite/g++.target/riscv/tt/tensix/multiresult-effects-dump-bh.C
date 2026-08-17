// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// Layer-1 golden lines for the multi-result operations (TOP3-2).
// The indexed SFPSWAP is one Simple-unit event whose audited write set is
// the value pair plus the companion pair at value+4 (here L0,L2,L4,L6 =
// 0x55); the eight-definition SFPTRANSP writes both banks (0xff).  The
// legacy four-definition SFPTRANSP tuple stays deliberately opaque: it
// under-states the architectural write set (hardware permutes both banks).
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0x55 lreg-write=0x55 port=borrows_mad cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0xff lreg-write=0xff port=none cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }

void multiresult_effects ()
{
  auto v0 = __builtin_rvtt_sfpreadlreg (0);
  auto v1 = __builtin_rvtt_sfpreadlreg (1);
  auto v2 = __builtin_rvtt_sfpreadlreg (2);
  auto v3 = __builtin_rvtt_sfpreadlreg (3);
  auto i0 = __builtin_rvtt_sfpreadlreg (4);
  auto i1 = __builtin_rvtt_sfpreadlreg (5);
  auto i2 = __builtin_rvtt_sfpreadlreg (6);
  auto i3 = __builtin_rvtt_sfpreadlreg (7);

  auto s = __builtin_rvtt_sfpswap_indexed (v0, v2, i0, i2, 8);
  v0 = __builtin_rvtt_sfpselect4 (s, 0);
  v2 = __builtin_rvtt_sfpselect4 (s, 1);
  i0 = __builtin_rvtt_sfpselect4 (s, 2);
  i2 = __builtin_rvtt_sfpselect4 (s, 3);

  auto t = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, i0, i1, i2, i3);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (t, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (t, 1), 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (t, 2), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (t, 3), 3);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (4), 4);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (5), 5);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (6), 6);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (7), 7);
}
