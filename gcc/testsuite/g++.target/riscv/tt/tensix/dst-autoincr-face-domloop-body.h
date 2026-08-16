/* A runtime-counted face loop (zero-trip capable: the loop guard precedes
   the dedicated preheader) with two explicit rows and the TYPED Dst/RWC
   face advance at the end of the body.  The advance separates the faces
   architecturally but cannot write address-modifier configuration, so the
   slot program may dominate it from the preheader.  A raw `.ttinsn' face
   advance in the same position is opaque and must refuse (see the
   raw-refuse variant).  */

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
