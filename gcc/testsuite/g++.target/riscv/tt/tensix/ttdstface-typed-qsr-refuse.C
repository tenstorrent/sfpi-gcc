// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti" }

void typed_face_advance_qsr_refuse ()
{
  __builtin_rvtt_ttdstface (); // { dg-error "QSR cannot represent the Dst face advance" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}
