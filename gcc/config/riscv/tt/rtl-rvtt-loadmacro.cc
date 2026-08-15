/* Discover and verify Tensix SFPLOADMACRO candidate regions.
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
#include "function.h"
#include "basic-block.h"
#include "insn-config.h"
#include "insn-codes.h"
#include "insn-attr.h"
#include "recog.h"
#include "rvtt-protos.h"

namespace {

/* These reasons are part of the dump contract.  Keep them fine-grained: a
   later emitter must discharge each proof instead of replacing this list with
   a single "known kernel" allowlist.  */
enum class reject_reason
{
  incomplete_region,
  non_sfpu_boundary,
  unsafe_replay_member,
  unsupported_bulk_operation,
  dynamic_encoding_unproved,
  external_lreg_livein_unproved,
  lreg_liveout_unproved,
  cc_effect_unproved,
  dst_rwc_effect_unproved,
  subunit_calendar_missing,
  simulator_event_model_missing
};

enum class descriptor_status
{
  no_match,
  described,
  dynamic_encoding,
  unclosed_dependency
};

static const char *
reject_reason_name (reject_reason reason)
{
  switch (reason)
    {
    case reject_reason::incomplete_region:
      return "incomplete-region";
    case reject_reason::non_sfpu_boundary:
      return "non-sfpu-boundary";
    case reject_reason::unsafe_replay_member:
      return "unsafe-replay-member";
    case reject_reason::unsupported_bulk_operation:
      return "unsupported-bulk-operation";
    case reject_reason::dynamic_encoding_unproved:
      return "dynamic-encoding-unproved";
    case reject_reason::external_lreg_livein_unproved:
      return "external-lreg-livein-unproved";
    case reject_reason::lreg_liveout_unproved:
      return "lreg-liveout-unproved";
    case reject_reason::cc_effect_unproved:
      return "cc-effect-unproved";
    case reject_reason::dst_rwc_effect_unproved:
      return "dst-rwc-effect-unproved";
    case reject_reason::subunit_calendar_missing:
      return "subunit-calendar-missing";
    case reject_reason::simulator_event_model_missing:
      return "simulator-event-model-missing";
    }
  gcc_unreachable ();
}

static bool
load_p (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return false;
  int code = recog_memoized (insn);
  return code == CODE_FOR_rvtt_sfpload_lv_int
    || code == CODE_FOR_rvtt_sfploadsrcs_lv_int;
}

static bool
store_p (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return false;
  int code = recog_memoized (insn);
  return code == CODE_FOR_rvtt_sfpstore_int
    || code == CODE_FOR_rvtt_sfpstoresrcs_int;
}

static bool
unsupported_bulk_p (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return false;

  switch (recog_memoized (insn))
    {
    case CODE_FOR_rvtt_sfptransp_int:
    case CODE_FOR_rvtt_sfpshft2_copy4_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_copy4_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_shfl1_copy4_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_shfl1_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_shfl1_dead:
      return true;
    default:
      return false;
    }
}

static bool
tensix_p (rtx_insn *insn)
{
  return NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
    && get_attr_type (insn) == TYPE_TENSIX;
}

struct macro_candidate
{
  rtx_insn *first = nullptr;
  rtx_insn *last = nullptr;
  unsigned words = 0;
  unsigned loads = 0;
  unsigned stores = 0;
  bool crossed_non_sfpu = false;
  bool unsupported_bulk = false;
};

struct macro_descriptor
{
  unsigned load0_reg = INVALID_REGNUM;
  unsigned load1_reg = INVALID_REGNUM;
  unsigned store_reg = INVALID_REGNUM;
  HOST_WIDE_INT swap_mod = 0;
};

static bool
hard_lreg_p (rtx x)
{
  return REG_P (x) && HARD_REGISTER_P (x);
}

static bool
same_reg_p (rtx a, rtx b)
{
  return hard_lreg_p (a) && hard_lreg_p (b) && REGNO (a) == REGNO (b);
}

/* Recognize a load-load-swap-store resource shape without relying on a source
   function or operation name.  This is descriptor construction only: emission
   remains blocked by the calendar and simulator proofs.  */
static descriptor_status
describe_load_load_swap_store (const macro_candidate &candidate,
				 macro_descriptor *descriptor)
{
  if (candidate.words != 4 || candidate.loads != 2
      || candidate.stores != 1)
    return descriptor_status::no_match;

  rtx_insn *insns[4];
  unsigned count = 0;
  for (rtx_insn *insn = candidate.first; insn;
       insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	{
	  if (count == ARRAY_SIZE (insns))
	    return descriptor_status::no_match;
	  insns[count++] = insn;
	}
      if (insn == candidate.last)
	break;
    }
  if (count != ARRAY_SIZE (insns)
      || !load_p (insns[0]) || !load_p (insns[1])
      || recog_memoized (insns[2]) != CODE_FOR_rvtt_sfpswap_int
      || !store_p (insns[3]))
    return descriptor_status::no_match;

  extract_insn (insns[0]);
  rtx load0 = recog_data.operand[0];
  bool load0_constant = CONST_INT_P (recog_data.operand[4])
    && CONST_INT_P (recog_data.operand[7])
    && CONST_INT_P (recog_data.operand[8]);
  extract_insn (insns[1]);
  rtx load1 = recog_data.operand[0];
  bool load1_constant = CONST_INT_P (recog_data.operand[4])
    && CONST_INT_P (recog_data.operand[7])
    && CONST_INT_P (recog_data.operand[8]);

  extract_insn (insns[2]);
  rtx swap_out0 = recog_data.operand[0];
  rtx swap_out1 = recog_data.operand[1];
  rtx swap_in0 = recog_data.operand[2];
  rtx swap_in1 = recog_data.operand[3];
  rtx swap_mod = recog_data.operand[4];

  extract_insn (insns[3]);
  rtx store_src = recog_data.operand[4];
  bool store_constant = CONST_INT_P (recog_data.operand[3])
    && CONST_INT_P (recog_data.operand[5])
    && CONST_INT_P (recog_data.operand[6]);

  bool inputs_closed = (same_reg_p (swap_in0, load0)
			&& same_reg_p (swap_in1, load1))
    || (same_reg_p (swap_in0, load1) && same_reg_p (swap_in1, load0));
  bool output_closed = same_reg_p (store_src, swap_out0)
    || same_reg_p (store_src, swap_out1);
  if (!load0_constant || !load1_constant || !store_constant
      || !CONST_INT_P (swap_mod))
    return descriptor_status::dynamic_encoding;
  if (!inputs_closed || !output_closed)
    return descriptor_status::unclosed_dependency;

  descriptor->load0_reg = REGNO (load0);
  descriptor->load1_reg = REGNO (load1);
  descriptor->store_reg = REGNO (store_src);
  descriptor->swap_mod = INTVAL (swap_mod);
  return descriptor_status::described;
}

