// { dg-options "-mcpu=tt-wh-tensix -O3 -fno-exceptions -fno-rtti" }
// One typed face advance is two architectural CR-mode Dst += 8 steps.
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }
// { dg-final { scan-assembler-not {\.ttinsn} } }

void typed_face_advance_wh ()
{
  __builtin_rvtt_ttdstface ();
}
