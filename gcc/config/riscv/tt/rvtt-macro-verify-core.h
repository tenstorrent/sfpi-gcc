/* Macro-planner descriptor verification core -- standalone.
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

/* Macro descriptor verification over plain data and the capability-table
   decoders, shared by the in-compiler verifier (rvtt-macro-verify.cc) and its
   standalone unit test (rvtt-macro-verify-test.cc).  Each check decodes the
   synthesized words back through the tables and compares them with
   expectations built independently from the region's facts; a mismatch reports
   the failing component.  */

#ifndef GCC_RVTT_MACRO_VERIFY_CORE_H
#define GCC_RVTT_MACRO_VERIFY_CORE_H

#include "rvtt-macro-tables.h"

namespace rvtt_macro_verify
{

struct expect_access
{
  unsigned macro_index;
  unsigned vd;
  unsigned address, mode, addr_mode;
};

struct expect_template
{
  bool whole_word;
  uint32_t word;		/* whole-word expectation	       */
  uint8_t opcode;		/* field expectations otherwise	       */
  uint16_t imm12;
  uint8_t dest_sel;
  uint8_t mod1;
};

/* CC-template model facts, re-derived from the region's facts and compared
   with the synthesized descriptor's model.  The timing inequalities are
   re-checked from these slots, so a wrong model cannot pass by agreeing with
   itself.  */
struct expect_cc
{
  bool active;
  bool complement;
  int def_visible_slot;
  int pre_load_slot;
  int post_load_slot;
  int store_exec_slot;
  int restore_visible_slot;
  int row_interval;
};

struct expectations
{
  expect_access accesses[4];
  unsigned n_accesses;
  expect_template templates[4];
  unsigned n_templates;
  uint32_t seq_words[4];
  unsigned n_seq;
  uint32_t misc;
  bool check_misc;
  int stride;			/* absorbed Dst stride; 0 = none       */
  uint32_t planned_lregs;
  expect_cc cc;
};

struct descriptor_words
{
  uint32_t templ[4];
  unsigned n_templates;
  uint32_t seq[4];
  unsigned n_seq;
  uint32_t misc;
  rvtt_macro::setc16_program setc16[8];
  unsigned n_setc16;
  uint32_t launch_words[4];
  unsigned n_launches;
  expect_cc cc;			/* the synthesized CC model	       */
};

/* Verify descriptor DESC against EXPECT for capability table C.  Return null
   on success, or the tag of the first failed comparison.  */
inline const char *
verify (const rvtt_macro::caps *c, const descriptor_words &desc,
	const expectations &expect)
{
  /* Launches decode back to exactly the expected typed accesses.  */
  if (desc.n_launches != expect.n_accesses)
    return "launch-count";
  for (unsigned ix = 0; ix != desc.n_launches; ++ix)
    {
      unsigned macro_index, vd, mode, addr_mode, address;
      if (!rvtt_macro::decode_launch (c, desc.launch_words[ix], &macro_index,
				      &vd, &mode, &addr_mode, &address))
	return "launch-decode";
      const expect_access &a = expect.accesses[ix];
      if (macro_index != a.macro_index || vd != a.vd || mode != a.mode
	  || addr_mode != a.addr_mode || address != a.address)
	return "launch-mismatch";
    }

  /* Templates decode to the expected fields, or equal the expected whole
     word.  */
  if (desc.n_templates != expect.n_templates)
    return "template-count";
  for (unsigned t = 0; t != desc.n_templates; ++t)
    {
      const expect_template &e = expect.templates[t];
      if (e.whole_word)
	{
	  if (desc.templ[t] != e.word)
	    return "template-mismatch";
	  continue;
	}
      rvtt_macro::template_spec spec;
      if (!rvtt_macro::decode_template (desc.templ[t], &spec))
	return "template-decode";
      if (spec.opcode != e.opcode || spec.imm12 != e.imm12
	  || spec.dest_sel != e.dest_sel || spec.mod1 != e.mod1)
	return "template-mismatch";
    }

  /* Sequence and misc words equal the expected programs.  */
  if (desc.n_seq != expect.n_seq)
    return "sequence-count";
  for (unsigned m = 0; m != desc.n_seq; ++m)
    if (desc.seq[m] != expect.seq_words[m])
      return "sequence-mismatch";
  if (expect.check_misc && desc.misc != expect.misc)
    return "misc-mismatch";

  /* SETC16 slot programs are re-derived from the stride.  */
  if (expect.stride)
    {
      rvtt_macro::setc16_program programs[8];
      unsigned n_programs = 0;
      bool needs_bank_base = false;
      if (!rvtt_macro::addr_mod_program (c, expect.stride, programs,
					 &n_programs, &needs_bank_base))
	return "setc16-underivable";
      if (n_programs != desc.n_setc16)
	return "setc16-count";
      for (unsigned s = 0; s != n_programs; ++s)
	if (desc.setc16[s].config_reg != programs[s].config_reg
	    || desc.setc16[s].value != programs[s].value)
	  return "setc16-mismatch";
    }
  else if (desc.n_setc16)
    return "setc16-count";

  /* Hidden physical writes of the chosen template words must be covered
     by the planner-owned register set.  */
  for (unsigned t = 0; t != desc.n_templates; ++t)
    {
      uint32_t hidden
	= rvtt_macro::template_hidden_lreg_writes (c, desc.templ[t]);
      if (hidden & ~expect.planned_lregs)
	return "hidden-write-unowned";
    }

  /* The synthesized CC-template model must equal the expectation field for
     field, and the deferred-CC timing constraints must hold.  */
  if (desc.cc.active != expect.cc.active)
    return "cc-model";
  if (expect.cc.active)
    {
      if (desc.cc.complement != expect.cc.complement
	  || desc.cc.def_visible_slot != expect.cc.def_visible_slot
	  || desc.cc.pre_load_slot != expect.cc.pre_load_slot
	  || desc.cc.post_load_slot != expect.cc.post_load_slot
	  || desc.cc.store_exec_slot != expect.cc.store_exec_slot
	  || desc.cc.restore_visible_slot != expect.cc.restore_visible_slot
	  || desc.cc.row_interval != expect.cc.row_interval)
	return "cc-model";
      /* Deferred-CC dataflow, plus the store-mask constraint
	 (store_lane_mask_live_at_execution): the all-lanes restore must take
	 effect before the store executes (restore_visible - lag < store_exec),
	 and the store must execute before the next row's predicate definition
	 (def_visible - lag + row_interval).  */
      int lag = (int) rvtt_macro::cc_visibility_lag ();
      if (expect.cc.pre_load_slot >= expect.cc.def_visible_slot
	  || expect.cc.post_load_slot < expect.cc.def_visible_slot
	  || expect.cc.store_exec_slot <= expect.cc.post_load_slot
	  || expect.cc.restore_visible_slot <= expect.cc.post_load_slot
	  || expect.cc.restore_visible_slot > expect.cc.row_interval
	  || expect.cc.restore_visible_slot - lag
	     >= expect.cc.store_exec_slot
	  || expect.cc.store_exec_slot
	     >= expect.cc.def_visible_slot - lag + expect.cc.row_interval)
	return "cc-timing";
    }

  return nullptr;
}

}  /* namespace rvtt_macro_verify */

#endif /* GCC_RVTT_MACRO_VERIFY_CORE_H */
