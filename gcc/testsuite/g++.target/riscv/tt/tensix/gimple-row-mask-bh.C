// LaneEnabled is column flags AND the independent LaneConfig ROW_MASK.
// Only four controls below prove a complete, executed reset.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

#define FLAGS() __builtin_rvtt_sfpencc_all_lanes ()
#define CLEAR() __builtin_rvtt_sfpconfig_i (0, 15, 1)
#define RESET() do { FLAGS (); CLEAR (); } while (0)
#define ROR(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)
#define TURN() do { auto v = __builtin_rvtt_sfpreadlreg (0); \
  auto r = ROR (ROR (ROR (ROR (ROR (ROR (ROR (ROR (v)))))))); \
  __builtin_rvtt_sfpwritelreg (r, 1); } while (0)
#define RAW(word) asm volatile (".ttinsn %0" : : "n" (word))

void unknown_entry () { TURN (); }
void flags_only () { FLAGS (); TURN (); }
void wrong_order () { CLEAR (); FLAGS (); TURN (); }
void nonzero_mask ()
{
  RESET ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  FLAGS ();
  TURN ();
}
void register_mask ()
{
  RESET ();
  __builtin_rvtt_sfpwriteconfig_v (__builtin_rvtt_sfpreadlreg (0), 0, 15);
  FLAGS ();
  TURN ();
}
void one_bad_predecessor (bool masked)
{
  RESET ();
  if (masked)
    __builtin_rvtt_sfpconfig_i (1, 15, 1);
  FLAGS ();
  TURN ();
}
void replay_capture ()
{
  RAW (0x04000401);
  RESET ();
  TURN ();
}
void runtime_issue (unsigned word)
{
  *reinterpret_cast<volatile unsigned *> (0xffe40000u) = word;
  RESET ();
  TURN ();
}

void explicit_reset () { RESET (); TURN (); }
void exact_raw_reset () { RAW (0x8a00300a); RAW (0x910000f1); TURN (); }
void unrelated_config ()
{
  RESET ();
  __builtin_rvtt_sfpwriteconfig_v (__builtin_rvtt_sfpreadlreg (0), 0, 12);
  TURN ();
}
void all_good_predecessors (bool which)
{
  FLAGS ();
  if (which)
    { CLEAR (); RAW (0); }
  else
    { CLEAR (); RAW (0x02000000); }
  TURN ();
}

// { dg-final { scan-tree-dump-times "rotate chain collapse 8 -> 0 links" 4 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "crosslane-lane-state-unproven" 8 "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 64 } }
