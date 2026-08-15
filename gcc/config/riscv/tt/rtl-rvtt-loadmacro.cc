/* Discover, verify, and optionally form Tensix SFPLOADMACRO regions.
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
#include "memmodel.h"
#include "emit-rtl.h"
#include "cfgrtl.h"
#include "df.h"
#include "tm_p.h"
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
  resource_mismatch,
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

/* The programmed template, sequence-zero, and misc fields are explicitly
   opt-in compiler-owned resources.  Do not mix the formed representation
   with source-visible configuration accesses in the same function: their
   relative slot semantics cannot be recovered after the explicit body is
   deleted.  */
static bool
source_config_access_p (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	if (NONDEBUG_INSN_P (insn))
	  {
	    if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	      return true;
	    int code = recog_memoized (insn);
	    if (code == CODE_FOR_rvtt_sfpreadconfig_lv
		|| code == CODE_FOR_rvtt_sfpwriteconfig_v)
	      return true;
	  }
    }
  return false;
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

struct configured_descriptor
{
  rtx_insn *insns[4] {};
  rtx_insn *increment_insn = nullptr;
  rtx macro_lreg = nullptr;
  rtx load_mem = nullptr;
  rtx store_mem = nullptr;
  rtx address = nullptr;
  rtx mode = nullptr;
  rtx address_mode = nullptr;
};

static rtx_insn *
all_lanes_enable_before (rtx_insn *first)
{
  rtx_insn *insn = prev_nonnote_nondebug_insn (first);
  if (!insn || BLOCK_FOR_INSN (insn) != BLOCK_FOR_INSN (first)
      || recog_memoized (insn) != CODE_FOR_rvtt_sfpencc)
    return nullptr;

  extract_insn (insn);
  return (CONST_INT_P (recog_data.operand[0])
	  && INTVAL (recog_data.operand[0]) == SFPENCC_MOD1_EI_RI
	  && CONST_INT_P (recog_data.operand[1])
	  && INTVAL (recog_data.operand[1]) == SFPENCC_IMM12_BOTH)
    ? insn : nullptr;
}

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

static bool
unspec_p (rtx_insn *insn, int code)
{
  rtx set = single_set (insn);
  if (!set)
    return false;
  rtx src = SET_SRC (set);
  return GET_CODE (src) == UNSPEC_VOLATILE && XINT (src, 1) == code;
}

/* Loading a configuration word through L0 is lane-predicated, while
   SFPCONFIG consumes lane zero.  Configuration materialization is therefore
   sound only when an immediately preceding architectural CC operation proves
   every lane enabled.  Keeping this proof local prevents it from being
   widened across an unmodelled CC effect.  */
static bool
all_lanes_enabled_immediately_before_p (rtx_insn *first)
{
  return all_lanes_enable_before (first) != nullptr;
}

/* Match the one calendar already executable by the transactional simulator:
   load; logical shift right by 31; sign-magnitude cast; store.  All values
   occupy L0 in the explicit form.  The macro VD must be L1 because the
   canonical SHFT2 immediate aliases its source selector to L1.  Prove L1 is
   otherwise unused until multi-VD scheduling is modeled.
   Requiring the store source to have no later reference prevents the launch's
   shifted VD value from escaping in place of the explicit cast result.  */
