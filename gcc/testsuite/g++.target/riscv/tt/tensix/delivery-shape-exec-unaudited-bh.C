// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// A row member with no audited latency fact (SFPNOT) makes the
// execution term unpriceable: refuse by name, bytes stay rolled.
// { dg-final { scan-tree-dump "refused .delivery-shape-exec-term-unaudited." "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

void ds_unaudited_kernel ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto n = __builtin_rvtt_sfpnot (v);
      __builtin_rvtt_sfpstore (nullptr, n, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
