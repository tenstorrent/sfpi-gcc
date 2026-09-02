/* TEN-2932 ENABLE_DEST_INDEX window enforcement (lane FG, X4;
   replay/MOP delivery vision lane FR).
   Copyright (C) 2026 Tenstorrent Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* -mtt-tensix-optimize-crosslane (default off; shared with the gimple
   fusion pass gimple-rvtt-crosslane.cc).

   THE PROBLEM (TEN-2932, Wormhole/Blackhole erratum; SFPCONFIG.md
   LaneConfig table).  While LaneConfig.ENABLE_DEST_INDEX is set, an
   instruction other than SFPLOAD / SFPLOADI / SFPSWAP / SFPTRANSP that
   writes LReg[4..7] is UnsupportedFunctionality.  The compiler had no
   model of this window: lane EX's bridge lifts caught a REAL
   allocator-inserted `SFPMOV L5, L4` inside an open window, and every
   bridged kernel has had to gate its emitted window content by
   disassembly inspection (sfpu_bridge.hpp discipline; the lane-EY-R
   design input names this the compiler-ownership item).

   THE MECHANISM.  The typed window toggles lower to the imm-form
   SFPCONFIG builtin (surface: sfpi_crosslane.h set_dest_index_window;
   hand kernels: TTI_SFPCONFIG(0x4|0x0, 0xF, 1)) -- DISTINGUISHABLE
   marker words this pass scopes windows by, on the FINAL RTL stream
   (after allocation, the hazard scheduler, and replay/MOP formation,
   so allocator copies and scheduled motion are all visible).  A
   three-state forward dataflow (CLOSED / OPEN / UNKNOWN, entry CLOSED,
   join of unequal states = UNKNOWN) tracks the bit:

     rvtt_sfpconfig_i to LaneConfig (dest 15):
       mod1 1 (write)      state = imm16 bit 2
       mod1 3 (OR)         imm16 bit 2 set   -> OPEN, else unchanged
       mod1 5 (AND)        imm16 bit 2 clear -> CLOSED, else unchanged
       mod1 7 (XOR)        imm16 bit 2 set   -> toggle (UNKNOWN stays)
     value-form SFPCONFIG to dest 15 (rvtt_sfpwriteconfig_v): UNKNOWN
       (the staged LReg[0] value is not tracked here; LaneConfig also
       carries ROW_MASK writes whose users never touch the window bit,
       so UNKNOWN only notes, never errors -- see below).

   Inside a proven-OPEN window, an insn that sets or clobbers a hard
   LReg in [4..7] and is not one of the four exempt opcode families is
   a hard, named USER ERROR (dest-index-window-violation) -- the
   compile-time spelling of the erratum, replacing the per-kernel
   disassembly gate.  Raw content (asm statements, synthesized opcode
   words) inside a proven-OPEN window cannot be audited and errors by
   name too (crosslane-window-raw-unproven), as do calls
   (crosslane-window-call-unproven).  SFPLOADMACRO is deliberately NOT
   exempt: TEN-2932 lists exactly four opcodes.

   UNKNOWN-state writes only DUMP a note (crosslane-window-state-
   unproven): the value form legitimately serves LaneConfig.ROW_MASK
   traffic whose imm never touches bit 2, and erroring there would
   punish kernels that never open a window.  A window left open at
   function exit is a NOTE as well, not an error: lane FD proved the
   packed-index kernels inherit an open window across phases
   deliberately (ENABLE_DEST_INDEX is store-visible state).

   REPLAY / MOP DELIVERY VISION (lane FR, closing lane FP's FP-2).
   The positional walk alone is blind to DELIVERED words: a TTREPLAY
   playback launch carries no LReg SET of its own, and the payload
   words sit at their record site -- where the state may be CLOSED --
   so the default-ON replay former could convert a hard 3-error
   TEN-2932 program into a silently-accepted binary that still
   executes the violating LReg writes in-window at playback (lane FP's
   pw1 witness).  The fix models the Replay Expander exactly
   (WormholeB0 REPLAY.md functional model):

     - a fixed capture (load=1) claims slots (Index+i)%32, i<Count,
       and its payload words are the next Count slot-occupying words
       in the stream (TYPE_TENSIX with nonzero length; the same shadow
       walk the replay former's own un-hoist sweep uses);
     - with Exec=0 the recorded words are SWALLOWED -- never issued --
       so they are exempt from diagnosis AND from marker state
       transfer AT THE RECORD SITE (the expander model: ingest, emit
       nothing);
     - a playback launch (load=0) EMITS the recorded slot words at the
       launch position, so the checker expands the resolved payload
       there: delivered non-exempt LReg4-7 writes inside a proven-OPEN
       window are the same hard dest-index-window-violation as inline
       words, and a delivered SFPCONFIG marker transfers the window
       state at the launch site (a captured OPEN/CLOSE re-scopes the
       window exactly where it plays back).

   Launch resolution is FAIL-CLOSED.  A launch resolves only when
   every slot it plays is covered by EXACTLY ONE record in the
   function, that record's span equals or covers the launch's span,
   the record's payload walk completed (single BB, no raw asm, no
   call, no nested REPLAY/MOP owner word, no volatile-memory delivery
   word before the count completes), and the record DOMINATES the
   launch (device-persistent slot state: a launch nothing dominates
   plays a previous invocation's -- unknowable -- content).  Any raw
   `.ttinsn' word carrying the architectural REPLAY opcode poisons all
   resolution (the replay former's own raw-capture census discipline),
   as does a capture with a non-constant or wrapping span (Count 0
   means 64: double-write wrap, refused).  An UNRESOLVED launch inside
   a proven-OPEN window is a named error
   (crosslane-window-replay-unproven); under CLOSED or UNKNOWN it
   degrades the state to UNKNOWN (delivered markers may have re-scoped
   the window) which notes, never errors -- the same honesty contract
   as the value-form marker.

   A TTMOP inside a proven-OPEN window errors by name
   (crosslane-window-mop-unproven): the MOP expander re-delivers
   replay playback words from a template this pass cannot audit
   positionally; after any TTMOP the state degrades to UNKNOWN.
   MOP-form and record-hoist deliveries are thereby the same covered
   class as the straight replay former (lane FP's composition matrix
   rows).

   All diagnostics are gated behind the default-off crosslane flag;
   code that never uses the typed markers is never diagnosed.  */

