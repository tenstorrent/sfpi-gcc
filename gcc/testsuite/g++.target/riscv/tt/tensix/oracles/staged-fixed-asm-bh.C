
/* Raw LREG effects are absent from DF.  This fixed SFPLOAD writes L1.  */
void fixed_load_l1_ttinsn ()
{
  asm volatile (".ttinsn %0" : : "n" (0x70100000));
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 0);
}

/* A fixed raw SFPCONFIG word is a visible macro owner and must still refuse.  */
void fixed_config_ttinsn ()
{
  asm volatile (".ttinsn %0" : : "n" (0x91000000));
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 0);
}

/* SWAP and TRANSP can read or write L1 despite having constant words.  */
void fixed_swap_ttinsn ()
{
  asm volatile (".ttinsn %0" : : "n" (0x92000110));
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 0);
}

void fixed_transp_ttinsn ()
{
  asm volatile (".ttinsn %0" : : "n" (0x8c000000));
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 0);
}

/* Raw CC stack operations are likewise invisible to the typed CC proof.  */
void fixed_cc_ttinsn ()
{
  asm volatile (".ttinsn %0" : : "n" (0x87000000));
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 0);
}
