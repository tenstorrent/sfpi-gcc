/* TEN-2932 ENABLE_DEST_INDEX window enforcement (lane FG, X4).
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

   All diagnostics are gated behind the default-off crosslane flag;
   code that never uses the typed markers is never diagnosed.  */

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
#include "diagnostic-core.h"
#include "rvtt-protos.h"
#include "rvtt.h"

namespace {

#define DUMP(...) if (dump_file) fprintf (dump_file, __VA_ARGS__)

/* LReg hard registers: SFPU_REG_FIRST + 0..7; the TEN-2932 window
   guards the companion bank L4..L7.  */
static const unsigned LREG4_REGNO = SFPU_REG_FIRST + 4;
static const unsigned LREG7_REGNO = SFPU_REG_FIRST + 7;

enum window_state : unsigned char
{
  WS_CLOSED = 0,
  WS_OPEN = 1,
  WS_UNKNOWN = 2,
};

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

/* Per-insn transfer WITHOUT diagnostics (for the fixpoint).  */

static window_state
transfer_insn (rtx_insn *insn, window_state st)
{
  window_state next;
  if (marker_transfer (insn, st, &next))
    return next;
  return st;
}

class window_check
{
public:
  window_check (function *fn) : m_fn (fn) {}
  void run ();

private:
  void diagnose (rtx_insn *insn, window_state st);
  function *m_fn;
};

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
	  /* An empty template emits nothing (the compiler memory-clobber
	     idiom).  */
	  const char *s = nullptr;
	  if (GET_CODE (PATTERN (insn)) == ASM_INPUT)
	    s = XSTR (PATTERN (insn), 0);
	  else if (rtx aop = extract_asm_operands (PATTERN (insn)))
	    s = ASM_OPERANDS_TEMPLATE (aop);
	  if (s)
	    {
	      while (*s == ' ' || *s == '\t')
		++s;
	      if (!*s)
		return;
	    }
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

void
window_check::run ()
{
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
	      cur = transfer_insn (insn, cur);
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
	  window_state next = transfer_insn (insn, cur);
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
}

const pass_data pass_data_rvtt_crosslane_window =
{
  RTL_PASS, /* type */
  "rvtt_crosslane_window", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
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
}; // class pass_rvtt_crosslane_window

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_crosslane_window (gcc::context *ctxt)
{
  return new pass_rvtt_crosslane_window (ctxt);
}