static bool
describe_configured_region (const macro_candidate &candidate,
			    configured_descriptor *out)
{
  if (candidate.words != 4 || candidate.loads != 1
      || candidate.stores != 1 || candidate.crossed_non_sfpu
      || candidate.unsupported_bulk
      || !all_lanes_enabled_immediately_before_p (candidate.first))
    return false;

  unsigned count = 0;
  for (rtx_insn *insn = candidate.first; insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	{
	  if (count == ARRAY_SIZE (out->insns))
	    return false;
	  out->insns[count++] = insn;
	}
      if (insn == candidate.last)
	break;
    }
  if (count != ARRAY_SIZE (out->insns)
      || recog_memoized (out->insns[0]) != CODE_FOR_rvtt_sfpload_lv_int
      || !unspec_p (out->insns[1], UNSPECV_SFPSHFT)
      || recog_memoized (out->insns[2]) != CODE_FOR_rvtt_sfpcast_lv
      || recog_memoized (out->insns[3]) != CODE_FOR_rvtt_sfpstore_int)
    return false;

  extract_insn (out->insns[0]);
  rtx load_reg = recog_data.operand[0];
  rtx load_mem = recog_data.operand[1];
  rtx load_opcode = recog_data.operand[2];
  rtx load_encoding = recog_data.operand[3];
  rtx load_address = recog_data.operand[4];
  rtx load_live_value = recog_data.operand[6];
  rtx load_mode = recog_data.operand[7];
  rtx load_address_mode = recog_data.operand[8];

  extract_insn (out->insns[1]);
  rtx shift_reg = recog_data.operand[0];
  rtx shift_mem = recog_data.operand[1];
  rtx shift_opcode = recog_data.operand[2];
  rtx shift_encoding = recog_data.operand[3];
  rtx shift_amount = recog_data.operand[4];
  rtx shift_src = recog_data.operand[5];
  rtx shift_live_value = recog_data.operand[6];
  rtx shift_mode = recog_data.operand[7];

  extract_insn (out->insns[2]);
  rtx cast_reg = recog_data.operand[0];
  rtx cast_live_value = recog_data.operand[1];
  rtx cast_src = recog_data.operand[2];
  rtx cast_mode = recog_data.operand[3];

  extract_insn (out->insns[3]);
  rtx store_mem = recog_data.operand[0];
  rtx store_opcode = recog_data.operand[1];
  rtx store_encoding = recog_data.operand[2];
  rtx store_address = recog_data.operand[3];
  rtx store_src = recog_data.operand[4];
  rtx store_mode = recog_data.operand[5];
  rtx store_address_mode = recog_data.operand[6];

  rtx_insn *increment = next_nonnote_nondebug_insn (out->insns[3]);
  if (!increment || BLOCK_FOR_INSN (increment) != BLOCK_FOR_INSN (out->insns[3])
      || recog_memoized (increment) != CODE_FOR_rvtt_ttincrwc)
    return false;
  extract_insn (increment);
  if (!CONST_INT_P (recog_data.operand[0])
      || INTVAL (recog_data.operand[0]) != 0
      || !CONST_INT_P (recog_data.operand[1])
      || INTVAL (recog_data.operand[1]) != 2
      || !CONST_INT_P (recog_data.operand[2])
      || INTVAL (recog_data.operand[2]) != 0
      || !CONST_INT_P (recog_data.operand[3])
      || INTVAL (recog_data.operand[3]) != 0)
    return false;

  HOST_WIDE_INT expected_shift_mode = TARGET_XTT_TENSIX_BH ? 5 : 1;
  HOST_WIDE_INT expected_address_mode = TARGET_XTT_TENSIX_BH ? 7 : 3;
  if (!same_reg_p (load_reg, shift_reg)
      || !same_reg_p (load_reg, shift_src)
      || !same_reg_p (load_reg, cast_reg)
      || !same_reg_p (load_reg, cast_src)
      || !same_reg_p (load_reg, store_src)
      || REGNO (load_reg) != SFPU_REG_FIRST
      || df_regs_ever_live_p (SFPU_REG_FIRST + 1)
      || !CONST_INT_P (load_opcode) || INTVAL (load_opcode) != 0
      || !CONST_INT_P (load_encoding) || INTVAL (load_encoding) != 0
      || !noval_operand (load_live_value, GET_MODE (load_live_value))
      || !CONST_INT_P (store_opcode) || INTVAL (store_opcode) != 0
      || !CONST_INT_P (store_encoding) || INTVAL (store_encoding) != 0
      || !CONST_INT_P (load_address) || !CONST_INT_P (store_address)
      || INTVAL (load_address) != INTVAL (store_address)
      || INTVAL (load_address) < 0 || INTVAL (load_address) > 0x3ff
      || (INTVAL (load_address) & 1) != 0
      || !CONST_INT_P (load_mode) || !CONST_INT_P (store_mode)
      || INTVAL (load_mode) != INTVAL (store_mode)
      || INTVAL (load_mode) < 0 || INTVAL (load_mode) > 0xf
      || !CONST_INT_P (load_address_mode)
      || !CONST_INT_P (store_address_mode)
      || INTVAL (load_address_mode) != INTVAL (store_address_mode)
      || INTVAL (load_address_mode) != expected_address_mode
      || shift_mem != const0_rtx
      || !CONST_INT_P (shift_opcode) || INTVAL (shift_opcode) != 0
      || !CONST_INT_P (shift_encoding) || INTVAL (shift_encoding) != 0
      || !CONST_INT_P (shift_amount) || INTVAL (shift_amount) != -31
      || !noval_operand (shift_live_value, GET_MODE (shift_live_value))
      || !CONST_INT_P (shift_mode)
      || INTVAL (shift_mode) != expected_shift_mode
      || !noval_operand (cast_live_value, GET_MODE (cast_live_value))
      || !CONST_INT_P (cast_mode) || INTVAL (cast_mode) != 0
      || bitmap_bit_p (df_get_live_out (BLOCK_FOR_INSN (out->insns[3])),
		       REGNO (store_src)))
    return false;

  basic_block bb = BLOCK_FOR_INSN (candidate.last);
  for (rtx_insn *insn = NEXT_INSN (candidate.last);
       insn && BLOCK_FOR_INSN (insn) == bb; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn)
	  && reg_referenced_p (store_src, PATTERN (insn)))
	return false;
      /* A later all-lane definition ends the old cast value's lifetime.  In
	 particular, permit an immediate same-row load only after the macro's
	 explicit drain; uses of that new value do not make the replaced cast
	 result live.  Check references first for read/modify/write instructions.  */
      if (NONDEBUG_INSN_P (insn) && reg_set_p (store_src, insn))
	break;
    }

  out->macro_lreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + 1);
  out->load_mem = load_mem;
  out->store_mem = store_mem;
  out->address = load_address;
  /* The explicit typed form uses the target no-increment address modifier
     and advances Dst with a following TTINCRWC.  The macro Store event must
     instead use the target auto-increment modifier and consume that
     increment.  Its data-format modifier is DEFAULT (zero).  */
  out->mode = const0_rtx;
  out->address_mode = GEN_INT (TARGET_XTT_TENSIX_BH ? 6 : 2);
  out->increment_insn = increment;
  return true;
}