#define INCLUDE_MAP
#define INCLUDE_SET
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "df.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "diagnostic-core.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-raw-boundary.h"

namespace {

#define DUMP(...) if (dump_file) fprintf (dump_file, __VA_ARGS__)

/* LReg hard registers: SFPU_REG_FIRST + 0..7; the TEN-2932 window
   guards the companion bank L4..L7.  */
static const unsigned LREG4_REGNO = SFPU_REG_FIRST + 4;
static const unsigned LREG7_REGNO = SFPU_REG_FIRST + 7;

/* The architectural replay buffer size (REPLAY.md: Index+i mod 32).  */
static const unsigned REPLAY_SLOTS = 32;

enum window_state : unsigned char
{
  WS_CLOSED = 0,
  WS_OPEN = 1,
  WS_UNKNOWN = 2,
};

/* Dataflow join of the window states A and B: agreement survives, any
   disagreement is UNKNOWN.  */

static window_state
join_state (window_state a, window_state b)
{
  return a == b ? a : WS_UNKNOWN;
}

/* The four TEN-2932-exempt opcode families (SFPCONFIG.md LaneConfig
   table: SFPLOAD / SFPLOADI / SFPSWAP / SFPTRANSP), as the final
   define_insn codes the post-reload stream carries.  SFPLOADMACRO is
   deliberately absent.  */

static bool
window_exempt_code_p (int code)
{
  switch (code)
    {
    case CODE_FOR_rvtt_sfpload_lv_int:
    case CODE_FOR_rvtt_sfploaddiscard_int:
    case CODE_FOR_rvtt_sfploadsrcs_lv_int:
    case CODE_FOR_rvtt_sfploadi_lv_int:
    case CODE_FOR_rvtt_sfpswap_int:
    case CODE_FOR_rvtt_sfpswap_indexed_int:
    case CODE_FOR_rvtt_sfptransp_int:
    case CODE_FOR_rvtt_sfptransp8_int:
    case CODE_FOR_rvtt_sfptransp_gather_int:
      return true;
    default:
      return false;
    }
}

/* Does INSN write (set or clobber) a hard LReg in [4..7]?  Returns the
   first such regno + 1, else 0.  */

static unsigned
writes_companion_lreg (rtx_insn *insn)
{
  rtx pat = PATTERN (insn);
  unsigned found = 0;
  auto check = [&] (rtx x)
  {
    if ((GET_CODE (x) == SET || GET_CODE (x) == CLOBBER))
      {
	rtx d = XEXP (x, 0);
	if (REG_P (d))
	  {
	    unsigned r = REGNO (d);
	    unsigned n = REG_NREGS (d);
	    for (unsigned i = 0; i != n; ++i)
	      if (r + i >= LREG4_REGNO && r + i <= LREG7_REGNO)
		{
		  found = r + i + 1;
		  return;
		}
	  }
      }
  };
  if (GET_CODE (pat) == PARALLEL)
    for (int i = 0; i < XVECLEN (pat, 0) && !found; ++i)
      check (XVECEXP (pat, 0, i));
  else
    check (pat);
  return found;
}

/* SFPCONFIG marker decoding.  Returns true when INSN affects the
   window state and sets *NEXT accordingly.  */

static bool
marker_transfer (rtx_insn *insn, window_state cur, window_state *next)
{
  int code = recog_memoized (insn);
  if (code == CODE_FOR_rvtt_sfpconfig_i)
    {
      rtx pat = PATTERN (insn);
      /* (unspec_volatile [imm16 dest mod1] ...) */
      rtx body = pat;
      if (GET_CODE (body) == PARALLEL)
	body = XVECEXP (body, 0, 0);
      if (GET_CODE (body) != UNSPEC_VOLATILE)
	return false;
      unsigned imm16 = UINTVAL (XVECEXP (body, 0, 0));
      unsigned dest = UINTVAL (XVECEXP (body, 0, 1));
      unsigned mod1 = UINTVAL (XVECEXP (body, 0, 2));
      if (dest != 15)
	return false;		/* not LaneConfig */
      bool bit = (imm16 >> 2) & 1;
      switch (mod1)
	{
	case 1:			/* write, imm is value */
	  *next = bit ? WS_OPEN : WS_CLOSED;
	  return true;
	case 3:			/* OR */
	  if (bit)
	    {
	      *next = WS_OPEN;
	      return true;
	    }
	  return false;
	case 5:			/* AND */
	  if (!bit)
	    {
	      *next = WS_CLOSED;
	      return true;
	    }
	  return false;
	case 7:			/* XOR */
	  if (bit)
	    {
	      *next = cur == WS_OPEN ? WS_CLOSED
		      : cur == WS_CLOSED ? WS_OPEN : WS_UNKNOWN;
	      return true;
	    }
	  return false;
	default:
	  *next = WS_UNKNOWN;	/* lane-mask forms etc.: untracked */
	  return true;
	}
    }
  if (code == CODE_FOR_rvtt_sfpwriteconfig_v)
    {
      /* Value form: the destination operand rides the pattern; only
	 dest 15 (LaneConfig) is state-relevant.  */
      rtx pat = PATTERN (insn);
      rtx body = pat;
      if (GET_CODE (body) == PARALLEL)
	body = XVECEXP (body, 0, 0);
      if (GET_CODE (body) == UNSPEC_VOLATILE && XVECLEN (body, 0) >= 2)
	{
	  rtx dst = XVECEXP (body, 0, 1);
	  if (CONST_INT_P (dst) && UINTVAL (dst) == 15)
	    {
	      *next = WS_UNKNOWN;
	      return true;
	    }
	}
      return false;
    }
  return false;
}

/* Non-empty asm detection (an empty template is the compiler
   memory-clobber idiom and emits nothing).  */

static bool
nonempty_asm_p (rtx_insn *insn)
{
  rtx pat = PATTERN (insn);
  if (asm_noperands (pat) < 0 && GET_CODE (pat) != ASM_INPUT)
    return false;
  const char *s = nullptr;
  if (GET_CODE (pat) == ASM_INPUT)
    s = XSTR (pat, 0);
  else if (rtx aop = extract_asm_operands (pat))
    s = ASM_OPERANDS_TEMPLATE (aop);
  if (s)
    {
      while (*s == ' ' || *s == '\t')
	++s;
      if (!*s)
	return false;
    }
  return true;
}

/* TTREPLAY owner-word decoding (rvtt_ttreplay_int: operand 3 = count
   imm or variable reg, operand 5 = slot index, operand 6 =
   exec-while-load, operand 7 = load).  */

struct replay_ref
{
  bool load;			/* capture (1) or playback (0) */
  bool exec;			/* exec-while-load */
  bool has_span;		/* begin and len both constant */
  unsigned begin;
  unsigned len;
};

/* Return true when INSN is the rvtt_ttreplay_int owner word, filling *R
   with its load and exec flags and, when index and count are both
   constant, the slot span (begin reduced modulo the 32-slot buffer).  */

static bool
decode_ttreplay (rtx_insn *insn, replay_ref *r)
{
  if (recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
    return false;
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) != UNSPEC_VOLATILE)
    return false;
  rtx len = XVECEXP (pat, 0, 3);
  rtx begin = XVECEXP (pat, 0, 5);
  r->load = XVECEXP (pat, 0, 7) != const0_rtx;
  r->exec = XVECEXP (pat, 0, 6) != const0_rtx;
  r->has_span = CONST_INT_P (len) && CONST_INT_P (begin);
  r->begin = 0;
  r->len = 0;
  if (r->has_span)
    {
      r->begin = UINTVAL (begin) % REPLAY_SLOTS;
      r->len = UINTVAL (len);
    }
  return true;
}

