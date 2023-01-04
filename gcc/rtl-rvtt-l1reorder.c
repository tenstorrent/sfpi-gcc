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

static const int stack_pointer_reg = 2;

static void
replace_l1_load_from_stack_with_load(const rvtt_insn_data *insnd, rtx_insn *insn)
{
  DUMP("  found a %s reading from stack, replacing with load\n", insnd->name);

  rtx pat = PATTERN(insn);
  gcc_assert(GET_CODE(pat) == SET);

  rtx new_insn;
  if (insnd->id == rvtt_insn_data::l1_load_qi ||
      insnd->id == rvtt_insn_data::l1_load_uqi ||
      insnd->id == rvtt_insn_data::l1_load_hi ||
      insnd->id == rvtt_insn_data::l1_load_uhi)
    {
      // When loading back from the stack, load in SI mode
      rtx new_op = XVECEXP(XEXP(pat, 1), 0, 0);
      PUT_MODE_RAW(new_op, SImode);
      new_insn = gen_rtx_SET(SET_DEST(pat), new_op);
    }
  else
    {
      new_insn = gen_rtx_SET(SET_DEST(pat), XVECEXP(XEXP(pat, 1), 0, 0));
    }
  emit_insn_after(new_insn, insn);
  recog_memoized(NEXT_INSN(insn));
  set_insn_deleted(insn);
}

static void
replace_load_from_l1_with_l1_load(const rvtt_insn_data *insnd, rtx_insn *insn)
{
  rtx pat = PATTERN(insn);
  rtx new_insn;
  switch (insnd->id)
    {
    case rvtt_insn_data::l1_load_qi:
      new_insn = gen_rvtt_l1_load_qi(SET_DEST(pat), XEXP(SET_SRC(pat), 0));
      break;
    case rvtt_insn_data::l1_load_uqi:
      new_insn = gen_rvtt_l1_load_uqi(SET_DEST(pat), XEXP(SET_SRC(pat), 0));
      break;
    case rvtt_insn_data::l1_load_hi:
      new_insn = gen_rvtt_l1_load_hi(SET_DEST(pat), XEXP(SET_SRC(pat), 0));
      break;
    case rvtt_insn_data::l1_load_uhi:
      new_insn = gen_rvtt_l1_load_uhi(SET_DEST(pat), XEXP(SET_SRC(pat), 0));
      break;
    case rvtt_insn_data::l1_load_si:
      new_insn = gen_rvtt_l1_load_si(SET_DEST(pat), SET_SRC(pat));
      break;
    case rvtt_insn_data::l1_load_usi:
      new_insn = gen_rvtt_l1_load_usi(SET_DEST(pat), SET_SRC(pat));
      break;
    case rvtt_insn_data::l1_load_ptr:
      new_insn = gen_rvtt_l1_load_usi(SET_DEST(pat), SET_SRC(pat));
      break;
    default:
      gcc_unreachable();
    }

  emit_insn_after(new_insn, insn);
  recog_memoized(NEXT_INSN(insn));
  set_insn_deleted(insn);
}

static unsigned int
find_store_to_stack(rtx_insn **insn)
{
  rtx pat = PATTERN(*insn);
  gcc_assert(GET_CODE(pat) == SET);
  rtx mem = XVECEXP(XEXP(pat, 1), 0, 0);
  gcc_assert(GET_CODE(mem) == MEM);
  rtx plus = XEXP(mem, 0);
  gcc_assert(GET_CODE(plus) == PLUS);
  rtx reg = XEXP(plus, 0);
  rtx offset = XEXP(plus, 1);
  int offset_value = INTVAL(offset);
  gcc_assert(GET_CODE(reg) == REG);
  gcc_assert(GET_CODE(offset) == CONST_INT);
  int sp = REGNO(reg);
  gcc_assert(sp == stack_pointer_reg);

  basic_block bb = BLOCK_FOR_INSN(*insn);

  while (*insn != PREV_INSN(BB_HEAD(bb)))
    {
      if (NONDEBUG_INSN_P(*insn))
	{
	  rtx pat = PATTERN(*insn);
	  if (GET_CODE(pat) == SET)
	    {
	      rtx dst = SET_DEST(pat);
	      rtx src = SET_SRC(pat);

	      if (GET_CODE(dst) == MEM &&
		  GET_CODE(src) == REG)
		{
		  rtx plus2 = XEXP(dst, 0);

		  if (GET_CODE(plus2) == PLUS)
		    {
		      rtx reg2 = XEXP(plus2, 0);
		      rtx offset2 = XEXP(plus2, 1);

		      if (GET_CODE(reg2) == REG &&
			  GET_CODE(offset2) == CONST_INT &&
			  REGNO(reg2) == stack_pointer_reg &&
			  INTVAL(offset2) == offset_value)
			{
			  DUMP("  found store to stack w/ reg %d offset %d\n", REGNO(src), offset_value);
			  return REGNO(src);
			}
		    }
		}
	    }
	}

      *insn = PREV_INSN(*insn);
    }

  // Reaching here means the store to the stack was not found.  There are 2
  // ways for this to happen:
  // 1) The program contains an explicit intrinsic load from the stack.  This
  //    is an error.
  // 2) The compiler generated store to the stack is in a different BB than
  //    the compiler generated intrinsic load from the stack.  This isn't
  //    handled and may not be possible (fingers crossed).  If it is, then each
  //    path into this BB needs this store, ugh.

  return 0xFFFFFFFF;
}