static void
emit_config_word (rtx lreg0, uint32_t value, unsigned config_dest)
{
  rvtt_emit_sfpxloadi (lreg0, rvtt_gen_rtx_noval (XTT32SImode),
		       GEN_INT (value));
  emit_insn (gen_rvtt_sfpwriteconfig_v (lreg0, GEN_INT (config_dest)));
}

static void
emit_descriptor_config ()
{
  rtx config_lreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST);
  emit_config_word (config_lreg, 0x94fe10c6u, 0);
  emit_config_word (config_lreg, 0x900000d0u, 1);
  emit_config_word (config_lreg, 0x5384004du, 4);
  emit_config_word (config_lreg, 0x00000110u, 8);
}

static void
emit_macro_launch (const configured_descriptor &descriptor)
{
  unsigned launch_word
    = 0x93100000u
      | (UINTVAL (descriptor.mode) << 16)
      | (UINTVAL (descriptor.address_mode)
	 << (TARGET_XTT_TENSIX_BH ? 13 : 14))
      | UINTVAL (descriptor.address);
  emit_insn (gen_rvtt_sfploadmacro_int (
      descriptor.macro_lreg, descriptor.load_mem, descriptor.store_mem,
      descriptor.address, descriptor.mode, descriptor.address_mode,
      GEN_INT (launch_word)));
  /* The admitted 0x5384004d calendar retires its final delayed store three
     issue slots after launch.  Keep the drain explicit until a generic
     calendar-aware scheduler can prove and fill these slots.  */
  emit_insn (gen_rvtt_sfpnop ());
  emit_insn (gen_rvtt_sfpnop ());
  emit_insn (gen_rvtt_sfpnop ());
}

