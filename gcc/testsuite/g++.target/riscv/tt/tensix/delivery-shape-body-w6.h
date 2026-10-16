/* A six-word counted SFPU row for which rolled delivery is priced cheapest.  */

#ifndef DS_KERNEL
#define DS_KERNEL ds_w6_kernel
#endif
#ifndef DS_TRIPS
#define DS_TRIPS 32
#endif
#ifndef DS_MODE
#define DS_MODE 7
#endif

void DS_KERNEL ()
{
  for (int d = 0; d < DS_TRIPS; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, DS_MODE);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f00, 0, 0, 0);
      auto t = __builtin_rvtt_sfpmad (a, c, v, 0);
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, DS_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
