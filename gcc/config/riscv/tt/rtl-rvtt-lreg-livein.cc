/* Make raw LLK L-register accesses visible to IRA.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "memmodel.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "rvtt.h"

namespace {

/* Metadata builtins name raw architectural L-register accesses.  Raw .ttinsn
   otherwise has no RTL def/use, so IRA is free to reuse a future input (for
   example L1 after a raw SFPLOAD) as a vFloat temporary.  A zero-length,
   fixed-register read creates a normal IRA interval.  The artificial value is
   used at each local endpoint; joins get a fresh local token, deliberately
   avoiding a cross-CFG pseudo/phi while conservatively reserving the LREG.  */

static int
read_lregno (rtx_insn *insn)
{
  switch (recog_memoized (insn))
    {
    case CODE_FOR_rvtt_sfpreadlreg0: return 0;
    case CODE_FOR_rvtt_sfpreadlreg1: return 1;
    case CODE_FOR_rvtt_sfpreadlreg2: return 2;
    case CODE_FOR_rvtt_sfpreadlreg3: return 3;
    case CODE_FOR_rvtt_sfpreadlreg4: return 4;
    case CODE_FOR_rvtt_sfpreadlreg5: return 5;
    case CODE_FOR_rvtt_sfpreadlreg6: return 6;
    case CODE_FOR_rvtt_sfpreadlreg7: return 7;
    default: return -1;
    }
}

static int
write_lregno (rtx_insn *insn)
{
  switch (recog_memoized (insn))
    {
    case CODE_FOR_rvtt_sfpwritelreg0: return 0;
    case CODE_FOR_rvtt_sfpwritelreg1: return 1;
    case CODE_FOR_rvtt_sfpwritelreg2: return 2;
    case CODE_FOR_rvtt_sfpwritelreg3: return 3;
    case CODE_FOR_rvtt_sfpwritelreg4: return 4;
    case CODE_FOR_rvtt_sfpwritelreg5: return 5;
    case CODE_FOR_rvtt_sfpwritelreg6: return 6;
    case CODE_FOR_rvtt_sfpwritelreg7: return 7;
    default: return -1;
    }
}

static bool
raw_access_p (rtx_insn *insn, unsigned *release_mask, unsigned *write_mask)
{
  if (recog_memoized (insn) != CODE_FOR_rvtt_sfprawlreg_access)
    return false;

  rtx pat = PATTERN (insn);
  gcc_assert (GET_CODE (pat) == UNSPEC_VOLATILE
              && XINT (pat, 1) == UNSPECV_SFPRAWLREG_ACCESS);
  *release_mask = UINTVAL (XVECEXP (pat, 0, 0)) & 0xff;
  *write_mask = UINTVAL (XVECEXP (pat, 0, 1)) & 0xff;
  return true;
}

static rtx
make_sentinel (unsigned regno, rtx value)
{
  switch (regno)
    {
    case 0: return gen_rvtt_sfpreadlreg0 (value);
    case 1: return gen_rvtt_sfpreadlreg1 (value);
    case 2: return gen_rvtt_sfpreadlreg2 (value);
    case 3: return gen_rvtt_sfpreadlreg3 (value);
    case 4: return gen_rvtt_sfpreadlreg4 (value);
    case 5: return gen_rvtt_sfpreadlreg5 (value);
    case 6: return gen_rvtt_sfpreadlreg6 (value);
    case 7: return gen_rvtt_sfpreadlreg7 (value);
    default: gcc_unreachable ();
    }
}

static rtx_insn *
emit_sentinel_after (unsigned regno, rtx value, rtx_insn *after)
{
  return emit_insn_after (make_sentinel (regno, value), after);
}

static rtx_insn *
emit_sentinel_before (unsigned regno, rtx value, rtx_insn *before)
{
  return emit_insn_before (make_sentinel (regno, value), before);
}

static void
end_sentinel (rtx value, rtx_insn *before)
{
  if (value)
    emit_insn_before (gen_rtx_USE (VOIDmode, value), before);
}

/* Keep VALUE live through the final instruction in BB.  A USE before a
   non-jump BB_END leaves that final instruction free to reuse VALUE's hard
   LREG, so append the USE and let emit_insn_after extend BB_END.  A jump may
   not be followed by an instruction in its block, but it cannot define an
   allocatable SFPU value, so a USE immediately before it is sufficient.  */
