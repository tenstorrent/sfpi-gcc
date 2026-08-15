// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { scan-assembler-times {\.ttinsn\t1887535104} 1 } }

void discard_load ()
{
  __builtin_rvtt_sfploaddiscard (0, 1, 2);
}