/* Slot bitmask for a span; a span this pass refuses to model (Count 0
   = 64-word double-write wrap, or Count > 32) claims ALL slots so it
   poisons every intersecting resolution.  */

static uint32_t
span_slots (bool sane, unsigned begin, unsigned len)
{
  if (!sane)
    return ~(uint32_t) 0;
  uint32_t m = 0;
  for (unsigned i = 0; i != len; ++i)
    m |= (uint32_t) 1 << ((begin + i) % REPLAY_SLOTS);
  return m;
}

/* A span this pass models: R has constant begin and count, with count
   in [1..32] (Count 0 means 64 words -- the double-write wrap).  */

static bool
span_sane_p (const replay_ref &r)
{
  return r.has_span && r.len >= 1 && r.len <= REPLAY_SLOTS;
}

/* One fixed capture and its recorded payload.  */

struct replay_record
{
  rtx_insn *cap = nullptr;
  unsigned begin = 0;
  unsigned len = 0;
  bool exec = false;
  bool resolved = false;	/* payload walk completed */
  uint32_t slots = 0;
  std::vector<rtx_insn *> payload;
};

/* A resolved launch: which record delivers it, at which payload
   offset, for how many words.  */

struct launch_resolution
{
  int rec = -1;			/* index into m_records, -1 unresolved */
  unsigned off = 0;
  unsigned len = 0;
};

