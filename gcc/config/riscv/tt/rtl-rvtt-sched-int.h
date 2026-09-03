/* Private interface between the Tensix scheduler units.
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
   Tensix scheduling pass (rtl-rvtt-schedule.cc and the
   rtl-rvtt-sched-*.cc units split from it).  Include it after the
   rtl/df headers.  Everything here is private to those units  */

#ifndef GCC_RTL_RVTT_SCHED_INT_H
#define GCC_RTL_RVTT_SCHED_INT_H

struct insn_regs
{
  HARD_REG_SET uses;
  HARD_REG_SET defs;
};

/* Filler-search window shared by the two fill phases below: an
   enumeration budget, NOT a cost-model constant (candidates beyond it
   are simply not considered -- refusal-direction only).  One definition
   so the two phases cannot drift (FH audit FHS-3).  */
constexpr unsigned SEARCH_WINDOW = 24;

struct ls_node
{
  rtx_insn *insn;
  insn_regs regs;	 /* uses include defs for predicated writes  */
  HARD_REG_SET raw_defs; /* defs alone				      */
  int lat;		 /* audited result latency		      */
  int words;		 /* issue slots this instruction occupies    */
  int orig;		 /* original index within the region	      */
  long cp;		 /* critical-path height to region exit      */
  int ready;		 /* earliest issue slot (filled during sim)  */
  int entry_pin;	 /* issue slot floor from the entry boundary */
  bool pin_to_baseline;	 /* unaudited entry producer dependence      */
};

struct ls_rename
{
  unsigned oldr, newr;
  std::vector<rtx_insn *> insns;	/* web members rewritten */
};

/* Capacity of the allocatable vector-register file -- the pressure engine's one
   spelling (rvtt-pressure.h; the header's model types are GIMPLE-side,
   the constant is not).  */
extern unsigned rvtt_pressure_capacity ();

/* rtl-rvtt-sched-fill.cc */
extern bool collect_sfpu_regs (rtx_insn *insn, insn_regs *regs);
extern rtx_insn *next_issued_insn (basic_block bb, rtx_insn *insn);
extern void fill_latency_bubbles (function *fn);
extern unsigned rvtt_delay_bubbles (rtx_insn *insn);
extern bool delay_nop_needed_p (std::vector<basic_block> &visited,
				basic_block bb, rtx_insn *insn,
				enum xtt_delay delay);
extern bool bare_lreg_copy_p (rtx_insn *insn);
extern bool issued_tensix_p (rtx_insn *insn);
extern void sfpu_reg_refs (rtx_insn *insn, insn_regs *regs);
extern bool shadow_crossing_safe_p (rtx_insn *insn,
				    bool hidden_free_filler);
extern bool shadow_filler_p (rtx_insn *insn, insn_regs *regs,
			     bool *hidden_free);
extern void fill_nop_shadows (function *fn);

/* rtl-rvtt-sched-interlock.cc */
extern int audited_latency (rtx_insn *insn);
extern int adjacency_stall (rtx_insn *p, rtx_insn *c);
extern void fill_interlock_shadows (function *fn);

/* rtl-rvtt-sched-region.cc */
extern int ls_dependence (const ls_node &p, const ls_node &c);
extern unsigned ls_pad_sites (std::vector<basic_block> &visited,
			      basic_block bb,
			      const std::vector<ls_node> &nodes);
extern void ls_queue_reg_replacements (rtx_insn *insn, rtx *loc,
				       unsigned oldr, unsigned newr);
extern void ls_refresh_node_regs (std::vector<ls_node> &nodes);
extern bool ls_cyclic_rename_collisions
  (basic_block bb, std::vector<ls_node> &nodes,
   std::vector<ls_rename> *record,
   const std::vector<bool> *start_allowed = nullptr,
   const std::vector<unsigned> *scan_order = nullptr,
   bool *no_free_lreg = nullptr);
extern void ls_undo_renames (std::vector<ls_rename> &record);
extern int ls_cyclic_ii (const std::vector<ls_node> &nodes,
			 const std::vector<int> &order);
extern void list_schedule_regions (function *fn);

/* rtl-rvtt-sched-pairing.cc */
extern void crossrow_pair_rows (function *fn);

/* rtl-rvtt-sched-rotation.cc */
extern basic_block rotation_dedicated_preheader (basic_block bb);
extern void rotate_capture_rows (function *fn);

#endif /* GCC_RTL_RVTT_SCHED_INT_H */
