// A visible, bound body may prove no recorder setup; an opaque ABI may not.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

#define RESET() do { __builtin_rvtt_sfpencc_all_lanes (); \
  __builtin_rvtt_sfpconfig_i (0, 15, 1); } while (0)
#define ROR(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)
#define TURN() do { auto v = __builtin_rvtt_sfpreadlreg (0); \
  auto r = ROR (ROR (ROR (ROR (ROR (ROR (ROR (ROR (v)))))))); \
  __builtin_rvtt_sfpwritelreg (r, 1); } while (0)

static __attribute__((noinline, noclone)) unsigned
scalar_callee (unsigned value) { return value * 3 + 1; }

static __attribute__((noinline, noclone)) void
recorder_callee () { asm volatile (".ttinsn 0x04000401"); }

extern void opaque_callee ();

void known_body_reset (unsigned value)
{
  auto result = scalar_callee (value);
  RESET ();
  TURN ();
  asm volatile ("" : : "r" (result));
}

void opaque_body_reset ()
{
  opaque_callee ();
  RESET ();
  TURN ();
}

void recording_body_reset ()
{
  recorder_callee ();
  RESET ();
  TURN ();
}

// { dg-final { scan-tree-dump-times "rotate chain collapse 8 -> 0 links" 1 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "crosslane-lane-state-unproven" 2 "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 16 } }
