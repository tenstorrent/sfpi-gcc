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
#include <unordered_map>
#include "config/riscv/rvtt.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

// The insn_link/log links code below was shamelessly copied from combine.c

struct insn_link {
  rtx_insn *insn;
  unsigned int regno;
  struct insn_link *next;
};

static struct insn_link **uid_log_links;
static int max_uid_known;

static inline int
insn_uid_check (const_rtx insn)
{
  int uid = INSN_UID (insn);
  gcc_checking_assert (uid <= max_uid_known);
  return uid;
}

#define LOG_LINKS(INSN)		(uid_log_links[insn_uid_check (INSN)])
#define FOR_EACH_LOG_LINK(L, INSN)				\
  for ((L) = LOG_LINKS (INSN); (L); (L) = (L)->next)
/* Links for LOG_LINKS are allocated from this obstack.	 */

static struct obstack insn_link_obstack;

/* Allocate a link.  */

static inline struct insn_link *
alloc_insn_link (rtx_insn *insn, unsigned int regno, struct insn_link *next)
{
  struct insn_link *l
    = (struct insn_link *) obstack_alloc (&insn_link_obstack,
					  sizeof (struct insn_link));
  l->insn = insn;
  l->regno = regno;
  l->next = next;
  return l;
}

/* Return false if we do not want to (or cannot) combine DEF.  */
static bool
can_combine_def_p (df_ref def)
{
  /* Do not consider if it is pre/post modification in MEM.  */
  if (DF_REF_FLAGS (def) & DF_REF_PRE_POST_MODIFY)
    return false;

  unsigned int regno = DF_REF_REGNO (def);

  /* Do not combine frame pointer adjustments.	*/
  if ((regno == FRAME_POINTER_REGNUM
       && (!reload_completed || frame_pointer_needed))
      || (!HARD_FRAME_POINTER_IS_FRAME_POINTER
	  && regno == HARD_FRAME_POINTER_REGNUM
	  && (!reload_completed || frame_pointer_needed))
      || (FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	  && regno == ARG_POINTER_REGNUM && fixed_regs[regno]))
    return false;

  return true;
}

/* Return false if we do not want to (or cannot) combine USE.  */
static bool
can_combine_use_p (df_ref use)
{
  /* Do not consider the usage of the stack pointer by function call.  */
  if (DF_REF_FLAGS (use) & DF_REF_CALL_STACK_USAGE)
    return false;

  return true;
}

/* Fill in log links field for all insns.  */

static void
create_log_links (void)
{
  basic_block bb;
  rtx_insn **next_use;
  rtx_insn *insn;
  df_ref def, use;

  next_use = XCNEWVEC (rtx_insn *, max_reg_num ());

  /* Pass through each block from the end, recording the uses of each
     register and establishing log links when def is encountered.
     Note that we do not clear next_use array in order to save time,
     so we have to test whether the use is in the same basic block as def.

     There are a few cases below when we do not consider the definition or
     usage -- these are taken from original flow.c did. Don't ask me why it is
     done this way; I don't know and if it works, I don't want to know.	 */

  FOR_EACH_BB_FN (bb, cfun)
    {
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  /* Log links are created only once.  */
	  gcc_assert (!LOG_LINKS (insn));

	  FOR_EACH_INSN_DEF (def, insn)
	    {
	      unsigned int regno = DF_REF_REGNO (def);
	      rtx_insn *use_insn;

	      if (!next_use[regno])
		continue;

	      if (!can_combine_def_p (def))
		continue;

	      use_insn = next_use[regno];
	      next_use[regno] = NULL;

	      if (BLOCK_FOR_INSN (use_insn) != bb)
		continue;

	      /* flow.c claimed:

		 We don't build a LOG_LINK for hard registers contained
		 in ASM_OPERANDs.  If these registers get replaced,
		 we might wind up changing the semantics of the insn,
		 even if reload can make what appear to be valid
		 assignments later.  */
	      if (regno < FIRST_PSEUDO_REGISTER
		  && asm_noperands (PATTERN (use_insn)) >= 0)
		continue;

	      /* Don't add duplicate links between instructions.  */
	      struct insn_link *links;
	      FOR_EACH_LOG_LINK (links, use_insn)
		if (insn == links->insn && regno == links->regno)
		  break;

	      if (!links)
		LOG_LINKS (use_insn)
		  = alloc_insn_link (insn, regno, LOG_LINKS (use_insn));
	    }

	  FOR_EACH_INSN_USE (use, insn)
	    if (can_combine_use_p (use))
	      next_use[DF_REF_REGNO (use)] = insn;
	}
    }

  free (next_use);
}

