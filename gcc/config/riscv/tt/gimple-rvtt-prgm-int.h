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

/* Internal interface of the PRGM constant pass, shared between
   gimple-rvtt-prgm-const.cc and the gimple-rvtt-prgm-*.cc units.
   Include it after the gimple and SSA headers.  */

#ifndef GCC_GIMPLE_RVTT_PRGM_INT_H
#define GCC_GIMPLE_RVTT_PRGM_INT_H

/* Translation-unit-wide facts about SFPCONFIG destinations.  */

struct prgm_tu_facts
{
  bool computed = false;
  bool refused = false;
  const char *reason = nullptr;
  /* SFPCONFIG destinations 0..15 written anywhere in the TU.  */
  unsigned claimed = 0;
  /* Destinations whose every TU write is a typed staged SFPCONFIG of
     the same 32-bit constant, given in VALUE.  A writer from a raw
     word, a template or an underivable staging chain clears the bit.  A
     residency candidate with that value may reuse the register: every
     write stores the same value, so no ordering proof across functions
     is needed.  */
  unsigned value_known = 0;	/* bitmask over destinations */
  uint32_t value[16] = {};
  /* Constant registers read anywhere in the TU by sfpreadlreg with a
     constant index; a non-constant index sets every bit.  Constant
     registers are read only through sfpreadlreg (sfpi vConst* lowers to
     it): the raw-word table has no creg-reading word and
     sfprawlreg_access covers only the allocatable file.  A claimed
     destination with a clear bit has no reader, so overwriting it with
     a different value is unobservable; this lets a hoisted candidate
     reclaim it.  Readers added by such reclaims are dominated by their
     own programming and are not recorded here.  */
  unsigned creg_read = 0;
  /* MOP template derivation facts (rvtt-mop-derive.h).  */
  rvtt_mop_derive_state mop;
};

/* PRGM hard-LREG indices (sfpi CREG_IDX_PRGM1..3 == SFPCONFIG dests).
   Index 11 (PRGM0) is the architectural -1.0 special case and is never
   allocated.  */
static const unsigned prgm_regs[] = { 12, 13, 14 };

/* A constant materialization chain: TAIL defines the value, ROOT is the
   first issue (equal to TAIL for single-issue chains).  */

struct remat_chain
{
  gcall *tail;			/* defines the candidate value */
  gcall *root;			/* == tail for single-issue chains */
};

/* Residency allocation state shared by the fusion and residency
   classes: SFPCONFIG destination claims and the table of allocated
   values.  */

struct prgm_alloc { unsigned value; unsigned reg; basic_block bb;
		    /* The slot was reclaimed from a TU writer of a
		       different value, so the value does not persist
		       across calls: a later candidate with the same value
		       must prove its own call-free window and always
		       reprograms.  */
		    bool reclaimed = false;
		    /* The programming is at the end of BB rather than at
		       its entry or in place, so it is visible in every
		       block strictly dominated by BB.  */
		    bool at_end = false; };

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
