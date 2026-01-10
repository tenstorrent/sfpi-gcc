/* Pass to generate SFPU synth-opcode and opcode synth sequences for
   currently-non-constant operands.
   Copyright (C) 2026 Tenstorrent Inc.
   Originated by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#define INCLUDE_VECTOR
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "gimple-iterator.h"
#include "gimple-ssa.h"
#include "tree-phinodes.h"
#include "ssa-iterators.h"
#include "value-range.h"
#include "tree-ssa-propagate.h"
#include "stringpool.h"
#include "tree-ssanames.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "regs.h"
#include "memmodel.h"
#include "ira.h"
#include "recog.h"
#include "print-rtl.h"
#include "rvtt.h"

// Propagate unspecs that behave as-if constant operands into their uses.  We
// can't rely on the various combine passes to do this all the time.

static void
transform (function *fn)
{
  struct defs_t {
    rtx val;
    basic_block bb;
  };
  defs_t *reg_vals = XCNEWVEC (defs_t, max_reg_num ());
  basic_block bb;
  rtx_insn *insn;
  std::vector<unsigned> invalidate;

  FOR_EACH_BB_FN (bb, fn)
    FOR_BB_INSNS (bb, insn)
    {
      if (GET_CODE (insn) != INSN)
	continue;
      rtx pattern = PATTERN (insn);

      if (GET_CODE (pattern) == USE)
	continue;

      if (get_attr_type (insn) != TYPE_TENSIX)
	continue;

      if (INSN_CODE (insn) == CODE_FOR_rvtt_sfpmovwhole)
	{
	  rtx src = SET_SRC (pattern);
	  if (GET_CODE (src) == UNSPEC && XINT (src, 1) == UNSPEC_SFPCSTLREG)
	    {
	      // Set reg to cstlreg, remember it
	      unsigned regno = REGNO (SET_DEST (pattern));
	      reg_vals[regno].val = src;
	      reg_vals[regno].bb = bb;
	      continue;
	    }
	}

      auto propagate_unspecs
	= [&invalidate, bb, reg_vals, insn](auto &self, rtx *slot) -> void
      {
	switch (GET_CODE (*slot))
	  {
	  default:
	    // Unknown tensix insn component
	    gcc_unreachable ();

	  case PARALLEL:
	  case UNSPEC:
	  case UNSPEC_VOLATILE:
	    {
	      // All 3 have the vector at position 0
	      auto &vec = XVEC (*slot, 0);
	      for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
		self (self, &RTVEC_ELT (vec, ix));
	    }
	    break;

	  case SET:
	    {
	      self (self, &SET_SRC (*slot));
	      auto dst = SET_DEST (*slot);
	      if (REG_P (dst))
		invalidate.push_back (REGNO (dst));
	    }
	    break;

	  case REG:
	    {
	      unsigned regno = REGNO (*slot);
	      if (!reg_vals[regno].bb)
		break;

	      char const *msg = nullptr;
	      if (reg_vals[regno].bb != bb)
		msg = "Register not initialized in this block";
	      else if (validate_change (insn, slot, reg_vals[regno].val, false))
		msg = "Replaced register";
	      else
		msg = "Failed to replace register";

	      if (dump_file)
		{
		  fprintf (dump_file, "%s %u\n", msg, regno);
 		  dump_insn_slim (dump_file, insn);
		  fprintf (dump_file, "\n");
		}
	    }
	    break;

	  CASE_CONST_ANY:
	  case MEM:
	  case CLOBBER:
	    break;
	  }
      };

      if (GET_CODE (pattern) == SET)
	{
	  rtx dst = SET_DEST (pattern);
	  if (REG_P (dst))
	    {
	      unsigned regno = REGNO (dst);
	      rtx src = SET_SRC (pattern);
	      if (GET_CODE (src) == UNSPEC
		  && XINT (src, 1) == UNSPEC_SFPCSTLREG)
		{
		  // Set reg to cstlreg, remember it
		  reg_vals[regno].val = src;
		  reg_vals[regno].bb = bb;
		}
	      else
		reg_vals[regno].bb = nullptr;
	    }

	  propagate_unspecs (propagate_unspecs, &SET_SRC (pattern));
	}
      else
	{
	  propagate_unspecs (propagate_unspecs, &pattern);
	  for (auto reg : invalidate)
	    reg_vals[reg].bb = nullptr;
	  invalidate.clear ();
	}
    }
  XDELETEVEC (reg_vals);
}

namespace {

const pass_data pass_data_rvtt_unspec_prop_rtl =
{
  RTL_PASS, /* type */
  "rvtt_unspec_prop", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_unspec_prop_rtl : public rtl_opt_pass
{
public:
  pass_rvtt_unspec_prop_rtl (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_unspec_prop_rtl, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    transform (fn);
    return 0;
  }
};

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_unspec_prop_rtl (gcc::context *ctxt)
{
  return new pass_rvtt_unspec_prop_rtl (ctxt);
}
