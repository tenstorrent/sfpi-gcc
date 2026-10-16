extern volatile unsigned iptr[];

void dynamic_bh_only()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpand(r, b), 3);
}

void dynamic_bh_qsr()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpshft_v(r, b, 0), 3);
}

void dynamic_unmasked()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpmul(r, b, 0), 3);
}

void static_immediate()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  __builtin_rvtt_sfpwritelreg(
      __builtin_rvtt_sfpshft2_subvec_shfl1(a, 3), 3);
}

void static_preexisting_nop()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto shifted = __builtin_rvtt_sfpshft2_subvec_shfl1(a, 3);
  __builtin_rvtt_sfpnop();
  __builtin_rvtt_sfpwritelreg(shifted, 3);
}

void dynamic_preexisting_nop()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  __builtin_rvtt_sfpnop();
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpshft_v(r, b, 0), 3);
}

void dynamic_ghost_then_consumer()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  auto marker = __builtin_rvtt_sfpreadlreg(2);
  __builtin_rvtt_sfpwritelreg(marker, 2);
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpshft_v(r, b, 0), 3);
}

void static_ghost_only()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto shifted = __builtin_rvtt_sfpshft2_subvec_shfl1(a, 3);
  auto marker = __builtin_rvtt_sfpreadlreg(2);
  __builtin_rvtt_sfpwritelreg(marker, 2);
  __builtin_rvtt_sfpwritelreg(shifted, 3);
}

void dynamic_diamond_merge(bool predicate)
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  if (predicate)
    iptr[0] = 7;
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpshft_v(r, b, 0), 3);
}

void dynamic_diamond_one_arm(bool predicate)
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  if (predicate)
    __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpshft_v(r, b, 0), 3);
  else
    __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpmul(a, b, 0), 3);
}

void dynamic_diamond_other_arm(bool predicate)
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  if (predicate)
    __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpmul(a, b, 0), 3);
  else
    __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpshft_v(r, b, 0), 3);
}

void dynamic_backedge(unsigned count)
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  do {
    a = __builtin_rvtt_sfpshft_v(a, b, 0);
    a = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  } while (--count);
  __builtin_rvtt_sfpwritelreg(a, 3);
}
