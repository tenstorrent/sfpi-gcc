/* One placement arbiter for invariant constants (FABLE_GOES_BURR #13).
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

#ifndef GCC_RVTT_PLACEMENT_H
#define GCC_RVTT_PLACEMENT_H

/* FABLE_GOES_BURR.md item #13: where a loop-invariant constant lives is
   one decision co-owned today by the early invariant hoist's
   park-ordering deferral heuristics, the const-residency walk's tiers
   (PRGM park / pressure-park LREG tier / store-source tier), the
   lut-select coefficient placement, and -- downstream of all of them --
   the dst-ownership identity-reload fold whose LREG pressure guard
   loses to whatever the placement authorities pinned (the erfinv
   9 > 8 anatomy, conf pin-48 / laneIZ; laneJT structurally refuted
   post-alloc coalescing as relief because dst-ownership runs before
   lp-alloc).

   This module is the one arbitration authority: the owning passes stop
   *deciding locally-greedily* and start *bidding* -- every legality
   proof (CC-canonical peel proofs, ambient proofs, consumer audits,
   slot-clobber censuses) stays exactly where it is, in its owning
   pass; the arbiter only ORDERS admitted alternatives by price.

   Pricing currency: centislots via tt/rvtt-delivery-cost (#12), on the
   RISC-push plane (placement words are pushed, never replay-delivered
   at their programming points); pressure via tt/rvtt-pressure (#10);
   trip facts via tt/rvtt-trips (#2).  No timing or pricing arithmetic
   lives here.

   STAGING (the plan's compatibility contract):

   - Stage A (shadow, always available): every arbitrated decision
     point computes the priced verdict NEXT TO the legacy one and dumps
     one `placement-arbiter:' line naming both -- agreement or
     disagreement -- to the owning pass's dump stream.  Generated code
     never depends on the shadow.  The corpus disagreement census over
     those lines is the design's proof artifact.

   - Stage B (-mtt-tensix-optimize-priced-placement, Init(0)): the
     priced verdict decides.  Every flip against the legacy verdict is
     a named tier-conflict dump line; every unpriceable ingredient
     refuses by name (place-alternative-unpriceable) and keeps the
     legacy decision byte-identically; an arbitration whose candidate
     set exceeds the deterministic budget refuses whole
     (place-budget-exhausted) and keeps the legacy decisions; the
     pressure-park tier's marginal LREG park can be outbid by the
     priced downstream fold demand (place-fold-reserve-outbid) -- the
     erfinv relief lever, "price the dst-ownership fold through the
     pressure-park tier" (the pin-48 named successor).  */

/* Deterministic arbitration budget: a single decision point arbitrates
   at most this many candidates; beyond it the arbiter refuses whole
   (place-budget-exhausted) and the legacy policy chain stands.  */
#define RVTT_PLACE_MAX_CANDIDATES 64

/* Trip weight of one placement's amortization: BODY executions per
   ENTRY execution (scaled into 48 bits by the shared discipline).
   PROVEN is false when LOOP is a real loop whose trip count the #2
   facade cannot prove -- the alternative is then unpriceable.  A null
   or root LOOP prices exactly once (1/1, proven).  */

struct rvtt_place_weight
{
  int64_t entry;
  int64_t body;
  bool proven;
};

extern rvtt_place_weight rvtt_place_loop_weight (class loop *loop);

/* Amortized net benefit, in centislots, of a placement that removes
   WORDS_BODY_SAVED issue words from every body execution and pays
   WORDS_ENTRY_PAID issue words once per entry (RISC-push plane).  */

extern int64_t rvtt_place_net_benefit (unsigned words_body_saved,
				       unsigned words_entry_paid,
				       const rvtt_place_weight &w);

/* Priced identity-reload fold demand of FN: the gimple pre-image census
   of the dst-ownership fold (two typed Dst loads with an identical
   constant operand tuple; the RTL pass keeps every proof and still
   decides the fold itself).  SHAPES counts the priceable duplicate
   groups (capped at RVTT_PLACE_MAX_CANDIDATES); BEST_BENEFIT is the
   highest single-fold benefit among them (one delivered SFPLOAD word
   per execution of the reload's position); UNPRICEABLE counts shapes
   refused pricing (trip-unproven positions) -- they contribute no
   demand (fail-closed: an unpriceable fold never outbids a park).  */

struct rvtt_place_fold_demand
{
  unsigned shapes;
  unsigned unpriceable;
  int64_t best_benefit;
};

/* DECIDE distinguishes stage B (the verdict will decide placement:
   refusals fire through the registry) from the stage-A shadow (dump
   lines only; the registry is never fired for a decision that decides
   nothing).  */
extern rvtt_place_fold_demand rvtt_place_fold_demand_of (function *fn,
							 FILE *dump,
							 bool decide);

/* The pressure-park reserve verdict (stage B, flag on): true when the
   marginal LREG park with amortized net benefit PARK_BENEFIT (priced
   by the caller from its own candidate facts; INT64_MIN marks an
   unpriceable park, which fail-closed KEEPS the park) must yield the
   last free LREG to FN's priced fold demand.  TIER names the calling
   tier in the dump line.  Only consulted when the caller's residual
   budget is down to the final register.  */

extern bool rvtt_place_fold_reserve_outbids (function *fn,
					     int64_t park_benefit,
					     bool park_priceable,
					     const char *tier, FILE *dump,
					     bool decide);

/* Shadow census entry (stage A): one uniform dump line.  */

extern void rvtt_place_dump_verdict (FILE *dump, const char *point,
				     const char *legacy, const char *priced,
				     const char *detail);

#endif /* GCC_RVTT_PLACEMENT_H */
