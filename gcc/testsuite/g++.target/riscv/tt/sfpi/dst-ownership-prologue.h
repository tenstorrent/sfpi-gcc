/* Shared prologue for the Dst/RWC ownership tests.  */
extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

__attribute__((always_inline)) inline void
dst_ownership_enable_all_lanes ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
}
