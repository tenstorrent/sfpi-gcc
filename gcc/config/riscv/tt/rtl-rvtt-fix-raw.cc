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
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree-pass.h"
#include "print-rtl.h"
#include "cfgbuild.h"
#include "rvtt.h"

static void
emit_load (rtx_insn *insn, bool before, rtx mem)
{
  mem = copy_rtx (mem);
  MEM_VOLATILE_P (mem) = true;
  if (GET_MODE (mem) != SImode && GET_MODE (mem) != SFmode)
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

namespace {
struct Access {
  rtx mem = nullptr;
  int reg = 0;
  int offset = 0;

  operator bool () const { return mem; }
  Access &operator = (rtx);

  bool is_subword () const {
    return GET_MODE (mem) == HImode
      || GET_MODE (mem) == QImode;
  }

  // The accesses partially overlap.
  bool overlaps (Access const &other) const {
    if (reg != other.reg)
      return false;

    auto get_size = [] (machine_mode mode) {
      return mode == QImode ? UNITS_PER_WORD / 4
	: mode == HImode ? UNITS_PER_WORD / 2
	: UNITS_PER_WORD;
    };
    int size = get_size (GET_MODE (mem));
    int other_size = get_size (GET_MODE (other.mem));

    if (size == other_size)
      return false;

    return (offset + size) > other.offset
      && (other.offset + other_size) > offset;
  }

  bool is_aligned () const {
    // If there's an offset, assume base pointer was aligned.
    if (offset)
      return !(offset & 3);

    // If there's no offset, only know sp and fp are aligned
    return reg == STACK_POINTER_REGNUM
      || (frame_pointer_needed && reg == HARD_FRAME_POINTER_REGNUM);
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
	    {
	      reg = REGNO (op);
	      offset = 0;
	    }
	  else if (GET_CODE (op) == PLUS
		   || GET_CODE (op) == LO_SUM)
	    {
	      reg = REGNO (XEXP (op, 0));
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

// WH has a read after write hazard bug because the load's checking of the
// outstanding writes compares the entire address, (not ignoring the bottom 2
// bits).  Thus:
//
// 1) A non-word-aligned byte (or half) store followed by an (aligneD) word
// load will read stale data.
//
// 2) An (aligned) word store followed by a non-word-aligned byte (or half)
// load will read stale data.
//
// Mem logic prioritizes loads over stores and even though there’s no reorder
// buffer, 2 loads could get issued before a store actually gets out. If there
// is an intervening store, it is not clear whether the hazard is resolved.
//
// Fully covering all cases is prohibitively expensive for performance.
//
// There are two access patterns, reg, or reg + cst.  The former is (usually)
// an arbitrary pointer where we do not know the alignment, but the latter is a
// structure (or stack, same thing) access, where we know the base pointer
// alignment and can presume it's at least word aligned (let's igore char-only
// structs).

// For the former case, with a sub-word store, we need to insert a sub-word
// load before the next larger load that is not a stack load.  If the base
// pointer changes, also insert such a load before the base pointer change.

// For the latter case, with a sub-word store at a non-aligned constant offset,
// insert a load before the next load off the same base pointer, or when the
// base pointer changes.

// For the latter case, with a word store, insert a load before the next
// subword load at a non-aligned offset from the same base pointer.

// If we reach the end of a bb with a live unaligned sub-word store insert a
// load.

// If we have a live unaligned sub-word store at a call site, insert a
// protecting load.

static void
workaround_raw (function *cfn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      Access store; // most recent store we need to remember
      bool store_is_unaligned = false;
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  rtx pat = PATTERN (insn);
	  Access access;
	  bool new_store = false;
	  rtx set_dst = nullptr;

	  if (GET_CODE (pat) == SET)
	    {
	      access = set_dst = SET_DEST (pat);
	      if (access)
		{
		  new_store = true;
		  set_dst = nullptr;
		}
	      else
		access = SET_SRC (pat);
	    }

	  // access indicates this insn's load or store info
	  if (store)
	    {
	      bool need_load = false;
	      if (store_is_unaligned)
		{
		  if (new_store
		      || GET_CODE (insn) == CALL_INSN
		      || access
		      || (set_dst && refers_to_regno_p (store.reg, set_dst)))
		    need_load = true;
		}
	      else if (!new_store
		       && access && access.is_subword () && !access.is_aligned ()
		       && access.overlaps (store))
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

	  if (new_store)
	    {
	      store = access;
	      store_is_unaligned = store.is_subword () && !store.is_aligned ();
	    }
	}

      if (store && store_is_unaligned)
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
