/* Pass to combine SFPU intrinsic sequences via generated patterns
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
#define INCLUDE_SET
#define INCLUDE_UNORDERED_MAP
#define INCLUDE_UNORDERED_SET
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
#include <deque>
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-delivery-cost.h"
#include <unordered_map>
#include <unordered_set>

/* A pattern-driven combiner over SFPU intrinsic calls.

   The patterns are described in a GimpleCombine (.gc) file (rvtt.gc),
   processed by genrvtt-combine, whose generated tables are #included
   below.  Each combiner rule is a list of call shapes to match, a list
   of replacement templates, optional bespoke predicates and init/fini
   hooks, and flags.  Rules can be gated (per architecture or per
   option); a fired rule is identified in the dump by its stable tag.

   Matching: the matcher is deliberately simple -- it records possible
   starting points and orders its checks cheapest-first.  A match is
   anchored at the LAST pattern of a sequence and searched backwards
   through the calls producing that pattern's inputs.  Iteration
   continues until no rule fires, so one rule's output can enable
   another's match.

   Overlap resolution: rules anchored on one statement are tried in
   priority (.gc line) order and the first match is applied
   immediately; a rule whose input was consumed by an earlier rewrite
   simply re-matches against the rewritten output on a later
   iteration.

   CC-region model: deliberately coarse -- every CC-setting builtin
   separates two regions, and block boundaries separate regions too.
   All non-SetAnywhere patterns are treated as SameRegion.  (The RTL
   engines have an exact region tree; refining this remains open.)  */

namespace {
  constexpr unsigned args_hwm = 10;

  // pattern possibilities
  enum class Flags : uint8_t {
    OtherUses = 1 << 0, // Other uses are permissible (do not delete)
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
      unsigned val : 31; // Either combine var slot, or constant
    };
    rvtt_insn_data::insn_id id; // The rvtt id
    uint8_t lhs;  // slot for Lhs var
    uint8_t flags; // flags for lhs var
    uint8_t num_args; // number of args to fn
    uint8_t used_by_mask; // which patterns use lhs
    uint8_t commute_arg; // arg that commutes (with previous)
    uint8_t commute_bit; // bit in the commute mask
    Arg args[args_hwm];  // Argument information

  public:
    bool is_match (const rvtt_insn_data *) const;
  };

  struct Deferred;

  // Combiner -- a set of patterns to match and a set of templates to replace
  // those with.  The templates are placed at the last pattern's
  // location. Patterns other than OtherUses are deleted
  struct Combiner {
    enum Tags : uint16_t;
    Shape const *shapes;
    uint8_t pats_hwm;
    uint8_t reps_hwm;

    uint8_t rep_lhs_hwm;
    uint8_t pat_var_hwm;
    uint8_t rep_var_hwm;

    uint8_t replace_mask; // patterns whose output is a replacement output
    uint8_t rep_use_mask; // patterns whose output is used in a replacement
    int8_t commute_bits;  // number of bits in the commute mask

    unsigned lineno; // line in rvtt.gc file
    bool is_deferred;
    Tags label;
    const char *tag; // the rule's gate ident ("" when ungated): the
		     // pin-stable dump witness (FH audit FHO-5/FHF-5)

    bool (*enable_hook) (); // combiner-specific emablement
    int (*pred_hook) (gcall *[], tree [], unsigned); // combiner-specific checks
    void (*init_hook) (gcall *[], tree [], unsigned); // combiner-specific initialization

  public:
    struct matched_data;
    int match (gcall *call, const rvtt_insn_data *insnd, matched_data &) const;
    void replace (gimple_stmt_iterator *, matched_data &, Deferred &) const;

  private:
    struct match_masks;
    bool match_init (unsigned ix, const Shape &, gcall *call, matched_data &matched, match_masks &masks) const;
    bool match_fini (const Shape &, const rvtt_insn_data *insnd, match_masks &masks) const;
    bool match_arg (const Shape &, int argno, basic_block bb, gcall *call,
		    const rvtt_insn_data *insnd, matched_data &matched, match_masks &masks) const;
    bool match_shape (unsigned ix, basic_block bb, gcall *call, const rvtt_insn_data *insnd,
		      matched_data &matched, match_masks &masks) const;
    bool match_check (matched_data &matched, match_masks const &masks) const;
  };
}

static bool moot_muli_addi_ok (gcall *call, bool never);
inline bool moot_muli_ok (gcall *call, bool never = false) { return moot_muli_addi_ok (call, never); }
inline bool moot_addi_ok (gcall *call, bool never = false) { return moot_muli_addi_ok (call, never); }

static bool ATTRIBUTE_UNUSED combiner_enable_false () { return false; }
static bool combiner_enable_WH () { return TARGET_XTT_TENSIX_WH; }
static bool combiner_enable_WH_BH () { return TARGET_XTT_TENSIX_WH_BH; }
static bool combiner_enable_BH () { return TARGET_XTT_TENSIX_BH; }
static bool combiner_enable_BH_QSR () { return TARGET_XTT_TENSIX_BH_QSR; }
static bool combiner_enable_QSR () { return TARGET_XTT_TENSIX_QSR; }
static bool combiner_enable_SETEXP_FOLD () { return riscv_tt_opt_setexp_fold; }
/* The reassociation license: BOTH halves of the key (owner
   ratification 2026-08-21) -- -fassociative-math (the generic industry
   opt-in to value-changing FP reassociation) AND our default-off
   -mtt-tensix-optimize-reassoc.  Guards the licensed multi-use
   mul+add->mad fusion pattern; with either flag absent the pattern is
   disabled and codegen is byte-identical.  */