class window_check
{
public:
  window_check (function *fn) : m_fn (fn) {}
  void run ();

private:
  void collect ();
  void resolve ();
  bool record_dominates_p (rtx_insn *cap, rtx_insn *launch);
  window_state transfer (rtx_insn *insn, window_state st);
  window_state expand_launch (rtx_insn *launch, window_state st,
			      bool diagnose_p);
  void diagnose (rtx_insn *insn, window_state st);
  void diagnose_delivered (rtx_insn *pw, rtx_insn *launch, window_state st);

  function *m_fn;
  std::vector<replay_record> m_records;
  std::set<rtx_insn *> m_noexec_payload;
  std::map<rtx_insn *, launch_resolution> m_launches;
  std::map<rtx_insn *, unsigned> m_pos;
  std::set<rtx_insn *> m_delivered_diagnosed;
  bool m_poison = false;	/* raw REPLAY word / unmodelable capture */
};

/* Pass 1: number the stream, census raw REPLAY-opcode words, collect
   fixed captures and their payload shadows (the replay former's own
   un-hoist walk recipe), and enumerate launches.  */

void
window_check::collect ()
{
  unsigned pos = 0;
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  m_pos[insn] = pos++;

	  /* Raw-capture census (the replay former's own discipline): a
	     hand-authored raw word carrying the architectural REPLAY
	     opcode records or plays slots this pass cannot see.  */
	  uint32_t raw_word;
	  if (asm_noperands (PATTERN (insn)) >= 0
	      && rvtt_raw_ttinsn_word (insn, &raw_word)
	      && rvtt_raw_replay_owner_word_p (raw_word))
	    {
	      DUMP ("crosslane-window: raw .ttinsn REPLAY word 0x%08x "
		    "(insn %d) poisons replay resolution\n",
		    raw_word, INSN_UID (insn));
	      m_poison = true;
	      continue;
	    }

	  replay_ref rr;
	  if (!decode_ttreplay (insn, &rr))
	    continue;
	  if (!rr.load)
	    {
	      m_launches[insn] = launch_resolution ();
	      continue;
	    }

	  replay_record rec;
	  rec.cap = insn;
	  rec.exec = rr.exec;
	  bool sane = span_sane_p (rr);
	  rec.begin = rr.begin;
	  rec.len = rr.len;
	  rec.slots = span_slots (sane, rr.begin, rr.len);
	  rec.resolved = false;

	  if (sane)
	    {
	      /* Payload shadow walk (unhoist_hazard_rerecords recipe,
		 hardened fail-closed: raw asm, calls, jumps, nested
		 owner words, volatile-memory delivery words, and
		 block exit all refuse resolution).  */
	      bool scan_ok = true;
	      unsigned remaining = rec.len;
	      for (rtx_insn *nxt = NEXT_INSN (insn); remaining;
		   nxt = NEXT_INSN (nxt))
		{
		  if (!nxt || BLOCK_FOR_INSN (nxt) != bb)
		    {
		      scan_ok = false;
		      break;
		    }
		  if (!NONDEBUG_INSN_P (nxt))
		    continue;
		  if (CALL_P (nxt) || JUMP_P (nxt))
		    {
		      scan_ok = false;
		      break;
		    }
		  rtx ppat = PATTERN (nxt);
		  if (GET_CODE (ppat) == USE || GET_CODE (ppat) == CLOBBER)
		    continue;
		  if (asm_noperands (ppat) >= 0
		      || GET_CODE (ppat) == ASM_INPUT)
		    {
		      if (nonempty_asm_p (nxt))
			{
			  scan_ok = false;
			  break;
			}
		      continue;
		    }
		  int pcode = recog_memoized (nxt);
		  if (pcode < 0
		      || pcode == CODE_FOR_rvtt_ttreplay_int
		      || pcode == CODE_FOR_rvtt_ttmop_int
		      || pcode == CODE_FOR_rvtt_ttmopcfg_int)
		    {
		      /* Unrecognized content or a nested owner word:
			 the shadow cannot be modeled.  */
		      scan_ok = false;
		      break;
		    }
		  if (get_attr_type (nxt) != TYPE_TENSIX)
		    {
		      /* Scalar work is not recorded, but a volatile
			 memory access may be a computed
			 instruction-FIFO push whose word IS.  */
		      if (volatile_refs_p (ppat))
			{
			  scan_ok = false;
			  break;
			}
		      continue;
		    }
		  if (!get_attr_length (nxt))
		    continue;	/* zero-length bookkeeping */
		  rec.payload.push_back (nxt);
		  --remaining;
		}
	      rec.resolved = scan_ok;
	    }

	  if (!rec.resolved)
	    DUMP ("crosslane-window: capture insn %d unmodelable "
		  "(%s); intersecting launches will not resolve\n",
		  INSN_UID (insn),
		  sane ? "payload shadow walk refused" : "span not modeled");
	  m_records.push_back (rec);
	}
    }
}

