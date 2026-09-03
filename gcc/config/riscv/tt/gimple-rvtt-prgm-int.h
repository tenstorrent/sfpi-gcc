/* Private interface between the PRGM constant-programming units.
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

/* Shared data structures and cross-unit entry points of the PRGM
   constant pass (gimple-rvtt-prgm-const.cc and the
   gimple-rvtt-prgm-*.cc units split from it).  Include it after
   the gimple/ssa headers.  Everything here is private to those
   units  */

#ifndef GCC_GIMPLE_RVTT_PRGM_INT_H
#define GCC_GIMPLE_RVTT_PRGM_INT_H

struct prgm_tu_facts
{
  bool computed = false;
  bool refused = false;
  const char *reason = nullptr;
  /* SFPCONFIG destinations 0..15 written anywhere in the TU.  */
  unsigned claimed = 0;
  /* Per-destination unique programmed value, when every TU write to
     that destination derives to the SAME 32-bit constant image
     (typed staged SFPCONFIG writes only; any claim from a raw word, a
     template, or an underivable staging chain clears the bit).  A
     residency candidate whose value equals a destination's unique TU
     value may REUSE that claimed register: every write anywhere
     stores the same value, and the candidate's own all-lanes
     programming makes the register hold it in every lane at every
     later point regardless of write order or the other writes' lane
     masks (value idempotence -- no cross-function ordering proof is
     needed or used).  */
  unsigned value_known = 0;	/* bitmask over destinations */
  uint32_t value[16] = {};
  /* Constant registers READ anywhere in the TU (typed sfpreadlreg with
     a constant index; a non-constant index poisons every bit).  The
     typed vocabulary reaches constant registers only through
     sfpreadlreg (sfpi vConst* reads lower to it), the audited raw-word
     table admits no creg-reading word, and sfprawlreg_access masks
     cover the allocatable file only -- so a clear bit here is a
     TU-wide no-reader proof for that destination.  A claimed
     destination nobody reads is a DEAD claim: overwriting it with a
     different value is unobservable, which is what lets the
     hoisted-reuse class reclaim an occupied slot whose unique TU value
     does not match (the tanh-fitted anatomy: the shared op init
     programs the hand polynomial's constants; the fitted kernel needs
     its own).  Readers ADDED by such reclaims are dominated by their
     own in-function programming and never enter this mask.  */
  unsigned creg_read = 0;
  /* The MOP template derivation facts (rvtt-mop-derive.h).  */
  rvtt_mop_derive_state mop;
};

/* PRGM hard-LREG indices (sfpi CREG_IDX_PRGM1..3 == SFPCONFIG dests).
   Index 11 (PRGM0) is the architectural -1.0 special case and is never
   allocated.  */
static const unsigned prgm_regs[] = { 12, 13, 14 };

struct remat_chain
{
  gcall *tail;			/* defines the candidate value */
  gcall *root;			/* == tail for single-issue chains */
};

/* ------------------------------------------------------------------ */
/* Residency allocation state shared between the M3 fusion class and
   the residency classes: SFPCONFIG destination claims and the
   identical-value allocation table.  */

struct prgm_alloc { unsigned value; unsigned reg; basic_block bb;
		    /* The slot was DEAD-claim reclaimed: a foreign TU
		       writer of a DIFFERENT value exists, so value
		       persistence across any call is unprovable -- a
		       later same-value candidate must re-prove its own
		       call-free window and always reprograms.  */
		    bool reclaimed = false; };

struct prgm_state
{
  bool initialized = false;
  unsigned claimed = 0;
  auto_vec<prgm_alloc, 4> allocs;
};

/* gimple-rvtt-prgm-const.cc (the TU facts scan) */
extern const prgm_tu_facts &tu_prgm_facts ();

/* gimple-rvtt-prgm-fuse.cc */
extern bool single_nondebug_use_p (tree value, gimple *expected);
extern unsigned madpair_value_base (const rvtt_insn_data *insnd);
extern gcall *madpair_vocab_mul_p (tree src, class loop *loop,
				   gimple *only_use, unsigned *value_base);
extern gcall *hoisted_madpair_load_p (tree src, class loop *loop,
				      gimple *only_use, unsigned *value,
				      bool *vulnerable, bool *shared);
extern void collect_cc_writers (function *fn, auto_vec<gimple *> *out);
extern bool cc_write_reaches_point_p (const auto_vec<gimple *> &writers,
				      basic_block point_bb,
				      gimple *point_stmt);
extern bool transform (function *fn, prgm_state *st);

/* gimple-rvtt-prgm-remat.cc */
extern tree loadi_lv_link (gcall *call);
extern bool remat_chain_p (tree name, remat_chain *out);
extern bool constant_chain_value_p (const remat_chain &c, unsigned *value);
extern bool single_issue_constant_image_p (gcall *load, unsigned *value);
extern bool staged_config_value (tree staged, unsigned *value);
extern bool remat_consumer_audited_p (gimple *stmt, tree name);
extern bool remat_transform (function *fn);

/* gimple-rvtt-prgm-residency.cc */
extern bool residency_transform (function *fn, prgm_state *st);

#endif /* GCC_GIMPLE_RVTT_PRGM_INT_H */