static bool combiner_enable_REASSOC_FP () { return rvtt_reassoc_fp_licensed_p (); }

/* Used by the licensed multi-use mad-fuse pattern in rvtt.gc (defined
   below the generated include).  */
static bool has_other_use (tree var, gcall *allowed[], unsigned num_allowed);

/* ==================================================================
   THE LICENSED MAD RESTRUCTURE
   (-mtt-tensix-optimize-reassoc-mad-restructure) -- the
   trigonometry constrained-floor certificate's named "muli+add ->
   fused mad" value-changing successor, shipped as licensed
   COMBINE-PREFERENCE STEERING.

   The immediate-fold rules in rvtt.gc (loadi+mul -> SFPMULI,
   loadi+add -> SFPADDI) run "in preference to mul,add->mad" because
   absorbing the SFPLOADI immediately frees a register.  The cost of
   that preference is SERIALIZATION: the folded pair is TWO dependent
   MAD-subunit roundings (round(round(x*imm) + c)) where the sfpi
   contract's single-use mul+add->SFPMAD rule would deliver ONE
   partially-fused rounding (round(x*imm + c), tt-isa-documentation
   SFPMAD.md) -- on recurrence-bound rows that extra result-latency
   hop is the row's critical path (the trigonometry-fresh
   certificate's autopsy names the "exponent muli->add" and Newton
   "mul+addi" stall pairs by shape).

   Under BOTH license keys (-fassociative-math + the token) the two
   immediate-fold guards below VETO the fold exactly when the pair
   would instead fuse through the following contract mad rule; the
   combiner's own sweep then forms the SFPMAD.  Nothing is emitted
   here: the fuse itself is the audited default rule, so the only
   delta vs the unlicensed pipeline is WHICH rule wins -- word-neutral
   by construction (fold: muli+add or mul+addi = 2 words; suppressed:
   loadi+mad = 2 words), one dependent MAD-subunit hop shorter, one
   loadi live range longer (pressure-checked).

   Value-change class (why the license): double->single rounding on
   the fused product, plus the sign-of-zero flush SFPMULI's embedded
   "+0" performs and SFPMAD does not (a -0 product stays -0).  This is
   the ratified licensed-fold divergence family; with either key
   absent every candidate refuses by
   name (dump + registry) and codegen is byte-identical.  */

static bool
madr_licensed_pair_p (gcall *mul_call, gcall *add_call, const char *arm)
{
  basic_block bb = gimple_bb (mul_call);
  if (!bb || gimple_bb (add_call) != bb)
    return false;

  /* The pair must be exactly what the contract a*b+c rule consumes:
     the mul a feed shape of the mad rule per the GENERATED tables
     (no hand mirror), dying into the add.  */
  if (!rvtt_combine_will_fuse_p (mul_call, rvtt_insn_data::sfpmul_lv,
				 rvtt_insn_data::sfpadd_lv))
    return false;
  gcall *allowed[1] = { add_call };
  if (has_other_use (gimple_call_lhs (mul_call), allowed, 1))
    return false;

  /* License wall: both keys or refuse by name (the fold proceeds
     byte-identically).  */
  if (!flag_associative_math)
    {
      rvtt_refuse (RVTT_REF_ASSOCIATIVE_MATH_LICENSE_ABSENT, dump_file,
		   "reassoc: refusing mad restructure (%s immediate-fold "
		   "kept, bb %d) (associative-math-license-absent: re-"
		   "offering the pair to the singly-rounded SFPMAD contract "
		   "rule is value-changing; needs -fassociative-math AND "
		   "-mtt-tensix-optimize-reassoc-mad-restructure)\n",
		   arm, bb->index);
      return false;
    }

  /* Pressure: pointwise, suppressing the fold adds exactly ONE live
     value on the points strictly between the pair members (muli arm:
     the kept cst plus the extended multiplicand minus the dead
     product; addi arm: the extended mul operands minus the dead
     product; elsewhere the live sets are unchanged, the kept loadi
     included -- it is live up to the product in the PRE state this
     query measures).  Budget = the engine's windowed peak over
     (product, add] plus that one; a licensed transform must never
     make a compilable kernel uncompilable (the corpus lreg-pressure
     finding).  The whole-block peak would be dishonest here: it
     refuses every candidate in any block that merely touches the
     8-LREG file somewhere else (the trig body's wall).  */
  unsigned peak = rvtt_pressure_window_peak (mul_call, add_call);
  if (peak + 1 > rvtt_pressure_capacity ())
    {
      rvtt_refuse (RVTT_REF_REASSOC_PRESSURE_BUDGET_EXCEEDED, dump_file,
		   "reassoc: refusing mad restructure (%s immediate-fold "
		   "kept, bb %d) (reassoc-pressure-budget-exceeded: "
		   "conservative pair-window peak %u + the kept loadi live "
		   "range > 8 LREGs)\n",
		   arm, bb->index, peak);
      return false;
    }

  if (dump_file)
    fprintf (dump_file,
	     "reassoc: licensed mad restructure (%s immediate-fold "
	     "suppressed, bb %d): pair re-offered to the single-use "
	     "mul+add->SFPMAD contract rule -- double->single rounding on "
	     "the fused product, one dependent MAD-subunit result-latency "
	     "hop removed, word-neutral (loadi kept: %+" PRId64
	     " centislots, mul+add fused: %+" PRId64 " centislots) "
	     "(flag_associative_math && "
	     "-mtt-tensix-optimize-reassoc-mad-restructure)\n",
	     arm, bb->index,
	     rvtt_dcost_words_to_centislots
	       (1, rvtt_delivery_cost::PLANE_RISC_PUSH),
	     rvtt_dcost_words_to_centislots
	       (-1, rvtt_delivery_cost::PLANE_RISC_PUSH));
  return true;
}

