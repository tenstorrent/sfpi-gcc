/* Pass to issue diagnostics for SFPU operations
   Copyright (C) 2022-2026 Tenstorrent Inc.
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

#define INCLUDE_ALGORITHM
#define INCLUDE_MAP
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "stringpool.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa-propagate.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-ssa.h"
#include "tree-into-ssa.h"
#include "diagnostic-core.h"
#include "rvtt.h"
#include <unordered_map>
#include <unordered_set>

// A pattern-driven combiner.  We iterate until no changes happen -- this
// allows combine patterns to enable other patterns, or overlap and be executed
// in some order. The patterns are described in a GimpleCombine (.gc) file,
// processed by genrvtt-combine whose output is #included above. Each combiner
// is a list of patterns to match, a list of replacements to substitute, a set of
// bespoke predicates, init & fini functions and a few extraneous flags.

// The matcher is pretty simplistic -- it doesn't try and minimize searches
// beyond recording possible starting points.  We do order the checks to do the
// simplest ones first and only do the complicated ones when those all
// pass. The starting point for a match is the last pattern in a sequence, and
// once found we search backwards to calls producing inputs to that pattern.

// A single pass finds all the combines that match, and then throws out matches
// that overlap the end(s) of other combines (the intention is that the
// later-matching combination iwll match (part-of) the output on the next pass. If two combines
// overlap differently, the longer combine is selected.

// We (currently) have a simplistic model of CC-regions -- every CC-setting
// builtin separates two regions. Regions are also separated by basic
// blocks. All non-SetAnywhere patterns are treated as SameRegion. It would be
// nicer to have better information, but that's a task for another day.

namespace {
  constexpr unsigned args_hwm = 10;

  // pattern possibilities
  enum class Flags : uint8_t {
    OtherUses = 1 << 0, // Other uses are permissable (do not delete)
    MaybeUnused = 1 << 1, // There might be no uses of this (it could be null)
    SetAnywhere = 1 << 2, // It may be set anywhere, (not in the same live region)
    SameRegion = 1 << 3, // It must be in the same CC region
  };

  // builtin call pattern to match or template to generate
  // Shapes (usually) specify the _lv variant.  The machinery can deduce how
  // to handle the non lv variant from that.
  struct Shape {
    struct Arg {
      bool is_var : 1; // It's an SSA var, not a constant
      bool commutes : 1; // It commutes with the next argument
      unsigned val : 30; // Either combine var slot, or constant
    };
    rvtt_insn_data::insn_id id; // The rvtt id
    uint8_t lhs;  // slot for Lhs var
    uint8_t flags; // flags for lhs var
    uint8_t num_args; // number of args to fn
    uint8_t used_by_mask; // which patterns use lhs
    Arg args[args_hwm];  // Argument information

  public:
    bool is_match (const rvtt_insn_data *) const;
  };

  // Combiner -- a set of patterns to match and a set of templates to replace
  // those with.  The templates are placed at the last pattern's
  // location. Patterns other than OtherUses are deleted
  struct Combiner {
    Shape const *shapes;
    uint8_t pats_hwm;
    uint8_t reps_hwm;

    uint8_t rep_lhs_hwm;
    uint8_t pat_var_hwm;
    uint8_t rep_var_hwm;

    uint8_t replace_mask; // patterns whos output is a replacement output
    unsigned id;

    bool (*enable_hook) (); // target-specific enablement
    bool (*pred_hook) (gcall *[], tree [], unsigned); // pattern-specific checks
    void (*init_hook) (gcall *[], tree [], unsigned); // template-specific initialization
    void (*fini_hook) (gcall *[], tree []); // template-specific finalization

  public:
    struct matched_data;
    bool match (gcall *call, const rvtt_insn_data *insnd, matched_data &) const;
    void replace (gimple_stmt_iterator *, matched_data &, gcall **replace) const;

  private:
    struct match_masks {
      unsigned calls = 0; // Which calls we matched
      unsigned vars = 0;  // Which vars we defined
      unsigned commuted = 0; // Which commute vars commuted
      unsigned live = 0;  // Which calls were live values

    public:
      operator bool () const { return calls != 0; }
      match_masks &operator |= (match_masks const &other) {
	calls |= other.calls;
	vars |= other.vars;
	commuted |= other.commuted;
	live |= other.live;
	return *this;
      }
      void clear () {
	calls = vars = commuted = live = 0;
      }
    };
    bool match_one (basic_block bb, unsigned ix, gcall *, const rvtt_insn_data *,
			  unsigned outer_vars, matched_data &, match_masks &) const;
  };
}

// sfp{add,mul}i insns that need dynamic imm reconstitution
struct imminfo {
  gcall *call;
  rvtt_insn_data const *insnd;
  unsigned id;

  imminfo (gcall *call, rvtt_insn_data const *insnd, unsigned id)
    : call (call), insnd (insnd), id (id) {}

  bool operator< (imminfo const &other) const {
    return id < other.id;
  }
  bool operator< (unsigned id_) const {
    return id < id_;
  }
};
static std::vector<imminfo> addimuli;
static std::vector<imminfo> synths;

static bool ATTRIBUTE_UNUSED combiner_enable_false () { return false; }
static bool combiner_enable_WH () { return TARGET_XTT_TENSIX_WH; }
static bool combiner_enable_BH_QSR () { return TARGET_XTT_TENSIX_BH_QSR; }

#define OU unsigned (Flags::OtherUses)
#define MU unsigned (Flags::MaybeUnused)
#define SA unsigned (Flags::SetAnywhere)
#define SR unsigned (Flags::SameRegion)
#include "rvtt-combine.inc"
#undef SR
#undef SA
#undef MU
#undef OU

struct Combiner::matched_data {
  gcall *calls[combiner_pats_hwm];
  tree vars[combiner_vars_hwm];
  unsigned commuted = 0;
  unsigned deleted = 0;
};

bool
Shape::is_match (const rvtt_insn_data *insnd) const
{
  if (insnd->id == id)
    return true;

  if (insnd->is_live ())
    return false;

  if (auto *live_insnd = insnd->get_live ())
    if (live_insnd->id == id)
      return true;

  return false;
}

static bool
has_cc_insn_between (gcall *first, gcall *last)
{
  for (auto gsi = gsi_for_stmt (first); *gsi != last; gsi_next (&gsi))
    if (auto *insnd = rvtt_get_insn_data (*gsi))
      if (insnd->sets_cc (as_a <gcall *> (*gsi)))
	return true;

  return false;
}

static bool
has_other_use (tree var, gcall *allowed[], unsigned num_allowed)
{
  use_operand_p use_p;
  imm_use_iterator iter;
  FOR_EACH_IMM_USE_FAST (use_p, iter, var)
    {
      gimple *g = USE_STMT (use_p);
      if (is_gimple_debug (g))
	continue;

      for (unsigned jx = num_allowed; jx--;)
	if (allowed[jx] == g)
	  goto next_use;
      // This is a different use
      return true;
    next_use:;
    }
  return false;
}

static bool
has_use_between (tree var, gcall *begin, gcall *end,
		 gcall *allowed[], unsigned num_allowed)
{
  // We expect the range to be small, so just iterate over it looking for a
  // non-allowed reference.
  for (auto gsi = gsi_for_stmt (begin);;)
    {
      gsi_next (&gsi);
      gimple *stmt = *gsi;
      if (stmt == end)
	break;

      // Only uses will be in our own calls!
      if (auto *call = dyn_cast <gcall *> (stmt))
	{
	  for (unsigned ix = num_allowed; ix--;)
	    if (allowed[ix] == call)
	      goto ok;

	  for (unsigned argno = gimple_call_num_args (call); argno--;)
	    if (gimple_call_arg (call, argno) == var)
	      return true;

	ok:;
	}
    }
  return false;
}

bool
Combiner::match_one (basic_block bb, unsigned ix, gcall *call, const rvtt_insn_data *insnd,
		     unsigned outer_vars, matched_data &matched, match_masks &masks) const
{
  auto &pat = shapes[ix];

  gcc_assert (!((1 << pat.lhs) & outer_vars));
  tree lhs = gimple_call_lhs (call);
  if (!lhs && !(pat.flags & unsigned (Flags::MaybeUnused)))
    return false;

  matched.calls[ix] = call;
  masks.calls |= 1 << ix;

  matched.vars[pat.lhs] = lhs;
  masks.vars |= 1u << pat.lhs;

  int lv_arg = insnd->id != pat.id ? insnd->live_arg (): -1;
  int lv_delta = 0;

  match_masks commute_masks;
  int commute_delta = 0;
  int commute_arg = -1;
  for (int argno = 0; argno != pat.num_args; argno++)
    {
      if (argno == lv_arg)
	{
	  matched.vars[pat.args[argno].val] = nullptr;
	  masks.vars |= 1u << pat.args[argno].val;
	  lv_delta = 1;
	  continue;
	}

    try_commute:
      auto &arg_info = pat.args[argno];
      if (arg_info.commutes)
	commute_arg = argno;
      auto arg = gimple_call_arg (call, argno + commute_delta - lv_delta);

      if (!arg_info.is_var)
	{
	  if (TREE_CODE (arg) != INTEGER_CST
	      || TREE_INT_CST_LOW (arg) != arg_info.val)
	    {
	    commute_or_fail:
	      if (!commute_delta && commute_arg >= 0)
		{
		  argno = commute_arg;
		  commute_delta = +1;
		  commute_masks.clear ();
		  goto try_commute;
		}
	      return false;
	    }
	}
      else if ((1 << arg_info.val) & (commute_masks.vars | masks.vars | outer_vars))
	{
	  if (matched.vars[arg_info.val] != arg)
	    goto commute_or_fail;
	}
      else if (arg_info.val >= pats_hwm)
	{
	  commute_masks.vars |= 1u << arg_info.val;
	  matched.vars[arg_info.val] = arg;
	}
      else
	{
	  auto &next_pat = shapes[arg_info.val];
	  auto *stmt = SSA_NAME_DEF_STMT (arg);
	  auto *inner_insnd = rvtt_get_insn_data (stmt);
	  if (!inner_insnd || !next_pat.is_match (inner_insnd))
	    goto commute_or_fail;

	  // We only combine within a single BB -- it'd be nice to do better,
	  // but then CC checking becomes much harder
	  if (!(next_pat.flags & unsigned (Flags::SetAnywhere))
	      && bb != gimple_bb (stmt))
	    goto commute_or_fail;

	  match_masks inner_masks;
	  if (!match_one (bb, arg_info.val, as_a <gcall *> (stmt), inner_insnd,
			  commute_masks.vars | masks.vars | outer_vars,
			  matched, inner_masks))
	    goto commute_or_fail;
	  commute_masks |= inner_masks;
	}

      commute_delta = -commute_delta;
      if (!arg_info.commutes)
	{
	  if (commute_delta)
	    commute_masks.commuted |= (1 << pat.args[commute_arg].val);
	  masks |= commute_masks;
	  commute_masks.clear ();
	  commute_delta = 0;
	  commute_arg = -1;
	}
    }

  gcc_assert (pat.num_args == gimple_call_num_args (call) + lv_delta);

  if (insnd->is_live ())
    masks.live |= 1 << pat.args[insnd->live_arg ()].val;

  return true;
}

bool
Combiner::match (gcall *call, const rvtt_insn_data *insnd, matched_data &matched) const
{
  match_masks masks;
  if (!match_one (gimple_bb (call), pats_hwm - 1, call, insnd, 0, matched, masks))
    return false;
  matched.commuted = masks.commuted;

  gcc_assert (masks.calls == ((1u << pats_hwm) - 1)
	      && masks.vars == (((1u << pats_hwm) - 1)
				| (((1u << (pat_var_hwm - rep_lhs_hwm)) - 1) << rep_lhs_hwm)));

  if (pred_hook && !pred_hook (matched.calls, matched.vars, matched.commuted))
    return false;

  // Expensive checks now
  matched.deleted = replace_mask;
  for (int ix = 0; ix != pats_hwm; ix++)
    {
      auto const &pat = shapes[ix];
      if (replace_mask & (1 << ix))
	{
	  // This is a replaced insn. Check that it has no uses (other than us)
	  // between its current location and the last insn -- because we'll be
	  // moving it.
	  if (pats_hwm - (ix + 1)
	      && has_use_between (matched.vars[ix],
				  matched.calls[ix], matched.calls[pats_hwm - 1],
				  &matched.calls[ix + 1],
				  pats_hwm - (ix + 1) - 1))
	    return false;
	}
      else
	{
	  // This a non-replaced insn. Check its other uses and figure if this
	  // is ok and/or we should delete this insn.
	  if (tree lhs = gimple_call_lhs (matched.calls[ix]))
	    {
	      if (has_other_use (lhs, &matched.calls[ix + 1], pats_hwm - (ix + 1)))
		{
		  if (!(pat.flags & unsigned (Flags::OtherUses)))
		    // Not allowed other uses
		    return false;
		}
	      else
		matched.deleted |= 1 << ix;
	    }
	  else
	    matched.deleted |= 1 << ix;
	}

      if (!(pat.flags & unsigned (Flags::SetAnywhere))
	  && pat.used_by_mask)
	{
	  // No cc insns between any non-setanywhere input and its last use
	  unsigned last_use = HOST_BITS_PER_WIDE_INT - 1 - clz_hwi (pat.used_by_mask);
	  if (has_cc_insn_between (matched.calls[ix], matched.calls[last_use]))
	    return false;
	}
    }

  // If any lhs vars are the same, we're not a match
  for (unsigned ix = pats_hwm; ix--; )
    if (auto v = matched.vars[ix])
      for (unsigned jx = ix; jx--; )
	if (v == matched.vars[jx])
	  return false;

  // If any non-lhs non-live var is the same as an lhs, we're not a match
  for (unsigned ix = rep_lhs_hwm; ix != pat_var_hwm; ix++)
    if (auto v = matched.vars[ix])
      for (unsigned jx = pats_hwm; jx--; )
	if (v == matched.vars[jx])
	  {
	    // This non-lhs var matches an lhs var
	    if (!((1 << ix) & masks.live))
	      return false;  // Not a live, not a match

	    if (!((1 << jx) & (matched.deleted & ~replace_mask)))
	      continue; // Not a non-replaced deleted output

	    // Chase live to deleted insn's live input
	    auto insnd = rvtt_get_insn_data (matched.calls[jx]);
	    if (!insnd->is_live ())
	      return false;
	    v = gimple_call_arg (matched.calls[jx], insnd->live_arg ());
	    matched.vars[ix] = v;
	    // We'll continue checking this in the next iteration
	  }

  // It's ok for any non-lhs vars to be the same
  for (unsigned ix = pats_hwm; ix != rep_lhs_hwm; ix++)
    matched.vars[ix] = nullptr;
  for (unsigned ix = pat_var_hwm; ix != rep_var_hwm; ix++)
    matched.vars[ix] = nullptr;

  return true;
}

void
Combiner::replace (gimple_stmt_iterator *gsi, matched_data &matched, gcall **replace) const
{
  if (init_hook)
    init_hook (matched.calls, matched.vars, matched.commuted);

  for (unsigned ix = pats_hwm; ix != reps_hwm; ix++)
    {
      auto &rep = shapes[ix];
      auto const *insnd = rvtt_get_insn_data (rep.id);
      int lv_arg = -1;
      int lv_delta = 0;
      if (insnd->is_live ())
	{
	  lv_arg = insnd->live_arg ();
	  auto live_slot = rep.args[lv_arg].val;
	  if (!matched.vars[live_slot])
	    {
	      // We might need to add non-lv assign and then handle it specially?
	      gcc_assert (rep.id != rvtt_insn_data::sfpassign_lv);
	      insnd = insnd->get_not_live ();
	      lv_delta = 1;
	    }
	}
      gcc_assert (insnd->num_args () + lv_delta == rep.num_args
		  && insnd->decl);
      auto *call = gimple_build_call (insnd->decl, insnd->num_args ());
      replace[rep.lhs] = call;

      gimple_set_location (call, gimple_location (matched.calls[rep.lhs]));
      if (rep.lhs >= pats_hwm)
	matched.vars[rep.lhs]
	  = make_temp_ssa_name (TREE_TYPE (TREE_TYPE (insnd->decl)), nullptr, "cmb");

      gimple_set_lhs (call, matched.vars[rep.lhs]);

      tree arg_types = TYPE_ARG_TYPES (TREE_TYPE (insnd->decl));
      for (int argno = 0; argno != rep.num_args; argno++)
	{
	  if (argno == lv_arg && lv_delta)
	    {
	      argno += lv_delta - 1;
	      continue;
	    }

	  auto &arg_info = rep.args[argno];
	  tree val = nullptr;
	  if (arg_info.is_var)
	    val = matched.vars[arg_info.val];
	  else
	    val = build_int_cst (TREE_VALUE (arg_types), arg_info.val);
	  gimple_call_set_arg (call, argno - (argno >= lv_arg ? lv_delta : 0), val);
	  arg_types = TREE_CHAIN (arg_types);
	}

      gsi_insert_before (gsi, call, GSI_SAME_STMT);
    }

  for (int ix = pats_hwm - 1; ix--;)
    if ((1 << ix) & matched.deleted)
      {
	auto gsi = gsi_for_stmt (matched.calls[ix]);
	gsi_remove (&gsi, true);
      }

  gsi_remove (gsi, true);
  *gsi = gsi_for_stmt (replace[shapes[reps_hwm - 1].lhs]);

  if (fini_hook)
    fini_hook (replace, matched.vars);
}

// This array is sorted by builtin-id of the last pattern insn of a combiner,
// patterns for the same ID are sorted by priority (order in the GC file).
static std::vector<const Combiner *> combiner_map;
// This maps builtin ids to points in the combiner_map array.
static std::map<rvtt_insn_data::insn_id, std::vector<const Combiner *>::iterator> starting_ids;

static void
init ()
{
  // We assume there will be at least one combiner
  if (!starting_ids.empty ())
    return;

  auto id2key = [](rvtt_insn_data::insn_id id, unsigned priority = 0) {
    return priority | unsigned (id) << 20;
  };
  std::map<unsigned, const Combiner *> tmp;

  for (auto &combiner : combiners)
    if (!combiner.enable_hook || combiner.enable_hook ())
      {
	auto id = combiner.shapes[combiner.pats_hwm - 1].id;
	tmp.emplace (id2key (id, combiner.id), &combiner);
	auto not_live_id = rvtt_get_insn_data (id)->get_not_live ()->id;
	if (not_live_id != id)
	  tmp.emplace (id2key (not_live_id, combiner.id), &combiner);
      }

  // Reserve space so iterators we put into starting_ids are not invalidated as we
  // append to combiner_map
  combiner_map.reserve (tmp.size () + 1);
  auto prev_id = rvtt_insn_data::hwm;
  for (auto I = tmp.begin (), E = tmp.end (); I != E; ++I)
    {
      auto new_id = rvtt_insn_data::insn_id (I->first >> 20);
      if (new_id != prev_id)
	{
	  starting_ids.emplace (new_id, combiner_map.end ());
	  prev_id = new_id;
	}
      combiner_map.push_back (I->second);
    }
  starting_ids.emplace (rvtt_insn_data::hwm, combiner_map.end ());
}

// There is at least one dynamic muli transform.  For each synth_id of such
// transforms, if all of them are used by the new muli/addi we can reuse.
// Otherwise we need to add new synth opcodes.

static void
addimuli_resynthing ()
{
  // Sort by id
  std::sort (addimuli.begin (), addimuli.end ());
  std::sort (synths.begin (), synths.end ());

  struct synth_add {
    gcall *synth;
    gassign *add;
    const rvtt_insn_data *use_insnd;
    tree use_imm;
  };
  std::unordered_map<gassign *, synth_add> add_map;
  std::unordered_set<gcall *> use_set;

  unsigned id_hwm = synths.back ().id;

  auto SI = synths.begin (), SE = synths.end (), SN = SI;
  for (auto I = addimuli.begin (), E = addimuli.end (), N = I;
       I != E; I = N, SI = SN)
    {
      add_map.clear ();
      use_set.clear ();

      unsigned kinds = 0;
      unsigned id = I->id;
      N = I;
      do
	{
	  tree var = gimple_call_arg (N->call, N->insnd->var_arg ());
	  tree imm = gimple_call_arg (N->call, N->insnd->imm_arg ());
	  auto *def = as_a <gassign *> (SSA_NAME_DEF_STMT (var));
	  add_map.emplace (def, synth_add {nullptr, def, N->insnd, imm});

	  use_set.insert (N->call);
	  kinds |= 1 << (N->insnd->get_not_live ()->id
			 == rvtt_insn_data::sfpmuli);
	}
      while (++N != E && N->id == id);
      // [I, N) are new insns with the same id.

      // We can't get muli and addi conversions for the same ID, as that
      // implies faulty ID generation. It'd be very bizzarre set
      // of circumstances though.
      if (kinds == 3)
	internal_error ("sfpmuli & sfpaddi combines share an id");

      SI = lower_bound (SI, SE, id);
      // We expect there to be few synths of the same ID, so just search
      // forwards
      for (SN = SI; ++SN != SE && SN->id == id;)
	continue;
      // [SI, SN) are synths for the same id

      // Trace every synth to see if we get to only new insns.
      // Each use of synth's result needs to be an add whose result is used in
      // a new insn.  Anything else is too complicated
      bool matching = true;
      for (auto probe = SI; probe != SN; ++probe)
	{
	  use_operand_p synth_use;
	  imm_use_iterator synth_iter;
	  FOR_EACH_IMM_USE_FAST (synth_use, synth_iter, gimple_call_lhs (probe->call))
	    {
	      gimple *use = USE_STMT (synth_use);
	      if (is_gimple_debug (use))
		continue;

	      auto *assign = dyn_cast <gassign *> (use);
	      if (!assign
		  || gimple_assign_rhs_code (assign) != PLUS_EXPR)
		{
		  matching = false;
		  continue;
		}

	      auto AMI = add_map.find (assign);
	      if (AMI == add_map.end ())
		{
		  matching = false;
		  continue;
		}

	      // Record the synth insn
	      // This is why we keep iterating
	      AMI->second.synth = probe->call;

	      if (matching)
		{
		  use_operand_p add_use;
		  imm_use_iterator add_iter;
		  FOR_EACH_IMM_USE_FAST (add_use, add_iter, gimple_get_lhs (assign))
		    {
		      gimple *stmt = USE_STMT (add_use);
		      if (is_gimple_debug (stmt))
			continue;

		      auto *call = dyn_cast <gcall *> (stmt);
		      if (!call || use_set.find (call) == use_set.end ())
			{
			  matching = false;
			  break;
			}
		    }
		}
	    }
	}

      int loadi_shift = rvtt_get_insn_data (rvtt_insn_data::sfploadi)->imm_encode ();
      if (matching)
	{
	  if (dump_file)
	    fprintf (dump_file, "All uses of synth_id %u replaced by new insns\n", id);

	  // Everything is new, reuse synths
	  for (auto AI = add_map.begin (), EI = add_map.end ();
	       AI != EI; ++AI)
	    {
	      gassign *add = AI->second.add;

	      tree op = gimple_assign_rhs1 (add);
	      tree second = gimple_assign_rhs2 (add);
	      bool is_first = second == gimple_call_lhs (AI->second.synth);
	      if (!is_first)
		{
		  gcc_assert (op == gimple_call_lhs (AI->second.synth));
		  op = second;
		}

	      // OP is the adjusted immediate, insert an additional shift of
	      // SHIFT_DELTA
	      int shift_delta = int (AI->second.use_insnd->imm_encode ()) - loadi_shift;
	      gcc_assert (shift_delta > 0);
	      tree var = make_temp_ssa_name (TREE_TYPE (op), nullptr, "xtra");
	      gimple *shift_stmt = gimple_build_assign (var, LSHIFT_EXPR, op,
							build_int_cst (unsigned_type_node, shift_delta));
	      gimple_set_location (shift_stmt, gimple_location (add));
	      auto add_gsi = gsi_for_stmt (add);
	      gsi_insert_before (&add_gsi, shift_stmt, GSI_SAME_STMT);
	      if (is_first)
		gimple_assign_set_rhs1 (add, var);
	      else
		gimple_assign_set_rhs2 (add, var);
	      update_stmt (add);
	      if (dump_file)
		{
		  fprintf (dump_file, "Inserted ");
		  print_gimple_stmt (dump_file, shift_stmt, 0);
		  fprintf (dump_file, "before modified ");
		  print_gimple_stmt (dump_file, add, 0);
		}
	    }
	}
      else
	{
	  // Something is still old, add new synths
	  if (dump_file)
	    fprintf (dump_file, "Not all uses of synth_id %u replaced by new insns\n", id);
	  // For every add in the add_map, insert a new sequence just after
	  // it.  If its synth insn is known, add the synth with the old one.
	  // We can still use the masked var operand, if we can find it.
	  tree new_id = build_int_cst (unsigned_type_node, ++id_hwm);

	  // Create the new synths & adds
	  for (auto AI = add_map.begin (), EI = add_map.end ();
	       AI != EI; ++AI)
	    {
	      auto add_gsi = gsi_for_stmt (AI->first);

	      // Create the new synth_opcode
	      auto synth_insnd = rvtt_get_insn_data (rvtt_insn_data::synth_opcode);
	      auto synth_call = gimple_build_call (synth_insnd->decl, synth_insnd->num_args ());
	      gimple_call_set_arg (synth_call, 0, integer_zero_node);
	      gimple_call_set_arg (synth_call, 1, new_id);
	      auto synth_ssa = make_temp_ssa_name (unsigned_type_node, nullptr, "id");
	      gimple_call_set_lhs (synth_call, synth_ssa);
	      tree mask_ssa = nullptr;
	      int shift_delta = 0;
	      gimple *mask_stmt = nullptr;
	      if (AI->second.synth)
		{
		  gimple_set_location (synth_call, gimple_location (AI->second.synth));
		  auto synth_gsi = gsi_for_stmt (AI->second.synth);
		  gsi_insert_before (&synth_gsi, synth_call, GSI_SAME_STMT);

		  // Find the other add input, which is masked & shifted
		  mask_ssa = gimple_assign_rhs1 (AI->first);
		  tree second = gimple_assign_rhs2 (AI->first);
		  if (second != gimple_call_lhs (AI->second.synth))
		    {
		      gcc_assert (mask_ssa == gimple_call_lhs (AI->second.synth));
		      mask_ssa = second;
		    }
		  shift_delta = loadi_shift;
		}
	      else
		{
		  gimple_set_location (synth_call, gimple_location (AI->first));
		  gsi_insert_before (&add_gsi, synth_call, GSI_SAME_STMT);

		  tree imm = AI->second.use_imm;
		  uint32_t mask = (uint32_t (1) << AI->second.use_insnd->imm_bits ()) - 1;
		  mask_ssa = make_temp_ssa_name (unsigned_type_node, nullptr, "mask");
		  mask_stmt = gimple_build_assign (mask_ssa,
							   BIT_AND_EXPR, imm,
							   build_int_cst (unsigned_type_node, mask));
		  gimple_set_location (mask_stmt, gimple_location (AI->first));
		  gsi_insert_before (&add_gsi, mask_stmt, GSI_SAME_STMT);
		}

	      // Create new shift
	      shift_delta = int (AI->second.use_insnd->imm_encode ()) - shift_delta;
	      gcc_assert (shift_delta > 0);
	      tree var = make_temp_ssa_name (TREE_TYPE (mask_ssa), nullptr, "xtra");
	      gimple *shift_stmt = gimple_build_assign (var, LSHIFT_EXPR, mask_ssa,
							build_int_cst (unsigned_type_node, shift_delta));
	      gimple_set_location (shift_stmt, gimple_location (AI->first));
	      gsi_insert_before (&add_gsi, shift_stmt, GSI_SAME_STMT);

	      // Create the new add
	      tree add_ssa = make_temp_ssa_name (unsigned_type_node, nullptr, "sum");
	      auto *add_stmt = gimple_build_assign (add_ssa, PLUS_EXPR, synth_ssa, var);
	      gsi_insert_before (&add_gsi, add_stmt, GSI_SAME_STMT);

	      AI->second.add = add_stmt;

	      if (dump_file)
		{
		  fprintf (dump_file, "Creating synth sequence:\n");
		  print_gimple_stmt (dump_file, synth_call, 2);
		  if (mask_stmt)
		    print_gimple_stmt (dump_file, mask_stmt, 2);
		  print_gimple_stmt (dump_file, shift_stmt, 2);
		  print_gimple_stmt (dump_file, add_stmt, 2);
		}
	    }

	  // Update the new insns
	  for (auto UI = I; UI != N; ++UI)
	    {
	      gimple_call_set_arg (UI->call, UI->insnd->id_arg (), new_id);
	      tree var = gimple_call_arg (UI->call, UI->insnd->var_arg ());
	      auto new_add = add_map.find (as_a <gassign *> (SSA_NAME_DEF_STMT (var)));
	      gimple_call_set_arg (UI->call, UI->insnd->var_arg (), gimple_get_lhs (new_add->second.add));
	      update_stmt (UI->call);
	      if (dump_file)
		{
		  fprintf (dump_file, "Updating use ");
		  print_gimple_stmt (dump_file, UI->call, 0);
		}
	    }
	}
    }
}

static bool
combine_block (basic_block bb)
{
  bool changed = false;

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
       !gsi_end_p (gsi); gsi_next (&gsi))
    {
    again:
      auto *insnd = rvtt_get_insn_data (*gsi);
      if (!insnd)
	continue;

      // Record synth_opcodes to deal with dynamic muli/addi combinations.
      if (insnd->id == rvtt_insn_data::synth_opcode)
	synths.emplace_back (as_a <gcall *> (*gsi), insnd,
	    TREE_INT_CST_LOW (gimple_call_arg (as_a <gcall *> (*gsi), 1)));

      auto start = starting_ids.lower_bound (insnd->id);
      // Because we've added insn_id::hwm, start will never be
      // starting_ids.end ()
      if (start->first != insnd->id)
	continue;
      for (auto I = start->second, E = (++start)->second; I != E; ++I)
	{
	  auto *combiner = *I;
	  Combiner::matched_data matched;
	  if (!combiner->match (as_a <gcall *> (*gsi), insnd, matched))
	    continue;

	  if (dump_file)
	    {
	      fprintf (dump_file, "Found pattern %u:\n", combiner->id);
	      for (unsigned ix = 0; ix != combiner->pats_hwm; ix++)
		{
		  char c = 'K';
		  if ((1 << ix) & combiner->replace_mask)
		    c = 'R';
		  else if ((1 << ix) & matched.deleted)
		    c = 'D';

		  fprintf (dump_file, "%c ", c);
		  print_gimple_stmt (dump_file, matched.calls[ix], 2);
		}
	    }

	  gcall *replace[combiner_reps_hwm];
	  combiner->replace (&gsi, matched, replace);
	  if (dump_file)
	    {
	      fprintf (dump_file, "Replaced with:\n");
	      for (unsigned ix = combiner->pats_hwm; ix != combiner->reps_hwm; ix++)
		{
		  auto *rep = replace[combiner->shapes[ix].lhs];
		  print_gimple_stmt (dump_file, rep, 2);
		}
	      fprintf (dump_file, "\n");
	    }
	  changed = true;
	  goto again;
	}
    }

  return changed;
}

namespace {

const pass_data pass_data_rvtt_combine =
{
  GIMPLE_PASS, /* type */
  "rvtt_combine", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_combine : public gimple_opt_pass
{
public:
  pass_rvtt_combine (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_combine, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    init ();

    addimuli.clear ();
    synths.clear ();

    bool changed = false;
    basic_block bb;

    FOR_EACH_BB_FN (bb, fn)
      if (combine_block (bb))
	changed = true;

    if (!addimuli.empty ())
      addimuli_resynthing ();

    return changed ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_combine (gcc::context *ctxt)
{
  return new pass_rvtt_combine (ctxt);
}
