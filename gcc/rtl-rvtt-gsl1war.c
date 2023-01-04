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

// A BB can resolve its parents' L1 loads by:
//  1) reading the incoming_l1_war_reg
//  2) issuing an L1 load (updates l1_war_reg) and reading the result
//
// #2 is a stronger result as any parent BB with an ll_count low enough can be
// resolved. If the BB reads the incoming_l1_war_reg, then other parents must
// depend on the same reg or resolve the L1 load themselves
// The present implementation  doesn't try to get correct results for both #1
// and #2, instead it tracks the first workaround hit and classifies it as one
// or the other.  Future work if valuable...
struct bb_entry {
  bool inited;                      // true once populated w/ incoming data
  bool visited;                     // true once processed
  int ll_incoming;                  // #ll for first parent BB to get to this BB
  int l1_war_reg_incoming;          // reg passed in that can resolve the parent (-1 if NA)
  int ll_before_resolve_incoming;   // #ll before l1_war_reg is read (-1 if NA)
  int ll_before_resolve_all;        // #ll before all WARs are resolved (-1 if NA)
};

using namespace std;

typedef vector<bb_entry> bb_data;


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
update_war_bookkeeping(bool *first_war,
		       bool resolves_all,
		       bb_entry *bbe,
		       int l1_war_reg,
		       int *ll_count)
{
  if (*first_war)
    {
      if (resolves_all)
	{
	  bbe->ll_before_resolve_all = *ll_count - bbe->ll_incoming;
	}
      else
	{
	  gcc_assert(bbe->l1_war_reg_incoming == l1_war_reg);
	  bbe->ll_before_resolve_incoming = *ll_count - bbe->ll_incoming;
	}
      *first_war = false;
    }

  *ll_count = -1;
}

