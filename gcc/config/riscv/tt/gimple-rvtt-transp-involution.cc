/* Transpose-involution formation and Dst-park elision.
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

/* Keep values live in L4-L7 across a pair of transposes
   (-mtt-tensix-optimize-transp-involution, default off).

   SFPTRANSP permutes both four-register banks (SFPTRANSP.md:
   Transpose4(0) and Transpose4(4)), and rvtt_sfptransp_int models all
   eight registers, so no value can stay in L4-L7 across a transpose.
   Kernels therefore park live values in Dst around every transpose.

   One transpose scatters a register's value across its whole bank, but
   two transposes compose to the identity (Transpose4 is an involution)
   provided only full-bank L0-L3 definitions lie between them.  This
   pass forms that TRANSP / loads / TRANSP idiom:

     r = sfptransp (a, b, c, d, e, f, g, h)
				  where a..d are single-use results of
				  four constant-address, no-increment,
				  same-format SFPLOADs in this block and
				  only sfpselect8 (r, 0..3) are used
       ==>
     q = sfptransp_gather (addr0..3, mod0, addr_mode, 0)
				  with each sfpselect8 (r, i) replaced
				  by sfpselect4 (q, i)

   a single multi-word insn (SFPTRANSP; 4x SFPLOAD; SFPTRANSP, pattern
   rvtt_sfptransp_gather_int) that leaves L4-L7 unchanged, so the
   register allocator may keep up to four values live across it.
   Because it is one insn, no spill or scheduled insn can land inside.

   The pair is an involution only with all lanes enabled: with a partial
   lane mask some swaps happen one way only and the loads do not fully
   overwrite L0-L3.  The pass therefore requires a dominating all-lanes
   SFPENCC with only lane-state-preserving statements in between, or
   emits that SFPENCC itself at the head of the group.  A function with
   any other typed CC writer is refused (transp-involution-cc-region).

   With the bundle formed, a Dst park around it

     sfpstore (v, A, M, noinc); ... bundle ...; w = sfpload (A, M, noinc)

   is removed by forwarding v to w's uses, provided M round-trips
   exactly (park_format_bit_exact_p), v cannot be a nonzero denormal
   if M's store flushes them, nothing in between overlaps A's Dst rows
   or has RWC, configuration or raw-word effects, both ends have all
   lanes enabled, and at most four values are live across the bundle.  Park
   stores whose loads were all forwarded are deleted when a later store
   to the same address overwrites them; the final stores remain, so the
   Dst state seen outside is unchanged.

   Hardware references: SFPTRANSP.md, SFPLOAD.md and SFPSTORE.md, and
   the corresponding routines of the reference simulator.  */

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-ssanames.h"
#include "tree-cfg.h"
#include "cfghooks.h"
#include "cfganal.h"
#include "dominance.h"
#include "fold-const.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-macro-tables.h"
#include "tree-dfa.h"

namespace {

/* ------------------------------------------------------------------ */
/* Statement classification.  */

enum stmt_class
{
  SC_SKIP,	/* debug / label / nop: no code			      */
  SC_SAFE,	/* no Dst access, no RWC, no config write, no
		   CC write, no hidden (raw) LREG write		      */
  SC_DST_READ,	/* typed constant-address Dst load (access known)     */
  SC_DST_WRITE,	/* typed constant-address Dst store (access known)    */
  SC_GATHER,	/* an already-formed involution bundle: four known
		   Dst reads; lane-state preserving		      */
  SC_ENCC_ALL,	/* all-lanes SFPENCC: forces the lane state	      */
  SC_CC_WRITE,	/* any other typed CC writer: refuses the function    */
  SC_BARRIER,	/* everything else				      */
};

struct dst_access
{
  unsigned addr;
  unsigned mod0;
  unsigned addr_mode;
  tree value;		/* stored value (SC_DST_WRITE only)  */
};
/* Parse a typed constant-address Dst access.  sfpload args:
   (instrn_ptr, addr, var, id, mod0, addr_mode); sfpstore args:
   (instrn_ptr, value, addr, var, id, mod0, addr_mode).  Only the fully
   static shape (null instruction pointer, zero var/id synth operands,
   constant fields) is accepted.  */

static bool
parse_dst_access (gcall *call, const rvtt_insn_data *insnd, dst_access *acc)
{
  bool store = insnd->id == rvtt_insn_data::sfpstore;
  unsigned base = store ? 1 : 0;
  tree ptr = gimple_call_arg (call, 0);
  if (!integer_zerop (ptr))
    return false;
  unsigned var, id;
  if (!rvtt_call_const_uarg (call, base + 1, &acc->addr)
      || !rvtt_call_const_uarg (call, base + 2, &var) || var != 0
      || !rvtt_call_const_uarg (call, base + 3, &id) || id != 0
      || !rvtt_call_const_uarg (call, base + 4, &acc->mod0)
      || !rvtt_call_const_uarg (call, base + 5, &acc->addr_mode))
    return false;
  acc->value = store ? gimple_call_arg (call, 1) : NULL_TREE;
  return true;
}

struct classified
{
  stmt_class cls;
  dst_access acc;		/* SC_DST_READ / SC_DST_WRITE  */
  unsigned gather_addr[4];	/* SC_GATHER		       */
  unsigned gather_mod0;
};

/* Classify STMT into the stmt_class values above, defaulting to
   SC_BARRIER.  For a constant Dst access also return the access in
   ACC; for a formed gather bundle, the four addresses and the format
   in GATHER_ADDR/GATHER_MOD0.  */

static classified
classify_stmt (gimple *stmt)
{
  classified c;
  c.cls = SC_BARRIER;

  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_NOP
      || gimple_code (stmt) == GIMPLE_PREDICT)
    {
      c.cls = SC_SKIP;
      return c;
    }

