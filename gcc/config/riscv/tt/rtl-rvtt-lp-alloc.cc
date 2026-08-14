/* Final pre-IRA reality audit for Tensix SFPU scheduling.
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
#include "tree-pass.h"
#include "df.h"
#include "regs.h"
#include "insn-config.h"
#include "recog.h"
#include "insn-attr.h"
#include "rvtt.h"

namespace {

static bool
xtt32_allocation_unit_p (unsigned regno)
{
  if (regno < FIRST_PSEUDO_REGISTER)
    return SFPU_REG_P (regno);
  return regno < static_cast<unsigned> (max_reg_num ()) && regno_reg_rtx[regno]
    && GET_MODE (regno_reg_rtx[regno]) == XTT32SImode;
}

static unsigned
count_xtt32_units (bitmap live)
{
  unsigned count = 0;
  unsigned regno;
  bitmap_iterator iterator;
  EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, iterator)
    count += xtt32_allocation_unit_p (regno);
  return count;
}

/* Observe the exact pseudo/hard-register liveness that reaches the allocator
   after expansion, sched1 and early rematerialization.  This milestone is
   intentionally dump-only: it must expose GIMPLE-to-RTL mismatches before a
   later patch attempts atomic hard-register substitution.  */
static void
audit_function (function *fn)
{
  df_note_add_problem ();
  df_analyze ();

  auto_bitmap live;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      bitmap_copy (live, DF_LR_IN (bb));
      df_simulate_initialize_forwards (bb, live);
      const unsigned live_in = count_xtt32_units (live);
      unsigned peak = live_in;
      unsigned tensix_insns = 0;

      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX)
	    ++tensix_insns;
	  df_simulate_one_insn_forwards (bb, insn, live);
	  peak = MAX (peak, count_xtt32_units (live));
	}

      if (dump_file && (tensix_insns || peak))
	fprintf (dump_file,
		 "SFPU pre-IRA audit: bb=%d insns=%u live-in=%u peak=%u "
		 "live-out=%u capacity=%u colorability=unchecked\n",
		 bb->index, tensix_insns, live_in, peak,
		 count_xtt32_units (live), SFPU_REG_NUM);
    }
}

const pass_data pass_data_rvtt_lp_alloc =
{
  RTL_PASS, /* type */
  "rvtt_lp_alloc", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_lp_alloc : public rtl_opt_pass
{
public:
  pass_rvtt_lp_alloc (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lp_alloc, ctxt)
  {}

  bool gate (function *) final override
  {
    return optimize > 0 && (TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH)
      && riscv_tt_opt_pressure_schedule;
  }

  unsigned execute (function *fn) final override
  {
    audit_function (fn);
    return TODO_df_finish;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_lp_alloc (gcc::context *ctxt)
{
  return new pass_rvtt_lp_alloc (ctxt);
}
