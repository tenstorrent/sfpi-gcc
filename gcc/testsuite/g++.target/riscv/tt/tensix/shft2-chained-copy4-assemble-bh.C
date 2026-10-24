// Check that SFPSHFT2 SUBVEC_CHAINED_COPY4 assembles in its chained form.
// { dg-do assemble }
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }

void chained_copy4 ()
{
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);

  auto r = __builtin_rvtt_sfpshft2_subvec_copy4 (v0, v1, v2, v3, 1);
  v0 = __builtin_rvtt_sfpselect4 (r, 0);
  v1 = __builtin_rvtt_sfpselect4 (r, 1);
  v2 = __builtin_rvtt_sfpselect4 (r, 2);
  v3 = __builtin_rvtt_sfpselect4 (r, 3);

  __builtin_rvtt_sfpstore (nullptr, v0, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, v1, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, v2, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, v3, 0, 0, 0, 0, 0);
}
