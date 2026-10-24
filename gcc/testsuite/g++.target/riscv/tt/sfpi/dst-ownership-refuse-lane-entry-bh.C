// Entry flags and ROW_MASK are independent facts: neither unknown entry,
// SFPENCC alone, nor a nonzero ROW_MASK permits a full-lane reload fold.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "cc-enable-unproved" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 6 } }

#include "dst-ownership-prologue.h"
using namespace sfpi;

__attribute__((always_inline)) static inline void
reload_body ()
{
  vFloat in = dst_reg[0];
  vFloat result = in * 0.30102999f + 1.25f;
  vFloat again = dst_reg[0];
  dst_reg[0] = copysgn (result, again);
}

void unknown_entry ()
{
  reload_body ();
}

void flags_only ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  reload_body ();
}

void row_mask_not_zero ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  reload_body ();
}