/* Does CAP dominate LAUNCH?  Same-BB uses stream order; cross-BB uses
   CFG dominance (every path to the launch re-records first, so the
   delivered content is this record's on every invocation).  */

bool
window_check::record_dominates_p (rtx_insn *cap, rtx_insn *launch)
{
  basic_block cb = BLOCK_FOR_INSN (cap);
  basic_block lb = BLOCK_FOR_INSN (launch);
  if (!cb || !lb)
    return false;
  if (cb == lb)
    return m_pos[cap] < m_pos[launch];
  return dominated_by_p (CDI_DOMINATORS, lb, cb);
}

/* Pass 2: resolve each launch fail-closed.  */

void
window_check::resolve ()
{
  for (auto &lp : m_launches)
    {
      rtx_insn *launch = lp.first;
      launch_resolution &res = lp.second;
      res.rec = -1;

      if (m_poison)
	continue;
      replay_ref rr;
      if (!decode_ttreplay (launch, &rr) || !span_sane_p (rr))
	continue;
      uint32_t lslots = span_slots (true, rr.begin, rr.len);

      int found = -1;
      bool ambiguous = false;
      for (unsigned i = 0; i != m_records.size (); ++i)
	if (m_records[i].slots & lslots)
	  {
	    if (found >= 0)
	      ambiguous = true;
	    found = i;
	  }
      if (found < 0 || ambiguous)
	continue;
      const replay_record &rec = m_records[found];
      if (!rec.resolved
	  || (lslots & ~rec.slots)
	  || rec.payload.size () != rec.len
	  || !record_dominates_p (rec.cap, launch))
	continue;

      res.rec = found;
      res.off = (rr.begin + REPLAY_SLOTS - rec.begin) % REPLAY_SLOTS;
      res.len = rr.len;
      /* Every played slot must map inside the record's payload.  */
      bool in_range = true;
      for (unsigned i = 0; i != res.len; ++i)
	if ((res.off + i) % REPLAY_SLOTS >= rec.len)
	  {
	    in_range = false;
	    break;
	  }
      if (!in_range)
	res.rec = -1;
    }
}