static void
end_sentinel_at_block_end (rtx value, basic_block bb)
{
  if (!value)
    return;

  rtx_insn *last = BB_END (bb);
  if (JUMP_P (last))
    end_sentinel (value, last);
  else
    emit_insn_after (gen_rtx_USE (VOIDmode, value), last);
}

static unsigned
transfer_block (basic_block bb, unsigned live)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
        continue;
      unsigned releases, writes;
      if (raw_access_p (insn, &releases, &writes))
	live = (live & ~releases) | writes;
      else
        {
          int regno = read_lregno (insn);
          if (regno < 0)
            regno = write_lregno (insn);
          if (regno >= 0)
            live &= ~(1u << regno);
        }
    }
  return live;
}

static void
make_raw_lregs_live (function *fn)
{
  const unsigned n_bbs = last_basic_block_for_fn (fn);
  auto_vec<unsigned> in (n_bbs), out (n_bbs);
  in.safe_grow_cleared (n_bbs);
  out.safe_grow_cleared (n_bbs);

  bool changed;
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
        {
          unsigned next_in = 0;
          edge e;
          edge_iterator ei;
          FOR_EACH_EDGE (e, ei, bb->preds)
            next_in |= out[e->src->index];
          unsigned next_out = transfer_block (bb, next_in);
          if (next_in != in[bb->index] || next_out != out[bb->index])
            {
              in[bb->index] = next_in;
              out[bb->index] = next_out;
              changed = true;
            }
        }
    }
  while (changed);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx live[8] = {};
      rtx_insn *producer[8] = {};
      rtx_insn *first = NULL;
      for (rtx_insn *insn = BB_HEAD (bb);; insn = NEXT_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn))
	    {
	      first = insn;
	      break;
	    }
	  if (insn == BB_END (bb))
	    break;
	}
      if (!first)
        continue;

      for (unsigned regno = 0; regno != 8; ++regno)
        if (in[bb->index] & (1u << regno))
          {
            rtx value = gen_reg_rtx (XTT32SImode);
            live[regno] = value;
            producer[regno] = emit_sentinel_before (regno, value, first);
          }

      for (rtx_insn *insn = BB_HEAD (bb), *next;
           insn != NEXT_INSN (BB_END (bb)); insn = next)
        {
          next = NEXT_INSN (insn);
          if (!NONDEBUG_INSN_P (insn))
            continue;

	  unsigned releases, writes;
	  if (raw_access_p (insn, &releases, &writes))
            {
              for (unsigned regno = 0; regno != 8; ++regno)
		if (releases & (1u << regno))
                  {
                    end_sentinel (live[regno], insn);
                    live[regno] = NULL_RTX;
                    producer[regno] = NULL;
                  }
              for (unsigned regno = 0; regno != 8; ++regno)
                if (writes & (1u << regno))
                  {
                    end_sentinel (live[regno], insn);
                    rtx value = gen_reg_rtx (XTT32SImode);
                    producer[regno] = emit_sentinel_after (regno, value, insn);
                    live[regno] = value;
                  }
              continue;
            }

          int regno = read_lregno (insn);
          if (regno < 0)
            regno = write_lregno (insn);
          if (regno >= 0)
            {
              if (insn == producer[regno])
                continue;
              end_sentinel (live[regno], insn);
              live[regno] = NULL_RTX;
              producer[regno] = NULL;
            }
        }

      /* A successor gets its own entry token.  This endpoint keeps the local
         interval alive through all instructions in the current block without
         inventing a cross-CFG pseudo or phi.  */
      for (unsigned regno = 0; regno != 8; ++regno)
        if (live[regno])
	  end_sentinel_at_block_end (live[regno], bb);
    }
}

const pass_data pass_data_rvtt_lreg_livein =
{
  RTL_PASS, "rvtt_lreg_livein", OPTGROUP_OTHER, TV_NONE,
  0, 0, 0, 0, TODO_df_finish
};

class pass_rvtt_lreg_livein : public rtl_opt_pass
{
public:
  pass_rvtt_lreg_livein (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lreg_livein, ctxt) {}

  bool gate (function *) final override { return TARGET_XTT_TENSIX; }

  unsigned execute (function *fn) final override
  {
    make_raw_lregs_live (fn);
    return TODO_df_finish;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_lreg_livein (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_livein (ctxt);
}
