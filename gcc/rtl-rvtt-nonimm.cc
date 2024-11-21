/* Pass to complete handling of the SFPU non-imm insns
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
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "cfghooks.h"
#include "df.h"
#include "memmodel.h"
#include "tm_p.h"
#include "insn-config.h"
#include "regs.h"
#include "emit-rtl.h"
#include "recog.h"
#include "cgraph.h"
#include "tree-pretty-print.h" /* for dump_function_header */
#include "varasm.h"
#include "insn-attr.h"
#include "conditions.h"
#include "flags.h"
#include "output.h"
#include "except.h"
#include "rtl-error.h"
#include "toplev.h" /* exact_log2, floor_log2 */
#include "reload.h"
#include "intl.h"
#include "cfgrtl.h"
#include "debug.h"
#include "tree-pass.h"
#include "tree-ssa.h"
#include "cfgloop.h"
#include "stringpool.h"
#include "attribs.h"
#include "asan.h"
#include "rtl-iter.h"
#include "print-rtl.h"
#include "function-abi.h"
#include <vector>
#include "config/riscv/rvtt.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

// Return the operation (opcode, imm values, operands) and id for the insn
static void get_opid(unsigned int *op,
		     unsigned int *id,
		     const rvtt_insn_data *insnd,
		     rtx pat)
{
  unsigned int dst_regno, dst_regshft, src_regno, src_regshft;

  switch (insnd->id) {
  case rvtt_insn_data::sfpnonimm_dst:
    dst_regno = rvtt_sfpu_regno(XEXP(pat, 0));
    dst_regshft = INTVAL(XVECEXP(XEXP(pat, 1), 0, 3));
    src_regno = 0;
    src_regshft = 0;
    *op = INTVAL(XVECEXP(XEXP(pat, 1), 0, insnd->nonimm_pos));
    *id = INTVAL(XVECEXP(XEXP(pat, 1), 0, insnd->nonimm_pos + 1));
    break;

  case rvtt_insn_data::sfpnonimm_dst_src:
    dst_regno = rvtt_sfpu_regno(XEXP(pat, 0));
    dst_regshft = INTVAL(XVECEXP(XEXP(pat, 1), 0, 4));
    src_regno = rvtt_sfpu_regno(XVECEXP(XEXP(pat, 1), 0, 3));
    src_regshft = INTVAL(XVECEXP(XEXP(pat, 1), 0, 5));
    *op = INTVAL(XVECEXP(XEXP(pat, 1), 0, insnd->nonimm_pos));
    *id = INTVAL(XVECEXP(XEXP(pat, 1), 0, insnd->nonimm_pos + 1));
    break;

  case rvtt_insn_data::sfpnonimm_src:
  case rvtt_insn_data::sfpnonimm_store:
    dst_regno = 0;
    dst_regshft = 0;
    src_regno = rvtt_sfpu_regno(XVECEXP(pat, 0, 0));
    src_regshft = INTVAL(XVECEXP(pat, 0, 3));
    *op = INTVAL(XVECEXP(pat, 0, insnd->nonimm_pos));
    *id = INTVAL(XVECEXP(pat, 0, insnd->nonimm_pos + 1));
    break;

  default:
    gcc_assert(0);
    break;
  }

  *op |= (dst_regno << dst_regshft) | (src_regno << src_regshft);

  DUMP("  id:%d updating a %s, D:lr%d<<%d S:lr%d<<%d %x\n",
       *id, insnd->name, dst_regno, dst_regshft, src_regno, src_regshft, *op);
}

static void set_opid(const rvtt_insn_data *insnd,
		     rtx pat,
		     unsigned int offset,
		     unsigned int val)

{
  switch (insnd->id) {
  case rvtt_insn_data::sfpnonimm_dst:
  case rvtt_insn_data::sfpnonimm_dst_src:
    XVECEXP(XEXP(pat, 1), 0, insnd->nonimm_pos + offset) = GEN_INT(val);
    break;

  case rvtt_insn_data::sfpnonimm_src:
  case rvtt_insn_data::sfpnonimm_store:
    XVECEXP(pat, 0, insnd->nonimm_pos + offset) = GEN_INT(val);
    break;

  default:
    gcc_assert(0);
    break;
  }
}

