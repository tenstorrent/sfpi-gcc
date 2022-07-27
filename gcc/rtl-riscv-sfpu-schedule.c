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
#include "config/riscv/sfpu.h"
#include "config/riscv/sfpu-protos.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static void insert_nop(rtx_insn *insn)
{
  emit_insn_after(gen_riscv_wh_sfpnop(), insn);
}

static bool reg_referenced_p(rtx reg, rtx_insn *insn)
{
  int noperands;
  rtx operands = riscv_sfpu_get_insn_operands(&noperands, insn);

  unsigned int regno = riscv_sfpu_regno(reg);
  for (int i = 0; i < noperands; i++) {
    rtx operand = XVECEXP(operands, 0, i);
    if (GET_CODE(operand) == REG &&
	regno == riscv_sfpu_regno(operand)) {
      return true;
    }
  }

  return false;
}

// Perform instruction scheduling.  For wormhole this means adding a NOP or
// moving a non-dependent instruction into the single instruction shadow of
// any instruction which uses the MAD unit, which are:	MAD, LUT, LUT32,
// MUL(I), ADD(I).  Note that SWAP/SHFT2 always require a NOP and that is
// emitted along with the instruction and not handled here.
static void transform (function *fn)
{
  DUMP("Schedule pass on: %s\n", function_name(fn));

  bool update = false;
  basic_block bb;
  // Processes the nonimm instructions
  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
       {
	 const riscv_sfpu_insn_data *insnd;

	 int len;
	 if (NONDEBUG_INSN_P(insn) &&
	     riscv_sfpu_p(&insnd, insn) &&
	     ((insnd->schedule & INSN_SCHED_DYN) ||
	      ((insnd->schedule & INSN_SCHED_IN_ARG) &&
	       INTVAL(XVECEXP(riscv_sfpu_get_insn_operands(&len, insn), 0, insnd->schedule & INSN_SCHED_ARG_MASK)) != 0)))
	   {
	     DUMP("  scheduling %s\n", insnd->name);

	     rtx_insn *next_insn;
	     const riscv_sfpu_insn_data *next_insnd;
	     if (riscv_sfpu_get_next_sfpu_insn(&next_insnd, &next_insn, insn))
	       {
		 rtx pat = PATTERN(insn);
		 if (GET_CODE (pat) == PARALLEL)
		   {
		     pat = XVECEXP(pat, 0, 0);
		   }
		 gcc_assert(GET_CODE (pat) == SET);
		 rtx reg = XEXP(pat, 0);

		 DUMP("  next insn, reg: %s %d\n", next_insnd->name, REGNO(reg) - SFPU_REG_FIRST);
		 if (reg_referenced_p(reg, next_insn))
		   {
		     DUMP("	next stmt (%s) uses lhs, inserting NOP\n", next_insnd->name);
		     insert_nop(insn);
		     update = true;
		   }
		 else
		   {
		     DUMP("	next stmt (%s) is independent, all good\n", next_insnd->name);
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
	}
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_schedule =
{
  RTL_PASS, /* type */
  "riscv_sfpu_schedule", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_schedule : public rtl_opt_pass
{
public:
  pass_riscv_sfpu_schedule (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_riscv_sfpu_schedule, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_schedule

} // anon namespace

/* Entry point to riscv_sfpu_schedule pass.	*/
unsigned int
pass_riscv_sfpu_schedule::execute (function *fun)
{
  if (flag_wormhole)
    {
      transform (fun);
    }

  return 0;
}

rtl_opt_pass *
make_pass_riscv_sfpu_schedule (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_schedule (ctxt);
}
