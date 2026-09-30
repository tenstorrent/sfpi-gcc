/* A two-word counted SFPU row (plus the typed row step): too small for
   the replay former's MIN_SEQUENCE as one row, but the solver's
   composite payload (two rows per window) makes the group capturable
   -- the class the fixed-factor pass refuses as row-too-small.  */

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
