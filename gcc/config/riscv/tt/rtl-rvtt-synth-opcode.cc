/* Pass to complete handling of the SFPU synth insns
   Copyright (C) 2022 Free Software Foundation, Inc.
   Contributed by Paul Keller (pkeller@tenstorrent.com).

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

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "rvtt.h"

// This phase of the non-immediate processing works by:
//  - finding all of the load_immediate calls and creating a lookup table
//    based on their id (the value currently being loaded, as set in the
//    nonimm-tag gimple pass)
//  - finding all of the non-immediate insns and matching them via the table
//    to their load immediate
//  - updating the immediate value to match that needed by the insn.  if the
//    value was already set, then confirming the match.  if the old value and
//    new value do not match, the nonimm is flagged to emit the fallback code
static void
transform (function *fn)
{
  std::vector<rtx> synth_opcodes;
  std::vector<rtx> duplicates;

  basic_block bb;

  // Generate lookup table of synth_opcode insns
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	if (NONJUMP_INSN_P (insn) && INSN_CODE (insn) == CODE_FOR_rvtt_synth_opcode)
	  {
	    rtx unspec = SET_SRC (PATTERN (insn));
	    unsigned id = INTVAL (XVECEXP (unspec, 0, 1));
	    if (synth_opcodes.size () <= id)
	      synth_opcodes.resize (id + 1);
	    if (!synth_opcodes[id])
	      synth_opcodes[id] = insn;
	    else
	      duplicates.push_back (insn);
	  }
    }

  if (synth_opcodes.empty ())
    // Nothing to do.
    return;

  // Processes the synth_insns
  // Potential optimization: Find all the uses of each synth_opcode
  // and pick the most common match, rather than the first.
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
       {
	 if (!NONJUMP_INSN_P (insn))
	   continue;
	 bool has_dst = INSN_CODE (insn) == CODE_FOR_rvtt_sfpsynth_insn_dst;
	 if (!has_dst && INSN_CODE (insn) != CODE_FOR_rvtt_sfpsynth_insn)
	   continue;
	
	 rtx pat = XVECEXP (PATTERN (insn), 0, 0);
	 rtx ops = has_dst ? SET_SRC (pat) : pat;

	 unsigned id = INTVAL (XVECEXP (ops, 0, 4));

	 gcc_checking_assert (id < synth_opcodes.size ());
	 rtx synth_insn = synth_opcodes[id];
	 rtx synth_unspec = SET_SRC (PATTERN (synth_insn));
	 rtx &synth_opcode_rtx = XVECEXP (synth_unspec, 0, 0);

	 if (!INTVAL (synth_opcode_rtx))
	   {
	     // Hasn't been set yet
	     unsigned opcode = INTVAL (XVECEXP (ops, 0, 3));
	     if (has_dst)
	       opcode |= rvtt_sfpu_regno (SET_DEST (pat)) << INTVAL (XVECEXP (ops, 0, 7));
	     rtx src = XVECEXP (ops, 0, 5);
	     if (REG_P (src))
	       opcode |= rvtt_sfpu_regno (src) << INTVAL (XVECEXP (ops, 0, 6));

	     synth_opcode_rtx = gen_rtx_CONST_INT (SImode, opcode);
	     if (rtx note = find_reg_equal_equiv_note (synth_insn))
	       {
		 gcc_checking_assert (GET_CODE (XEXP (note, 0)) == UNSPEC);
		 XEXP (note, 0) = synth_unspec;
	       }
	   }

	 // Store the synthed opcode in the synth_insn so it knows if
	 // it needs to fix things.
	 XVECEXP (ops, 0, 3) = synth_opcode_rtx;
       }
    }

  // Fixup the duplicates.
  for (auto *insn : duplicates)
    {
      rtx unspec = SET_SRC (PATTERN (insn));
      unsigned id = INTVAL (XVECEXP (unspec, 0, 1));
      rtx synth_unspec = SET_SRC (PATTERN (synth_opcodes[id]));
      XVECEXP (unspec, 0, 0) = XVECEXP (synth_unspec, 0, 0);
      if (rtx note = find_reg_equal_equiv_note (insn))
	{
	  gcc_checking_assert (GET_CODE (XEXP (note, 0)) == UNSPEC);
	  XEXP (note, 0) = unspec;
	}
    }
}

namespace {

const pass_data pass_data_rvtt_synth_opcode =
{
  RTL_PASS, /* type */
  "rvtt_synth_opcode", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_synth_opcode : public rtl_opt_pass
{
public:
  pass_rvtt_synth_opcode (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_synth_opcode, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_RVTT;
  }
  virtual unsigned execute (function *fn) override
  {
    transform (fn);
    return 0;
  }
};

}

rtl_opt_pass *
make_pass_rvtt_synth_opcode (gcc::context *ctxt)
{
  return new pass_rvtt_synth_opcode (ctxt);
}
