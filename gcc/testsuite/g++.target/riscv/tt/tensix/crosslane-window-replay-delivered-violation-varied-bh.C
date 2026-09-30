// Renamed-varied twin of the replay-delivery violation: a
// differently-spelled row (SFPOR result pinned to LReg4) records before
// the window and launches inside it -- the delivered LReg4 write errors
// by mechanism, not by any name or opcode keying.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

using tensor_row_t = __xtt_vector;

#define GATHER_STEP do {                                                \
    tensor_row_t key = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    __builtin_rvtt_sfpwritelreg (key, 5);                               \
    tensor_row_t idx = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    __builtin_rvtt_sfpwritelreg (idx, 6);                               \
    tensor_row_t tag = __builtin_rvtt_sfpor (key, idx);                 \
    __builtin_rvtt_sfpwritelreg (tag, 4);                               \
    __builtin_rvtt_sfpstore (nullptr, tag, 0, 0, 0, 0, 7);              \
  } while (0)

void gather_epilogue ()
{
  GATHER_STEP;
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  GATHER_STEP; GATHER_STEP; GATHER_STEP;
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "dest-index-window-violation: replay launch delivers" "" { target *-*-* } 0 }