static bool
find_and_replace_load_with_l1_load(vector<bool>& visited,
				   const rvtt_insn_data *insnd,
				   rtx_insn *insn, unsigned int reg)
{
  basic_block bb = BLOCK_FOR_INSN(insn);

  // Can the first BB visited loop back on itself and set the reg later in the
  // BB than the initial insn?  Seems yes, so don't clear visited for the first
  // block this is called on unless we covered the whole block
  if (insn != BB_END(bb))
    {
      visited[bb->index] = true;
    }

  while (insn != PREV_INSN(BB_HEAD(bb)))
    {
      if (NONDEBUG_INSN_P(insn))
	{
	  rtx pat = PATTERN(insn);
	  if (GET_CODE(pat) == SET &&
	      GET_CODE(SET_DEST(pat)) == REG &&
	      REGNO(SET_DEST(pat)) == reg)
	    {
	      const rvtt_insn_data *insnd_tmp;

	      if (rvtt_p(&insnd_tmp, insn))
		{
		  DUMP("  found load of reg %d, already replaced, moving on\n", reg);
		}
	      else
		{
		  DUMP("  found load of reg %d, replacing with L1_LOAD\n", reg);
		  replace_load_from_l1_with_l1_load(insnd, insn);
		}

	      return true;
	    }
	}

      insn = PREV_INSN(insn);
    }

  edge_iterator ei;
  edge e;
  int num_edges = 0;
  bool all_paths_found = true;
  FOR_EACH_EDGE(e, ei, bb->preds)
    {
      num_edges++;
      if (!visited[e->src->index])
	{
	  all_paths_found &= find_and_replace_load_with_l1_load(visited, insnd, BB_END(e->src), reg);
	}
    }

  return num_edges == 0 ? false : all_paths_found;
}

// Unfortunately, gcc can, in some instances, see an L1_LOAD intrinsic loading
// from L1 and replace it with its own load from L1, store to the stack, then
// use the intrinsic to load from the stack, thus defeating the whole point of
// using the intrinsic (minor latency perf issue on WH but correctness issue on
// GS).
//
// This pass detects the L1 load occuring from the stack, replaces it with a
// "normal" load and then looks for the load that read from L1 and replaces
// that with the intrinsic.
static void
transform(function *fn)
{
  DUMP("GS L1 reorder pass on %s\n", function_name(fn));

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;

      FOR_BB_INSNS(bb, insn)
	{
	  if (NONDEBUG_INSN_P(insn))
	    {
	      const rvtt_insn_data *insnd;

	      if (rvtt_p(&insnd, insn) &&
		  insnd->l1_load_p() &&
		  refers_to_regno_p(stack_pointer_reg, stack_pointer_reg + 1, insn, nullptr))
		{
		  vector<bool> visited;
		  visited.resize(n_basic_blocks_for_fn(fn));
		  rtx_insn *store_insn = insn;
		  unsigned int reg = find_store_to_stack(&store_insn);

		  if (reg == 0xFFFFFFFF)
		    {
		      DUMP("  did not find store to stack, not replacing load\n");
		      expanded_location exploc = insn_location(insn);
		      // XXXXX expanded location to location?????
		      char msg[500];
		      sprintf(msg, "%s:%d: ERROR: found __builtin_rvtt_%s loading from stack\n",
			      exploc.file, exploc.line, insnd->name);
		      error(msg);
		    }
		  else
		    {
		      replace_l1_load_from_stack_with_load(insnd, insn);

		      bool all_found = find_and_replace_load_with_l1_load(visited, insnd, store_insn, reg);
		      gcc_assert(all_found);
		    }
		}
	    }
	}
    }
}

namespace {

const pass_data pass_data_rvtt_l1reorder =
{
  RTL_PASS, /* type */
  "rvtt_l1reorder", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_l1reorder : public rtl_opt_pass
{
public:
  pass_rvtt_l1reorder (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_l1reorder, ctxt)
  {
  }

  /* opt_pass methods: */
  virtual unsigned int execute (function *cfn)
    {
      if (flag_grayskull || flag_wormhole)
	{
	  transform (cfn);
	}
      return 0;
    }
}; // class pass_rvtt_l1reorder

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_l1reorder (gcc::context *ctxt)
{
  return new pass_rvtt_l1reorder (ctxt);
}
