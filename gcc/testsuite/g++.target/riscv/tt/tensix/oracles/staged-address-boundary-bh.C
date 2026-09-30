
template <unsigned ADDRESS>
__attribute__((noinline)) void address_body ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, ADDRESS, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, ADDRESS, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

template void address_body<1022> ();
template void address_body<1024> ();
