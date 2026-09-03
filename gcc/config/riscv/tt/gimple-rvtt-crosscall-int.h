/* Private interface between the cross-call delivery units.
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* Shared data structures and cross-unit entry points of the
   cross-call constant-delivery pass (gimple-rvtt-crosscall.cc and
   the gimple-rvtt-crosscall-*.cc units split from it).  Include
   it after the gimple/ssa headers.  Everything here is private to
   those units  */

#ifndef GCC_GIMPLE_RVTT_CROSSCALL_INT_H
#define GCC_GIMPLE_RVTT_CROSSCALL_INT_H

struct global_census_entry
{
  bool stored_unknown = false;	 /* some store didn't fold / disagreed */
  bool assumed = false;		 /* some load assumed the initializer  */
};

/* A template-slot word that is a parameter-relative field load (the
   out-of-line ckernel_template::program shape): the word is resolved
   at every reachable call site instead, from the constant field
   stores that dominate the call.  */

struct slot_demand
{
  tree fndecl;			/* the demanding function	     */
  unsigned parm_index;		/* which argument carries the object */
  HOST_WIDE_INT offset;		/* field bit offset within it	     */
};

struct crosscall_tu_facts
{
  bool computed = false;
  /* The MOP template-file audit.  */
  bool slots_unproven = false;
  const char *slot_reason = nullptr;
  /* Refusal provenance for the slots_unproven verdict: every node whose
     body the census could not walk (already expanded / no gimple cfg),
     and whether ANY refusal other than body-unavailability fired
     (SLOT_REASON keeps only the first).  The init-hoist value-equality
     guard may excuse body-unavailability attributable solely to the
     contract subject itself -- whose delivered words its own planner
     audits -- and nothing else.  */
  hash_set<cgraph_node *> *unavailable_bodies = nullptr;
  bool slot_refusal_non_body = false;
  bool slot_replay = false;
  unsigned slot_loadi_dests = 0;   /* SFPLOADI destinations programmed
				      into instruction slots	       */
  vec<uint32_t> slot_words = vNULL; /* every audited slot word
				      (re-classified per proof face)    */
  hash_map<tree, global_census_entry> *globals = nullptr;
  vec<slot_demand> demands = vNULL;
  /* The executable closure and its direct roots (file header, [TU]).
     ENTRY_ROOTS are the closure roots themselves -- the functions the
     link image may enter from OUTSIDE the TU, whose call sites the TU
     therefore cannot enumerate.  CENSUS_UNROOTED records the
     fail-closed no-root verdict.  */
  hash_set<cgraph_node *> *executable = nullptr;
  hash_set<cgraph_node *> *entry_roots = nullptr;
  bool census_unrooted = false;
};

struct scan_ctx
{
  unsigned contract_mask;
  tree callee_decl;		/* the contract call target (caller scan);
				   NULL_TREE for the callee's own scan */
  bool in_caller = false;	/* which side this scan covers (names) */
  bool region = false;		/* audited hoist-region discipline (the
				   cross-loop hoist consumers): vector
				   dataflow is register-allocation
				   visible and admitted, side-effecting
				   typed calls beyond the explicit Dst
				   boundary set refuse, and delivered
				   SFPCONFIG words refuse */
  bool config_strict = false;	/* a config-prefix pair rides the
				   contract: delivered SFPCONFIG-class
				   words refuse (they could rewrite the
				   programmed constant register)       */
  bool cc_immaterial = false;	/* programming-only region discipline:
				   typed structured-CC atoms
				   are admitted -- the consumer's lifted
				   object executes before the region and
				   its parked constant-register state is
				   out of any CC write's reach; every
				   other discipline is unchanged        */
  bool cc_ambient_ok = false;	/* -mtt-tensix-optimize-cc-region-general:
				   the scanned
				   loop's CC activity is CC-region-tree
				   proven ambient-preserving-and-
				   narrowing (rvtt-cc-region.h,
				   loop_cc_ambient_preserving_p) -- the
				   enable set at every in-loop point is
				   a subset of the lifted entry's
				   ambient, so an all-lanes hoisted
				   materialization is a refinement (the
				   invariant pass's containment fact,
				   carried across the crossed loop);
				   typed structured-CC atoms are then
				   admitted under the cc_immaterial
				   whitelist discipline               */
  bool saw_mop = false;
  const char *why = nullptr;
  gimple *why_stmt = nullptr;
};

/* The TU census facts (computed once by compute_tu_facts; defined in
   gimple-rvtt-crosscall.cc).  */
extern crosscall_tu_facts tu_facts;

/* gimple-rvtt-crosscall.cc */
extern bool vector_typed_p (tree t);
extern bool call_has_vector_dataflow_p (gcall *call);
extern bool pushed_word_base (tree val, uint32_t *base, unsigned depth = 0);
extern bool pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
				      unsigned depth = 0);
extern bool ref_constant_address (tree ref, unsigned HOST_WIDE_INT *addr);
extern bool blocking_store_asm_p (const gasm *stmt, tree *value, tree *addr);
extern void compute_tu_facts ();
extern bool mop_contract_ok_p (unsigned contract_mask, const char **why);
extern bool audited_scalar_asm_p (const char *s);
extern void insert_in_preheader (basic_block ph, gimple *stmt);
extern bool scan_stmt (scan_ctx *ctx, gimple *stmt, bool in_caller);

#endif /* GCC_GIMPLE_RVTT_CROSSCALL_INT_H */
