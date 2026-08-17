/* Shared prologue for the Dst/RWC ownership tests.  */
extern volatile unsigned __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>