/* State transfer for one insn, replay-aware; used identically by the
   fixpoint and the diagnostic walk (diagnose_p distinguishes them so
   the two can never disagree about the state an insn executes in).  */

window_state
window_check::expand_launch (rtx_insn *launch, window_state st,
			     bool diagnose_p)
{
  auto it = m_launches.find (launch);
  gcc_checking_assert (it != m_launches.end ());
  /* Fail closed if the collection walk somehow missed this launch.  */
  static const launch_resolution unresolved;
  const launch_resolution &res
    = it == m_launches.end () ? unresolved : it->second;
  if (res.rec < 0)
    {
      if (diagnose_p)
	{
	  if (st == WS_OPEN)
	    error_at (INSN_LOCATION (launch),
		      "crosslane-window-replay-unproven: replay launch "
		      "inside an ENABLE_DEST_INDEX window delivers "
		      "recorded content this compilation cannot audit "
		      "against TEN-2932 (no dominating single-record "
		      "resolution for the played slots)");
	  else if (st == WS_UNKNOWN)
	    DUMP ("crosslane-window: note crosslane-window-replay-unproven "
		  "at insn %d (unresolved launch under unproven window "
		  "state)\n", INSN_UID (launch));
	}
      /* Delivered markers may have re-scoped the window.  */
      return WS_UNKNOWN;
    }

  const replay_record &rec = m_records[res.rec];
  for (unsigned i = 0; i != res.len; ++i)
    {
      rtx_insn *pw = rec.payload[(res.off + i) % REPLAY_SLOTS];
      window_state nx;
      if (marker_transfer (pw, st, &nx))
	{
	  /* A delivered marker re-scopes the window at the launch
	     site; the marker word itself is exempt, as inline.  */
	  st = nx;
	  continue;
	}
      if (diagnose_p)
	diagnose_delivered (pw, launch, st);
    }
  return st;
}

/* Window state after INSN executes starting in ST.  Swallowed no-exec
   payload words transfer nothing; a TTMOP degrades to UNKNOWN; a
   capture is transparent when modeled (or when it executes while
   loading) and degrades to UNKNOWN otherwise; a playback launch expands
   its delivered words without diagnosing; everything else follows the
   SFPCONFIG marker decoding.  */

window_state
window_check::transfer (rtx_insn *insn, window_state st)
{
  /* Words being recorded without execution are swallowed by the
     Replay Expander (REPLAY.md: ingest, emit nothing) -- they neither
     transfer state nor execute at the record site.  */
  if (m_noexec_payload.count (insn))
    return st;

  int code = recog_memoized (insn);
  if (code == CODE_FOR_rvtt_ttmop_int)
    /* The MOP expander re-delivers replay content this pass cannot
       audit positionally.  */
    return WS_UNKNOWN;

  replay_ref rr;
  if (decode_ttreplay (insn, &rr))
    {
      if (rr.load)
	{
	  if (rr.exec)
	    return st;		/* payload executes inline: no change */
	  /* No-exec capture: a modeled one is fully accounted by the
	     m_noexec_payload skip; an unmodelable one leaves the set
	     of swallowed words unknown.  */
	  for (const replay_record &rec : m_records)
	    if (rec.cap == insn)
	      return rec.resolved ? st : WS_UNKNOWN;
	  return WS_UNKNOWN;
	}
      return expand_launch (insn, st, false);
    }

  window_state next;
  if (marker_transfer (insn, st, &next))
    return next;
  return st;
}

/* Diagnose one inline (non-delivered) INSN executing under window state
   ST.  Under proven-OPEN, raw assembly and calls error as unauditable,
   and a non-exempt write of an LReg in [4..7] is the hard
   dest-index-window-violation; under UNKNOWN such a write only dumps a
   note.  Zero-length bookkeeping insns emit no architectural word and
   are never diagnosed.  */

