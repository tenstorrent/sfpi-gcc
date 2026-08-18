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

/* The SFPU register file has eight allocatable LREGs (riscv.h
   SFPU_REG_NUM) and no memory spill path: the rvtt_sfpassign memory
   alternatives exist only so that LRA's constraint matching succeeds
   (rvtt.md: "the simple set must accept reg-movs, loads and stores...
   otherwise reload blows up"), and emitting one is impossible.  Until
   now the impossibility surfaced as an internal compiler error at
   assembly output (rvtt.cc rvtt_mov_error) -- an ICE is a compiler bug
   unconditionally, and over-pressure source is a user-capacity fact,
   not a compiler bug.

   This pass runs directly after register allocation, before any other
   Tensix RTL pass consumes the stream, and turns every allocated
   XTT32SI memory move into a named user error (lreg-pressure-exceeded)
   at the offending statement's location, pointing at the two relief
   mechanisms (-mtt-tensix-optimize-const-residency /
   -mtt-tensix-optimize-const-remat).  rvtt_mov_error stays as the
   backstop for streams this pass has not seen, and stands down only
   when an error has already been reported here.

   The pass changes nothing on spill-free streams: flag-off and clean
   compilations are byte-identical.  */

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
#include "diagnostic-core.h"
#include "rvtt.h"
#include "rvtt-protos.h"

namespace {

/* An allocated SFPU fill or spill: the rvtt_sfpassign pattern with a
   memory operand on either side.  No other XTT32SI pattern accepts a
   memory vector operand (the SImode memory operand of
   rvtt_sfploadi_lv_int is the synthesized opcode word, not a vector).  */

static bool
sfpu_mem_move_p (rtx_insn *insn, bool *is_fill)
{
  if (!NONJUMP_INSN_P (insn) || recog_memoized (insn) < 0)
    return false;
  if (INSN_CODE (insn) != CODE_FOR_rvtt_sfpassign)
    return false;
  rtx set = single_set (insn);
  if (!set)
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

static unsigned
diagnose_spills (function *fn)
{
  unsigned reported = 0;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  bool is_fill;
	  if (!sfpu_mem_move_p (insn, &is_fill))
	    continue;
	  /* Report each spilled value once, at the spill store (the
	     fills follow from it); a lone fill still reports.  */
	  if (is_fill && reported)
	    continue;
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
	}
    }
  return reported;
}

const pass_data pass_data_rvtt_spill_diag =
{
  RTL_PASS, /* type */
  "rvtt_spill_diag", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
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
	       "reported (lreg-pressure-exceeded)\n", n);
    return 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_spill_diag (gcc::context *ctxt)
{
  return new pass_rvtt_spill_diag (ctxt);
}
