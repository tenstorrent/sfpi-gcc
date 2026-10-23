// Check the row-pair interleave all-lanes entry proof across joins and a raw NOP word.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump-times "rows paired, row step 2 -> 4" 2 "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump-times "refused: loop \\d+ \\(dst-row-interleave-entry-cc-unproven\\)" 1 "rvtt_dst_row_interleave" } }

using vec_t = __xtt_vector;

void
joined_entry (unsigned n)
{
  // Lane flags and LaneConfig ROW_MASK are independent hardware state.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  if (n)
    __builtin_rvtt_ttincrwc (0, 4, 0, 0);
  else
    __builtin_rvtt_sfprawlreg_effect (0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
raw_word_entry ()
{
  // Lane flags and LaneConfig ROW_MASK are independent hardware state.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x02000000));
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
joined_cc_refuse (unsigned n)
{
  // Lane flags and LaneConfig ROW_MASK are independent hardware state.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t m = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  __builtin_rvtt_sfppushc (0);
  if (n)
    __builtin_rvtt_sfpsetcc (m, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfppopc (0);
}
