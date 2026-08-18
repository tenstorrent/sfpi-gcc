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
   Tensix RTL pass consumes the stream.  Every allocated SFPU-mode
   memory move (detected by MODE, not by insn code, so a wrapped or
   re-patterned reload cannot slip past) becomes a named user error
   (lreg-pressure-exceeded) at the offending statement's location,
   pointing at the two relief mechanisms
   (-mtt-tensix-optimize-const-residency /
   -mtt-tensix-optimize-const-remat).  The offending moves are then
   DELETED so the remaining Tensix RTL passes -- which assume vector
   operands are registers -- never see them: after a hard error no
   object file is produced, and crash-freedom of the doomed compilation
   is what matters.  rvtt_mov_error stays as the backstop for streams
   this pass has not diagnosed (rvtt_spill_diag_reported), so a genuine
   compiler bug still ICEs loudly.

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
#include "cfgrtl.h"
#include "diagnostic-core.h"
#include "rvtt.h"
#include "rvtt-protos.h"

namespace {

/* An allocated SFPU fill or spill: any single-set move of an SFPU
   vector mode with a memory operand on either side.  Keyed on the MODE
   rather than the insn code so a PARALLEL-wrapped or secondary-reload
   re-patterning of the move cannot dodge the diagnosis.  */

static bool
sfpu_mem_move_p (rtx_insn *insn, bool *is_fill)
{
  if (!NONJUMP_INSN_P (insn))
    return false;
  rtx set = single_set (insn);
  if (!set)
    return false;
  machine_mode mode = GET_MODE (SET_DEST (set));
  if (mode != XTT32SImode && mode != XTT64SImode && mode != XTT128SImode)
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
  /* Collect first: reporting policy needs to know whether any spill
     STORE exists (each store is a distinct spill site; a fill without
     any store still proves over-pressure and reports once), and the
     offenders are deleted afterwards.  */
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

  /* Neutralize: the downstream Tensix RTL passes assume vector
     operands are registers; the doomed stream must not ICE past the
     named error.  */
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
	       "reported (lreg-pressure-exceeded) and deleted\n", n);
    return 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_spill_diag (gcc::context *ctxt)
{
  return new pass_rvtt_spill_diag (ctxt);
}