  if (gasm *a = dyn_cast <gasm *> (stmt))
    {
      /* An empty template emits nothing (the memory-clobber idiom).  Any
	 other asm is a barrier: raw words may have hidden LREG/CC/Dst
	 effects.  */
      const char *s = gimple_asm_string (a);
      while (s && (*s == ' ' || *s == '\t'))
	++s;
      if (s && !*s)
	c.cls = SC_SAFE;
      return c;
    }

  if (gimple_code (stmt) == GIMPLE_COND || gimple_code (stmt) == GIMPLE_PHI)
    {
      c.cls = SC_SAFE;		/* scalar control flow: no Tensix word */
      return c;
    }

  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    {
      /* Plain gimple: scalar/SSA data flow, which cannot reach the SFPU
	 register file, lane state, RWC or Dst.  */
      c.cls = SC_SAFE;
      return c;
    }

  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    {
      if (gimple_call_internal_p (call))
	{
	  c.cls = SC_SAFE;
	  return c;
	}
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && fndecl_built_in_p (fndecl))
	{
	  c.cls = SC_SAFE;	/* scalar compiler builtin */
	  return c;
	}
      return c;			/* real call: barrier */
    }

  if (insnd->id == rvtt_insn_data::sfptransp_gather)
    {
      gcall *g = call;
      unsigned mode;
      bool ok = true;
      for (unsigned i = 0; i != 4; ++i)
	ok &= rvtt_call_const_uarg (g, i, &c.gather_addr[i]);
      ok &= rvtt_call_const_uarg (g, 4, &c.gather_mod0);
      ok &= rvtt_call_const_uarg (g, 5, &mode);
      c.cls = ok ? SC_GATHER : SC_BARRIER;
      return c;
    }

  if (rvtt_encc_all_lanes_call_p (call, insnd))
    {
      c.cls = SC_ENCC_ALL;
      return c;
    }

  if (rvtt_cc_writer_id_p (insnd->id) || insnd->sets_cc (call))
    {
      c.cls = SC_CC_WRITE;
      return c;
    }

  if (insnd->id == rvtt_insn_data::sfpload
      || insnd->id == rvtt_insn_data::sfpstore)
    {
      if (parse_dst_access (call, insnd, &c.acc))
	c.cls = insnd->id == rvtt_insn_data::sfpstore
		? SC_DST_WRITE : SC_DST_READ;
      return c;
    }

  if (rvtt_dst_inert_compute_id_p (insnd->id))
    {
      c.cls = SC_SAFE;
      return c;
    }

  return c;			/* default: barrier */
}

/* ------------------------------------------------------------------ */
/* Dst physical-row model (BH/WH).

   An access at constant address A touches lane rows r in
   [A & ~3, (A & ~3) + 3].  The 32-bit format class maps lane row r to
   physical rows adj32(r) and adj32(r)+8 with
   adj32(r) = ((r & 0x1F8) << 1) | (r & 0x207) (the simulator's
   dst32b_adjust_row; the BH remap/swizzle debug bits are assumed
   clear).  The 16-bit class maps r to itself or, when the 32-bit
   layout is configured, through the 32-bit path, so its row set is the
   union of both.  Formats resolved by configuration (mod0 0) also take
   the union.  The result is a superset, used only to show
   disjointness.  */

