/* Pass to work around GS' memory aribtration bug
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#include "print-rtl.h"
#include "cfgbuild.h"
#include "rvtt.h"

static void
emit_load (rtx_insn *insn, bool before, rtx mem)
{
  mem = copy_rtx (mem);
  MEM_VOLATILE_P (mem) = true;
  if (GET_MODE (mem) != SImode)
    mem = gen_rtx_ZERO_EXTEND (SImode, mem);
  rtx new_insn = gen_rtx_SET (gen_rtx_REG (SImode, 0), mem);
  auto inserted = before ? emit_insn_before (new_insn, insn)
    : emit_insn_after (new_insn, insn);
  if (dump_file)
    {
      fprintf (dump_file, "Inserting ");
      dump_insn_slim (dump_file, inserted);
    }
}

// WH has a read after write hazard bug where loading a word after a byte or
// half store issues the load before the store.  The bug is in the address
// comparator logic and it’s 32bits wide. if addresses match, RAW hazard will
// be detected. So if the shorter store is word-aligned, we have no hazard. (we
// do not take advantage of that) Mem logic is prioritizing loads over stores
// and even though there’s no reorder buffer, 2 loads could get issued before a
// store actually gets out. If there is an intervening store, it is not clear
// whether the hazard is resolved.  As the bug is very sensitive, we anull it
// in all cases by placing a short load as late as possible after the short
// store. That's when we encounter the first control-flow change, write to
// store's ptr register, a load of any size, or the end of the block. (It is
// desirable to sink the load as late as possible.)

namespace {
struct Access {
  rtx mem = nullptr;
  int regno = -1;
  int offset = 0;

  operator bool () const { return mem; }
  Access &operator = (rtx);

  bool is_subword () const {
    return GET_MODE (mem) != SImode;
  }

  bool overlaps (Access const &other) const {
    if (regno != other.regno)
      return false;

    int size = GET_MODE_SIZE (GET_MODE (mem)).to_constant ();
    int other_size = GET_MODE_SIZE (GET_MODE (other.mem)).to_constant ();

    return (offset + size) > other.offset
      && (other.offset + other_size) > offset;
  }
};
}

Access &
Access::operator= (rtx m)
{
  if (m)
    {
      if (GET_CODE (m) == ZERO_EXTEND
	  || GET_CODE (m) == SIGN_EXTEND)
	m = XEXP (m, 0);

      if (MEM_P (m) && !rvtt_reg_mem_p (m)
	  && (GET_MODE (m) == SImode
	      || GET_MODE (m) == HImode
	      || GET_MODE (m) == QImode))
	{
	  rtx op = XEXP (m, 0);
	  if (REG_P (op))
	    regno = REGNO (op);
	  else if (GET_CODE (op) == PLUS
		   || GET_CODE (op) == LO_SUM)
	    {
	      regno = REGNO (XEXP (op, 0));
	      offset = INTVAL (XEXP (op, 1));
	    }
	  else
	    m = nullptr;;
	}
      else
	m = nullptr;
    }
  mem = m;

  return *this;
}

static void
workaround_raw (function *cfn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      Access store; // most recent store we need to remember
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  rtx pat = PATTERN (insn);
	  Access access;
	  bool is_store = false;
	  rtx set_dst = nullptr;

	  if (GET_CODE (pat) == SET)
	    {
	      access = set_dst = SET_DEST (pat);
	      if (access)
		{
		  is_store = true;
		  set_dst = nullptr;
		}
	      else
		access = SET_SRC (pat);
	    }

	  // access indicates this insn's load or store info
	  if (store)
	    {
	      bool need_load = false;
	      if (store.is_subword ())
		{
		  if (is_store
		      || GET_CODE (insn) == CALL_INSN
		      || access
		      || (set_dst && refers_to_regno_p (store.regno, set_dst)))
		    need_load = true;
		}
	      else if (access && access.is_subword ()
		       && store.overlaps (access))
		need_load = true;

	      if (need_load)
		{
		  emit_load (insn, true, store.mem);
		  if (dump_file)
		    {
		      fprintf (dump_file, "before ");
		      dump_insn_slim (dump_file, insn);
		    }
		  store.mem = nullptr;
		}
	    }

	  if (is_store)
	    store = access;
	}

      if (store && store.is_subword ())
	{
	  emit_load (BB_END (bb), control_flow_insn_p (BB_END (bb)), store.mem);
	  if (dump_file)
	    fprintf (dump_file, "at end of block");
	  store = nullptr;
	}
    }
}

namespace {

const pass_data pass_data_rvtt_fix_raw =
{
  RTL_PASS, /* type */
  "rvtt_fix_raw", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_fix_raw : public rtl_opt_pass
{
private:

public:
  pass_rvtt_fix_raw (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_fix_raw, ctxt)
  {
  }

  virtual bool gate (function *) override
  {
    return riscv_tt_fix_wh_raw > 0;
  }
  
  /* opt_pass methods: */
  virtual unsigned execute (function *cfn) override
    {
      workaround_raw (cfn);

      return 0;
    }
}; // class pass_rvtt_fix_wh

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_fix_raw (gcc::context *ctxt)
{
  return new pass_rvtt_fix_raw (ctxt);
}