static void
check_extend(rtx_insn *insn)
{
  rtx pat = PATTERN (insn);
  int which;

  if (GET_CODE(pat) == SET &&
      ((which = GET_CODE(SET_SRC(pat))) == SIGN_EXTEND ||
       which == ZERO_EXTEND))
    {
      DUMP("  found a %s, ", insn_data[INSN_CODE(insn)].name);

      struct insn_link *links;
      int count = 0;
      rtx_insn *load_insn = nullptr;
      FOR_EACH_LOG_LINK (links, insn)
	{
	  count++;
	  rtx lpat = PATTERN(links->insn);
	  if (GET_CODE(lpat) == SET &&
	      REG_P(SET_DEST(lpat)) &&
	      GET_CODE(SET_SRC(lpat)) == MEM &&
	      // This condition is safe, is it over re-strictive?
	      GET_MODE(SET_DEST(lpat)) == GET_MODE(XEXP(SET_SRC(pat), 0)) &&
	      dead_or_set_p(insn, SET_DEST(lpat)))
	    {
	      load_insn = links->insn;
	    }
	}

      if (load_insn && count == 1)
	{
	  rtx lpat = PATTERN(load_insn);
	  rtx extend = (which == SIGN_EXTEND) ?
	    gen_rtx_SIGN_EXTEND(SImode, SET_SRC(lpat)) :
	    gen_rtx_ZERO_EXTEND(SImode, SET_SRC(lpat));

	  if (validate_change(load_insn, &SET_DEST(lpat), SET_DEST(pat), true) &&
	      validate_change(load_insn, &SET_SRC(lpat), extend, true) &&
	      apply_change_group())
	    {
	      DUMP("merged load, deleted extend\n");
	      set_insn_deleted(insn);
	    }
	  else
	    {
	      gcc_unreachable();
	    }
	}
      else
	{
	  DUMP("could not merge with load\n");
	}
    }
}

