/* Runtime-counted face loop of two explicit rows ending in the typed Dst/RWC
   face advance.  */

using vec_t = __xtt_vector;

void
FACE_FN (unsigned nfaces)
{
  for (unsigned face = 0; face != nfaces; ++face)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, FACE_MODE);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, FACE_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      vec_t b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, FACE_MODE);
      vec_t q = __builtin_rvtt_sfpmul (b, b, 0);
      __builtin_rvtt_sfpstore (nullptr, q, 0, 0, 0, 0, FACE_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      FACE_ADVANCE;
    }
}
