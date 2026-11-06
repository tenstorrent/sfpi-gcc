/* Representation propagation for Tensix SFPU value webs.
   Copyright (C) 2026 Tenstorrent Inc.

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

/* Representation-conversion cancellation (-mtt-tensix-optimize-repr-prop,
   default off).

   On Blackhole a DataLayout::SM32 Dst access lowers to a raw INT32
   SFPLOAD/SFPSTORE plus an explicit SFPCAST that converts between
   two's-complement and sign-magnitude.  When a value only travels from
   such a conversion through predicated merges, PHIs and copies to the
   inverse conversion, the pair composes to the identity and both casts
   can be deleted.

   A web is the SSA closure made of:

     sources   conversion calls whose (arch, mod) is a bit involution
	       (c(c(x)) == x for every 32-bit pattern, see
	       repr_involution_p);
     interior  statements that pass one data operand's bits through
	       unmodified per lane: PHIs, COND_EXPRs whose condition is
	       not a web value, SSA copies, and sfpassign/sfpassign_lv;
     sinks     conversion calls with the same mod consuming a web value.

   If every use of every web value is interior, a sink or a debug use,
   and every interior operand enters through a source, then each lane
   sees sink (choose (source (x))) == x, so deleting sources and sinks is
   bit-exact.  Predication does not matter: a choose passes each lane
   the bits of one input, and lanes a predicated cast did not write are
   already unspecified under the pure-builtin model.  No LREG live range
   is created or extended.

   Pairs that are value-exact but not bit-exact (e.g. canonicalizing
   maps on the two zeros) are not handled.  Any unproven case edits
   nothing and refuses with one of: repr-conversion-unaudited,
   repr-web-kind-mismatch, repr-web-consumer-not-transparent,
   repr-web-leaf-unproven.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-into-ssa.h"
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "cfghooks.h"
#include "tree-cfg.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-refuse.h"

namespace {

/* Return true if CALL (an rvtt_sfpcast, described by INSND) is a bit
   involution on the current arch, storing its constant mod in
   *MOD_OUT.

   The only such case is BH SFPCAST mod1=3, the sign-preserving
   conditional negate between two's-complement and sign-magnitude int32.
   It is self-inverse for every 32-bit pattern; the corner value
   0x80000000 (sign-magnitude -0) is a fixed point (SFPCAST.md).  BH
   mod1=2 (the cast-as-ABS hardware bug) is not.  QSR's directional
   encodings would need separate inverse pairs and are not handled; WH
   has no int<->int cast insn.  */

static bool
repr_involution_p (const rvtt_insn_data *insnd, const gcall *call,
		   unsigned HOST_WIDE_INT *mod_out)
{
  if (insnd->id != rvtt_insn_data::sfpcast)
    return false;
  tree mod = gimple_call_arg (call, 1);
  if (TREE_CODE (mod) != INTEGER_CST || !tree_fits_uhwi_p (mod))
    return false;
  *mod_out = tree_to_uhwi (mod);
  return TARGET_XTT_TENSIX_BH && *mod_out == 3;
}

/* Dump helper: named refusal, optionally with the offending
   statement.  Refusals never edit anything.  */

static void
repr_refuse (const char *reason, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "repr-prop: refused (%s)", reason);
  if (!dump_file)
    return;
  if (stmt)
    {
      fprintf (dump_file, ": ");
      print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
    }
  else
    fprintf (dump_file, "\n");
}

/* One web-cancellation attempt seeded at the audited conversion call
   SEED.  On a complete proof, rewrites and returns true; on any
   refusal, edits nothing and returns false.  Either way the seed's
   fellow sources are added to ATTEMPTED so a web is only attempted
   (and dump-logged) once.  */

class repr_web
{
public:
  repr_web (unsigned HOST_WIDE_INT mod, hash_set<gimple *> *attempted)
    : m_mod (mod), m_attempted (attempted), m_failed (false) {}

  bool build (gcall *seed);
  void rewrite ();

