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
#include "config/riscv/rvtt.h"
#include "config/riscv/rvtt-protos.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static void insert_nop(rtx_insn *insn)
{
  emit_insn_after(gen_rvtt_wh_sfpnop(), insn);
}

static bool reg_referenced_p(unsigned int regno, rtx_insn *insn)
{
  int noperands = rvtt_get_insn_operand_count(insn);

  for (int i = 0; i < noperands; i++) {
    rtx operand = rvtt_get_insn_operand(i, insn);
    if (GET_CODE(operand) == REG &&
	regno == rvtt_sfpu_regno(operand)) {
      return true;
    }
  }

  return false;
}

// Perform instruction scheduling
//
// For wormhole there is dynamic and static schedule.  Dynamic scheduling
// requires adding a NOP or moving a non-dependent instruction into the single
// instruction shadow of ny instruction which uses the MAD unit, which are:
// MAD, LUT, LUT32, MUL(I), ADD(I).  Presently, this only inserts a NOP.
//
// SWAP/SHFT2 are statically scheduled and always require a NOP.
static void transform ()
{
  DUMP("Schedule pass on: %s\n", function_name(cfun));

  bool update = false;
  basic_block bb;

  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  const rvtt_insn_data *insnd;

	  int len;
	  if (NONDEBUG_INSN_P(insn) &&
	      rvtt_p(&insnd, insn))
	    {
	      if (insnd->schedule_p() &&
		  (!insnd->schedule_in_arg_p() ||
		   insnd->schedule_from_arg_p(insn)))
		{
		  if (insnd->schedule_dynamic_p(insn))
		    {
		      DUMP("  dynamic scheduling %s\n", insnd->name);
		      rtx_insn *next_insn;
		      const rvtt_insn_data *next_insnd;
		      if (rvtt_get_next_sfpu_insn(&next_insnd, &next_insn, insn))
			{
			  int regint = rvtt_get_insn_dst_regno(insn) - SFPU_REG_FIRST;
			  gcc_assert(regint != -1 - SFPU_REG_FIRST);
			  unsigned int regno = regint;

			  DUMP("  next insn, reg: %s %d\n", next_insnd->name, regno);

			  if (reg_referenced_p(regno, next_insn))
			    {
			      DUMP("     next stmt (%s) uses lhs, inserting NOP\n", next_insnd->name);
			      insert_nop(insn);
			      update = true;
			    }
			  else
			    {
			      DUMP("     next stmt (%s) is independent, all good\n", next_insnd->name);
			    }
			}
		      else
			{
			  // The stmt needing scheduling is the last stmt in the BB
			  // For now, just add a NOP.  XXXX Could chase down all the next
			  // BBs and possibly do better
			  DUMP(" last stmt in BB, inserting NOP\n");
			  insert_nop(insn);
			}
		    }
		  else
		    {
		      DUMP("  static scheduling %s\n", insnd->name);
		      int count = insnd->schedule_static_nops(insn);
		      for (int i = 0; i < count; i++)
			{
			  insert_nop(insn);
			}
		    }
		}
	    }
       }
    }
}

namespace {

const pass_data pass_data_rvtt_schedule =
{
  RTL_PASS, /* type */
  "rvtt_schedule", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_schedule : public rtl_opt_pass
{
public:
  pass_rvtt_schedule (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_schedule, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_rvtt_schedule

} // anon namespace

/* Entry point to rvtt_schedule pass.	*/
unsigned int
pass_rvtt_schedule::execute (function *)
{
  if (flag_wormhole)
    {
      transform ();
    }

  return 0;
}

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}