/* Guard hook for the loadi+mul -> SFPMULI fold: MUL_CALL is the
   matched product statement.  True = veto the fold (the pair fuses
   through the contract mad rule later in this sweep).  */

static bool
madr_suppress_mul_fold_p (gcall *mul_call)
{
  if (riscv_tt_opt_reassoc_mad_restructure <= 0)
    return false;
  tree lhs = gimple_call_lhs (mul_call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
    return false;
  use_operand_p use_p;
  gimple *use_stmt;
  if (!single_imm_use (lhs, &use_p, &use_stmt))
    return false;
  gcall *add_call = dyn_cast<gcall *> (use_stmt);
  if (!add_call)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (add_call);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpadd
	  && insnd->id != rvtt_insn_data::sfpadd_lv))
    return false;
  /* The product must feed a VALUE operand, not an _lv lane victim.  */
  unsigned base = insnd->id == rvtt_insn_data::sfpadd_lv ? 1 : 0;
  if (gimple_call_arg (add_call, base) != lhs
      && gimple_call_arg (add_call, base + 1) != lhs)
    return false;
  return madr_licensed_pair_p (mul_call, add_call, "muli");
}

/* Guard hook for the loadi+add -> SFPADDI fold: OTHER is the add's
   non-immediate operand, ADD_CALL the matched add.  True = veto the
   fold (the pair fuses through the contract mad rule with the loadi
   as the mad's addend).  */

static bool
madr_suppress_add_fold_p (tree other, gcall *add_call)
{
  if (riscv_tt_opt_reassoc_mad_restructure <= 0)
    return false;
  if (TREE_CODE (other) != SSA_NAME)
    return false;
  gcall *mul_call = dyn_cast<gcall *> (SSA_NAME_DEF_STMT (other));
  if (!mul_call)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (mul_call);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpmul
	  && insnd->id != rvtt_insn_data::sfpmul_lv))
    return false;
  return madr_licensed_pair_p (mul_call, add_call, "addi");
}

#define OU unsigned (Flags::OtherUses)
#define SA unsigned (Flags::SetAnywhere)
#define SR unsigned (Flags::SameRegion)
#include "rvtt-combine.inc"
#undef SR
#undef SA
#undef OU

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

/* The generated-vocabulary query.  Discovery
   passes used to hand-mirror which spellings the combiner walks (the
   madpair discovery vocabulary; the muli/addi immediate-fold
   vulnerability test).  Answering from the same generated tables the
   combiner fires deletes that drift channel: every future rvtt.gc
   vocabulary widening reaches the discoveries automatically.

   True when DEF matches the FEED_ID interior pattern (one whose LHS
   feeds a later pattern) of some rule whose FINAL pattern id is
   CONSUMER_ID: Shape::is_match on DEF's insn (the _lv pattern admits
   the non-lv spelling) and the matcher's own constant-operand test,
   with the non-lv argument shift, on every constant pattern operand.
   Enable gates and rule predicates are NOT consulted -- the vocabulary
   is the union over targets and licenses, exactly as the hand mirrors
   were; placement/use-count/CC discipline stay with the caller.  */

bool
rvtt_combine_will_fuse_p (gcall *def, rvtt_insn_data::insn_id feed_id,
			  rvtt_insn_data::insn_id consumer_id)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return false;

  for (const Combiner &comb : combiners)
    {
      if (comb.shapes[comb.pats_hwm - 1].id != consumer_id)
	continue;
      for (unsigned ix = 0; ix + 1 < comb.pats_hwm; ix++)
	{
	  const Shape &pat = comb.shapes[ix];
	  if (pat.id != feed_id || !pat.used_by_mask
	      || !pat.is_match (insnd))
	    continue;

	  bool ok = true;
	  for (unsigned argno = 0; ok && argno != pat.num_args; argno++)
	    {
	      /* Mirror Combiner::match_arg's argument seating.  */
	      unsigned lv_delta = 0;
	      if (insnd->id == pat.id)
		;
	      else if ((int) argno == insnd->live_arg ())
		continue;	/* the absent lane-carrier slot */
	      else if (argno)
		lv_delta = 1;

	      if (pat.args[argno].is_var)
		continue;
	      if (argno - lv_delta >= gimple_call_num_args (def))
		ok = false;
	      else
		{
		  tree arg = gimple_call_arg (def, argno - lv_delta);
		  ok = (TREE_CODE (arg) == INTEGER_CST
			&& TREE_INT_CST_LOW (arg) == pat.args[argno].val);
		}
	    }
	  if (ok)
	    return true;
	}
    }
  return false;
}

