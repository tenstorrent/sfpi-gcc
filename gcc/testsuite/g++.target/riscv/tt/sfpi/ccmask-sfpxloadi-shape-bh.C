// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// rvtt_arg_info may treat only canonical, full-width sfpxloadi calls as
// scalar constants.  The canonical +0 forms fold; malformed spellings must
// remain visible to the CC-mask recognizer and refuse the transformation.
// The builtin's signed six-bit width field permits the full-word forms
// signed-width 31 and unsigned-width -32.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 2 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPGT" 2 } }
// { dg-final { scan-assembler-times "SFPLE" 4 } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
// Exercise the compiler builtin's complete ABI rather than SFPI's two-argument
// convenience spelling.
#undef __builtin_rvtt_sfpxloadi

unsigned long impostor_buffer[1];

#define ZERO_ROW(NAME, BUFFER, IMM, VAR, ID, BITS)                         \
  __attribute__((noinline)) void NAME ()                                  \
  {                                                                        \
    for (int ix = 0; ix < 8; ++ix)                                        \
      {                                                                    \
        sfpi::vFloat x = sfpi::dst_reg[0];                                 \
        sfpi::vFloat z (__builtin_rvtt_sfpxloadi                           \
                        ((BUFFER), (IMM), (VAR), (ID), (BITS)));           \
        sfpi::vFloat w = x;                                                \
        v_if (x <= z) { w = z; }                                           \
        v_endif;                                                           \
        sfpi::dst_reg[0] = w;                                              \
        sfpi::dst_reg++;                                                   \
      }                                                                    \
  }

ZERO_ROW (canonical_w31, nullptr, 0u, 0u, 0u, 31)
ZERO_ROW (canonical_wm32, nullptr, 0u, 0u, 0u, -32)

ZERO_ROW (narrow_width, nullptr, 0u, 0u, 0u, 15)
ZERO_ROW (wrong_buffer, impostor_buffer, 0u, 0u, 0u, 31)
ZERO_ROW (nonzero_var, nullptr, 0u, 1u, 0u, 31)
ZERO_ROW (nonzero_id, nullptr, 0u, 0u, 1u, 31)