// This phase of the non-immediate processing works by:
//  - finding all of the load_immediate calls and creating a lookup table
//    based on their id (the value currently being loaded, as set in the
//    nonimm-tag gimple pass)
//  - finding all of the non-immediate insns and matching them via the table
//    to their load immediate
//  - updating the immediate value to match that needed by the insn.  if the
//    value was already set, then confirming the match.  if the old value and
//    new value do not match, the nonimm is flagged to emit the fallback code
void transform(function *cfn)
{
  std::vector<rtx> load_imm_map;
  load_imm_map.reserve(20);

  DUMP("Nonimm rtl pass on: %s\n", function_name(cfn));

  basic_block bb;

  // Generate lookup table from the load_immediates
  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
       {
	 const rvtt_insn_data *insnd;
	 if (NONDEBUG_INSN_P(insn) &&
	     rvtt_p(&insnd, insn) &&
	     insnd->id == rvtt_insn_data::load_immediate)
	   {
	     rtx li_pat = PATTERN(insn);
	     unsigned int id = INTVAL(XVECEXP(XEXP(li_pat, 1), 0, 0));
	     DUMP("  saving a %s at slot %u\n", insnd->name, id);
	     if (load_imm_map.size() <= id)
	       {
		 load_imm_map.resize(id + 1);
	       }
	     load_imm_map[id] = insn;
	   }
       }
    }

  if (load_imm_map.size() == 0)
    return;

  // Processes the nonimm instructions
  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
       {
	 const rvtt_insn_data *insnd;

	 if (NONDEBUG_INSN_P(insn) &&
	     rvtt_p(&insnd, insn) &&
	     insnd->nonimm_pos != -1 &&
	     insnd->rtl_only_p())
	   {
	     gcc_assert(GET_CODE(PATTERN(insn)) == PARALLEL);
	     rtx pat = XVECEXP(PATTERN(insn), 0, 0);
	     unsigned int op, id;
	     get_opid(&op, &id, insnd, pat);

	     gcc_assert(id < load_imm_map.size());
	     rtx li_insn = load_imm_map[id];
	     rtx li_pat = PATTERN(li_insn);
	     unsigned int li_op = INTVAL(XVECEXP(XEXP(li_pat, 1), 0, 0));
	     // Hasn't been processed if opcode field of operation is 0
	     if ((li_op & 0xFF000000) == 0)
	       {
		 // Set both the li's and insn's opcode to current value
		 DUMP("    id:%d first use of li, updating\n", id);
		 set_opid(insnd, pat, 0, op);
		 XVECEXP(XEXP(li_pat, 1), 0, 0) = gen_rtx_CONST_INT(SImode, op);
	       }
	     else if (li_op == op)
	       {
		 // Set just the insn's opcode to current value (li already done)
		 DUMP("    id:%d re-use of li\n", id);
		 set_opid(insnd, pat, 0, op);
	       }
	     else
	       {
		 // Optimization potential: use histogram to figure out which
		 // pattern is used most to fall back the least
		 // Set the insn's opcode to the li (base) op for comparison at emit
		 // Set the insn's id to flag a fallback
		 DUMP("    id:%d ops do not match (old 0x%x new 0x%x), fall back\n", id, li_op, op);
		 set_opid(insnd, pat, 0, li_op);
		 set_opid(insnd, pat, 1, id | SFPNONIMM_ID_FALLBACK_FLAG);
	       }
	   }
       }
    }
}

namespace {

const pass_data pass_data_rvtt_nonimm =
{
  RTL_PASS, /* type */
  "rvtt_nonimm", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_nonimm : public rtl_opt_pass
{
public:
  pass_rvtt_nonimm (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_nonimm, ctxt)
  {}

  /* opt_pass methods: */
  virtual unsigned int execute (function *cfn)
    {
      if (TARGET_RVTT)
	transform (cfn);
      return 0;
    }
}; // class pass_rvtt_nonimm

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_nonimm (gcc::context *ctxt)
{
  return new pass_rvtt_nonimm (ctxt);
}
