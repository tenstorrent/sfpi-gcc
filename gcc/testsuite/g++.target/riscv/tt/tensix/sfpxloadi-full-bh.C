// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { scan-assembler-not "SFPLOADI[^\\n]*\\n[^\\n]*SFPMOV[^\\n]*\\n[^\\n]*SFPLOADI" } }

// Full-width bit patterns must be materialized directly in their result LREG.
// Keeping all results live until the stores also exercises LREG pressure.
void *buf;

void full_literals ()
{
  auto neg_zero = __builtin_rvtt_sfpxloadi (buf, 0x80000000u, 0, 0, 31);
  auto subnormal = __builtin_rvtt_sfpxloadi (buf, 0x00000001u, 0, 0, 31);
  auto infinity = __builtin_rvtt_sfpxloadi (buf, 0x7f800000u, 0, 0, 31);
  auto qnan = __builtin_rvtt_sfpxloadi (buf, 0x7fc12345u, 0, 0, 31);
  __builtin_rvtt_sfpstore (buf, neg_zero, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (buf, subnormal, 2, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (buf, infinity, 4, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (buf, qnan, 6, 0, 0, 0, 0);
}
