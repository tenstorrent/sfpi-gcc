// X6 FPU face-transpose builtins: QSR refusal twin.  QSR
// encodes this family with different operand ranges (gas rejects the
// WH/BH forms) and no choreography is audited there: every builtin
// refuses at expansion, by message.
// { dg-options "-mcpu=tt-qsr32-tensix -fno-exceptions -fno-rtti -O2" }

void movd2b ()
{
  __builtin_rvtt_ttmovd2b (1, 16, 7, 2, 0);	// { dg-error "QSR TTMOVD2B is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void movb2a ()
{
  __builtin_rvtt_ttmovb2a (12, 7, 2, 28);	// { dg-error "QSR TTMOVB2A is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void movb2d ()
{
  __builtin_rvtt_ttmovb2d (0, 20, 7, 4, 4);	// { dg-error "QSR TTMOVB2D is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void mova2d ()
{
  __builtin_rvtt_ttmova2d (1, 8, 7, 2, 8);	// { dg-error "QSR TTMOVA2D is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void trnspsrcb ()
{
  __builtin_rvtt_tttrnspsrcb ();		// { dg-error "QSR TTTRNSPSRCB is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void stallwait ()
{
  __builtin_rvtt_ttstallwait (0x80, 0x980);	// { dg-error "QSR TTSTALLWAIT is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}

void rmwcib ()
{
  __builtin_rvtt_ttrmwcib (0, 1, 1, 2);		// { dg-error "QSR TTRMWCIB is unaudited" }
  // { dg-error "invalid argument to built-in function" "" { target *-*-* } .-1 }
}