/* Is there a CC-setting intrinsic strictly between FIRST and LAST
   (same block)?  The coarse region test of the file comment.  */

static bool
has_cc_insn_between (gcall *first, gcall *last)
{
  for (auto gsi = gsi_for_stmt (first); *gsi != last; gsi_next (&gsi))
    if (auto *insnd = rvtt_get_insn_data (*gsi))
      if (insnd->sets_cc (as_a <gcall *> (*gsi)))
	return true;

  return false;
}

/* Does VAR have any non-debug use outside the NUM_ALLOWED statements
   of ALLOWED (the matched calls)?  */

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

/* Is VAR referenced by a statement strictly between BEGIN and END that
   is not one of the NUM_ALLOWED statements of ALLOWED?  */

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

struct Combiner::matched_data {
  gcall *calls[combiner_pats_hwm];
  gcall *replace[combiner_reps_hwm];
  tree vars[combiner_vars_hwm];
  const Combiner *combiner;
  unsigned deleted = 0;  // Will delete insn
  unsigned delete_last = 0; // Delete if this is last use
  unsigned commute_mask = 0; // Which shapes had commuted args
  int mooted = false; // Another combiner deleted a call we rewrite

  matched_data (const Combiner *c)
    : combiner (c) {};
};

namespace {
struct Deferred {
  std::vector<Combiner::matched_data> matches;
  std::multimap<gcall *, unsigned> call_map;
  std::unordered_map<unsigned, gcall *> synth_map;
  std::set<gassign *> add_map;

public:
  void clear () {
    matches.clear ();
    call_map.clear ();
    synth_map.clear ();
    add_map.clear ();
  }

  bool is_deferred (gcall *call, Combiner::Tags tag) {
    for (auto I = call_map.lower_bound (call);
	 I != call_map.end () && I->first == call;
	 ++I)
      if (!matches[I->second].mooted
	  && matches[I->second].combiner->label == tag)
	return true;
    return false;
  }
  void moot (gcall *call) {
    for (auto I = call_map.lower_bound (call);
	 I != call_map.end () && I->first == call;
	 ++I)
      if (matches[I->second].mooted >= 0)
	{
	  if (dump_file)
	    fprintf (dump_file, "Mooting deferral %u\n", I->second);
	  matches[I->second].mooted = true;
	}
  }
  unsigned defer (const Combiner::matched_data &match) {
    unsigned slot = matches.size ();
    matches.emplace_back (match);
    for (unsigned ix = match.combiner->pats_hwm; ix--;)
      call_map.insert ({match.calls[ix], slot});
    return slot;
  }

public:
  void record_synth (gcall *call) {
    unsigned id = TREE_INT_CST_LOW (gimple_call_arg (call, 0));
    auto [I, inserted] = synth_map.insert ({id, call});
    if (!inserted)
      I->second = nullptr;
  }

public:
  void record_muli_addi (gcall *call, const rvtt_insn_data *insnd)
  {
    auto var = gimple_call_arg (call, insnd->var_arg ());
    auto add = as_a <gassign *> (SSA_NAME_DEF_STMT (var));
    add_map.insert (add);
  }
  void preprocess_muli_addi ();
  void postprocess_muli_addi ();
};
}

struct Combiner::match_masks {
  unsigned calls = 0; // Which calls we matched
  unsigned vars = 0;  // Which vars we defined
  unsigned live = 0;  // Which calls were live values

public:
  operator bool () const { return calls != 0; }
  match_masks &operator |= (match_masks const &other) {
    calls |= other.calls;
    vars |= other.vars;
    live |= other.live;
    return *this;
  }
};

// Current register pressure
static int lreg_pressure;

// Deferred combines -- these might be mooted by later-discovered combines
static Deferred deferred;

// Mooting a deferred combine is ok, unless the register pressure is high and
// there is one to moot.
bool moot_muli_addi_ok (gcall *call, bool never)
{
  return !((never || lreg_pressure > 0)
	   && deferred.is_deferred (call, Combiner::T_MULI_ADDI));
}

bool
Combiner::match_init (unsigned ix, const Shape &pat, gcall *call, matched_data &matched, match_masks &masks) const
{
  gcc_checking_assert (!((1 << pat.lhs) & masks.vars));

  matched.calls[ix] = call;
  masks.calls |= 1 << ix;

  matched.vars[pat.lhs] = gimple_call_lhs (call);
  masks.vars |= 1u << pat.lhs;

  return true;
}

bool
Combiner::match_fini (const Shape &pat, const rvtt_insn_data *insnd, match_masks &masks) const
{
  if (insnd->is_live ())
    masks.live |= 1 << pat.args[insnd->live_arg ()].val;

  return true;
}

