// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// Symbolic trip count: refuse by name, bytes stay rolled.
// { dg-final { scan-tree-dump "refused .delivery-shape-trip-count-unproven." "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

void ds_trip_unproven (int n)
{
  for (int d = 0; d < n; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
