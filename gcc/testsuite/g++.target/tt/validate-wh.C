// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }

void fn (int i)
{
  __builtin_rvtt_sfpreadlreg (i); // { dg-error "is not a constant" }

  __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpreadlreg (15);
  __builtin_rvtt_sfpreadlreg (16); // { dg-error "is out of range" }

  auto cst = __builtin_rvtt_sfpreadlreg (8);
  __builtin_rvtt_sfpwritelreg (cst, 0);
  __builtin_rvtt_sfpwritelreg (cst, 7);
  __builtin_rvtt_sfpwritelreg (cst, 8); // { dg-error "is out of range" }

  auto x2 = __builtin_rvtt_sfpswap (cst, cst, 0);
  __builtin_rvtt_sfpselect2 (x2, 0);
  __builtin_rvtt_sfpselect2 (x2, 1);
  __builtin_rvtt_sfpselect2 (x2, 2); // { dg-error "is out of range" }
  
  auto x4 = __builtin_rvtt_sfptransp (cst, cst, cst, cst);
  __builtin_rvtt_sfpselect4 (x4, 0);
  __builtin_rvtt_sfpselect4 (x4, 3);
  __builtin_rvtt_sfpselect4 (x4, 5); // { dg-error "is out of range" }

  __builtin_rvtt_sfpnop ();

  __builtin_rvtt_sfptransp (cst, cst, cst, cst);

  __builtin_rvtt_sfpswap (cst, cst, 8);
  __builtin_rvtt_sfpswap (cst, cst, 16); // { dg-error "is out of range" }
  __builtin_rvtt_sfpswap (cst, cst, 9); // { dg-error "is invalid mod1 value" }

  __builtin_rvtt_sfpshft2_copy4 (cst, cst, cst, 0);
  __builtin_rvtt_sfpshft2_copy4 (cst, cst, cst, 1); // { dg-error "is invalid mod1 value" }

  __builtin_rvtt_sfpshft2_subvec_copy4 (cst, cst, cst, cst, 1);
  __builtin_rvtt_sfpshft2_subvec_copy4 (cst, cst, cst, cst, 2); // { dg-error "is invalid mod1 value" }

  __builtin_rvtt_sfpshft2_subvec_shfl1_copy4 (cst, cst, cst, cst, 2);
  __builtin_rvtt_sfpshft2_subvec_shfl1_copy4 (cst, cst, cst, cst, 3); // { dg-error "is invalid mod1 value" }

  __builtin_rvtt_sfpshft2_subvec_shfl1 (cst, 2); // { dg-error "is invalid mod1 value" } 
  __builtin_rvtt_sfpshft2_subvec_shfl1 (cst, 3);
  __builtin_rvtt_sfpshft2_subvec_shfl1_lv (cst, cst, 4);
  __builtin_rvtt_sfpshft2_subvec_shfl1_lv (cst, cst, 5); // { dg-error "is invalid mod1 value" }

  __builtin_rvtt_ttincrwc (-32, -8, -8, -8);
  __builtin_rvtt_ttincrwc ( 31,  7,  7,  7);
  __builtin_rvtt_ttincrwc (-33, -8, -8, -8); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc (-32, -9, -8, -8); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc (-32, -8, -9, -8); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc (-32, -8, -8, -9); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc ( 32,  7,  7,  7); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc ( 31,  8,  7,  7); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc ( 31,  7,  8,  7); // { dg-error "is out of range" }
  __builtin_rvtt_ttincrwc ( 31,  7,  7,  8); // { dg-error "is out of range" }

  __builtin_rvtt_ttreplay (nullptr,  1, 0, 0,  0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 32, 0, 0, 31, 1, 1);
  __builtin_rvtt_ttreplay (nullptr,  i, 0, 0,  0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr,  0, 0, 0,  0, 0, 0); // { dg-error "is out of range" }
  __builtin_rvtt_ttreplay (nullptr,  1, 0, 0, 32, 0, 0); // { dg-error "is out of range" }
  __builtin_rvtt_ttreplay (nullptr,  1, 0, 0,  0, 2, 0); // { dg-error "is out of range" }
  __builtin_rvtt_ttreplay (nullptr,  1, 0, 0,  0, 0, 2); // { dg-error "is out of range" }

  __builtin_rvtt_sfpmov (cst, 0); // { dg-error "is invalid mod1 value" }
  __builtin_rvtt_sfpmov (cst, 1);
  __builtin_rvtt_sfpmov (cst, 2); // { dg-error "is invalid mod1 value" } 
  __builtin_rvtt_sfpmov (cst, 4); // { dg-error "is invalid mod1 value" } 
  __builtin_rvtt_sfpmov (cst, 8); // { dg-error "is invalid mod1 value" } 

  __builtin_rvtt_sfpmul (cst, cst, 0);
  __builtin_rvtt_sfpmul (cst, cst, 1); // { dg-error "is invalid mod1 value" } 
  __builtin_rvtt_sfpadd (cst, cst, 0);
  __builtin_rvtt_sfpadd (cst, cst, 1); // { dg-error "is invalid mod1 value" } 
}