void
window_check::diagnose (rtx_insn *insn, window_state st)
{
  if (!NONDEBUG_INSN_P (insn))
    return;
  int code = recog_memoized (insn);

  /* Zero-length bookkeeping insns (the fixed-LReg readlreg/writelreg
     pinning markers, "# READ Ln"/"# WRITE Ln") emit no architectural
     word: only the instruction that actually computes or moves the
     value can violate the window, and it is diagnosed where it
     stands.  */
  if (code >= 0 && get_attr_length (insn) == 0)
    return;

  if (st == WS_OPEN)
    {
      if (asm_noperands (PATTERN (insn)) >= 0
	  || GET_CODE (PATTERN (insn)) == ASM_INPUT)
	{
	  if (!nonempty_asm_p (insn))
	    return;
	  error_at (INSN_LOCATION (insn),
		    "crosslane-window-raw-unproven: raw assembly inside an "
		    "ENABLE_DEST_INDEX window cannot be audited against "
		    "TEN-2932 (only SFPLOAD/SFPLOADI/SFPSWAP/SFPTRANSP may "
		    "write %<LReg4%>..%<LReg7%> while the window is open)");
	  return;
	}
      if (CALL_P (insn))
	{
	  error_at (INSN_LOCATION (insn),
		    "crosslane-window-call-unproven: call inside an "
		    "ENABLE_DEST_INDEX window cannot be audited against "
		    "TEN-2932");
	  return;
	}
      unsigned hit = writes_companion_lreg (insn);
      if (hit && !window_exempt_code_p (code))
	error_at (INSN_LOCATION (insn),
		  "dest-index-window-violation: instruction writes "
		  "%<LReg%d%> inside an ENABLE_DEST_INDEX window "
		  "(TEN-2932: only SFPLOAD/SFPLOADI/SFPSWAP/SFPTRANSP "
		  "may write %<LReg4%>..%<LReg7%> while "
		  "%<LaneConfig.ENABLE_DEST_INDEX%> is set)",
		  (int) (hit - 1 - SFPU_REG_FIRST));
    }
  else if (st == WS_UNKNOWN)
    {
      unsigned hit = writes_companion_lreg (insn);
      if (hit && !window_exempt_code_p (code))
	DUMP ("crosslane-window: note crosslane-window-state-unproven at "
	      "insn %d (LReg%d write under unproven window state)\n",
	      INSN_UID (insn), (int) (hit - 1 - SFPU_REG_FIRST));
    }
}

/* A delivered payload word executing at LAUNCH under state ST.  The
   payload walk already excluded asm, calls, and owner words, so only
   the LReg-write check remains.  Deduplicated per payload insn: the
   first in-window launch reports each violating word once.  */

void
window_check::diagnose_delivered (rtx_insn *pw, rtx_insn *launch,
				  window_state st)
{
  int code = recog_memoized (pw);
  unsigned hit = writes_companion_lreg (pw);
  if (!hit || window_exempt_code_p (code))
    return;
  if (st == WS_OPEN)
    {
      if (!m_delivered_diagnosed.insert (pw).second)
	return;
      error_at (INSN_LOCATION (launch),
		"dest-index-window-violation: replay launch delivers a "
		"recorded instruction writing %<LReg%d%> inside an "
		"ENABLE_DEST_INDEX window (TEN-2932: only "
		"SFPLOAD/SFPLOADI/SFPSWAP/SFPTRANSP may write "
		"%<LReg4%>..%<LReg7%> while "
		"%<LaneConfig.ENABLE_DEST_INDEX%> is set; the write was "
		"recorded at a site where the window was not open)",
		(int) (hit - 1 - SFPU_REG_FIRST));
      if (INSN_LOCATION (pw) != INSN_LOCATION (launch))
	inform (INSN_LOCATION (pw),
		"the delivered instruction was recorded here");
    }
  else if (st == WS_UNKNOWN)
    DUMP ("crosslane-window: note crosslane-window-state-unproven at "
	  "launch insn %d (delivered LReg%d write under unproven window "
	  "state)\n", INSN_UID (launch), (int) (hit - 1 - SFPU_REG_FIRST));
}

/* Whole-function driver.  Collect captures and launches, resolve the
   launches fail-closed, mark the swallowed no-exec payload words, run
   the three-state forward fixpoint over the CFG (entry state CLOSED),
   then walk each reached block once more diagnosing every insn under
   the state it executes in (TTMOPs and launches expand in place).  A
   window still open at function exit is only noted.  */

