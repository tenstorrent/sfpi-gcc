/* A two-word counted SFPU row, capturable only as a two-row payload.  */

#ifndef DS_KERNEL
#define DS_KERNEL ds_two_word_kernel
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
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 0, DS_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
