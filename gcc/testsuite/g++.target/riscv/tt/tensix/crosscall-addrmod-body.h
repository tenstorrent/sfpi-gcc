/* Callee of CAM_ROWS explicit mod-write rows called from a caller loop;
   CAM_CALLER_EXTRA and CAM_CALLEE_TAIL inject optional statements.  */

using cam_vec_t = __xtt_vector;

static inline void
cam_row (unsigned addr)
{
  cam_vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, CAM_MODE);
  cam_vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, addr, 0, 0, 0, CAM_MODE);
  __builtin_rvtt_ttincrwc (0, CAM_STRIDE, 0, 0);
}

__attribute__((noinline)) void
cam_callee ()
{
#if CAM_ROWS >= 1
  cam_row (0);
#endif
#if CAM_ROWS >= 2
  cam_row (0);
#endif
#if CAM_ROWS >= 3
  cam_row (0);
#endif
#if CAM_ROWS >= 4
  cam_row (0);
#endif
#if CAM_ROWS >= 5
  cam_row (0);
#endif
#if CAM_ROWS >= 6
  cam_row (0);
#endif
#if CAM_ROWS >= 7
  cam_row (0);
#endif
#if CAM_ROWS >= 8
  cam_row (0);
#endif
#ifdef CAM_CALLEE_TAIL
  CAM_CALLEE_TAIL;
#endif
}

#ifndef CAM_NO_CALLER
int cam_pace;

void
cam_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
#ifdef CAM_CALLER_EXTRA
      CAM_CALLER_EXTRA;
#endif
      cam_callee ();
    }
}
#endif
