// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti" }
// Check that one typed face advance is two CR-mode Dst += 8 steps.
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }
// { dg-final { scan-assembler-not {\.ttinsn} } }

void typed_face_advance_bh ()
{
  __builtin_rvtt_ttdstface ();
}