  unsigned n_sources () const { return m_sources.length (); }
  unsigned n_sinks () const { return m_sinks.length (); }
  unsigned n_chooses () const { return m_chooses.elements (); }

private:
  bool require_entrant (tree v, gimple *why);
  bool admit_choose (gimple *stmt);
  bool check_uses (tree v);
  static bool cond_choose_p (gimple *stmt);
  bool fail (const char *reason, gimple *stmt)
  {
    repr_refuse (reason, stmt);
    m_failed = true;
    return false;
  }

  unsigned HOST_WIDE_INT m_mod;
  hash_set<gimple *> *m_attempted;
  bool m_failed;
  hash_set<tree> m_web;		    /* SSA names carrying converted bits.  */
  auto_vec<tree> m_queue;	    /* web members pending a use check.  */
  auto_vec<gcall *> m_sources;
  auto_vec<gcall *> m_sinks;
  hash_set<gimple *> m_sink_set;
  hash_set<gimple *> m_source_set;
  hash_set<gimple *> m_chooses;
};

/* V must carry converted bits produced inside the web: through a
   source conversion, an interior choose already admitted, or a copy.
   WHY is the statement that demanded it (for refusal dumps).  */

bool
repr_web::require_entrant (tree v, gimple *why)
{
  if (TREE_CODE (v) != SSA_NAME
      /* replace_uses_by is invalid on abnormal edges.  */
      || SSA_NAME_OCCURS_IN_ABNORMAL_PHI (v))
    return fail ("repr-web-leaf-unproven", why);
  if (m_web.contains (v))
    return true;

  gimple *def = SSA_NAME_DEF_STMT (v);
  if (!def || gimple_nop_p (def))
    return fail ("repr-web-leaf-unproven", why);

  if (const rvtt_insn_data *insnd = rvtt_get_insn_data (def))
    {
      gcall *call = as_a <gcall *> (def);
      if (insnd->id == rvtt_insn_data::sfpcast)
	{
	  unsigned HOST_WIDE_INT mod;
	  if (!repr_involution_p (insnd, call, &mod))
	    return fail (TREE_CODE (gimple_call_arg (call, 1)) == INTEGER_CST
			 ? "repr-web-kind-mismatch"
			 : "repr-conversion-unaudited", def);
	  if (mod != m_mod)
	    return fail ("repr-web-kind-mismatch", def);
	  if (m_sink_set.contains (def))
	    return fail ("repr-web-kind-mismatch", def);
	  m_sources.safe_push (call);
	  m_source_set.add (def);
	  m_web.add (v);
	  m_queue.safe_push (v);
	  return true;
	}
      if (insnd->id == rvtt_insn_data::sfpassign
	  || insnd->id == rvtt_insn_data::sfpassign_lv)
	return admit_choose (def);
      return fail ("repr-web-leaf-unproven", def);
    }

  if (gimple_code (def) == GIMPLE_PHI)
    return admit_choose (def);
  if (cond_choose_p (def))
    return admit_choose (def);
  if (gimple_assign_ssa_name_copy_p (def))
    {
      if (!require_entrant (gimple_assign_rhs1 (def), def))
	return false;
      m_web.add (v);
      m_queue.safe_push (v);
      return true;
    }
  return fail ("repr-web-leaf-unproven", def);
}

/* Return true if STMT is a COND_EXPR assignment.  Only its two arms are
   data operands; check_uses refuses a web value used in the
   condition.  */

bool
repr_web::cond_choose_p (gimple *stmt)
{
  gassign *assign = dyn_cast <gassign *> (stmt);
  return assign && gimple_assign_rhs_code (assign) == COND_EXPR;
}

/* STMT is a PHI or an audited choose call: its result joins the web
   and every data operand must enter through the web.  */