static void
access_rows (const dst_access &acc, std::vector<unsigned> *rows)
{
  unsigned r0 = acc.addr & ~3u;
  bool m32 = acc.mod0 == 3 || acc.mod0 == 4 || acc.mod0 == 7
	     || acc.mod0 == 9 || acc.mod0 == 12 || acc.mod0 == 10;
  bool m16 = !m32;		/* incl. mod0 0: config-resolved, union */
  if (acc.mod0 == 0)
    m32 = true;
  for (unsigned r = r0; r != r0 + 4; ++r)
    {
      unsigned lane_row = r & 1023;
      if (m32 || m16)
	{
	  unsigned adj = ((lane_row & 0x1F8) << 1) | (lane_row & 0x207);
	  if (m32)
	    {
	      rows->push_back (adj & 1023);
	      rows->push_back ((adj + 8) & 1023);
	    }
	  if (m16)
	    {
	      rows->push_back (lane_row);    /* 16-bit layout */
	      rows->push_back (adj & 1023);  /* 32-bit layout via dst16->32 */
	      rows->push_back ((adj + 8) & 1023);
	    }
	}
    }
}

/* Whether the physical-row sets of accesses A and B (see access_rows
   above) are disjoint, so the accesses cannot overlap under any layout
   configuration.  */

static bool
rows_disjoint (const dst_access &a, const dst_access &b)
{
  std::vector<unsigned> ra, rb;
  access_rows (a, &ra);
  access_rows (b, &rb);
  for (unsigned x : ra)
    if (std::find (rb.begin (), rb.end (), x) != rb.end ())
      return false;
  return true;
}

/* ------------------------------------------------------------------ */
/* Park formats that round-trip exactly.

   mod0 == 4 (INT32 class): the store writes encode_fp32(value) and the
   load returns decode_fp32(stored), exact inverse bit permutations, so
   every 32-bit value round-trips.

   mod0 == 3 (FP32): the same codec pair, but on BH the store first
   flushes nonzero denormals, so the round trip is exact only if the
   value cannot be a nonzero denormal.  WH's store does not flush; the
   check is applied there too, which is merely conservative.

   Every other format is partial, lossy or layout-dependent.  */

static bool
park_format_bit_exact_p (unsigned mod0, bool *needs_denormal_proof)
{
  if (mod0 == 4)
    {
      *needs_denormal_proof = false;
      return true;
    }
  if (mod0 == 3)
    {
      *needs_denormal_proof = true;
      return true;
    }
  return false;
}

/* Return true if VAL cannot be a nonzero fp32 denormal.  Recognized
   producers:
   - FMA-family arithmetic, which flushes denormal inputs and outputs;
   - the fixed constant registers 8/9/10 (0x3F56594B / 0 / 0x3F800000);
     the programmable constants 11..14 are not accepted;
   - literal loads whose fp32 image is not a nonzero denormal.  */

static bool
value_never_denormal_p (tree val, unsigned depth = 0)
{
  if (depth > 4 || TREE_CODE (val) != SSA_NAME)
    return false;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (val));
  if (!def)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return false;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
      return true;
    case rvtt_insn_data::sfpreadlreg:
      {
	unsigned reg;
	return rvtt_call_const_uarg (def, 0, &reg)
	       && (reg == 8 || reg == 9 || reg == 10);
      }
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
      /* Pass-through of its (last) source.  */
      return value_never_denormal_p
	(gimple_call_arg (def, gimple_call_num_args (def) - 1), depth + 1);
    default:
      return false;
    }
}

/* ------------------------------------------------------------------ */

struct candidate
{
  gcall *transp;		/* the sfptransp call		*/
  gcall *defs[4];		/* the four sfpload defs	*/
  unsigned addr[4];
  unsigned mod0, addr_mode;
  gimple *group_head;		/* earliest stmt of the contiguous
				   lane-safe run before the transp */
  bool needs_encc;
};

/* Statements are compared by uid.  Uids are assigned once and only
   pre-existing statements in the same block are compared.  */

/* Match one transpose call against the involution shape.  */