void
window_check::run ()
{
  calculate_dominance_info (CDI_DOMINATORS);
  collect ();
  resolve ();
  for (const replay_record &rec : m_records)
    if (rec.resolved && !rec.exec)
      for (rtx_insn *pw : rec.payload)
	m_noexec_payload.insert (pw);

  unsigned n = last_basic_block_for_fn (m_fn);
  std::vector<window_state> in (n, WS_CLOSED), out (n, WS_CLOSED);
  std::vector<bool> reached (n, false);

  /* Fixpoint over the CFG; entry state CLOSED.  */
  bool changed = true;
  while (changed)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, m_fn)
	{
	  window_state st;
	  bool first = true;
	  bool from_entry = false;
	  edge e;
	  edge_iterator ei;
	  st = WS_CLOSED;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      window_state ps;
	      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (m_fn))
		{
		  ps = WS_CLOSED;
		  from_entry = true;
		}
	      else if (!reached[e->src->index])
		continue;
	      else
		ps = out[e->src->index];
	      st = first ? ps : join_state (st, ps);
	      first = false;
	    }
	  if (first && !from_entry)
	    continue;		/* unreached so far */
	  if (!reached[bb->index] || st != in[bb->index])
	    {
	      in[bb->index] = st;
	      reached[bb->index] = true;
	      changed = true;
	    }
	  window_state cur = in[bb->index];
	  rtx_insn *insn;
	  FOR_BB_INSNS (bb, insn)
	    if (NONDEBUG_INSN_P (insn))
	      cur = transfer (insn, cur);
	  if (cur != out[bb->index])
	    {
	      out[bb->index] = cur;
	      changed = true;
	    }
	}
    }

  /* Diagnostic walk.  */
  basic_block bb;
  bool any_open_exit = false;
  FOR_EACH_BB_FN (bb, m_fn)
    {
      if (!reached[bb->index])
	continue;
      window_state cur = in[bb->index];
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  /* Swallowed record-site words: never executed here (they are
	     diagnosed at each launch that delivers them).  */
	  if (m_noexec_payload.count (insn))
	    continue;
	  int code = recog_memoized (insn);
	  if (code == CODE_FOR_rvtt_ttmop_int)
	    {
	      if (cur == WS_OPEN)
		error_at (INSN_LOCATION (insn),
			  "crosslane-window-mop-unproven: MOP inside an "
			  "ENABLE_DEST_INDEX window re-delivers replay "
			  "content this compilation cannot audit against "
			  "TEN-2932");
	      else if (cur == WS_UNKNOWN)
		DUMP ("crosslane-window: note crosslane-window-mop-unproven "
		      "at insn %d (MOP under unproven window state)\n",
		      INSN_UID (insn));
	      cur = WS_UNKNOWN;
	      continue;
	    }
	  replay_ref rr;
	  if (decode_ttreplay (insn, &rr))
	    {
	      if (rr.load)
		cur = transfer (insn, cur);   /* captures never diagnose */
	      else
		cur = expand_launch (insn, cur, true);
	      continue;
	    }
	  window_state next = transfer (insn, cur);
	  /* Diagnose the insn under the state it EXECUTES in; a marker
	     itself is exempt.  */
	  if (next == cur)
	    diagnose (insn, cur);
	  cur = next;
	}
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest == EXIT_BLOCK_PTR_FOR_FN (m_fn) && cur == WS_OPEN)
	  any_open_exit = true;
    }
  if (any_open_exit)
    DUMP ("crosslane-window: note window open at function exit "
	  "(deliberate cross-phase inheritance is a documented pattern; "
	  "lane FD)\n");
  free_dominance_info (CDI_DOMINATORS);
}

const pass_data pass_data_rvtt_crosslane_window =
{
  RTL_PASS, /* type */
  "rvtt_crosslane_window", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_crosslane_window : public rtl_opt_pass
{
public:
  pass_rvtt_crosslane_window (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_crosslane_window, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
	   && (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH)
	   && riscv_tt_opt_crosslane > 0;
  }

  unsigned int execute (function *fn) final override
  {
    window_check wc (fn);
    wc.run ();
    return 0;
  }
}; /* class pass_rvtt_crosslane_window */

} /* anon namespace */

/* Instantiate the window-enforcement pass for CTXT; rvtt-passes.def
   places it before free_cfg, after every code-motion pass, and it gates
   on a WH/BH Tensix target plus -mtt-tensix-optimize-crosslane.  */

rtl_opt_pass *
make_pass_rvtt_crosslane_window (gcc::context *ctxt)
{
  return new pass_rvtt_crosslane_window (ctxt);
}
