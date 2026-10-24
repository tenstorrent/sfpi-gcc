// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { scan-assembler-not "SFPLOADI.*\\n.*SFPMOV.*\\n.*SFPLOADI" } }

// Check that 32-bit SFPXLOADI constants load directly into their result LREG.
void *buf;
#define P ((volatile unsigned long *) buf)

void full_literals ()
{
  auto neg_zero = __builtin_rvtt_sfpxloadi (P, 0x80000000u, 0, 0, 31);
  auto subnormal = __builtin_rvtt_sfpxloadi (P, 0x00000001u, 0, 0, 31);
  auto infinity = __builtin_rvtt_sfpxloadi (P, 0x7f800000u, 0, 0, 31);
  auto qnan = __builtin_rvtt_sfpxloadi (P, 0x7fc12345u, 0, 0, 31);
  __builtin_rvtt_sfpstore (P, neg_zero, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (P, subnormal, 2, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (P, infinity, 4, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (P, qnan, 6, 0, 0, 0, 0);
}
