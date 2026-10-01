// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

// Regression witness for the interposed-replay-owner sequence boundary
// (a demonstrated wrong-code fix): the word-exact scan used to treat an
// interposed replay owner as transparent to sequence continuity, so a
// repeated word-exact run split around an owner still matched and the
// formed capture's RECORDING physically swallowed the owner word (a
// REPLAY issued while recording is recorded, not executed) -- playback
// then decodes a REPLAY from the buffer (pinned BH sim: "REPLAY should
// not exist at this stage").  Production genesis: counted-row formation
// leaves its inline reference record mid-row (tanh/tanh-fresh/addcmul/
// lerp simulator failures).  Here the owners are planted with
// the explicit ttreplay builtin, which reaches the same scan_insns
// owner arm without depending on counted-row formation or allocator
// behavior: every value chains through one lreg, so the words are
// identical by construction.
//
// A replay owner must END sequence continuity: neither function below
// may form a capture whose span crosses the interposed owner.  On the
// unfixed compiler both do (interposed_playback_owner: a formed
// "TTREPLAY 4, 4, 1, 1" records MUL, MUL, <owner>, MUL;
// interposed_capture_owner: a formed "TTREPLAY 2, 4, 1, 1" records
// MUL, MUL, MUL, <owner>), and the trailing formed playback re-issues
// the recorded owner.

// Playback-owner flavor: an exec-only owner (the shape a counted-row
// launch leaves mid-row) splits eleven identical words 5 | owner | 6.
// No run on either side repeats profitably, so the only replay words
// in the fixed output are the two explicit owners.
void interposed_playback_owner ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (0, x, 0, 0, 0, 0, 0);
}

/*
**_Z25interposed_playback_ownerv:
**	TTREPLAY	0, 4, 1, 1
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 4, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/

// Fixed-capture-owner flavor: the owner records its own two-word
// shadow (the shape a counted-row inline reference record leaves
// mid-row).  Words split 3 | owner+shadow | 5; on the unfixed compiler
// the imagined 8-run's first-half record straddles the owner.
void interposed_capture_owner ()
{
  auto x = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_ttreplay (nullptr, 2, 0, 0, 0, 0, 1);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (0, x, 0, 0, 0, 0, 0);
}

/*
**_Z24interposed_capture_ownerv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 2, 0, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/

// Control: the same word inventory with no interposed owner still
// forms (record five, play five) -- the barrier keys on the owner, not
// on the shape, the opcode, or a general formation refusal.
void owner_free_control ()
{
  auto x = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (0, x, 0, 0, 0, 0, 0);
}

/*
**_Z18owner_free_controlv:
**	SFPLOAD	L0, 0, 0, 0
**	TTREPLAY	0, 5, 1, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 5, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/
