/* Pass to work around GS' memory aribtration bug
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
#include "domwalk.h"
#include <vector>
#include "config/riscv/rvtt.h"

//#define DUMP_ANALYSIS
#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

const int stack_ptr_regno = 2;

using namespace std;

// A BB can resolve its parents' HLL loads by:
//  1) reading the incoming war reg
//  2) issuing a load (updates HLL or otherwise) and reading the result
//
// #2 is a stronger result as any parent BB with an ll_count low enough can be
// resolved. If the BB reads the incoming reg, then other parents must
// depend on the same reg or resolve the HLL load themselves
// The present implementation  doesn't try to get correct results for both #1
// and #2, instead it tracks the first workaround hit and classifies it as one
// or the other.  Future work if valuable...
struct bb_entry {
  bool inited;                      // true once populated w/ incoming data
  int ll_incoming;                  // #ll for first parent BB to get to this BB
  vector<int> hll_war_reg_incoming; // regs passed in that can resolve the parent
  int ll_before_resolve_all;        // #ll before all WARs are resolved (-1 if NA)
};

typedef vector<bb_entry> bb_data;

class gshllwar_dom_walker : public dom_walker
{
 public:
  gshllwar_dom_walker(bb_data& inbbd) : dom_walker(CDI_DOMINATORS), bbd(inbbd) {}
  ~gshllwar_dom_walker() {}

  edge before_dom_children(basic_block bb) FINAL OVERRIDE;

 private:
  bb_data& bbd;
};

// Copied (and modified) from rtl.c.  This is potentially subject to rot.
//
// That routine didn't quite do what was needed - primarily because it would
// call RETURN's JUMP_INSN and the client of this code needs to ignore jumps
// between BBs but act on RETURNs.
static enum rtx_code
my_classify_insn (rtx x)
{
  if (LABEL_P (x))
    return CODE_LABEL;
  if (GET_CODE (x) == CALL)
    return CALL_INSN;
  if (ANY_RETURN_P (x))
    return RETURN;
  if (GET_CODE (x) == ASM_OPERANDS && ASM_OPERANDS_LABEL_VEC (x))
    return JUMP_INSN;
  if (GET_CODE (x) == SET)
    {
      if (GET_CODE (SET_DEST (x)) == PC)
	return JUMP_INSN;
      else if (GET_CODE (SET_SRC (x)) == CALL)
	return CALL_INSN;
      else
	return INSN;
    }

  if (GET_CODE (x) == PARALLEL)
    {
      int j;
      bool has_return_p = false;
      for (j = XVECLEN (x, 0) - 1; j >= 0; j--)
	{
	  if (GET_CODE (XVECEXP (x, 0, j)) == CALL)
	    return CALL_INSN;
	  else if (ANY_RETURN_P (XVECEXP (x, 0, j)))
	    has_return_p = true;
	  else if (GET_CODE (XVECEXP (x, 0, j)) == SET
		   && GET_CODE (SET_DEST (XVECEXP (x, 0, j))) == PC)
	    return JUMP_INSN;
	  else if (GET_CODE (XVECEXP (x, 0, j)) == SET
		   && GET_CODE (SET_SRC (XVECEXP (x, 0, j))) == CALL)
	    return CALL_INSN;
	}

      if (has_return_p)
	return RETURN;
      if (GET_CODE (XVECEXP (x, 0, 0)) == ASM_OPERANDS
	  && ASM_OPERANDS_LABEL_VEC (XVECEXP (x, 0, 0)))
	return JUMP_INSN;
    }

  return INSN;
}

static void
emit_war(rtx_insn *where,
	 int hll_war_reg,
	 int *ll_count)
{
  DUMP("    emitting WAR with reg %d\n", hll_war_reg);

  *ll_count = -1;

  rtx reg = gen_rtx_REG(SImode, hll_war_reg);
  emit_insn_before(gen_rvtt_gs_l1_load_war(reg), where);
}

// Return the last insn in the BB unless it is a jump, then return the insn
// before that
static rtx_insn *
bb_insn_before_jump(basic_block bb)
{
  rtx_insn *insn = BB_END(bb);
  rtx insn_pat = PATTERN(insn);
  if (my_classify_insn(insn_pat) == JUMP_INSN)
    {
      return insn;
    }
  else
    {
      return NEXT_INSN(insn);
    }
}

static bool
load_mem_p(rtx pat)
{
  return GET_CODE(pat) == SET &&
    GET_CODE(SET_SRC(pat)) != CALL &&
    contains_mem_rtx_p(SET_SRC(pat));
}

static bool
nonstack_store_p(rtx pat)
{
  return
    GET_CODE(pat) == SET &&
    contains_mem_rtx_p(SET_DEST(pat)) &&
    !refers_to_regno_p(stack_ptr_regno, pat);
}

static bool
volatile_store_p(rtx pat)
{
  return MEM_VOLATILE_P(SET_DEST(pat));
}

static bool
volatile_load_p(rtx pat)
{
  return load_mem_p(pat) && MEM_VOLATILE_P(SET_SRC(pat));
}

static void
set_hll_war_reg(vector<int>& hll_war_regs, int reg)
{
  hll_war_regs.clear();
  hll_war_regs.push_back(reg);
}

static bool refers_to_any_regno_p(vector<int>& regs, rtx_insn *insn)
{
  for (auto reg : regs)
    {
      if (refers_to_regno_p(reg, PATTERN(insn)))
	{
	  return true;
	}
    }

  return false;
}

//
// This code works around the GS HLL arbiter HW bug
//
// The idea is to go through the insns and once an HLL load is seen count the
// local memory loads and emit the workaround before enough local
// loads have been hit to result in a hang.  The workaround consists of a use
// of a register that was the target of the most recent HLL load or a
// subsequent local memory load - these guarentee all loads have been
// resolved.  Note that the ordering of the code itself will workaround most
// of the potential hangs without explicitly emitting a workaround.
// Details:
//  - track the most recent HLL load
//  - track all loads after the latest HLL load
//  - reset the count if the target register of a load is referenced as an
//    input operand.  Note that we only track the most recent HLL load and
//    subsequent local loads.  In theory, we could track all of them and
//    reduce the count if an earlier HLL load lands, in practice this is
//    unlikely to occur since the latency of HLL loads is so high that the
//    compiler hoists them all together - tracking the most recent is probably
//    sufficient.  Revisit if needed
//  - reset the count if a manual workaround is encountered
//  - delete manually inserted workarounds that are not needed
//  - emit the workaround if the local load after HLL load count gets too high
//    (4 are allowed)
//  - abort on certain instructions (eg, inline assembly)
//
// In addition, BBs are traversed and the local load count is carried into the
// next BB.  The BBs are visted in dom order, so all parents of a BB have been
// visited before the BB.  Each BB is responsible for resolving any unresolved
// HLL loads in the parents.  The first parent forces its requirements on the
// child, subsequent parents either must conform or restrict those
// requirements.  If they cannot, then any pending workaround is emitted in
// those parents, otherwise the workaround is pushed in the child.
edge gshllwar_dom_walker::before_dom_children(basic_block bb)
{
  bb_entry& bbe = bbd[bb->index];
  gcc_assert(bbe.hll_war_reg_incoming.size() <= 1);
  DUMP(" processing bb[%d], in_ll %d, reg %d, ll_all %d\n", bb->index, bbe.ll_incoming,
       bbe.hll_war_reg_incoming.size() == 0 ? -1 : bbe.hll_war_reg_incoming[0],
       bbe.ll_before_resolve_all);

  vector<int>hll_war_regs = bbe.hll_war_reg_incoming;
  int ll_count = bbe.ll_incoming;

  rtx_insn *insn, *next;
  FOR_BB_INSNS_SAFE(bb, insn, next)
    {
      if (GET_CODE(insn) == NOTE &&
	  NOTE_KIND(insn) == NOTE_INSN_EPILOGUE_BEG &&
	  ll_count != -1)
	{
	  DUMP("  epilogue beginning with hll active, emitting WAR\n");
	  // Note: could return here, no harm in continuing since war is done
	  emit_war(insn, hll_war_regs[0], &ll_count);
	}
      else if (NONDEBUG_INSN_P(insn))
	{
	  int code = recog_memoized(insn);
	  rtx insn_pat = PATTERN(insn);
	  const rvtt_insn_data *insnd;
	  rvtt_p (&insnd, insn);
	  if (ll_count == -1)
	    {
	      if (rvtt_needs_gs_hll_war_p(insn_pat))
		{
		  ll_count = 0;
		  set_hll_war_reg(hll_war_regs, rvtt_get_insn_dst_regno(insn));
		  DUMP("  found an hll load of reg %d, war pending\n", hll_war_regs[0]);
		}
	      else if (insnd->id == rvtt_insn_data::l1_load_war)
		{
		  DUMP("  found a %s, war not pending, removing\n", insnd->name);
		  set_insn_deleted(insn);
		}
	    }
	  else
	    {
	      int classify = my_classify_insn(insn_pat);
	      if (rvtt_needs_gs_hll_war_p(insn_pat))
		{
		  set_hll_war_reg(hll_war_regs, rvtt_get_insn_dst_regno(insn));
		  DUMP("  found an hll load of reg %d, updating war regs\n", hll_war_regs[0]);
		}
	      else if (code != -1 &&
		       (classify == INSN || classify == JUMP_INSN) &&
		       refers_to_any_regno_p(hll_war_regs, insn))
		{
		  DUMP("  a %s refers to hll loaded reg %d, resetting ll_count\n",
		       insn_data[code].name, hll_war_regs[0]);
		  ll_count = -1;
		}
	      else if (insnd->id == rvtt_insn_data::l1_load_war)
		{
		  int reg = REGNO(rvtt_get_insn_operand(0, insn));
		  if (reg != hll_war_regs[0])
		    {
		      DUMP("  found a %s for reg %d mismatching last hll_war_reg %d\n", insnd->name, reg, hll_war_regs[0]);
		      // XXXX, what's the best warning code for this?
		      warning(OPT_Wunused, "__builtin_rvtt_gs_%s found using out of date hll loaded register, deleted\n", insnd->name);
		      set_insn_deleted(insn);
		    }
		  else
		    {
		      DUMP("  found a %s for reg %d, resetting ll_count\n", insnd->name, reg);
		      ll_count = -1;
		    }
		}
	      else if (classify != INSN && classify != JUMP_INSN)
		{
		  DUMP("  %s with hll active, emitting WAR\n", GET_RTX_NAME(classify));
		  emit_war(insn, hll_war_regs[0], &ll_count);
		}
	      else if (load_mem_p(insn_pat))
		{
		  ll_count++;
		  int reg = rvtt_get_insn_dst_regno(insn);
		  DUMP("  local load of %d w/ hll active, bumped ll_count to %d\n", reg, ll_count);
		  if (ll_count == 5)
		    {
		      DUMP("  local load is 5, emitting WAR\n");
		      emit_war(insn, hll_war_regs[0], &ll_count);
		    }
		  else
		    {
		      hll_war_regs.push_back(reg);
		    }
		}
	      else if (GET_CODE(insn_pat) == PARALLEL)
		{
		  for (int i = XVECLEN (insn_pat, 0) - 1; i >= 0; i--)
		    {
		      rtx sub = XVECEXP(insn_pat, 0, i);
		      DUMP("  processing parallel %s\n", GET_RTX_NAME(GET_CODE(sub)));

		      if (load_mem_p(sub))
			{
			  ll_count++;
			  int reg = REGNO(SET_DEST(sub));
			  DUMP("    load of %d in parallel w/ hll active, ll_bumped count to %d\n", reg, ll_count);
			  if (ll_count == 5)
			    {
			      DUMP("  local load is 5 with parallel, emitting WAR\n");
			      emit_war(insn, hll_war_regs[0], &ll_count);
			    }
			  else
			    {
			      hll_war_regs.push_back(reg);
			    }
			  break;
			}
		    }
		}
	    }
	}
    }

  // If this BB has a pending WAR, see if this can be pushed into all of the children
  edge_iterator ei;
  edge e;
  if (ll_count != -1)
    {
      bool children_can_handle = true;
      FOR_EACH_EDGE(e, ei, bb->succs)
	{
	  // If we previously inited the outgoing BB, it's war location is determined
	  // If this block doesn't conform, then force the war at the end of this block
	  bb_entry& dst_bbe = bbd[e->dest->index];

	  DUMP("  checking BB[%d] w/ inited %d ll_count %d hll_reg %d dst.ll %d dst.ll_all %d dst.hll_reg %d\n",
	       e->dest->index,
	       dst_bbe.inited,
	       ll_count,
	       hll_war_regs[0],
	       dst_bbe.ll_incoming,
	       dst_bbe.ll_before_resolve_all,
	       (dst_bbe.ll_incoming == -1) ? -1 : dst_bbe.hll_war_reg_incoming[0]);

	  if ((dst_bbe.ll_before_resolve_all != -1 && dst_bbe.ll_before_resolve_all + ll_count < 5) ||
	      !dst_bbe.inited ||
	      (dst_bbe.inited && dst_bbe.hll_war_reg_incoming[0] == hll_war_regs[0]))
	    {
	      DUMP("    checked BB[%d], all ok\n", e->dest->index);
	    }
	  else if (dst_bbe.inited && dst_bbe.hll_war_reg_incoming[0] == hll_war_regs[0] && ll_count > dst_bbe.ll_incoming)
	    {
	      DUMP("    checked BB[%d], restricting ll_count from %d to %d\n", e->dest->index, dst_bbe.ll_incoming, ll_count);
	    }
	  else 
	    {
	      DUMP("    checked BB[%d], can't handle, emitting WAR\n", e->dest->index);
	      children_can_handle = false;
	      emit_war(bb_insn_before_jump(bb), hll_war_regs[0], &ll_count);
	      break;
	    }
	}

      if (children_can_handle)
	{
	  FOR_EACH_EDGE(e, ei, bb->succs)
	    {
	      bb_entry& dst_bbe = bbd[e->dest->index];
	      if (!dst_bbe.inited)
		{
		  dst_bbe.inited = true;
		  dst_bbe.hll_war_reg_incoming.push_back(hll_war_regs[0]);
		  dst_bbe.ll_incoming = ll_count;
		}
	      else if (ll_count > dst_bbe.ll_incoming)
		{
		  // Restrict ll_count based on this bb.  ll_incoming was
		  // either -1, or we hit the restriction code above
		  dst_bbe.ll_incoming = ll_count;
		}
	    }
	}
    }

  return NULL;
}

// Process each BB, count the number of local loads that occur before any load
// is resolved by a use.  Set ll_before_resolve_all to that value (that use
// will resolve any incoming hll load after ll_before_resolve_all local
// loads).
static void pre_populate_bbd(bb_data &bbd, function *fn)
{
  basic_block bb;

  FOR_EACH_BB_FN (bb, fn)
    {
      bb_entry& bbe = bbd[bb->index];
      vector<int>hll_war_regs = bbe.hll_war_reg_incoming;
      rtx_insn *insn;
      int ll_count = 0;
      FOR_BB_INSNS(bb, insn)
	{
	  if (NONDEBUG_INSN_P(insn))
	    {
	      int code = recog_memoized(insn);
	      rtx insn_pat = PATTERN(insn);
	      const rvtt_insn_data *insnd;
	      rvtt_p (&insnd, insn);
	      int classify = my_classify_insn(insn_pat);

	      if (rvtt_needs_gs_hll_war_p(insn_pat))
		{
		  hll_war_regs.push_back(rvtt_get_insn_dst_regno(insn));
		}
	      else if (code != -1 &&
		       (classify == INSN || classify == JUMP_INSN) &&
		       refers_to_any_regno_p(hll_war_regs, insn))
		{
		  bbe.ll_before_resolve_all = ll_count;
		  break;
		}
	      else if (insnd->id == rvtt_insn_data::l1_load_war)
		{
		  int reg = REGNO(rvtt_get_insn_operand(0, insn));
		  bool found = false;
		  for (auto warreg : hll_war_regs)
		    {
		      if (reg == warreg)
			{
			  bbe.ll_before_resolve_all = ll_count;
			  found = true;
			  break;
			}
		    }
		  if (found) break;
		}
	      else if (classify != INSN && classify != JUMP_INSN)
		{
		  bbe.ll_before_resolve_all = 5;
		  break;
		}
	      else if (load_mem_p(insn_pat))
		{
		  ll_count++;
		  if (ll_count == 5)
		    {
		      bbe.ll_before_resolve_all = 5;
		      break;
		    }
		  else
		    {
		      hll_war_regs.push_back(rvtt_get_insn_dst_regno(insn));
		    }
		}
	      else if (GET_CODE(insn_pat) == PARALLEL)
		{
		  bool done = false;
		  for (int i = XVECLEN (insn_pat, 0) - 1; i >= 0; i--)
		    {
		      rtx sub = XVECEXP(insn_pat, 0, i);

		      if (load_mem_p(sub))
			{
			  ll_count++;
			  if (ll_count == 5)
			    {
			      bbe.ll_before_resolve_all = 5;
			      done = true;
			      break;
			    }
			  else
			    {
			      hll_war_regs.push_back(REGNO(SET_DEST(sub)));
			    }
			}
		    }
		  if (done) break;
		}
	    }
	}

      DUMP("  done preprocess bb[%d]: resolve_all: %d\n", bb->index, bbe.ll_before_resolve_all);
      bbe.hll_war_reg_incoming.resize(0);
    }
}

static void
workaround_gs_hll(function *fn)
{
  DUMP("TT riscv GS rtl hll war pass on: %s\n", function_name(fn));

  bb_data bbd;
  bbd.resize(n_basic_blocks_for_fn(fn));
  for (auto& bbe : bbd)
    {
      bbe.inited = false;
      bbe.ll_incoming = -1;
      bbe.hll_war_reg_incoming.resize(0);
      bbe.hll_war_reg_incoming.reserve(5);
      bbe.ll_before_resolve_all = -1;
    }

  pre_populate_bbd(bbd, fn);

  calculate_dominance_info(CDI_DOMINATORS);
  gshllwar_dom_walker dw(bbd);
  dw.walk(ENTRY_BLOCK_PTR_FOR_FN(fn));
}

struct hl_load_use {
  rtx_insn *insn;     // the insn that uses a hl load
  int insn_count;
};

// hl: high latency
struct hl_load {
  rtx_insn *insn;             // the high latency load insn (hll/reg)
  basic_block bb;             // bb for insn (why why why)
  vector<hl_load_use> uses;   // each of the uses (until re-def) of this reg
  int insn_count;
};

typedef vector<struct hl_load_use> hl_load_uses;

// Build up a vectory of hl_loads such that:
//  - there is one entry per hll/reg load in reverse order
//  - each entry points to the insn, all the uses of the def of that insn
//    including the number of local loads between the def and use, the
//    previous use of that register in the BB (if there is one) and the number
//    of ll_loads between this def an the end of the BB
static void
create_log_links (function *fn, vector<hl_load>& hl_loads)
{
  basic_block bb;
  rtx_insn *insn;
  df_ref use;

  vector<hl_load_uses> all_uses;
  all_uses.resize(max_reg_num());

  FOR_EACH_BB_FN (bb, fn)
    {
      int insn_count = 0;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  df_ref def;
	  rtx insn_pat = PATTERN(insn);
	  if (load_mem_p(insn_pat))
	    {
	      if (rvtt_l1_load_p(insn_pat) ||
		  rvtt_reg_load_p(insn_pat))
		{
		  def = df_single_def(DF_INSN_INFO_GET(insn));
		  gcc_assert(def != nullptr);

		  unsigned int regno = DF_REF_REGNO (def);
		  auto& uses = all_uses[regno];

		  struct hl_load hll;
		  hll.insn = insn;
		  hll.bb = bb;
		  hll.uses = uses;
		  hll.insn_count = insn_count;
		  hl_loads.push_back(hll);
		}
	    }

	  FOR_EACH_INSN_DEF (def, insn)
	    {
	      unsigned int regno = DF_REF_REGNO (def);
	      all_uses[regno].resize(0);
	    }

	  FOR_EACH_INSN_USE (use, insn)
	    {
	      unsigned int regno = DF_REF_REGNO (use);

	      struct hl_load_use hl_use;
	      hl_use.insn = insn;
	      hl_use.insn_count = insn_count;
	      all_uses[regno].push_back(hl_use);
	    }

	  insn_count++;
	}

      for (auto &uses : all_uses)
	{
	  uses.resize(0);
	}
    }
}

static bool
get_mem_reg_and_offset(rtx pat, int *reg, int *offset)
{
  if (GET_CODE(pat) == ZERO_EXTEND ||
      GET_CODE(pat) == SIGN_EXTEND)
    {
      pat = XEXP(pat, 0);
    }
  if (GET_CODE(pat) == ASM_OPERANDS)
    {
      return false;
    }
  gcc_assert(MEM_P(pat));

  if (REG_P(XEXP(pat, 0)))
    {
      *reg = REGNO(XEXP(pat, 0));;
      *offset = 0;
    }
  else if (GET_CODE(XEXP(pat, 0)) != PLUS)
    {
      return false;
    }
  else
    {
      gcc_assert(GET_CODE(XEXP(pat, 0)) == PLUS &&
		 REG_P(XEXP(XEXP(pat, 0), 0)) &&
		 CONST_INT_P((XEXP(XEXP(pat, 0), 1))));
      *reg = REGNO(XEXP(XEXP(pat, 0), 0));
      *offset = INTVAL(XEXP(XEXP(pat, 0), 1));
    }

  return true;
}

static bool
can_move_past_load_or_store(rtx base, rtx pat)
{
  if (load_mem_p(base))
    {
      if (nonstack_store_p(pat))
	{
	  int store_reg, store_offset;
	  int load_reg, load_offset;
	  if (!get_mem_reg_and_offset(SET_DEST(pat), &store_reg, &store_offset) ||
	      !get_mem_reg_and_offset(SET_SRC(base), &load_reg, &load_offset))
	    {
	      return false;
	    }

	  // Conservative, could check the size of the operation
	  // Also, relies on aligned accesses
	  if (store_reg == load_reg &&
	      store_offset < load_offset + 4 && store_offset >= load_offset)
	    {
	      return false;
	    }

	  return rvtt_store_has_restrict_p(pat) && !volatile_store_p(pat);
	}

      return !volatile_load_p(pat);
    }

  if (nonstack_store_p(base) && nonstack_store_p(pat))
    {
      return rvtt_store_has_restrict_p(base) || rvtt_store_has_restrict_p(pat);
    }

  return true;
}

static bool
can_move_past(rtx_insn *base, rtx_insn *query)
{
  df_ref base_def, query_def;

  rtx pat = PATTERN(query);
  int classify = my_classify_insn(pat);
  if (classify != INSN ||
      GET_CODE(pat) == UNSPEC_VOLATILE ||
      GET_CODE(pat) == ASM_INPUT ||
      GET_CODE(pat) == ASM_OPERANDS)
    {
      return false;
    }

  if (GET_CODE(pat) == PARALLEL)
    {
      for (int i = XVECLEN (pat, 0) - 1; i >= 0; i--)
	{
	  rtx sub = XVECEXP(pat, 0, i);

	  if (!can_move_past_load_or_store(PATTERN(base), sub) ||
	      !can_move_past_load_or_store(sub, PATTERN(base)))
	    {
	      return false;
	    }
	}
    }
  else if (!can_move_past_load_or_store(PATTERN(base), pat) ||
	   !can_move_past_load_or_store(pat, PATTERN(base)))
    {
      return false;
    }

  FOR_EACH_INSN_DEF (query_def, query)
    {
      // If query defines what base defines
      FOR_EACH_INSN_DEF (base_def, base)
	{
	  if (DF_REF_REGNO(base_def) == DF_REF_REGNO(query_def))
	    {
	      return false;
	    }
	}

      // If query defines what base uses
      df_ref use;
      FOR_EACH_INSN_USE (use, base)
	{
	  if (DF_REF_REGNO(use) == DF_REF_REGNO(query_def))
	    {
	      return false;
	    }
	}
    }

  // If base defines what query uses
  FOR_EACH_INSN_DEF (base_def, base)
    {
      FOR_EACH_INSN_USE (query_def, query)
	{
	  if (DF_REF_REGNO(base_def) == DF_REF_REGNO(query_def))
	    {
	      return false;
	    }
	}
    }

  return true;
}

static void
move_insn(rtx_insn *insn, rtx_insn *where, bool after)
{
  remove_insn(insn);
  SET_PREV_INSN(insn) = NULL_RTX;
  SET_NEXT_INSN(insn) = NULL_RTX;

  if (after)
    {
      emit_insn_after(insn, where);
    }
  else
    {
      emit_insn_before(insn, where);
    }
}

// "Schedule" high latency loads
// This is a dirt simple attempt to improve the scheduling of insns dependent
// on hlls.  The code first pushes hlls "up" as high as possible in the BB,
// then pushes their dependencies (uses) "down" as low as possible in the BB.
// No effort is made to check interdependencies between these hlls or uses or
// to check a chain of dependencies and move the chain down.  At least, not
// yet...
static void
schedule_hll(function *fn)
{
  DUMP("TT riscv GS rtl hll schedule pass on: %s\n", function_name(fn));

  vector<hl_load> hl_loads;
  df_set_flags (DF_LR_RUN_DCE);
  df_note_add_problem ();
  df_analyze();
  create_log_links(fn, hl_loads);

  for (int i = hl_loads.size() - 1; i >= 0; i--)
    {
      hl_load &hll = hl_loads[i];
      DUMP("  processing hll load\n");

      // Move load up
      rtx_insn *insn = PREV_INSN(hll.insn);
      // Not sure why BLOCK_FOR_INSN(insn) would be null here
      rtx_insn *anchor = nullptr;

      while (insn && insn != PREV_INSN(BB_HEAD(hll.bb)))
	{
	  if (NONDEBUG_INSN_P(insn))
	    {
	      if (!can_move_past(hll.insn, insn))
		{
		  break;
		}
	      anchor = insn;
	    }
	  insn = PREV_INSN(insn);
	}

      if (anchor)
	{
	  DUMP("  moving load up\n");
	  move_insn(hll.insn, anchor, false);
	}
    }

  for (int i = hl_loads.size() - 1; i >= 0; i--)
    {
      hl_load &hll = hl_loads[i];
      DUMP("  processing hll load with %zu uses\n", hll.uses.size());

      // Move uses down.  First use should be lowest
      rtx_insn *insn = PREV_INSN(hll.insn);
      // Not sure why BLOCK_FOR_INSN(insn) would be null here
      rtx_insn *anchor = nullptr;
      for (auto &use : hll.uses)
	{
	  anchor = nullptr;
	  if (my_classify_insn(PATTERN(use.insn)) == INSN)
	    {
	      insn = NEXT_INSN(use.insn);
	      while (insn && insn != NEXT_INSN(BB_END(hll.bb)))
		{
		  if (NONDEBUG_INSN_P(insn))
		    {
		      if (!can_move_past(use.insn, insn))
			{
			  break;
			}
		      anchor = insn;
		    }
		  insn = NEXT_INSN(insn);
		}

	      if (anchor)
		{
		  DUMP("  moving use down\n");
		  move_insn(use.insn, anchor, true);
		}
	    }
	}
    }
}

#ifdef DUMP_ANALYSIS
static void
anaylze_results(function *fn, vector<int> &hist, int& n_hlls)
{
  vector<hl_load> hl_loads;
  df_set_flags (DF_LR_RUN_DCE);
  df_note_add_problem ();
  df_analyze();
  create_log_links(fn, hl_loads);

  for (int i = hl_loads.size() - 1; i >= 0; i--)
    {
      hl_load &hll = hl_loads[i];

      int max = -1;
      for (auto &use : hll.uses)
	{
	  max = (use.insn_count > max) ? use.insn_count : max;
	}

      if (max != -1)
	{
	  int cycles = hll.insn_count - max;
	  if (cycles >= (int)hist.size())
	    {
	      hist.resize(cycles + 1);
	    }
	  hist[cycles]++;
	}
    }

  n_hlls += hl_loads.size();
}

static void
print_results(function *fn, vector<int>& hist, int n_hlls)
{
  if (hist.size() != 0)
    {
      fprintf(stderr, "\nCumulative load to use after %s\n", function_name(fn));
    }
  int total = 0;
  int cycles = 0;
  for (int i = 0; i < (int)hist.size(); i++)
    {
      total += hist[i];
      cycles += i * hist[i];
      fprintf(stderr, "%2d: %d\n", i, hist[i]);
    }
  int median = 0;
  int count = 0;
  for (int i = 0; i < (int)hist.size(); i++)
    {
      count += hist[i];
      if (count > total / 2)
	{
	  median = i;
	  break;
	}
    }

  if (hist.size() != 0)
    {
      fprintf(stderr, "Total uses: %d hlls %d\n", total, n_hlls);
      fprintf(stderr, "Average: %f\n", (float)cycles / (float)total);
      fprintf(stderr, "Median: %d\n", median);
    }
}
#endif

namespace {

const pass_data pass_data_rvtt_hll =
{
  RTL_PASS, /* type */
  "rvtt_hll", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  TODO_df_finish, /* todo_flags_finish */
};

class pass_rvtt_hll : public rtl_opt_pass
{
private:
#ifdef DUMP_ANALYSIS
  int n_hlls;
  vector<int>hll_load_to_use_hist;
#endif

public:
  pass_rvtt_hll (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_hll, ctxt)
  {
#ifdef DUMP_ANALYSIS
    n_hlls = 0;
#endif
  }

  /* opt_pass methods: */
  virtual unsigned int execute (function *cfn)
    {
      if (flag_rvtt_hll && optimize > 0)
	{
	  schedule_hll(cfn);
#ifdef DUMP_ANALYSIS
	  anaylze_results(cfn, hll_load_to_use_hist, n_hlls);
	  print_results(cfn, hll_load_to_use_hist, n_hlls);
#endif
	}

      if (flag_grayskull && flag_rvtt_gshllwar)
	{
	  workaround_gs_hll(cfn);
	}
      return 0;
    }
}; // class pass_rvtt_hll

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_hll (gcc::context *ctxt)
{
  return new pass_rvtt_hll (ctxt);
}
