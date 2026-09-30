/* A tiny (four-word) counted SFPU row: the control-dominated rolled
   class where the measured table prices windowed delivery ahead of
   per-trip explicit words (DX-F2's rolled-loop finding).  */

#ifndef DS_KERNEL
#define DS_KERNEL ds_tiny_kernel
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
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, DS_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
