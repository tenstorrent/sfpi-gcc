// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// A SETC16 in the row programs machine state outside the row (the
// shared census vocabulary): refuse by name.
// { dg-final { scan-tree-dump "refused .delivery-shape-denied-class." "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

void ds_denied_class ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      __builtin_rvtt_ttsetc16 (18, 0);
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