bool
Combiner::match_arg (const Shape &pat, int argno, basic_block bb,
		     gcall *call, const rvtt_insn_data *insnd,
		     matched_data &matched, match_masks &masks) const
{
  unsigned lv_delta = 0;
  if (insnd->id == pat.id)
    ;
  else if (argno == insnd->live_arg ())
    {
      matched.vars[pat.args[argno].val] = nullptr;
      masks.vars |= 1u << pat.args[argno].val;
      return true;
    }
  else if (argno)
    lv_delta = 1; // FIXME: Change if ever more than one LV

  auto &arg_info = pat.args[argno];
  if (pat.commute_arg && matched.commute_mask & (1 << pat.commute_bit))
    {
      // We're commuting
      if (argno == pat.commute_arg)
	argno--;
      else if (argno + 1 == pat.commute_arg)
	argno++;
    }
  auto arg = gimple_call_arg (call, argno - lv_delta);

  if (!arg_info.is_var)
    // A constant, must match
    return TREE_CODE (arg) == INTEGER_CST
      && TREE_INT_CST_LOW (arg) == arg_info.val;

  if ((1 << arg_info.val) & masks.vars)
    // An already-seen var, must match
    return matched.vars[arg_info.val] == arg;
  
  if (arg_info.val >= pats_hwm)
    {
      // A new free var, record it
      masks.vars |= 1u << arg_info.val;
      matched.vars[arg_info.val] = arg;
      return true;
    }

  // A new var supplied by an earlier insn, match that insn
  auto &next_pat = shapes[arg_info.val];
  auto *stmt = SSA_NAME_DEF_STMT (arg);
  auto *inner_insnd = rvtt_get_insn_data (stmt);
  if (!inner_insnd || !next_pat.is_match (inner_insnd))
    return false;

  // We only combine within a single BB -- it'd be nice to do better,
  // but then CC checking becomes much harder
  if (!(next_pat.flags & unsigned (Flags::SetAnywhere))
      && bb != gimple_bb (stmt))
    return false;

  return match_shape (arg_info.val, bb, as_a <gcall *> (stmt), inner_insnd,
		      matched, masks);
}

bool
Combiner::match_shape (unsigned ix, basic_block bb, gcall *call, const rvtt_insn_data *insnd,
		       matched_data &matched, match_masks &masks) const
{
  auto &pat = shapes[ix];
  if (!match_init (ix, pat, call, matched, masks))
    return false;
  for (int argno = 0; argno != pat.num_args; argno++)
    if (!match_arg (pat, argno, bb, call, insnd, matched, masks))
      return false;

  return match_fini (pat, insnd, masks);
}

// Expensive checks matching checks, defer to as late as possible

bool
Combiner::match_check (matched_data &matched, match_masks const &masks) const
{
  /* Re-evaluate enablement HERE, not once when the dispatch table was
     built.  A rule's enable_hook can read per-function state:
     combiner_enable_REASSOC_FP consults flag_associative_math, which GCC
     swaps for every function (an `optimize' attribute, a pragma, or an
     LTO partition can mix strict and associative functions in one
     compilation).  The table is built once per compilation, so a hook
     evaluated only at build time would let the FIRST function decide for
     every later one -- admitting value-changing fusion into a strict
     function, or withholding it from a licensed one, purely on
     compilation order.  */
  if (enable_hook && !enable_hook ())
    return false;
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
	      if (pat.flags & unsigned (Flags::OtherUses))
		{
		  if (!(rep_use_mask & (1 << ix)))
		    matched.delete_last |= 1 << ix;
		}
	      else if (has_other_use (lhs, &matched.calls[ix + 1], pats_hwm - (ix + 1)))
		return false;
	      else if (!(rep_use_mask & (1 << ix)))
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

  // If any non-lhs non-live var is the same as a deleted lhs, we're not a match
  for (unsigned ix = rep_lhs_hwm; ix != pat_var_hwm; ix++)
    if (auto v = matched.vars[ix])
      for (unsigned jx = pats_hwm; jx--; )
	if (v == matched.vars[jx]
	    && ((1 << jx) & (matched.deleted | matched.delete_last)))
	  {
	    // This non-lhs var matches a deleted (or replaced) lhs var
	    if (!((1 << ix) & masks.live))
	      return false;  // Not a live, not a match

	    if (!((1 << jx) & replace_mask))
	      {
		// Chase live to deleted insn's live input
		auto insnd = rvtt_get_insn_data (matched.calls[jx]);
		if (!insnd->is_live ())
		  return false;
		v = gimple_call_arg (matched.calls[jx], insnd->live_arg ());
		matched.vars[ix] = v;
		// We'll continue checking this in the next iteration
	      }
	  }
  return true;
}

int
Combiner::match (gcall *call, const rvtt_insn_data *insnd, matched_data &matched) const
{
  for (; matched.commute_mask < (1u << commute_bits);
       matched.commute_mask++)
    {
      match_masks masks;
      if (!match_shape (pats_hwm - 1, gimple_bb (call), call, insnd, matched, masks))
	continue;

      gcc_checking_assert (masks.calls == ((1u << pats_hwm) - 1)
			   && masks.vars == (((1u << pats_hwm) - 1)
					     | (((1u << (pat_var_hwm - rep_lhs_hwm)) - 1) << rep_lhs_hwm)));
      int ok = pred_hook ? pred_hook (matched.calls, matched.vars, matched.commute_mask) : true;
      if (!ok)
	continue;
      if (!match_check (matched, masks))
	continue;
      // We have a match.

      // It's ok for any non-lhs vars to be the same
      for (unsigned ix = pats_hwm; ix != rep_lhs_hwm; ix++)
	matched.vars[ix] = nullptr;
      for (unsigned ix = pat_var_hwm; ix != rep_var_hwm; ix++)
	matched.vars[ix] = nullptr;

      if (dump_file)
	{
	  /* The parenthesized gate tag, when the rule has one, is the
	     PIN-STABLE witness (the line number shifts as rvtt.gc grows --
	     harness regexes should key the tag, e.g. "(SETEXP_FOLD)").  */
	  fprintf (dump_file, "Found pattern %u%s%s%s:\n", lineno,
		   *tag ? " (" : "", tag, *tag ? ")" : "");
	  for (unsigned ix = 0; ix != pats_hwm; ix++)
	    {
	      char c = 'K';
	      if ((1 << ix) & replace_mask)
		c = 'R';
	      else if ((1 << ix) & matched.deleted)
		c = 'D';
	      else if ((1 << ix) & matched.delete_last)
		c = 'L';

	      fprintf (dump_file, "%c ", c);
	      print_gimple_stmt (dump_file, matched.calls[ix], 2);
	    }
	}
      return ok;
    }

  return false;
}

