// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti" }

void typed_face_transition_qsr_refuse_a ()
{
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 1); // { dg-error "QSR TTSETRWC cannot represent this CR/mask combination" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void typed_face_transition_qsr_refuse_b ()
{
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 2); // { dg-error "QSR TTSETRWC cannot represent this CR/mask combination" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void typed_face_transition_qsr_refuse_mixed ()
{
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 5); // { dg-error "QSR TTSETRWC cannot represent this CR/mask combination" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void typed_face_transition_qsr_refuse_ctocr ()
{
  __builtin_rvtt_ttsetrwc (0, 8, 8, 0, 0, 4); // { dg-error "QSR TTSETRWC cannot represent this CR/mask combination" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void typed_face_transition_qsr_refuse_ctocr_d ()
{
  __builtin_rvtt_ttsetrwc (0, 12, 8, 0, 0, 4); // { dg-error "QSR TTSETRWC cannot represent this CR/mask combination" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void typed_face_transition_qsr_refuse_cr_d_without_set_d ()
{
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 8); // { dg-error "QSR TTSETRWC cannot represent this CR/mask combination" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}
