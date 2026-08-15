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