static void
check_store(rtx_insn *insn)
{
  rtx pat = PATTERN (insn);

  machine_mode mode;
  if (GET_CODE(pat) == SET &&
      GET_CODE(SET_DEST(pat)) == MEM &&
      SUBREG_P(SET_SRC(pat)) &&
      ((mode = GET_MODE(SET_DEST(pat))) == HImode ||
       mode == QImode) &&
      dead_or_set_p(insn, SUBREG_REG(SET_SRC(pat))))
    {
      DUMP("  found a %s store, ", mode == QImode ? "QI" : "HI");

      struct insn_link *links;
      rtx_insn *extend_insn = nullptr;

      // Links can be either for the address of the DEST (don't care)
      // or the SRC
      rtx src = SET_SRC(pat);
      rtx srcreg = SUBREG_REG(src);
      FOR_EACH_LOG_LINK (links, insn)
	{
	  rtx lpat = PATTERN(links->insn);
	  if (GET_CODE(lpat) == SET &&
	      GET_CODE(SET_DEST(lpat)) == REG &&
	      REGNO(SET_DEST(lpat)) == REGNO(srcreg) &&
	      (GET_CODE(SET_SRC(lpat)) == ZERO_EXTEND ||
	       GET_CODE(SET_SRC(lpat)) == SIGN_EXTEND) &&
	      SUBREG_P(XEXP(SET_SRC(lpat), 0)) &&
	      dead_or_set_p(links->insn, SUBREG_REG(XEXP(SET_SRC(lpat), 0))))
	    {
	      gcc_assert(GET_MODE(SET_DEST(lpat)) == GET_MODE(XEXP(src, 0)));
	      extend_insn = links->insn;
	    }
	  else if (refers_to_regno_p(REGNO(srcreg), END_REGNO(srcreg), links->insn, nullptr))
	    {
	      if (GET_CODE(lpat) == SET &&
		  GET_CODE(SET_DEST(lpat)) == MEM &&
		  SUBREG_P(SET_SRC(lpat)) &&
		  GET_MODE(SET_DEST(lpat)) == mode)
		{
		  DUMP("  found another %s store ", mode == QImode ? "QI" : "HI");
		  extend_insn = nullptr;
		}
	      else
		{
		  extend_insn = nullptr;
		  break;
		}
	    }
	}

      if (extend_insn)
	{
	  unsigned int regno = REGNO(SUBREG_REG(SET_SRC(pat)));
	  rtx *link;
	  for (link = &REG_NOTES (insn); *link; link = &XEXP (*link, 1))
	    {
	      if (REG_NOTE_KIND (*link) == REG_DEAD &&
		  REG_P(XEXP (*link, 0)) &&
		  REGNO (XEXP (*link, 0)) <= regno &&
		  END_REGNO (XEXP (*link, 0)) > regno)
		{
		  break;
		}
	    }
	  gcc_assert(*link);

	  if (validate_change(insn, &SUBREG_REG(SET_SRC(PATTERN(insn))),
			      SUBREG_REG(XEXP(SET_SRC(PATTERN(extend_insn)), 0)), true) &&
	      validate_change(insn, link,
			      find_reg_note(extend_insn, REG_DEAD, SET_DEST(XEXP(SET_SRC(PATTERN(extend_insn)), 0))), true) &&
	      apply_change_group())
	    {
	      DUMP("merged store, deleted extend\n");
	      set_insn_deleted(extend_insn);
	    }
	  else
	    {
	      gcc_unreachable();
	    }
	}
      else
	{
	  DUMP("src not set by extend or other intervening use\n");
	}
    }
}

// This pass removes unnecessary sign/zero extension operations
//
// This shows up in our code a lot because, at present, we use volatiles
// heavily and GCC bails out on merging zero/sign extend ops with loads/stores
// when the address is volatile.
//
// The 2 (most common) cases handled are:
// 1) A load followed immediately by a zero/sign ext
// 2) A zero/sign ext followed immediately by a store
//
// There are 2 more less common more complex cases that aren't handled at
// present:
// 1) A zero/sign ext followed by multiple stores of the result
// 2) A zero/sign ext followed by a store and another (safe) use of the result
static void
transform(function *fn)
{
  DUMP("TT Riscv opt pass on: %s\n", function_name(fn));

  basic_block bb;

  max_uid_known = get_max_uid ();
  uid_log_links = XCNEWVEC (struct insn_link *, max_uid_known + 1);
  gcc_obstack_init (&insn_link_obstack);
  create_log_links ();
  df_analyze();

  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (NONDEBUG_INSN_P(insn))
	    {
	      check_extend(insn);
	      check_store(insn);
	    }
	}
    }

  obstack_free (&insn_link_obstack, NULL);
  free (uid_log_links);
}

namespace {

const pass_data pass_data_rvtt_rmext =
{
  RTL_PASS, /* type */
  "rvtt_rmext", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_rmext : public rtl_opt_pass
{
public:
  pass_rvtt_rmext (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_rmext, ctxt)
  {
  }

  /* opt_pass methods: */
  virtual unsigned int execute (function *cfn)
    {
      if (flag_rvtt_rmext)
	{
	  transform (cfn);
	}
      return 0;
    }
}; // class pass_rvtt_rmext

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_rmext (gcc::context *ctxt)
{
  return new pass_rvtt_rmext (ctxt);
}