static bool
match_candidate (gcall *transp, candidate *cand, const char **why)
{
  *why = "transp-involution-operand-shape";
  if (gimple_call_num_args (transp) != 8 || !gimple_call_lhs (transp))
    return false;
  /* The bank-1 results are discarded: every use selects L0-L3.  */
  {
    imm_use_iterator it;
    gimple *use;
    FOR_EACH_IMM_USE_STMT (use, it, gimple_call_lhs (transp))
      {
	if (is_gimple_debug (use))
	  continue;
	gcall *sel = dyn_cast <gcall *> (use);
	const rvtt_insn_data *sd = sel ? rvtt_get_insn_data (sel) : nullptr;
	unsigned idx;
	if (!sd || sd->id != rvtt_insn_data::sfpselect8
	    || !rvtt_call_const_uarg (sel, 1, &idx) || idx > 3)
	  return false;
      }
  }
  int noinc = rvtt_no_increment_address_mode ();
  if (noinc < 0)
    {
      *why = "transp-involution-unsupported-target";
      return false;
    }
  basic_block bb = gimple_bb (transp);
  for (unsigned i = 0; i != 4; ++i)
    {
      tree arg = gimple_call_arg (transp, i);
      if (TREE_CODE (arg) != SSA_NAME || !has_single_use (arg))
	return false;
      gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (arg));
      if (!def || gimple_bb (def) != bb)
	return false;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
      if (!insnd || insnd->id != rvtt_insn_data::sfpload)
	return false;
      dst_access acc;
      if (!parse_dst_access (def, insnd, &acc))
	return false;
      if ((int) acc.addr_mode != noinc)
	return false;
      cand->defs[i] = def;
      cand->addr[i] = acc.addr;
      if (i == 0)
	cand->mod0 = acc.mod0;
      else if (acc.mod0 != cand->mod0)
	return false;
    }
  cand->addr_mode = (unsigned) noinc;

  /* The loads sink to the transpose: everything strictly between the
     earliest def and the transpose must be a sibling def or a statement
     with no lane, Dst, RWC, config or raw-word effect.  */
  gimple *first = cand->defs[0];
  for (unsigned i = 1; i != 4; ++i)
    if (gimple_uid (cand->defs[i]) < gimple_uid (first))
      first = cand->defs[i];
  *why = "transp-involution-window";
  for (gimple_stmt_iterator gsi = gsi_for_stmt (first);
       gsi_stmt (gsi) != (gimple *) transp; gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == cand->defs[0] || stmt == cand->defs[1]
	  || stmt == cand->defs[2] || stmt == cand->defs[3])
	continue;
      classified c = classify_stmt (stmt);
      if (c.cls != SC_SKIP && c.cls != SC_SAFE)
	return false;
    }

  /* Group head: the start of the contiguous run of
     lane-state-preserving statements before the first def, where an
     emitted all-lanes enable covers the whole group, park stores
     included.  */
  gimple *head = first;
  for (gimple_stmt_iterator gsi = gsi_for_stmt (first);;)
    {
      if (gsi_stmt (gsi) == gsi_start_bb (bb).ptr)
	break;
      gsi_prev (&gsi);
      classified c = classify_stmt (gsi_stmt (gsi));
      if (c.cls == SC_SKIP || c.cls == SC_SAFE || c.cls == SC_DST_READ
	  || c.cls == SC_DST_WRITE || c.cls == SC_GATHER)
	head = gsi_stmt (gsi);
      else
	break;
    }
  cand->group_head = head;
  cand->transp = transp;
  *why = nullptr;
  return true;
}

/* ------------------------------------------------------------------ */

class involution_transform
{
public:
  involution_transform (function *fn) : m_fn (fn) {}
  bool run ();

private:
  void find_candidates ();
  void compute_lane_states ();
  gcall *rewrite_candidate (candidate &cand);
  void forward_parks (gcall *bundle, basic_block bb);
  void dse_parks (basic_block bb);
  unsigned live_across_count (gimple *site);

  function *m_fn;
  std::vector<candidate> m_cands;
  /* Lane state known all-lanes at BB entry?  */
  auto_bitmap m_clean_in;
};

/* Find all formable transposes.  */

void
involution_transform::find_candidates ()
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	if (!call)
	  continue;
	const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	if (!insnd || insnd->id != rvtt_insn_data::sfptransp)
	  continue;
	candidate cand;
	const char *why;
	if (match_candidate (call, &cand, &why))
	  {
	    if (dump_file)
	      fprintf (dump_file, "transp-involution: forming bundle at uid %u "
		       "(addrs %u %u %u %u, mod0 %u)\n",
		       gimple_uid (call), cand.addr[0], cand.addr[1],
		       cand.addr[2], cand.addr[3], cand.mod0);
	    m_cands.push_back (cand);
	  }
	else
	  if (dump_file)
	    fprintf (dump_file, "transp-involution: refusing transpose at uid %u (%s)\n",
		     gimple_uid (call), why);
      }
}

/* Forward lane-state dataflow: CLEAN means all lanes enabled since an
   all-lanes SFPENCC (typed, or one this pass will emit), through
   lane-state-preserving statements only.  Entry is DIRTY.  Candidate
   sites are CLEAN, either by inheritance or because the rewrite emits
   the enable at their group head.  */