static void
dump_candidate (basic_block bb, const macro_candidate &candidate,
		bool complete)
{
  if (!dump_file)
    return;

  fprintf (dump_file,
	   "SFPLOADMACRO candidate: bb=%d start_uid=%d end_uid=%d "
	   "words=%u loads=%u stores=%u emit=no\n",
	   bb->index, INSN_UID (candidate.first),
	   candidate.last ? INSN_UID (candidate.last) : -1,
	   candidate.words, candidate.loads, candidate.stores);

  macro_descriptor descriptor;
  descriptor_status status = complete
    ? describe_load_load_swap_store (candidate, &descriptor)
    : descriptor_status::no_match;
  if (status == descriptor_status::described)
    fprintf (dump_file,
	     "  descriptor=periodic-load-load-swap-store "
	     "lregs=%u,%u->%u swap_mod=" HOST_WIDE_INT_PRINT_DEC " "
	     "encoding=constant deps=closed cc=none "
	     "rwc=outside-candidate emit=no\n",
	     descriptor.load0_reg, descriptor.load1_reg, descriptor.store_reg,
	     descriptor.swap_mod);
  else if (status == descriptor_status::dynamic_encoding)
    fprintf (dump_file,
	     "  descriptor-reject=dynamic-encoding emit=no\n");
  else if (status == descriptor_status::unclosed_dependency)
    fprintf (dump_file,
	     "  descriptor-reject=unclosed-dependency emit=no\n");

  if (!complete)
    fprintf (dump_file, "  reject=%s\n",
	     reject_reason_name (reject_reason::incomplete_region));
  if (candidate.crossed_non_sfpu)
    fprintf (dump_file, "  reject=%s\n",
	     reject_reason_name (reject_reason::non_sfpu_boundary));
  if (candidate.unsupported_bulk)
    fprintf (dump_file, "  reject=%s\n",
	     reject_reason_name (reject_reason::unsupported_bulk_operation));

  /* D4 deliberately stops before emission.  These are not generic caveats:
     they are the concrete proofs absent from the current RTL.  Listing each
     one makes the dump a stable checklist for the event-model and descriptor
     patches which follow.  */
  const reject_reason pending[] = {
    reject_reason::unsafe_replay_member,
    reject_reason::dynamic_encoding_unproved,
    reject_reason::external_lreg_livein_unproved,
    reject_reason::lreg_liveout_unproved,
    reject_reason::cc_effect_unproved,
    reject_reason::dst_rwc_effect_unproved,
    reject_reason::subunit_calendar_missing,
    reject_reason::simulator_event_model_missing
  };
  for (reject_reason reason : pending)
    fprintf (dump_file, "  reject=%s\n", reject_reason_name (reason));
}

/* Discover maximal load...store stretches in each basic block.  No RTL is
   created, removed, reordered, or annotated here.  In particular, this pass
   must remain byte-identical whether its gate is enabled or disabled.  */
static void
discover (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      macro_candidate candidate;
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!candidate.first)
	    {
	      if (!load_p (insn))
		continue;
	      candidate.first = insn;
	      candidate.last = insn;
	      candidate.words = 1;
	      candidate.loads = 1;
	      candidate.unsupported_bulk = unsupported_bulk_p (insn);
	      continue;
	    }

	  if (!tensix_p (insn))
	    {
	      /* Notes and labels do not issue and do not split the stream.  */
	      if (!NONDEBUG_INSN_P (insn))
		continue;
	      candidate.crossed_non_sfpu = true;
	      dump_candidate (bb, candidate, false);
	      candidate = macro_candidate {};
	      continue;
	    }

	  candidate.last = insn;
	  ++candidate.words;
	  candidate.loads += load_p (insn);
	  candidate.unsupported_bulk |= unsupported_bulk_p (insn);
	  if (store_p (insn))
	    {
	      ++candidate.stores;
	      dump_candidate (bb, candidate, true);
	      candidate = macro_candidate {};
	    }
	}

      if (candidate.first)
	dump_candidate (bb, candidate, false);
    }
}

const pass_data pass_data_rvtt_loadmacro =
{
  RTL_PASS,
  "rvtt_loadmacro",
  OPTGROUP_NONE,
  TV_NONE,
  0,
  0,
  0,
  0,
  0
};

class pass_rvtt_loadmacro : public rtl_opt_pass
{
public:
  pass_rvtt_loadmacro (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_loadmacro, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_analyze_loadmacro;
  }

  unsigned execute (function *fn) final override
  {
    discover (fn);
    return 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_loadmacro (gcc::context *ctxt)
{
  return new pass_rvtt_loadmacro (ctxt);
}
