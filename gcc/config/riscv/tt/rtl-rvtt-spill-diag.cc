/* Named diagnosis of SFPU register spills after allocation.
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

/* The SFPU register file has eight allocatable LREGs (SFPU_REG_NUM) and no
   memory spill path: the scalar stack is not addressable from the SFPU
   datapath.  The rvtt_sfpassign memory alternatives exist only so that LRA's
   constraint matching succeeds; such a move can never be emitted, and
   rvtt_mov_error only reports it at assembly output with a generic message.

   GCC's allocators assume every allocno class can be spilled to memory and
   there is no hook to say otherwise, so the problem is only observable on
   the allocated stream.  This pass runs directly after register allocation,
   before any other Tensix RTL pass.  Every allocated SFPU-mode memory move
   (detected by mode rather than insn code, so a re-patterned reload is
   still caught) becomes the named error lreg-pressure-exceeded at the
   statement's location, with a note naming the relief options
   -mtt-tensix-optimize-const-residency and -mtt-tensix-optimize-const-remat.
   The offending moves are then deleted so the later passes, which assume
   vector operands are registers, do not crash; no object file is produced
   after the error anyway.  rvtt_mov_error remains the backstop for streams
   this pass has not diagnosed (rvtt_spill_diag_reported).  */

#define IN_TARGET_CODE 1

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
#include "insn-codes.h"
#include "cfgrtl.h"
#include "diagnostic-core.h"
#include "rvtt.h"
#include "rvtt-protos.h"

namespace {

/* Return true if INSN is an allocated SFPU fill or spill: a single-set
   move of an SFPU vector mode with a memory operand on either side.  Set
   *IS_FILL when the memory operand is the source.  */

static bool
sfpu_mem_move_p (rtx_insn *insn, bool *is_fill)
{
  if (!NONJUMP_INSN_P (insn))
    return false;
  rtx set = single_set (insn);
  if (!set)
    return false;
  machine_mode mode = GET_MODE (SET_DEST (set));
  if (mode != XTT32SImode && mode != XTT64SImode && mode != XTT128SImode
      && mode != XTT256SImode)
    return false;
  if (MEM_P (SET_DEST (set)))
    {
      *is_fill = false;
      return true;
    }
  if (MEM_P (SET_SRC (set)))
    {
      *is_fill = true;
      return true;
    }
  return false;
}

/* Report every allocated SFPU memory move in FN as the named user error
   lreg-pressure-exceeded, then delete the offenders so the later Tensix
   RTL passes never see a memory vector operand.  Each spill store is a
   distinct error site; when only fills exist, one fill is reported.  The
   relief-option note is emitted once.
   Sets rvtt_spill_diag_reported when anything was found and returns the
   number of errors emitted.  */

static unsigned
diagnose_spills (function *fn)
{
  /* Collect first: the reporting depends on whether any spill store
     exists, and the offenders are deleted afterwards.  */
  auto_vec<rtx_insn *> stores;
  auto_vec<rtx_insn *> fills;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  bool is_fill;
	  if (sfpu_mem_move_p (insn, &is_fill))
	    (is_fill ? fills : stores).safe_push (insn);
	}
    }
  if (stores.is_empty () && fills.is_empty ())
    return 0;

  unsigned reported = 0;
  auto report = [&] (rtx_insn *insn, bool is_fill)
    {
      location_t loc = INSN_HAS_LOCATION (insn)
	? INSN_LOCATION (insn) : fn->function_start_locus;
      error_at (loc,
		"SFPU vector register pressure exceeds the "
		"%d-register LREG file: a vector value must be "
		"%s memory, which the Tensix SFPU cannot do "
		"(lreg-pressure-exceeded)",
		SFPU_REG_NUM, is_fill ? "reloaded from" : "spilled to");
      if (!reported)
	inform (loc,
		"proven-constant values can be parked in programmable "
		"constant registers with "
		"%<-mtt-tensix-optimize-const-residency%> or "
		"rematerialized at their uses with "
		"%<-mtt-tensix-optimize-const-remat%>; otherwise reduce "
		"the number of simultaneously live vector values");
      ++reported;
    };

  for (rtx_insn *insn : stores)
    report (insn, false);
  if (stores.is_empty ())
    report (fills[0], true);

  /* The downstream Tensix RTL passes assume vector operands are
     registers; delete the moves so they do not ICE after the error.  */
  rvtt_spill_diag_reported = true;
  for (rtx_insn *insn : stores)
    delete_insn (insn);
  for (rtx_insn *insn : fills)
    delete_insn (insn);
  return reported;
}

const pass_data pass_data_rvtt_spill_diag =
{
  RTL_PASS, /* type */
  "rvtt_spill_diag", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_spill_diag : public rtl_opt_pass
{
public:
  pass_rvtt_spill_diag (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_spill_diag, ctxt)
  {}

  /* Unconditional under Tensix: this is diagnosis, not optimization.  */
  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX;
  }

  unsigned execute (function *fn) final override
  {
    unsigned n = diagnose_spills (fn);
    if (n && dump_file)
      fprintf (dump_file, "SFPU spill diagnosis: %u memory move(s) "
	       "reported (lreg-pressure-exceeded) and deleted\n", n);
    return 0;
  }
};

} /* anonymous namespace */

/* Create the spill-diagnosis pass for CTXT.  */

rtl_opt_pass *
make_pass_rvtt_spill_diag (gcc::context *ctxt)
{
  return new pass_rvtt_spill_diag (ctxt);
}