void
involution_transform::compute_lane_states ()
{
  bool changed = true;
  while (changed)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, m_fn)
	{
	  bool in_clean = false;
	  if (bb != ENTRY_BLOCK_PTR_FOR_FN (m_fn)
	      && EDGE_COUNT (bb->preds) > 0)
	    {
	      in_clean = true;
	      edge e;
	      edge_iterator ei;
	      FOR_EACH_EDGE (e, ei, bb->preds)
		if (e->src == ENTRY_BLOCK_PTR_FOR_FN (m_fn)
		    || !bitmap_bit_p (m_clean_in, e->src->index))
		  {
		    /* Predecessor's OUT: recompute below; we store OUT
		       in the same bitmap keyed by src, updated when the
		       src block is walked.  */
		    in_clean = false;
		    break;
		  }
	    }
	  bool state = in_clean;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      bool is_site = false;
	      for (const candidate &cand : m_cands)
		if (cand.transp == stmt)
		  {
		    is_site = true;
		    break;
		  }
	      if (is_site)
		{
		  state = true;	/* forced or inherited */
		  continue;
		}
	      classified c = classify_stmt (stmt);
	      switch (c.cls)
		{
		case SC_SKIP: case SC_SAFE: case SC_DST_READ:
		case SC_DST_WRITE: case SC_GATHER:
		  break;
		case SC_ENCC_ALL:
		  state = true;
		  break;
		default:
		  state = false;
		  break;
		}
	    }
	  if (state != bitmap_bit_p (m_clean_in, bb->index))
	    {
	      if (state)
		bitmap_set_bit (m_clean_in, bb->index);
	      else
		bitmap_clear_bit (m_clean_in, bb->index);
	      changed = true;
	    }
	}
    }
  /* m_clean_in now holds each block's OUT state; IN is recomputed from
     preds when consumed (walk_state_to below).  */
}

/* Lane state immediately before STMT in BB (using the fixpoint OUT
   states of the predecessors).  */

static bool
in_state_of_bb (function *fn, basic_block bb, bitmap outs)
{
  if (bb == ENTRY_BLOCK_PTR_FOR_FN (fn) || EDGE_COUNT (bb->preds) == 0)
    return false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn)
	|| !bitmap_bit_p (outs, e->src->index))
      return false;
  return true;
}

/* Count 32-lane vector SSA values live across SITE (def before, some
   use after or in another block).  The companion bank holds four.  */

unsigned
involution_transform::live_across_count (gimple *site)
{
  basic_block bb = gimple_bb (site);
  unsigned site_uid = gimple_uid (site);
  unsigned count = 0;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (gimple_uid (stmt) >= site_uid)
	break;
      tree lhs = gimple_get_lhs (stmt);
      if (!lhs || TREE_CODE (lhs) != SSA_NAME)
	continue;
      if (TYPE_MODE (TREE_TYPE (lhs)) != XTT32SImode)
	continue;
      imm_use_iterator it;
      gimple *use;
      bool live = false;
      FOR_EACH_IMM_USE_STMT (use, it, lhs)
	if (gimple_bb (use) != bb || gimple_uid (use) > site_uid)
	  {
	    live = true;
	    break;
	  }
      count += live;
    }
  return count;
}

/* Replace the matched transpose with the fused bundle; delete the
   consumed loads; emit the all-lanes enable at the group head when the
   reaching lane state is not known to be all-lanes.  */

gcall *
involution_transform::rewrite_candidate (candidate &cand)
{
  if (cand.needs_encc)
    {
      const rvtt_insn_data *encc = rvtt_get_insn_data (rvtt_insn_data::sfpencc);
      gcall *e = gimple_build_call
	(encc->decl, 2,
	 build_int_cst (unsigned_type_node, SFPENCC_MOD1_EI_RI),
	 build_int_cst (unsigned_type_node, SFPENCC_IMM12_BOTH));
      gimple_set_location (e, gimple_location (cand.transp));
      gimple_stmt_iterator hgsi = gsi_for_stmt (cand.group_head);
      gsi_insert_before (&hgsi, e, GSI_SAME_STMT);
      gimple_set_uid (e, gimple_uid (cand.group_head));
      update_stmt (e);
      if (dump_file)
	fprintf (dump_file, "transp-involution: materialized all-lanes enable before "
		 "uid %u\n", gimple_uid (cand.group_head));
    }

  const rvtt_insn_data *gather
    = rvtt_get_insn_data (rvtt_insn_data::sfptransp_gather);
  gcall *bundle = gimple_build_call
    (gather->decl, 6,
     build_int_cst (unsigned_type_node, cand.addr[0]),
     build_int_cst (unsigned_type_node, cand.addr[1]),
     build_int_cst (unsigned_type_node, cand.addr[2]),
     build_int_cst (unsigned_type_node, cand.addr[3]),
     build_int_cst (unsigned_type_node, cand.mod0),
     build_int_cst (unsigned_type_node, cand.addr_mode));
  tree quad = make_ssa_name (TREE_TYPE (TREE_TYPE (gather->decl)));
  gimple_call_set_lhs (bundle, quad);
  gimple_set_location (bundle, gimple_location (cand.transp));

  /* Each sfpselect8 (r, i) becomes sfpselect4 (q, i).  */
  tree select4 = rvtt_get_insn_data (rvtt_insn_data::sfpselect4)->decl;
  auto_vec<gimple *> selects;
  {
    imm_use_iterator it;
    gimple *use;
    FOR_EACH_IMM_USE_STMT (use, it, gimple_call_lhs (cand.transp))
      if (!is_gimple_debug (use))
	selects.safe_push (use);
  }
  for (gimple *use : selects)
    {
      gcall *sel = as_a <gcall *> (use);
      gcall *rep = gimple_build_call (select4, 2, quad,
				      gimple_call_arg (sel, 1));
      gimple_call_set_lhs (rep, gimple_call_lhs (sel));
      gimple_set_location (rep, gimple_location (sel));
      gimple_stmt_iterator sgsi = gsi_for_stmt (sel);
      unsigned suid = gimple_uid (sel);
      gsi_replace (&sgsi, rep, false);
      gimple_set_uid (rep, suid);
      update_stmt (rep);
    }

  gimple_stmt_iterator gsi = gsi_for_stmt (cand.transp);
  gimple_set_uid (bundle, gimple_uid (cand.transp));
  gsi_insert_before (&gsi, bundle, GSI_SAME_STMT);
  update_stmt (bundle);
  rvtt_prep_stmt_for_deletion (cand.transp);
  gsi_remove (&gsi, true);

  for (unsigned i = 0; i != 4; ++i)
    {
      gimple_stmt_iterator dgsi = gsi_for_stmt (cand.defs[i]);
      rvtt_prep_stmt_for_deletion (cand.defs[i]);
      gsi_remove (&dgsi, true);
    }
  return bundle;
}

