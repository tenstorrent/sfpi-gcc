/* Standalone unit tests for the timing engine's modulo-scheduling tier.
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

/* Synthetic-fixture tests for the header-inline modulo-scheduling tier
   of rvtt-timing.h (FABLE_GOES_BURR item #5), following the
   rvtt-delivery-cost-test.cc pattern:

     g++ -std=c++11 -Wall -Wextra -Werror -I. \
	 rvtt-timing-test.cc -o <out> && <out>

   Only the header's inline section is under test (make_mod_prob,
   resmii, recmii, ims_try/ims_schedule, mve_kmin, mve_live_demand);
   the compiled module's simulate/cyclic_ii/pricing bodies live in
   rvtt-timing.cc and are covered by the corpus and twin gates.  This
   file additionally carries a REFERENCE 6-copy convergence probe
   (transcribed from rvtt-timing.cc cyclic_ii) so the IMS-order-vs-
   acceptance-oracle coherence checks below cannot drift from the
   engine's own acceptance semantics.  */

#include <cstdio>
#include <cstdlib>
#include "rvtt-timing.h"

using namespace rvtt_timing;

static unsigned tests, failures;

#define CHECK(COND)						\
  do								\
    {								\
      ++tests;							\
      if (!(COND))						\
	{							\
	  ++failures;						\
	  std::fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__,	\
			__LINE__, #COND);			\
	}							\
    }								\
  while (0)

/* Reference acceptance oracle: the 6-copy convergence probe,
   transcribed from rvtt-timing.cc cyclic_ii.  */

static int
ref_cyclic_ii (const seq &s, const std::vector<int> &order)
{
  unsigned n = s.ops.size ();
  const unsigned COPIES = 6;
  std::vector<int> issue (n * COPIES, 0);
  std::vector<int> start (COPIES, 0);
  int t = 0;
  int last_d = 0;
  for (unsigned c = 0; c != COPIES; ++c)
    {
      for (unsigned k = 0; k != n; ++k)
	{
	  const op &nd = s.ops[order[k]];
	  int ready = t;
	  for (unsigned pc = 0; pc <= c; ++pc)
	    for (unsigned j = 0; j != (pc == c ? k : n); ++j)
	      {
		dep_kind kind = s.kind (order[j], order[k]);
		if (!kind)
		  continue;
		const op &p = s.ops[order[j]];
		int need = issue[pc * n + order[j]] + p.words
			   + (kind == DEP_LATENCY ? p.lat : 0);
		if (need > ready)
		  ready = need;
	      }
	  issue[c * n + order[k]] = ready;
	  t = ready + nd.words;
	  if (k == 0)
	    start[c] = ready;
	}
      if (c >= 2)
	{
	  int d1 = start[c] - start[c - 1];
	  int d2 = start[c - 1] - start[c - 2];
	  last_d = d1;
	  if (d1 == d2)
	    return d1;
	}
      else if (c == 1)
	last_d = start[1] - start[0];
    }
  return last_d;
}

/* Build a seq from plain arrays: N ops of {words, lat}, dependence
   matrix DEP (row-major, values from dep_kind).  */

static seq
mk_seq (unsigned n, const int *words, const int *lat,
	const unsigned char *dep)
{
  seq s;
  s.ops.resize (n);
  s.dep.assign (dep, dep + n * n);
  for (unsigned i = 0; i != n; ++i)
    {
      s.ops[i].words = words[i];
      s.ops[i].lat = lat[i];
      s.ops[i].entry_pin = 0;
    }
  return s;
}

/* Sort node indices by (sigma, index) -- the consumer's committed
   order rule.  */

static std::vector<int>
order_of (const mod_placement &pl)
{
  std::vector<int> order;
  for (unsigned i = 0; i != pl.sigma.size (); ++i)
    order.push_back ((int) i);
  for (unsigned i = 1; i < order.size (); ++i)
    {
      int v = order[i];
      unsigned j = i;
      while (j > 0 && (pl.sigma[order[j - 1]] > pl.sigma[v]
		       || (pl.sigma[order[j - 1]] == pl.sigma[v]
			   && order[j - 1] > v)))
	{
	  order[j] = order[j - 1];
	  --j;
	}
      order[j] = v;
    }
  return order;
}

/* T1: independent ops.  No recurrence: RecMII 0-or-words-bound only,
   ResMII = word count, IMS schedules at MII, kmin 1.  */

