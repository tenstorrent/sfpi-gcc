/* Internal interface between the two halves of the Tensix LREG
   graph-colouring allocator.

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

#ifndef GCC_RTL_RVTT_LP_ALLOC_INT_H
#define GCC_RTL_RVTT_LP_ALLOC_INT_H

/* rtl-rvtt-lp-alloc.cc grew past 3000 lines.  The colouring core --
   graph construction, conservative coalescing, DSATUR and the spill
   choices -- is in rtl-rvtt-lp-alloc-color.cc; the effect data, the
   transaction log, the rewrite and the pass driver stay in the
   original file.  These are the only names that cross.  */

struct spill_ctx
{
  bool ok;
  const char *refusal;		/* named refusal */
  const char *detail;
  rtx_insn *at;
  int noinc_addr_mode;
  /* Epoch-relative Dst rows the function's typed accesses touch
     (immediate + proven offset, with the epoch token), plus assigned
     scratch offsets.  */
  auto_vec<HOST_WIDE_INT> used_rows;
  auto_vec<int> row_epoch;
  bool have_dst_access;
  bool have_mod0_srcb;		/* runtime-resolved (mod0 0) access */
  /* Per-insn recorded states, indexed by INSN_UID.  */
  auto_vec<uint8_t> cc_before, cc_after;
  auto_vec<int> epoch_before, epoch_after;
  auto_vec<int> off_before, off_after;
  auto_vec<bool> known_before, known_after;
  /* Blocks whose in-state minted a fresh epoch: values live into them
     cross a base-identity boundary and are never spilled.  */
  auto_vec<basic_block> minted_bbs;
  /* Per-bb sweep metadata for minted epochs (DP-11): the audited
     per-iteration step (0 = unproven) and the proven max trip count
     (-1 = unproven) of the epoch's own cycle.  A spill in a minted
     epoch is admitted only when both are proven, and its scratch
     window is checked across the WHOLE swept range.  */
  auto_vec<int> mint_step;
  auto_vec<int> mint_trips;
};

/* --------------------------- web collection ------------------------ */

struct lpa_web
{
  unsigned regno;
  int precolor;			/* -1, or the pinned LREG index */
  bool reservation;		/* livein sentinel: never spill */
  bool reload_tmp;		/* spill-generated: never spill */
  unsigned occ;			/* occurrence count (spill cost) */
};

struct lpa_graph
{
  auto_vec<lpa_web> webs;
  auto_vec<int> node_of_reg;	/* regno -> node, -1 */
  sbitmap conflicts;		/* n*n symmetric matrix */
  auto_vec<unsigned> degree;
  auto_vec<int> alias;		/* conservative coalescing: node ->
				   merged-into node, -1 = representative.
				   Identity (all -1) unless
				   -mtt-tensix-optimize-lreg-coalesce
				   performed merges this round.  */
  const char *fail;		/* fail-closed collection refusal */
  rtx_insn *fail_at;

  lpa_graph () : conflicts (NULL), fail (NULL), fail_at (NULL) {}
  ~lpa_graph ()
  {
    if (conflicts)
      sbitmap_free (conflicts);
  }

  bool conflict_p (unsigned i, unsigned j) const
  {
    return bitmap_bit_p (conflicts, i * webs.length () + j);
  }
  /* Whether node I is a live representative (not merged away).  */
  bool live_p (unsigned i) const
  {
    return alias[i] < 0;
  }
  /* The representative of node I's coalesced web.  */
  int rep (int i) const
  {
    while (alias[i] >= 0)
      i = alias[i];
    return i;
  }
  void add_conflict (unsigned i, unsigned j)
  {
    if (i == j)
      return;
    unsigned n = webs.length ();
    if (!bitmap_bit_p (conflicts, i * n + j))
      {
	bitmap_set_bit (conflicts, i * n + j);
	bitmap_set_bit (conflicts, j * n + i);
	degree[i]++;
	degree[j]++;
      }
  }
};

extern void lpa_refuse (spill_ctx &ctx, const char *name,
			const char *detail, rtx_insn *at);
extern void scan_spill_legality (function *fn, spill_ctx &ctx);
extern HOST_WIDE_INT choose_scratch_row (spill_ctx &ctx, int max_delta,
					 int epoch);
extern int sentinel_read_lregno (int code);
extern int sentinel_write_lregno (int code);
extern bool xtt32_pseudo_p (unsigned regno);
extern void build_graph (function *fn, lpa_graph &g, bitmap spill_tmps);
extern unsigned coalesce_conservative (function *fn, lpa_graph &g);
extern bool dsatur_color (const lpa_graph &g, auto_vec<int> &color,
			  int *blocked);
extern int choose_spill_web (const lpa_graph &g, int blocked, function *fn,
			     const spill_ctx &ctx, int *max_delta,
			     int *epoch_out, const char **why);
extern bool dst_mode_32bit_p (HOST_WIDE_INT m);

#endif /* GCC_RTL_RVTT_LP_ALLOC_INT_H */