/* Park forwarding around one formed bundle.  */

void
involution_transform::forward_parks (gcall *bundle, basic_block bb)
{
  unsigned site_uid = gimple_uid (bundle);

  /* The lane state must be all-lanes at the stores and loads.  The
     group-head enable (or inherited CLEAN state) covers the contiguous
     group, so walk back from the bundle collecting park stores while
     every statement crossed preserves the lane state; an SC_ENCC_ALL on
     the way makes everything after it CLEAN.  */

  struct pending { dst_access acc; gimple *stmt; };
  std::vector<pending> stores;

  bool state_proven_here = in_state_of_bb (m_fn, bb, m_clean_in);
  /* Statement-level: walk the block from the top tracking the state up
     to the bundle, collecting candidate park stores in the contiguous
     safe run before it.  */
  std::vector<pending> run_stores;
  bool state = state_proven_here;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == (gimple *) bundle)
	break;
      classified c = classify_stmt (stmt);
      switch (c.cls)
	{
	case SC_ENCC_ALL:
	  state = true;
	  run_stores.clear ();
	  break;
	case SC_SKIP:
	case SC_SAFE:
	  break;
	case SC_DST_READ:
	  break;
	case SC_GATHER:
	  break;
	case SC_DST_WRITE:
	  if (state)
	    run_stores.push_back ({ c.acc, stmt });
	  break;
	default:
	  state = false;
	  run_stores.clear ();
	  break;
	}
    }
  if (!state)
    {
      if (dump_file)
	fprintf (dump_file, "transp-park: no proven all-lanes state before bundle uid %u "
		 "(transp-park-lane-state)\n", site_uid);
      return;
    }
  stores = run_stores;
  if (stores.empty ())
    return;

  /* Between each collected store and the bundle there must be no Dst
     write overlapping the store's rows and no barrier; the walk above
     only accepted reads, safe statements and other stores, so check
     write overlap now.  */
  auto overlap_write = [&] (const dst_access &park, gimple *from)
  {
    for (gimple_stmt_iterator gsi = gsi_for_stmt (from);; )
      {
	gsi_next (&gsi);
	if (gsi_end_p (gsi) || gsi_stmt (gsi) == (gimple *) bundle)
	  return false;
	classified c = classify_stmt (gsi_stmt (gsi));
	if (c.cls == SC_DST_WRITE && !rows_disjoint (c.acc, park))
	  return true;
      }
  };

  /* Pressure guard: values made live across the bundle must fit the
     four-register companion bank together with everything already live
     across it.  */
  unsigned base_live = live_across_count (bundle);

  unsigned forwarded = 0;
  for (pending &p : stores)
    {
      bool needs_denorm;
      if (!park_format_bit_exact_p (p.acc.mod0, &needs_denorm))
	{
	  if (dump_file)
	    fprintf (dump_file, "transp-park: store uid %u addr %u refused "
		     "(transp-park-format mod0 %u)\n",
		     gimple_uid (p.stmt), p.acc.addr, p.acc.mod0);
	  continue;
	}
      if (needs_denorm && !value_never_denormal_p (p.acc.value))
	{
	  if (dump_file)
	    fprintf (dump_file, "transp-park: store uid %u addr %u refused "
		     "(transp-park-denormal-unproven)\n",
		     gimple_uid (p.stmt), p.acc.addr);
	  continue;
	}
      if (overlap_write (p.acc, p.stmt))
	{
	  if (dump_file)
	    fprintf (dump_file, "transp-park: store uid %u addr %u refused "
		     "(transp-park-window: overlapping write)\n",
		     gimple_uid (p.stmt), p.acc.addr);
	  continue;
	}
      if (base_live + forwarded + 1 > 4)
	{
	  if (dump_file)
	    fprintf (dump_file, "transp-park: store uid %u addr %u refused "
		     "(transp-park-pressure: %u live + %u forwarded)\n",
		     gimple_uid (p.stmt), p.acc.addr, base_live, forwarded);
	  continue;
	}

      /* Find the matching unpark load after the bundle: same address,
	 same format, no-increment mode, with only safe or disjoint
	 statements between the bundle and the load.  */
      gcall *load = nullptr;
      bool blocked = false;
      for (gimple_stmt_iterator gsi = gsi_for_stmt ((gimple *) bundle);
	   !blocked && !load; )
	{
	  gsi_next (&gsi);
	  if (gsi_end_p (gsi))
	    break;
	  gimple *stmt = gsi_stmt (gsi);
	  classified c = classify_stmt (stmt);
	  switch (c.cls)
	    {
	    case SC_SKIP: case SC_SAFE:
	      break;
	    case SC_ENCC_ALL:
	      break;		/* re-forcing all-lanes is state-preserving
				   for a full-register read */
	    case SC_DST_READ:
	      if (c.acc.addr == p.acc.addr && c.acc.mod0 == p.acc.mod0
		  && c.acc.addr_mode == p.acc.addr_mode)
		load = as_a <gcall *> (stmt);
	      break;
	    case SC_DST_WRITE:
	      if (!rows_disjoint (c.acc, p.acc))
		blocked = true;
	      break;
	    case SC_GATHER:
	      {
		/* Reads only; reads never block forwarding.  */
		break;
	      }
	    default:
	      blocked = true;
	      break;
	    }
	}
      if (!load || !gimple_call_lhs (load))
	{
	  if (dump_file)
	    fprintf (dump_file, "transp-park: store uid %u addr %u kept "
		     "(transp-park-window: no matching unpark load)\n",
		     gimple_uid (p.stmt), p.acc.addr);
	  continue;
	}

      tree lhs = gimple_call_lhs (load);
      if (dump_file)
	fprintf (dump_file, "transp-park: forwarding store uid %u -> load uid %u "
		 "(addr %u mod0 %u)\n", gimple_uid (p.stmt), gimple_uid (load),
		 p.acc.addr, p.acc.mod0);
      replace_uses_by (lhs, p.acc.value);
      gimple_stmt_iterator lgsi = gsi_for_stmt (load);
      rvtt_prep_stmt_for_deletion (load);
      gsi_remove (&lgsi, true);
      forwarded++;
    }
}