void
Combiner::replace (gimple_stmt_iterator *gsi, matched_data &matched, Deferred &deferred) const
{
  if (init_hook)
    init_hook (matched.calls, matched.vars, matched.commute_mask);

  unsigned assign_mask = 0, assign_lv_mask = 0;;
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
	      insnd = insnd->get_non_live ();
	      lv_delta = 1;
	      if (insnd->id == rvtt_insn_data::sfpassign)
		assign_mask = 1 << rep.lhs;
	    }
	  else if (insnd->id == rvtt_insn_data::sfpassign_lv)
	    assign_lv_mask = 1 << rep.lhs;
	}
      gcc_checking_assert (insnd->num_args () + lv_delta == rep.num_args
			   && insnd->decl);
      auto *call = gimple_build_call (insnd->decl, insnd->num_args ());
      matched.replace[rep.lhs] = call;

      unsigned from_loc = rep.lhs;
      if (rep.lhs >= pats_hwm)
	{
	  matched.vars[rep.lhs]
	    = make_temp_ssa_name (TREE_TYPE (TREE_TYPE (insnd->decl)), nullptr, "cmb");
	  from_loc = pats_hwm - 1;
	}
      gimple_set_location (call, gimple_location (matched.calls[from_loc]));

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

  for (int ix = pats_hwm; ix--;)
    {
      if ((1 << ix) & matched.delete_last)
	{
	  auto lhs = gimple_call_lhs (matched.calls[ix]);
	  if (!has_zero_uses (lhs))
	    continue;
	  if (dump_file)
	    {
	      fprintf (dump_file, "Deleting now-unused ");
	      print_gimple_stmt (dump_file, matched.calls[ix], 0);
	    }
	}
      else if (!((1 << ix) & matched.deleted))
	continue;

      auto *call = matched.calls[ix];
      deferred.moot (call);
      auto gsi = gsi_for_stmt (call);
      unlink_stmt_vdef (call);
      gsi_remove (&gsi, true);
    }

  unlink_stmt_vdef (**gsi);
  gsi_remove (gsi, true);
  *gsi = gsi_for_stmt (matched.replace[shapes[reps_hwm - 1].lhs]);

  if (dump_file)
    {
      fprintf (dump_file, "Replaced with:\n");
      for (unsigned ix = pats_hwm; ix != reps_hwm; ix++)
	print_gimple_stmt (dump_file, matched.replace[shapes[ix].lhs], 2);
    }

  assign_mask |= assign_lv_mask;
  if (assign_mask)
    {
      // Replace sfpassign with nothing. We want to do this so that it exposes
      // other combines to us.
      for (unsigned ix = pats_hwm; ix != reps_hwm; ix++)
	{
	  auto const &shape = shapes[ix];
	  if ((1 << shape.lhs) & assign_mask)
	    {
	      auto *rep = matched.replace[shape.lhs];
	      auto lhs = gimple_call_lhs (rep);
	      if (lhs && ((1 << shape.lhs) & assign_lv_mask)
		  && gimple_call_arg (rep, 0) != gimple_call_arg (rep, 1))
		continue;

	      if (dump_file)
		{
		  fprintf (dump_file, "Eliding ");
		  print_gimple_stmt (dump_file, rep, 0);
		}
	      rvtt_substitute_value (lhs, gimple_call_arg (rep, 0));
	      unlink_stmt_vdef (rep);
	      if (**gsi == rep)
		gsi_remove (gsi, true);
	      else
		{
		  auto gsi = gsi_for_stmt (rep);
		  gsi_remove (&gsi, true);
		}
	    }
	}
    }
  if (dump_file)
    fprintf (dump_file, "\n");
}

// This array is sorted by builtin-id of the last pattern insn of a combiner,
// patterns for the same ID are sorted by priority (order in the GC file).
static std::vector<const Combiner *> combiner_map;
// This maps builtin ids to points in the combiner_map array.
static std::map<rvtt_insn_data::insn_id, std::vector<const Combiner *>::iterator> starting_ids;

/* Build the dispatch tables once: instantiate every enabled combiner
   rule, keyed (and prioritized by .gc line order) on the insn id of
   its anchor (last) pattern -- under both the _lv and non-_lv id.  */