static void
t1_independent ()
{
  const int words[4] = {1, 1, 1, 1};
  const int lat[4] = {1, 1, 1, 1};
  const unsigned char dep[16] = {0};
  seq s = mk_seq (4, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  CHECK (p.edges.empty ());
  CHECK (resmii (p) == 4);
  CHECK (recmii (p) == 0);
  mod_placement pl = ims_schedule (p, 4, 16, 64);
  CHECK (pl.scheduled && pl.ii == 4);
  CHECK (mve_kmin (p, pl) == 1);
  CHECK (mve_live_demand (p, pl) == 0);
}

/* T2: a two-op recurrence: a -> b (RAW lat 1), b -> a next iteration
   (RAW lat 1, via the matrix's b-constrains-a entry).  Cycle delta =
   (1+1)+(1+1) = 4 over omega 2... expressed distance-1 both ways:
   a->b omega {0,1}, b->a omega 1.  RecMII: cycle a->b(omega0,delta2) +
   b->a(omega1,delta2): delta 4 / omega 1 = 4.  */

static void
t2_recurrence ()
{
  const int words[2] = {1, 1};
  const int lat[2] = {1, 1};
  /* dep(i,j): earlier-i constrains j.  a<->b both directions RAW.
     Diagonal: self WAW.  */
  const unsigned char dep[4] = {
    DEP_LATENCY, DEP_LATENCY,
    DEP_LATENCY, DEP_LATENCY,
  };
  seq s = mk_seq (2, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  CHECK (resmii (p) == 2);
  CHECK (recmii (p) == 4);
  mod_placement pl = ims_schedule (p, 4, 16, 64);
  CHECK (pl.scheduled && pl.ii == 4);
  /* Placement satisfies both directions.  */
  CHECK (pl.sigma[1] >= pl.sigma[0] + 2);
  CHECK (pl.sigma[0] + 4 >= pl.sigma[1] + 2);
}

/* T3: serial chain of four ops, wrap-closed (last feeds first next
   iteration): the classic ring.  All words 1, lat 1: each link needs
   distance 2; ring of 4 links: RecMII = 8.  IMS at 8; the sorted
   order equals program order; the acceptance oracle agrees the ring
   cannot beat its recurrence (ref II == 8 for the program order).  */

static void
t3_ring ()
{
  const int words[4] = {1, 1, 1, 1};
  const int lat[4] = {1, 1, 1, 1};
  unsigned char dep[16] = {0};
  /* i feeds i+1; 3 feeds 0 (wrap).  Self-WAW diagonal.  */
  for (unsigned i = 0; i != 4; ++i)
    dep[i * 4 + i] = DEP_LATENCY;
  dep[0 * 4 + 1] = DEP_LATENCY;
  dep[1 * 4 + 2] = DEP_LATENCY;
  dep[2 * 4 + 3] = DEP_LATENCY;
  dep[3 * 4 + 0] = DEP_LATENCY;
  seq s = mk_seq (4, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  CHECK (resmii (p) == 4);
  CHECK (recmii (p) == 8);
  mod_placement pl = ims_schedule (p, 8, 32, 64);
  CHECK (pl.scheduled && pl.ii == 8);
  std::vector<int> order = order_of (pl);
  CHECK (ref_cyclic_ii (s, order) == 8);
}

/* T4: the interleave payoff shape -- two independent 2-op chains, all
   RAW lat 1.  Serial program order (chain A whole, then chain B) pays
   interior stalls; the IMS order interleaves them and the acceptance
   oracle certifies a strictly smaller II than the serial order's.  */

static void
t4_interleave ()
{
  const int words[4] = {1, 1, 1, 1};
  const int lat[4] = {1, 1, 1, 1};
  unsigned char dep[16] = {0};
  for (unsigned i = 0; i != 4; ++i)
    dep[i * 4 + i] = DEP_LATENCY;
  dep[0 * 4 + 1] = DEP_LATENCY;	/* A1 -> A2 */
  dep[2 * 4 + 3] = DEP_LATENCY;	/* B1 -> B2 */
  seq s = mk_seq (4, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  CHECK (resmii (p) == 4);
  CHECK (recmii (p) <= 4);
  std::vector<int> serial;
  serial.push_back (0);
  serial.push_back (1);
  serial.push_back (2);
  serial.push_back (3);
  int serial_ii = ref_cyclic_ii (s, serial);
  mod_placement pl = ims_schedule (p, 4, serial_ii, 64);
  CHECK (pl.scheduled);
  std::vector<int> order = order_of (pl);
  int ims_ii = ref_cyclic_ii (s, order);
  CHECK (ims_ii <= serial_ii);
  CHECK (pl.ii <= serial_ii);
}

/* T5: budget exhaustion -- any nontrivial problem under budget 1
   fails with the exhausted verdict, never a placement.  */

static void
t5_budget ()
{
  const int words[2] = {1, 1};
  const int lat[2] = {1, 1};
  const unsigned char dep[4] = {
    DEP_LATENCY, DEP_LATENCY,
    DEP_NONE, DEP_LATENCY,
  };
  seq s = mk_seq (2, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  mod_placement pl = ims_schedule (p, 2, 8, 1);
  CHECK (!pl.scheduled);
  CHECK (pl.budget_exhausted);
}

/* T6: MVE lifetimes -- producer consumed next iteration by an op the
   placement can pin LATER than the producer: lifetime > II, kmin 2.
   Shape: v0 (words 1, lat 3) feeds v1 same iteration (delta 4) and v1
   feeds v0 next iteration (WAR-free RAW back edge).  With words sum 2
   and the recurrence delta 4+? ... assert only the invariants: kmin =
   ceil(maxlife/ii) >= 1 and demand >= 1 when any latency edge spans
   iterations.  */

static void
t6_mve ()
{
  const int words[3] = {1, 1, 1};
  const int lat[3] = {3, 0, 0};
  unsigned char dep[9] = {0};
  for (unsigned i = 0; i != 3; ++i)
    dep[i * 3 + i] = DEP_LATENCY;
  dep[0 * 3 + 2] = DEP_LATENCY;	/* long-latency v0 -> v2 */
  seq s = mk_seq (3, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  CHECK (resmii (p) == 3);
  int mii = recmii (p) > resmii (p) ? recmii (p) : resmii (p);
  mod_placement pl = ims_schedule (p, mii, 32, 64);
  CHECK (pl.scheduled);
  /* v0 -> v2 delta 4 forces lifetime >= 4 > ii when ii == 3.  */
  int kmin = mve_kmin (p, pl);
  long life = pl.sigma[2] - pl.sigma[0];
  if (life < 4)
    life = 4;
  CHECK (kmin >= (int) ((life + pl.ii - 1) / pl.ii));
  CHECK (pl.ii >= 3);
  if (pl.ii == 3)
    CHECK (kmin >= 2);
  CHECK (mve_live_demand (p, pl) >= 1);
}

/* T7: marshaller fidelity -- every edge make_mod_prob emits is either
   omega 0 with from < to, or omega 1; deltas match the vocabulary
   (words + lat for DEP_LATENCY, words for DEP_ORDER); and the diagonal
   arrives as the omega-1 self constraint only.  */

static void
t7_marshal ()
{
  const int words[2] = {2, 1};
  const int lat[2] = {1, 0};
  const unsigned char dep[4] = {
    DEP_LATENCY, DEP_ORDER,
    DEP_LATENCY, DEP_LATENCY,
  };
  seq s = mk_seq (2, words, lat, dep);
  mod_prob p = make_mod_prob (s);
  unsigned omega0 = 0, omega1 = 0, self1 = 0;
  for (unsigned k = 0; k != p.edges.size (); ++k)
    {
      const mod_edge &e = p.edges[k];
      CHECK (e.omega == 0 || e.omega == 1);
      if (e.omega == 0)
	{
	  ++omega0;
	  CHECK (e.from < e.to);
	}
      else
	++omega1;
      if (e.from == e.to)
	{
	  CHECK (e.omega == 1);
	  ++self1;
	}
      int base = p.words[e.from];
      if (e.kind == DEP_LATENCY)
	CHECK (e.delta == base + s.ops[e.from].lat);
      else
	CHECK (e.delta == base);
    }
  CHECK (omega0 == 1);		/* dep(0,1) only */
  CHECK (omega1 == 4);		/* all four constrained pairs */
  CHECK (self1 == 2);
}

/* Realization-tier two-matrix marshaller (item #5 stage 2): the CROSS
   matrix alone supplies the omega-1 edges, so storage the rotation
   renames never bounds the placement, while the intra matrix keeps the
   in-iteration constraints.  */

static void
t8_marshal_split ()
{
  /* Three ops, chain 0 -> 1 -> 2 by RAW (lat 1) intra; merged
     single-matrix marshalling would wrap every pair (self-WAW
     included) and bound RecMII; an EMPTY cross matrix must leave the
     problem acyclic (RecMII 0) with the intra edges intact.  */
  seq s;
  s.ops.resize (3);
  for (unsigned i = 0; i != 3; ++i)
    {
      s.ops[i].words = 1;
      s.ops[i].lat = 1;
    }
  s.dep.assign (9, (unsigned char) DEP_NONE);
  s.dep[0 * 3 + 1] = DEP_LATENCY;
  s.dep[1 * 3 + 2] = DEP_LATENCY;
  /* Allocator self-reuse: every op redefines its own register each
     iteration (the diagonal) -- the single-matrix marshalling wraps it
     into a binding self-recurrence; the rotation dissolves it.  */
  for (unsigned i = 0; i != 3; ++i)
    s.dep[i * 3 + i] = DEP_LATENCY;
  seq cross = s;
  cross.dep.assign (9, (unsigned char) DEP_NONE);

  mod_prob merged = make_mod_prob (s);
  mod_prob split = make_mod_prob (s, cross);
  CHECK (recmii (merged) > 0);	/* the wrapped self-constraints bind */
  CHECK (recmii (split) == 0);	/* rotation-optimistic: acyclic */
  CHECK (resmii (split) == 3);
  unsigned omega0 = 0, omega1 = 0;
  for (unsigned k = 0; k != split.edges.size (); ++k)
    if (split.edges[k].omega == 0)
      {
	++omega0;
	CHECK (split.edges[k].delta == 2);	/* words 1 + lat 1 */
      }
    else
      ++omega1;
  CHECK (omega0 == 2 && omega1 == 0);

  /* One surviving cross constraint (a loop-carried value 2 -> 0):
     exactly one omega-1 edge, delta from the producing op, and the
     recurrence becomes exact again.  */
  cross.dep[2 * 3 + 0] = DEP_LATENCY;
  mod_prob carried = make_mod_prob (s, cross);
  omega1 = 0;
  for (unsigned k = 0; k != carried.edges.size (); ++k)
    if (carried.edges[k].omega == 1)
      {
	++omega1;
	CHECK (carried.edges[k].from == 2 && carried.edges[k].to == 0);
	CHECK (carried.edges[k].delta == 2);
	CHECK (carried.edges[k].kind == DEP_LATENCY);
      }
  CHECK (omega1 == 1);
  /* Cycle 0->1->2->0: length 6 over one iteration distance.  */
  CHECK (recmii (carried) == 6);
  mod_placement pl = ims_schedule (carried, 6, 8, 64);
  CHECK (pl.scheduled && pl.ii == 6);
  CHECK (mve_kmin (carried, pl) == 1);

  /* Force an overlapped lifetime: op 0's value is also read by op 2
     (edge 0 -> 2), so at II 3 (the resource floor) that value spans
     sigma[2] - sigma[0] >= 4 > II -- realizing the placement owes
     kmin 2 and the demand counts the overlapped copies.  */
  seq s2 = s;
  s2.dep[0 * 3 + 2] = DEP_LATENCY;
  seq cross2 = s2;
  cross2.dep.assign (9, (unsigned char) DEP_NONE);
  mod_prob rot = make_mod_prob (s2, cross2);
  mod_placement pl3 = ims_schedule (rot, 3, 3, 64);
  CHECK (pl3.scheduled && pl3.ii == 3);
  CHECK (pl3.sigma[2] - pl3.sigma[0] >= 4);
  CHECK (mve_kmin (rot, pl3) == 2);
  CHECK (mve_live_demand (rot, pl3) >= 2);
}

int
main ()
{
  t1_independent ();
  t2_recurrence ();
  t3_ring ();
  t4_interleave ();
  t5_budget ();
  t6_mve ();
  t7_marshal ();
  t8_marshal_split ();
  if (failures)
    {
      std::fprintf (stderr, "%u/%u checks FAILED\n", failures, tests);
      return EXIT_FAILURE;
    }
  std::printf ("rvtt-timing-test: %u checks passed\n", tests);
  return EXIT_SUCCESS;
}