/* Same-block dead park store elimination: a constant-address store
   dies when a later store of the same address and format overwrites it
   with no possibly overlapping Dst read, RWC or lane-state event, or
   barrier in between.  The later store must have all lanes enabled, or
   it would not fully overwrite.  */

void
involution_transform::dse_parks (basic_block bb)
{
  bool state = in_state_of_bb (m_fn, bb, m_clean_in);
  /* Collect (stmt, class) linearly with running lane state at each
     store.  */
  struct entry { gimple *stmt; classified c; bool state_after; };
  std::vector<entry> line;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      classified c = classify_stmt (stmt);
      switch (c.cls)
	{
	case SC_ENCC_ALL: case SC_GATHER:
	  state = c.cls == SC_ENCC_ALL ? true : state;
	  break;
	case SC_SKIP: case SC_SAFE: case SC_DST_READ: case SC_DST_WRITE:
	  break;
	default:
	  state = false;
	  break;
	}
      line.push_back ({ stmt, c, state });
    }

  for (unsigned i = 0; i != line.size (); ++i)
    {
      if (line[i].c.cls != SC_DST_WRITE)
	continue;
      const dst_access &acc = line[i].c.acc;
      bool needs_denorm;
      if (!park_format_bit_exact_p (acc.mod0, &needs_denorm))
	continue;		/* only exact 32-bit-class parks */
      for (unsigned j = i + 1; j != line.size (); ++j)
	{
	  const classified &c = line[j].c;
	  if (c.cls == SC_SKIP || c.cls == SC_SAFE || c.cls == SC_ENCC_ALL)
	    continue;
	  if (c.cls == SC_DST_READ)
	    {
	      if (rows_disjoint (c.acc, acc))
		continue;
	      break;		/* potentially reads the parked rows */
	    }
	  if (c.cls == SC_GATHER)
	    {
	      bool over = false;
	      for (unsigned k = 0; k != 4 && !over; ++k)
		{
		  dst_access g { c.gather_addr[k], c.gather_mod0, 0,
				  NULL_TREE };
		  over = !rows_disjoint (g, acc);
		}
	      if (over)
		break;
	      continue;
	    }
	  if (c.cls == SC_DST_WRITE)
	    {
	      if (c.acc.addr == acc.addr && c.acc.mod0 == acc.mod0
		  && c.acc.addr_mode == acc.addr_mode
		  && line[j].state_after)
		{
		  if (dump_file)
		    fprintf (dump_file, "transp-park: dead park store uid %u addr %u "
			     "killed by store uid %u\n",
			     gimple_uid (line[i].stmt), acc.addr,
			     gimple_uid (line[j].stmt));
		  gimple_stmt_iterator sgsi = gsi_for_stmt (line[i].stmt);
		  rvtt_prep_stmt_for_deletion (line[i].stmt);
		  gsi_remove (&sgsi, true);
		  line[i].c.cls = SC_SKIP;
		  break;
		}
	      continue;		/* other writes never block a kill */
	    }
	  break;		/* barrier */
	}
    }
}