static void
init ()
{
  // We always push rvtt_insnd_date::hwm
  if (!starting_ids.empty ())
    return;

  auto id2key = [](rvtt_insn_data::insn_id id, unsigned priority = 0) {
    return priority | unsigned (id) << 20;
  };
  std::map<unsigned, const Combiner *> tmp;

  /* Register every rule regardless of its enable_hook.  The hook is
     per-function and is evaluated in Combiner::match; filtering here would
     bake the first function's answer into a table built once per
     compilation.  */
  for (auto &combiner : combiners)
      {
	// Check all patterns and replacements have decls and correct number of arguments
	for (unsigned ix = combiner.reps_hwm; ix--;) {
	  auto const *insnd = rvtt_get_insn_data (combiner.shapes[ix].id);
	  gcc_checking_assert (insnd->decl && insnd->get_non_live ()->decl);
	  gcc_checking_assert (insnd->num_args () == combiner.shapes[ix].num_args);
	  if (!ix && combiner.is_deferred && combiner.label == Combiner::T_MULI_ADDI)
	    // The first pattern to match must be an sfploadi
	    gcc_checking_assert (insnd->id == rvtt_insn_data::sfploadi);
	}

	auto id = combiner.shapes[combiner.pats_hwm - 1].id;
	tmp.emplace (id2key (id, combiner.lineno), &combiner);
	auto nlv_id = rvtt_get_insn_data (id)->get_non_live ()->id;
	if (nlv_id != id)
	  tmp.emplace (id2key (nlv_id, combiner.lineno), &combiner);
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

// Check every dynamic MULI_ADDI combiner is simple.
void
Deferred::preprocess_muli_addi ()
{
  std::map<gcall *, std::pair<unsigned, const Combiner *>> loadis;
  for (unsigned ix = matches.size (); ix--;)
    {
      auto &match = matches[ix];
      if (match.combiner->label == Combiner::T_MULI_ADDI)
	{
	  auto *loadi_call = match.calls[0];
	  auto *loadi_insnd = rvtt_get_insn_data (loadi_call);
	  if (auto id = TREE_INT_CST_LOW (gimple_call_arg (loadi_call, loadi_insnd->id_arg ())))
	    loadis.insert ({loadi_call, {id, match.combiner}});
	}
    }

  for (auto &loadi_pair : loadis)
    {
      auto [loadi, pair] = loadi_pair;
      auto [id, combiner] = pair;

      auto *synth = synth_map.find (id)->second;
      if (!synth)
	{
	moot:
	  loadi_pair.second.first = 0;
	  continue;
	}

      use_operand_p use_p;
      imm_use_iterator iter;
      gimple *use_stmt;

      // Check the synth's lhs goes to one add, and that add's result goes to
      // this loadi.
      if (!single_imm_use (gimple_call_lhs (synth), &use_p, &use_stmt))
	goto moot;
      auto *assign = dyn_cast <gassign *> (use_stmt);
      if (!assign)
	goto moot;
      if (!single_imm_use (gimple_assign_lhs (assign), &use_p, &use_stmt))
	goto moot;
      if (use_stmt != loadi)
	goto moot;

      // Make sure every use of the loadi is to a non-mooted instance of this combiner
      FOR_EACH_IMM_USE_FAST (use_p, iter, gimple_call_lhs (loadi))
	{
	  gimple *g = USE_STMT (use_p);
	  if (is_gimple_debug (g))
	    continue;

	  gcall *call = dyn_cast <gcall *> (g);
	  auto I = call_map.lower_bound (call);
	  if (!(I != call_map.end ()
		&& I->first == call
		&& !matches[I->second].mooted
		&& matches[I->second].combiner == combiner))
	    goto moot;
	}
    }

  for (unsigned ix = matches.size (); ix--;)
    {
      auto &match = matches[ix];
      if (match.combiner->label == Combiner::T_MULI_ADDI)
	{
	  auto I = loadis.find (match.calls[0]);
	  if (I != loadis.end () && !I->second.first)
	    {
	      if (dump_file)
		fprintf (dump_file, "Mooting deferred %u due to synth complexity", ix);
	      match.mooted = true;
	    }
	}
    }
}

void
Deferred::postprocess_muli_addi ()
{
  for (auto *add : add_map)
    {
      // One input is from a synth and the other is the value, which we need to
      // convert from a loadi imm to a muli/addi imm.  Hard wire the shifts
      // here.
      constexpr unsigned left_shift = 8;

      auto arg = gimple_assign_rhs1 (add);
      bool is_second = rvtt_get_insn_data (SSA_NAME_DEF_STMT (arg));
      if (is_second)
	arg = gimple_assign_rhs2 (add);

      tree var = make_temp_ssa_name (TREE_TYPE (arg), nullptr, "shift");
      gassign *shift_stmt = gimple_build_assign (var, LSHIFT_EXPR, arg,
						 build_int_cst (unsigned_type_node, left_shift));
      gimple_set_location (shift_stmt, gimple_location (add));
      auto add_gsi = gsi_for_stmt (add);
      gsi_insert_before (&add_gsi, shift_stmt, GSI_SAME_STMT);
      if (is_second)
	gimple_assign_set_rhs2 (add, var);
      else
	gimple_assign_set_rhs1 (add, var);
      update_stmt (add);

      if (dump_file)
	{
	  fprintf (dump_file, "Inserting ");
	  print_gimple_stmt (dump_file, shift_stmt, 0);
	  fprintf (dump_file, "Before ");
	  print_gimple_stmt (dump_file, add, 0);
	  fprintf (dump_file, "\n");
	}
    }
}

/* Run one matching iteration over BB: at each statement try the rules
   anchored on its insn id in priority order, applying the first that
   matches and rescanning from the replacement.  Also records
   SYNTH_OPCODEs for the dynamic muli/addi bookkeeping.  Returns true
   if anything changed.  */

static bool
combine_block (Deferred &deferred, basic_block bb)
{
  bool changed = false;

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
    {
    again:;
      if (auto *insnd = rvtt_get_insn_data (*gsi))
	{
	  // Record synth_opcodes to deal with dynamic muli/addi combinations.
	  if (insnd->id == rvtt_insn_data::synth_opcode)
	    deferred.record_synth (as_a <gcall *> (*gsi));
	  else if (insnd->id == rvtt_insn_data::lreg_pressure)
	    {
	      // Argument is constrained to [0,1]
	      lreg_pressure += TREE_INT_CST_LOW (gimple_call_arg (*gsi, 0)) * 2 - 1;
	      if (dump_file)
		fprintf (dump_file, "Register pressure is now %d\n", lreg_pressure);
	    }
	  else
	    {
	      auto start = starting_ids.lower_bound (insnd->id);
	      // Because we've added insn_id::hwm, start will never be
	      // starting_ids.end ()
	      if (start->first == insnd->id)
		for (auto I = start->second, E = (++start)->second; I != E; ++I)
		  {
		    auto *combiner = *I;
		    Combiner::matched_data match (combiner);
		    if (auto found = combiner->match (as_a <gcall *> (*gsi), insnd, match))
		      {
			if (combiner->is_deferred || found < 0)
			  {
			    unsigned ix = deferred.defer (match);
			    if (dump_file)
			      fprintf (dump_file, "Deferment %u\n\n", ix);
			    if (combiner->is_deferred)
			      // Continue the loop because a later combiner might fire
			      continue;
			    break;
			  }
			else
			  {
			    combiner->replace (&gsi, match, deferred);
			    changed = true;
			    // Start over because another combiner might fire
			    goto again;
			  }
		      }
		  }
	    }
	}
    }

  return changed;
}

namespace {

const pass_data pass_data_rvtt_combine =
{
  GIMPLE_PASS, /* type */
  "rvtt_combine", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
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

    deferred.clear ();

    bool changed = false;

    // Walk the blocks in something like graph order.  With the exception of
    // loop back edges every block is walked after its predecessors.
    std::deque<std::pair<basic_block, int>> worklist;

    basic_block bb;
    FOR_ALL_BB_FN (bb, fn)
      bb->flags &= ~BB_VISITED;
    lreg_pressure = 0;

    basic_block entry = ENTRY_BLOCK_PTR_FOR_FN (fn);
    entry->flags |= BB_VISITED;
    worklist.emplace_back (entry, 0);

    while (!worklist.empty ())
      {
	auto &front = worklist.front ();
	auto bb = front.first;
	if (lreg_pressure != front.second)
	  {
	    lreg_pressure = front.second;
	    if (dump_file)
	      fprintf (dump_file, "New block's register pressure is %d\n", lreg_pressure);
	  }
	worklist.pop_front ();

	if (combine_block (deferred, bb))
	  changed = true;

	edge e;
	edge_iterator ei;
	FOR_EACH_EDGE (e, ei, bb->succs)
	  {
	    auto s = e->dest;
	    if (!(s->flags & BB_VISITED))
	      {
		s->flags |= BB_VISITED;
		worklist.emplace_back (s, lreg_pressure);
	      }
	  }
      }

    deferred.preprocess_muli_addi ();

    std::vector<unsigned> post_list;

    for (unsigned ix = 0; ix != deferred.matches.size (); ix++)
      {
	auto &match = deferred.matches[ix];
	if (!match.combiner->is_deferred)
	  {
	    // We deferred a non-deferred change.  Save it to apply later if
	    // it's not mooted
	    post_list.push_back (ix);
	    continue;
	  }
	if (dump_file)
	  fprintf (dump_file,
		   match.mooted ? "Deferment %u is moot\n\n"
		   : "Deferment %u:\n", ix);

	if (match.mooted)
	  continue;
	match.mooted = -1; // Avoid confusing self-mooting message

	auto gsi = gsi_for_stmt (match.calls[match.combiner->pats_hwm - 1]);
	match.combiner->replace (&gsi, match, deferred);

	if (match.combiner->label == Combiner::T_MULI_ADDI)
	  {
	    // If this is a dynamic constant, remember it
	    auto *call = match.replace[match.combiner->pats_hwm - 1];
	    auto *insnd = rvtt_get_insn_data (call);
	    if (TREE_INT_CST_LOW (gimple_call_arg (call, insnd->id_arg ())))
	      deferred.record_muli_addi (call, insnd);
	  }

	changed = true;
      }

    for (unsigned ix : post_list)
      {
	auto &match = deferred.matches[ix];
	if (dump_file)
	  fprintf (dump_file,
		   match.mooted ? "Non-defered %us is moot\n\n"
		   : "Non-deferred %u:\n", ix);
	if (match.mooted)
	  continue;
	match.mooted = -1;

	auto gsi = gsi_for_stmt (match.calls[match.combiner->pats_hwm - 1]);
	match.combiner->replace (&gsi, match, deferred);
      }

    deferred.postprocess_muli_addi ();

    return changed ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_combine (gcc::context *ctxt)
{
  return new pass_rvtt_combine (ctxt);
}