static void
emit_war(bool *first_war,
	 bool resolves_all,
	 rtx_insn *where,
	 int l1_war_reg,
	 bb_entry *bbe,
	 int *ll_count)
{
  DUMP("    emitting WAR with reg %d\n", l1_war_reg);

  update_war_bookkeeping(first_war, resolves_all, bbe, l1_war_reg, ll_count);

  rtx reg = gen_rtx_REG(SImode, l1_war_reg);
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

// The general idea here is to go through the insns and once an L1 load is
// seen, starting counting local memory loads before enough have been hit, then
// emit the workaround.  Details:
//  - delete manually inserted workaround intrinsics that are not needed
//  - delete uneeded workarounds (local_store_usi followed by local_load_usi)
//  - reset the count if the target register of an l1 load is referenced as an
//    input operand.  Note that we only track the most recent l1 load.  In
//    theory, we could track all of them and reduce the count if an earlier l1
//    load lands, in practice this is unlikely to occur since the latency of
//    l1 loads is so high that the compiler hoists them all together so
//    tracking the most recent is sufficient.  Revisit if needed
//  - reset the count if a manual workaround is encountered
//  - abort on certain instructions (eg, inline assembly)
//
// In addition, BBs are traversed and the count is carried into the next BB.
// If a BB is hit multiple times, all entires after the first must have a
// count less than or equal to the count the first time the BB was
// encountered.
static void
process_bb(basic_block bb, bb_data& bbd)
{
  bb_entry& bbe = bbd[bb->index];
  bbe.visited = true;

  bool first_war = true;
  bool resolves_all = false;
  int l1_war_reg = bbe.l1_war_reg_incoming; // Could be -1
  int ll_count = bbe.ll_incoming;

  rtx_insn *insn = BB_HEAD(bb);
  FOR_BB_INSNS(bb, insn)
    {
      if (GET_CODE(insn) == NOTE &&
	  NOTE_KIND(insn) == NOTE_INSN_EPILOGUE_BEG &&
	  ll_count != -1)
	{
	  DUMP("  epilogue beginning with l1 active, emitting WAR\n");
	  // Note: could return here, no harm in continuing since war is done
	  emit_war(&first_war, resolves_all, insn, l1_war_reg, &bbe, &ll_count);
	}
      else if (NONDEBUG_INSN_P(insn))
	{
	  int code = recog_memoized(insn);
	  rtx insn_pat = PATTERN(insn);
	  const rvtt_insn_data *insnd;
	  rvtt_p (&insnd, insn);
	  if (ll_count == -1)
	    {
	      if (insnd->l1_load_p())
		{
		  ll_count = 0;
		  resolves_all = true;
		  l1_war_reg = rvtt_get_insn_dst_regno(insn);
		  DUMP("  found an %s loading reg %d, war pending\n", insnd->name, l1_war_reg);
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
	      if (insnd->l1_load_p())
		{
		  resolves_all = true;
		  l1_war_reg = rvtt_get_insn_dst_regno(insn);
		  DUMP("  found an %s loading reg %d, updating war reg\n", insnd->name, l1_war_reg);
		}
	      else if (code != -1 &&
		       classify == INSN &&
		       refers_to_regno_p(l1_war_reg, l1_war_reg + 1, insn, nullptr))
		{
		  DUMP("  a %s refers to l1 loaded reg %d, resetting ll_count\n",
		       insn_data[code].name, l1_war_reg);
		  update_war_bookkeeping(&first_war, resolves_all, &bbe, l1_war_reg, &ll_count);
		}
	      else if (insnd->id == rvtt_insn_data::l1_load_war)
		{
		  int reg = REGNO(rvtt_get_insn_operand(0, insn));
		  if (reg != l1_war_reg)
		    {
		      DUMP("  found a %s for reg %d mismatching last l1_war_reg %d\n", insnd->name, reg, l1_war_reg);
		      // XXXX, what's the best warning code for this?
		      warning(OPT_Wunused, "__builtin_rvtt_gs_%s found using out of date L1 loaded register, deleted\n", insnd->name);
		      set_insn_deleted(insn);
		    }
		  else
		    {
		      DUMP("  found a %s for reg %d, resetting ll_count\n", insnd->name, reg);
		      update_war_bookkeeping(&first_war, resolves_all, &bbe, l1_war_reg, &ll_count);
		    }
		}
	      else if (classify != INSN && classify != JUMP_INSN)
		{
		  DUMP("  %s with l1 active, emitting WAR\n", GET_RTX_NAME(classify));
		  emit_war(&first_war, resolves_all, insn, l1_war_reg, &bbe, &ll_count);
		}
	      else if (GET_CODE(insn_pat) == SET &&
		       insnd->id != rvtt_insn_data::l1_load_si)
		{
		  rtx src = SET_SRC(insn_pat);
		  if (contains_mem_rtx_p(src))
		    {
		      ll_count++;
		      DUMP("  local load w/ l1 active, bumped ll_count to %d\n", ll_count);
		      if (ll_count == 5)
			{
			  DUMP("  local load is 5, emitting WAR\n");
			  emit_war(&first_war, resolves_all, insn, l1_war_reg, &bbe, &ll_count);
			}
		    }
		}
	      else if (GET_CODE(insn_pat) == PARALLEL)
		{
		  for (int i = XVECLEN (insn_pat, 0) - 1; i >= 0; i--)
		    {
		      rtx sub = XVECEXP(insn_pat, 0, i);
		      DUMP("  processing parallel %s\n", GET_RTX_NAME(GET_CODE(sub)));

		      if (contains_mem_rtx_p(sub))
			{
			  ll_count++;
			  DUMP("    load in parallel w/ l1 active, ll_bumped count to %d\n", ll_count);
			  if (ll_count == 5)
			    {
			      DUMP("  local load is 5 with parallel, emitting WAR\n");
			      emit_war(&first_war, resolves_all, insn, l1_war_reg, &bbe, &ll_count);
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

	  DUMP("  checking BB[%d] w/ inited %d ll_count %d l1_reg %d dst.ll %d dst.ll_all %d dst.ll_inc %d dst.l1_reg %d\n",
	       e->dest->index,
	       dst_bbe.inited,
	       ll_count,
	       l1_war_reg,
	       dst_bbe.ll_incoming,
	       dst_bbe.ll_before_resolve_all,
	       dst_bbe.ll_before_resolve_incoming,
	       dst_bbe.l1_war_reg_incoming);

	  if (dst_bbe.inited &&
	      ((dst_bbe.ll_before_resolve_all != -1 && dst_bbe.ll_before_resolve_all + ll_count > 4) ||
	       (dst_bbe.ll_before_resolve_incoming + ll_count > 4 || dst_bbe.l1_war_reg_incoming != l1_war_reg) ||
	       (dst_bbe.ll_before_resolve_all == -1 &&
		dst_bbe.ll_before_resolve_incoming == -1 &&
		ll_count > dst_bbe.ll_incoming)))
	    {
	      children_can_handle = false;
	      DUMP("    checked BB[%d], emitting WAR\n", e->dest->index);
	      emit_war(&first_war, resolves_all, bb_insn_before_jump(bb), l1_war_reg, &bbe, &ll_count);
	      break;
	    }
	  else
	    {
	      DUMP("    checked BB[%d], all ok\n", e->dest->index);
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
		  dst_bbe.ll_incoming = ll_count;
		  dst_bbe.l1_war_reg_incoming = l1_war_reg;
		}
	    }
	}
    }

  FOR_EACH_EDGE(e, ei, bb->succs)
    {
      if (!bbd[e->dest->index].visited)
	{
	  DUMP("  going from bb %d to bb %d\n", bb->index, e->dest->index);
	  process_bb(e->dest, bbd);
	}
    }
}

static void
transform(function *fn)
{
  DUMP("TT riscv GS rtl l1 war pass on: %s\n", function_name(fn));

  bb_data bbd;
  bbd.resize(n_basic_blocks_for_fn(fn));
  for (auto& bbe : bbd)
    {
      bbe.inited = false;
      bbe.visited = false;
      bbe.ll_incoming = -1;
      bbe.l1_war_reg_incoming = -1;
      bbe.ll_before_resolve_incoming = -1;
      bbe.ll_before_resolve_all = -1;
    }

  process_bb(ENTRY_BLOCK_PTR_FOR_FN(fn), bbd);
}

namespace {

const pass_data pass_data_rvtt_gsl1war =
{
  RTL_PASS, /* type */
  "rvtt_gsl1war", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_gsl1war : public rtl_opt_pass
{
public:
  pass_rvtt_gsl1war (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_gsl1war, ctxt)
  {
  }

  /* opt_pass methods: */
  virtual unsigned int execute (function *cfn)
    {
      if (flag_grayskull)
	{
	  transform (cfn);
	}
      return 0;
    }
}; // class pass_rvtt_gsl1war

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_gsl1war (gcc::context *ctxt)
{
  return new pass_rvtt_gsl1war (ctxt);
}