/* Run the transform over the function: refuse on any typed CC writer
   other than the all-lanes enable, find the transpose-pair candidates,
   compute the all-lanes reaching states, decide per candidate whether a
   new SFPENCC must head its bundle, rewrite each candidate to the gather
   bundle, then forward and delete the Dst parks around the bundles.
   Returns whether the IL changed.  */

bool
involution_transform::run ()
{
  /* Any typed CC writer other than the all-lanes enable refuses the
     whole function; CC regions are not modeled here.  */
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (classify_stmt (gsi_stmt (gsi)).cls == SC_CC_WRITE)
	{
	  if (dump_file)
	    fprintf (dump_file, "transp-involution: function refused "
		     "(transp-involution-cc-region at uid %u)\n",
		     gimple_uid (gsi_stmt (gsi)));
	  return false;
	}

  find_candidates ();
  if (m_cands.empty ())
    return false;

  compute_lane_states ();

  /* Decide encc placement per candidate in program order per block.  */
  FOR_EACH_BB_FN (bb, m_fn)
    {
      bool state = in_state_of_bb (m_fn, bb, m_clean_in);
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  candidate *cand = nullptr;
	  for (candidate &c : m_cands)
	    if (c.transp == stmt)
	      {
		cand = &c;
		break;
	      }
	  if (cand)
	    {
	      cand->needs_encc = !state;
	      state = true;
	      continue;
	    }
	  classified c = classify_stmt (stmt);
	  if (c.cls == SC_ENCC_ALL)
	    state = true;
	  else if (c.cls != SC_SKIP && c.cls != SC_SAFE
		   && c.cls != SC_DST_READ && c.cls != SC_DST_WRITE
		   && c.cls != SC_GATHER)
	    state = false;
	}
    }

  /* Rewrite + local elision.  */
  std::vector<std::pair<gcall *, basic_block>> bundles;
  for (candidate &cand : m_cands)
    {
      basic_block cbb = gimple_bb (cand.transp);
      gcall *bundle = rewrite_candidate (cand);
      bundles.push_back ({ bundle, cbb });
    }
  for (auto &b : bundles)
    forward_parks (b.first, b.second);

  /* Dead park stores, per block containing a bundle.  */
  hash_set<basic_block> seen;
  for (auto &b : bundles)
    if (!seen.add (b.second))
      dse_parks (b.second);

  return true;
}

const pass_data pass_data_rvtt_transp_involution =
{
  GIMPLE_PASS, /* type */
  "rvtt_transp_involution", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  TODO_update_ssa, /* todo_flags_finish */
};

class pass_rvtt_transp_involution : public gimple_opt_pass
{
public:
  pass_rvtt_transp_involution (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_transp_involution, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
	   && (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH)
	   && riscv_tt_opt_transp_involution > 0;
  }

  unsigned int execute (function *fn) final override
  {
    renumber_gimple_stmt_uids (fn);
    involution_transform xf (fn);
    if (xf.run ())
      return TODO_update_ssa;
    return 0;
  }
}; /* class pass_rvtt_transp_involution */

} /* anon namespace */

/* Create the transp-involution pass.  It runs late, after the
   structured-CC lowering, so the windows and lane-state scan see the
   statements that will be expanded.  */

gimple_opt_pass *
make_pass_rvtt_transp_involution (gcc::context *ctxt)
{
  return new pass_rvtt_transp_involution (ctxt);
}
