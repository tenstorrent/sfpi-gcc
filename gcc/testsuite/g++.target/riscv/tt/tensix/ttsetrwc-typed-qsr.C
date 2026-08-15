// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 4} 1 } }
// { dg-final { scan-assembler-not {\.ttinsn} } }

void typed_face_transition_qsr ()
{
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
}
