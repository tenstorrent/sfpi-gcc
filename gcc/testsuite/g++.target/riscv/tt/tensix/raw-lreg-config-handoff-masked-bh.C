// SFPCONFIG's implicit L0 input is assigned by the unconditional mod-2 copy,
// not a lane-predicated mod-0 merge.  Both masking mechanisms are exercised.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-assembler-times {SFPMOV\tL0, L[0-9]+, 2} 2 } }
// { dg-final { scan-assembler-times {SFPMOV\tL[0-9]+, L[0-9]+, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t12, 0, 0} 3 } }
// { dg-final { scan-assembler-not "SFPBAD" } }
// { dg-final { scan-rtl-dump "UNSPECV_SFPASSIGN" "rvtt_lreg_livein" } }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>

#define RAW(word) asm volatile (".ttinsn %0" : : "n" (word))
#define RESET() do { __builtin_rvtt_sfpencc_all_lanes (); \
  __builtin_rvtt_sfpconfig_i (0, 15, 1); } while (0)
#define SEED_L0() do { RAW (0x71020001); \
  __builtin_rvtt_sfprawlreg_effect (0, 1); } while (0)
#define CONSUME_L0() do { RAW (0x710a0504); \
  __builtin_rvtt_sfprawlreg_effect (1, 1); \
  RESET (); \
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (0), 2); } while (0)

void flags_disabled_handoff ()
{
  RESET ();
  SEED_L0 ();
  __builtin_rvtt_sfpencc (10, 0);
  auto value = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpwriteconfig_v (value, 0, 12);
  CONSUME_L0 ();
}

void row_masked_handoff ()
{
  RESET ();
  SEED_L0 ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  auto value = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpwriteconfig_v (value, 0, 12);
  CONSUME_L0 ();
}

void live_value_is_not_a_full_overwrite ()
{
  RESET ();
  SEED_L0 ();
  sfpi::vFloat old (__builtin_rvtt_sfpreadlreg (0));
  sfpi::vFloat replacement (__builtin_rvtt_sfpreadlreg (1));
  v_if (replacement < 0.0f)
    { old = replacement; }
  v_endif;
  __builtin_rvtt_sfpwriteconfig_v (old.get (), 0, 12);
  CONSUME_L0 ();
}