bool
repr_web::admit_choose (gimple *stmt)
{
  if (m_chooses.contains (stmt))
    return true;
  tree lhs;
  if (gimple_code (stmt) == GIMPLE_PHI)
    lhs = gimple_phi_result (stmt);
  else if (cond_choose_p (stmt))
    lhs = gimple_assign_lhs (stmt);
  else
    lhs = gimple_call_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME
      || SSA_NAME_OCCURS_IN_ABNORMAL_PHI (lhs))
    return fail ("repr-web-consumer-not-transparent", stmt);
  m_chooses.add (stmt);
  m_web.add (lhs);
  m_queue.safe_push (lhs);
  if (gimple_code (stmt) == GIMPLE_PHI)
    {
      gphi *phi = as_a <gphi *> (stmt);
      for (unsigned i = 0; i < gimple_phi_num_args (phi); i++)
	if (!require_entrant (gimple_phi_arg_def (phi, i), stmt))
	  return false;
    }
  else if (cond_choose_p (stmt))
    {
      /* Only the two arms are data operands; the condition is not
	 part of the web.  */
      if (!require_entrant (gimple_assign_rhs2 (stmt), stmt)
	  || !require_entrant (gimple_assign_rhs3 (stmt), stmt))
	return false;
    }
  else
    {
      gcall *call = as_a <gcall *> (stmt);
      for (unsigned i = 0; i < gimple_call_num_args (call); i++)
	if (!require_entrant (gimple_call_arg (call, i), stmt))
	  return false;
    }
  return true;
}

/* Every use of web member V must be transparent or a matching
   sink.  */

bool
repr_web::check_uses (tree v)
{
  imm_use_iterator iter;
  gimple *use_stmt;
  FOR_EACH_IMM_USE_STMT (use_stmt, iter, v)
    {
      if (is_gimple_debug (use_stmt))
	continue;
      if (gimple_code (use_stmt) == GIMPLE_PHI)
	{
	  if (!admit_choose (use_stmt))
	    return false;
	  continue;
	}
      if (const rvtt_insn_data *insnd = rvtt_get_insn_data (use_stmt))
	{
	  gcall *call = as_a <gcall *> (use_stmt);
	  if (insnd->id == rvtt_insn_data::sfpcast)
	    {
	      unsigned HOST_WIDE_INT mod;
	      if (!repr_involution_p (insnd, call, &mod) || mod != m_mod)
		return fail ("repr-web-kind-mismatch", use_stmt);
	      if (m_source_set.contains (use_stmt))
		return fail ("repr-web-kind-mismatch", use_stmt);
	      tree slhs = gimple_call_lhs (call);
	      if (slhs && TREE_CODE (slhs) == SSA_NAME
		  && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (slhs))
		return fail ("repr-web-consumer-not-transparent", use_stmt);
	      if (!m_sink_set.add (use_stmt))
		m_sinks.safe_push (call);
	      continue;
	    }
	  if (insnd->id == rvtt_insn_data::sfpassign
	      || insnd->id == rvtt_insn_data::sfpassign_lv)
	    {
	      if (!admit_choose (use_stmt))
		return false;
	      continue;
	    }
	  return fail ("repr-web-consumer-not-transparent", use_stmt);
	}
      if (cond_choose_p (use_stmt))
	{
	  /* Admit only when V is used as an arm; a (type-invalid, but
	     defensively checked) appearance in the condition is not a
	     choose of V's bits.  */
	  if (gimple_assign_rhs2 (use_stmt) != v
	      && gimple_assign_rhs3 (use_stmt) != v)
	    return fail ("repr-web-consumer-not-transparent", use_stmt);
	  tree c = gimple_assign_rhs1 (use_stmt);
	  if (c == v
	      || (COMPARISON_CLASS_P (c)
		  && (TREE_OPERAND (c, 0) == v || TREE_OPERAND (c, 1) == v)))
	    return fail ("repr-web-consumer-not-transparent", use_stmt);
	  if (!admit_choose (use_stmt))
	    return false;
	  continue;
	}
      if (gimple_assign_ssa_name_copy_p (use_stmt))
	{
	  tree lhs = gimple_assign_lhs (use_stmt);
	  if (TREE_CODE (lhs) != SSA_NAME
	      || SSA_NAME_OCCURS_IN_ABNORMAL_PHI (lhs))
	    return fail ("repr-web-consumer-not-transparent", use_stmt);
	  if (!m_web.contains (lhs))
	    {
	      m_web.add (lhs);
	      m_queue.safe_push (lhs);
	    }
	  continue;
	}
      return fail ("repr-web-consumer-not-transparent", use_stmt);
    }
  return true;
}

/* Build the web from SEED.  Returns true iff the whole proof
   succeeded and the web has at least one sink to cancel.  */

