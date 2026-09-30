/* A counted SFPU row loop: one dst_reg row per trip, every statement a
   typed SFPU builtin plus the loop's own scalar control -- the shape a
   production author writes under `#pragma GCC unroll 8'.  */

#ifndef RLU_KERNEL
#define RLU_KERNEL rlu_kernel
#endif
#ifndef RLU_TRIPS
#define RLU_TRIPS 32
#endif
#ifndef RLU_MODE
#define RLU_MODE 7
#endif
#ifndef RLU_C0
#define RLU_C0 0x3f00
#endif

void RLU_KERNEL ()
{
  for (int d = 0; d < RLU_TRIPS; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, RLU_MODE);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      auto c = __builtin_rvtt_sfploadi (nullptr, RLU_C0, 0, 0, 0);
      auto t = __builtin_rvtt_sfpmad (a, c, v, 0);
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, RLU_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