/* Return the unique external predecessor of a canonical one-block loop.
   Requiring the loop header to have exactly its backedge and one incoming
   edge, and the incoming block to have no other successor, makes that block
   a structural preheader without depending on loop metadata this late in
   the RTL pipeline.  */
static basic_block
single_block_loop_preheader (const configured_descriptor &descriptor)
{
  basic_block body = BLOCK_FOR_INSN (descriptor.insns[0]);
  edge edge_to_body = nullptr;
  unsigned self_edges = 0;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, body->preds)
    if (e->src == body)
      ++self_edges;
    else if (edge_to_body)
      return nullptr;
    else
      edge_to_body = e;
  if (self_edges != 1 || !edge_to_body
      || EDGE_COUNT (edge_to_body->src->succs) != 1)
    return nullptr;

  /* The enable and described region must be the only Tensix issue in the
     loop body.  This proves that moving the idempotent all-lanes enable out
     of the loop crosses no CC user/owner and that descriptor state cannot be
     changed between the preheader materialization and any launch.  */
  rtx_insn *enable = all_lanes_enable_before (descriptor.insns[0]);
  if (!enable)
    return nullptr;
  for (rtx_insn *insn = BB_HEAD (body); insn; insn = NEXT_INSN (insn))
    {
      if (tensix_p (insn))
	{
	  bool described = insn == enable || insn == descriptor.increment_insn;
	  for (rtx_insn *member : descriptor.insns)
	    described |= insn == member;
	  if (!described)
	    return nullptr;
	}
      if (insn == BB_END (body))
	break;
    }
  return edge_to_body->src;
}

static bool
self_loop_p (const configured_descriptor &descriptor)
{
  basic_block body = BLOCK_FOR_INSN (descriptor.insns[0]);
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, body->preds)
    if (e->src == body)
      return true;
  return false;
}

static void
emit_configured_region (const configured_descriptor &descriptor,
			basic_block preheader)
{
  start_sequence ();
  if (preheader)
    {
      rtx_insn *enable = all_lanes_enable_before (descriptor.insns[0]);
      emit_insn (copy_rtx (PATTERN (enable)));
      emit_descriptor_config ();
    }
  else
    emit_descriptor_config ();
  rtx_insn *prefix = get_insns ();
  end_sequence ();

  start_sequence ();
  emit_macro_launch (descriptor);
  rtx_insn *replacement = get_insns ();
  end_sequence ();

  if (preheader)
    {
      rtx_insn *anchor = BB_END (preheader);
      if (JUMP_P (anchor))
	emit_insn_before (prefix, anchor);
      else
	emit_insn_after (prefix, anchor);
      delete_insn (all_lanes_enable_before (descriptor.insns[0]));
    }
  else
    emit_insn_before (prefix, descriptor.insns[0]);
  emit_insn_before (replacement, descriptor.insns[0]);
  for (rtx_insn *insn : descriptor.insns)
    delete_insn (insn);
  delete_insn (descriptor.increment_insn);
}

static const char *
macro_target_name ()
{
  if (TARGET_XTT_TENSIX_WH)
    return "wh-v2";
  if (TARGET_XTT_TENSIX_BH)
    return "bh-v3";
  if (TARGET_XTT_TENSIX_QSR)
    return "qsr-v4";
  gcc_unreachable ();
}

