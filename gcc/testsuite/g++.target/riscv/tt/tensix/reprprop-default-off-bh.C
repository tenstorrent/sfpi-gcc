// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Default-off witness: without -mtt-tensix-optimize-repr-prop the
// cancellable pair survives byte-identically.
// { dg-final { scan-assembler-times "SFPCAST" 2 } }

__attribute__((noinline)) void roundtrip_row_default ()
{
  auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  v = __builtin_rvtt_sfpcast (v, 3);
  v = __builtin_rvtt_sfpcast (v, 3);
  __builtin_rvtt_sfpstore (nullptr, v, 2, 0, 0, 4, 7);
}