bool
repr_web::build (gcall *seed)
{
  tree lhs = gimple_call_lhs (seed);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME
      || SSA_NAME_OCCURS_IN_ABNORMAL_PHI (lhs))
    return false;		/* Dead cast; DCE territory.  */
  m_sources.safe_push (seed);
  m_source_set.add (seed);
  m_web.add (lhs);
  m_queue.safe_push (lhs);

  /* The queue only ever grows with values newly added to m_web, so
     this terminates.  */
  for (unsigned i = 0; i < m_queue.length () && !m_failed; i++)
    check_uses (m_queue[i]);

  /* Sources are decided by this attempt either way.  A sink of a failed
     web may still seed a valid web downstream, so sinks are only marked
     attempted when the web is rewritten.  */
  for (unsigned i = 0; i < m_sources.length (); i++)
    m_attempted->add (m_sources[i]);

  bool ok = !m_failed && n_sinks () > 0;
  if (ok)
    for (unsigned i = 0; i < m_sinks.length (); i++)
      m_attempted->add (m_sinks[i]);
  return ok;
}

/* Rewrite the web: route each sink's uses to the sink's input, then
   each source's uses to the source's input, deleting both.  Interior
   statements keep their SSA names and now carry raw bits.  */

void
repr_web::rewrite ()
{
  for (unsigned i = 0; i < m_sinks.length (); i++)
    {
      gcall *sink = m_sinks[i];
      tree lhs = gimple_call_lhs (sink);
      if (lhs)
	replace_uses_by (lhs, gimple_call_arg (sink, 0));
      rvtt_prep_stmt_for_deletion (sink);
      unlink_stmt_vdef (sink);
      gimple_stmt_iterator gsi = gsi_for_stmt (sink);
      gsi_remove (&gsi, true);
      release_defs (sink);
    }
  for (unsigned i = 0; i < m_sources.length (); i++)
    {
      gcall *src = m_sources[i];
      tree lhs = gimple_call_lhs (src);
      replace_uses_by (lhs, gimple_call_arg (src, 0));
      rvtt_prep_stmt_for_deletion (src);
      unlink_stmt_vdef (src);
      gimple_stmt_iterator gsi = gsi_for_stmt (src);
      gsi_remove (&gsi, true);
      release_defs (src);
    }
}

/* Pass body: seed a web attempt at every audited conversion call not
   yet attempted.  */

static bool
transform (function *fn)
{
  bool changed = false;
  hash_set<gimple *> attempted;
  auto_vec<gcall *> candidates;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	if (!insnd || insnd->id != rvtt_insn_data::sfpcast)
	  continue;
	gcall *call = as_a <gcall *> (stmt);
	unsigned HOST_WIDE_INT mod;
	if (!repr_involution_p (insnd, call, &mod))
	  {
	    repr_refuse ("repr-conversion-unaudited", stmt);
	    continue;
	  }
	candidates.safe_push (call);
      }

  for (unsigned i = 0; i < candidates.length (); i++)
    {
      gcall *seed = candidates[i];
      if (attempted.contains (seed))
	continue;
      unsigned HOST_WIDE_INT mod;
      repr_involution_p (rvtt_get_insn_data (seed), seed, &mod);
      repr_web web (mod, &attempted);
      if (web.build (seed))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "repr-prop: cancelled web (%u sources, %u chooses, "
		     "%u sinks)\n",
		     web.n_sources (), web.n_chooses (), web.n_sinks ());
	  web.rewrite ();
	  changed = true;
	}
    }
  return changed;
}

const pass_data pass_data_rvtt_reprprop =
{
  GIMPLE_PASS,
  "rvtt_reprprop",
  OPTGROUP_OTHER,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_reprprop : public gimple_opt_pass
{
public:
  pass_rvtt_reprprop (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_reprprop, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_repr_prop;
  }

  unsigned execute (function *fn) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	rvtt_refuse (RVTT_REF_QSR_UNPROVEN, dump_file,
		     "repr-prop: refused (qsr-unproven)\n");
	return 0;
      }
    bool changed = transform (fn);
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the pass.  It runs after CSE/DCE have merged duplicate
   conversions and before pass_rvtt_vif lowers the structured CC
   forms.  */

gimple_opt_pass *
make_pass_rvtt_reprprop (gcc::context *ctxt)
{
  return new pass_rvtt_reprprop (ctxt);
}
