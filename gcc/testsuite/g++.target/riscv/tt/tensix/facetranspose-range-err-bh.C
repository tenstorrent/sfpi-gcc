// X6 FPU face-transpose builtins: operand-envelope refusals (lane FV).
// The {CU, width} specs are the architectural encoding-field widths
// (ckernel_ops.h TT_*_VALID); out-of-range constants error by message.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }

void movd2b_lo_wide ()
{
  __builtin_rvtt_ttmovd2b (2, 16, 7, 2, 0);	// { dg-error "out of range" }
}

void movd2b_srcrow_wide ()
{
  __builtin_rvtt_ttmovd2b (0, 64, 7, 2, 0);	// { dg-error "out of range" }
}

void movb2d_mode_wide ()
{
  __builtin_rvtt_ttmovb2d (0, 16, 7, 8, 0);	// { dg-error "out of range" }
}

void stallwait_stall_wide ()
{
  __builtin_rvtt_ttstallwait (0x200, 0);	// { dg-error "out of range" }
}

void rmwcib_byte_wide ()
{
  __builtin_rvtt_ttrmwcib (4, 1, 1, 2);		// { dg-error "out of range" }
}

void rmwcib_nonconst (unsigned m)
{
  __builtin_rvtt_ttrmwcib (0, m, 1, 2);		// { dg-error "not a constant" }
}