static const char *
macro_target_encoding_name ()
{
  if (TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH)
    return "lregind-address";
  if (TARGET_XTT_TENSIX_QSR)
    return "seqid-split-vd-done";
  gcc_unreachable ();
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

  if (get_attr_xtt_macro_resource (insns[0]) != XTT_MACRO_RESOURCE_LOAD
      || get_attr_xtt_macro_resource (insns[1]) != XTT_MACRO_RESOURCE_LOAD
      || (get_attr_xtt_macro_resource (insns[2])
	  != XTT_MACRO_RESOURCE_SIMPLE_MAD_WRITE)
      || get_attr_xtt_macro_resource (insns[3]) != XTT_MACRO_RESOURCE_STORE)
    return descriptor_status::resource_mismatch;

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
	     "rwc=outside-candidate "
	     "resources=load,load,simple+mad-write,store "
	     "target=%s target-encoding=%s calendar=missing emit=no\n",
	     descriptor.load0_reg, descriptor.load1_reg, descriptor.store_reg,
	     descriptor.swap_mod, macro_target_name (),
	     macro_target_encoding_name ());
  else if (status == descriptor_status::resource_mismatch)
    fprintf (dump_file,
	     "  descriptor-reject=resource-attribute-mismatch emit=no\n");
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

/* Discover maximal load...store stretches in each basic block.  Analysis mode
   remains byte-identical; emission mode replaces only a fully configured and
   proven region.  */
static bool
discover (function *fn)
{
  bool changed = false;
  bool config_available = !source_config_access_p (fn);
  auto_vec<configured_descriptor, 2> configured_regions;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      macro_candidate candidate;
      rtx_insn *insn;

      for (insn = BB_HEAD (bb); insn; )
	{
	  rtx_insn *next = insn == BB_END (bb) ? nullptr : NEXT_INSN (insn);
	  if (!candidate.first)
	    {
	      if (!load_p (insn))
		{
		  insn = next;
		  continue;
		}
	      candidate.first = insn;
	      candidate.last = insn;
	      candidate.words = 1;
	      candidate.loads = 1;
	      candidate.unsupported_bulk = unsupported_bulk_p (insn);
	      insn = next;
	      continue;
	    }

	  if (!tensix_p (insn))
	    {
	      /* Notes and labels do not issue and do not split the stream.  */
	      if (!NONDEBUG_INSN_P (insn))
		{
		  insn = next;
		  continue;
		}
	      candidate.crossed_non_sfpu = true;
	      dump_candidate (bb, candidate, false);
	      candidate = macro_candidate {};
	      insn = next;
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
	      configured_descriptor configured;
	      if (riscv_tt_emit_loadmacro && config_available
		  && (TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH)
		  && describe_configured_region (candidate, &configured))
		configured_regions.safe_push (configured);
	      candidate = macro_candidate {};
	    }
	  insn = next;
	}

      if (candidate.first)
	dump_candidate (bb, candidate, false);
    }
  /* A function-owned descriptor is materialized once only when the complete
     function has exactly one static launch site.  Multiple sites need path-
     sensitive descriptor ownership and are deliberately left byte-identical.
     A canonical one-block loop gets its enable and configuration in the
     structural preheader; a straight-line site retains local setup.  */
  if (configured_regions.length () == 1)
    {
      basic_block preheader
	= single_block_loop_preheader (configured_regions[0]);
      /* Never fall back to local materialization for a loop.  If the
	 structural preheader or whole-loop ownership proof fails, retaining
	 the explicit body is both the safe and the performance-honest result.  */
      if (preheader || !self_loop_p (configured_regions[0]))
	{
	  emit_configured_region (configured_regions[0], preheader);
	  changed = true;
	}
    }
  return changed;
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
    return TARGET_XTT_TENSIX
      && (riscv_tt_analyze_loadmacro || riscv_tt_emit_loadmacro);
  }

  unsigned execute (function *fn) final override
  {
    return discover (fn) ? TODO_df_finish : 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_loadmacro (gcc::context *ctxt)
{
  return new pass_rvtt_loadmacro (ctxt);
}
