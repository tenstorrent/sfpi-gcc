/* Standalone unit tests for the macro-planner schedule constraint core.
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

/* Synthetic-fixture tests for the pure constraint checkers, following the
   rvtt-macro-tables-test.cc pattern:

     g++ -std=c++11 -Wall -Wextra -Werror -I. \
         rvtt-macro-sched-test.cc -o <out> && <out>

   The fixtures are synthetic event sets; no GCC machinery is involved.  */

#include <cstdio>
#include "rvtt-macro-sched-core.h"

using namespace rvtt_macro_sched;

static int checks, failures;

static void
check (bool ok, const char *what)
{
  ++checks;
  if (!ok)
    {
      ++failures;
      std::fprintf (stderr, "FAIL: %s\n", what);
    }
}

int
main ()
{
  /* ---- Subunit occupancy.  */
  {
    core_event two_simple_same_slot[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 1 },
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 1, 0 },
    };
    check (!core_check_subunit_occupancy (two_simple_same_slot, 2),
	   "two Simple events in one slot must refuse");

    core_event staggered[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 0 },
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 1 },
      { CSU_ROUND, CP_SHARED_SIMPLE_ROUND, 0, 2 },
    };
    check (core_check_subunit_occupancy (staggered, 3),
	   "staggered subunit events are legal");

    core_event unknown_delay[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, CORE_DELAY_UNKNOWN },
    };
    check (!core_check_subunit_occupancy (unknown_delay, 1),
	   "unknown delay cannot be proven and refuses");
  }

  /* ---- Write ports.  */
  {
    core_event shared_conflict[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 2 },
      { CSU_ROUND, CP_SHARED_SIMPLE_ROUND, 1, 1 },
    };
    check (!core_check_write_ports (shared_conflict, 2, 3),
	   "Simple and Round sharing one writeback slot must refuse");

    core_event shared_ok[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 0 },
      { CSU_ROUND, CP_SHARED_SIMPLE_ROUND, 0, 1 },
    };
    check (core_check_write_ports (shared_ok, 2, 3),
	   "Simple and Round in distinct slots are legal");

    core_event borrows_conflict[] = {
      { CSU_SIMPLE, CP_BORROWS_MAD, 0, 1 },	/* swap-like borrow  */
      { CSU_MAD, CP_OWN, 0, 1 },
    };
    check (!core_check_write_ports (borrows_conflict, 2, 3),
	   "a MAD-port borrow colliding with a MAD write must refuse");

    core_event own_ports_ok[] = {
      { CSU_LOAD, CP_OWN, 0, 0 },
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 0 },
      { CSU_MAD, CP_OWN, 0, 0 },
    };
    check (core_check_write_ports (own_ports_ok, 3, 3),
	   "three same-slot writes on distinct ports are legal");
    check (!core_check_write_ports (own_ports_ok, 3, 2),
	   "the per-slot write budget arrives as data and binds");

    core_event no_writes[] = {
      { CSU_STORE, CP_NONE, 0, 3 },
      { CSU_STORE, CP_NONE, 1, 2 },
    };
    check (core_check_write_ports (no_writes, 2, 3),
	   "stores write no LREG and never occupy a port");
  }

  /* ---- Dependency latency.  */
  {
    check (core_check_dependency (0, 0), "same-slot handoff is legal");
    check (core_check_dependency (1, 2), "later consumption is legal");
    check (!core_check_dependency (2, 1),
	   "consuming before the producer's writeback must refuse");
    check (!core_check_dependency (CORE_DELAY_UNKNOWN, 1),
	   "unknown producer readiness refuses");
    check (!core_check_dependency (1, CORE_DELAY_UNKNOWN),
	   "unknown consumer slot refuses");
  }

  /* ---- Drain.  */
  {
    core_event cast_round_like[] = {
      { CSU_LOAD, CP_OWN, 0, 0 },
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, 0 },
      { CSU_ROUND, CP_SHARED_SIMPLE_ROUND, 0, 1 },
      { CSU_STORE, CP_NONE, 0, 2 },
    };
    check (core_drain_slots (cast_round_like, 4, 0) == 2,
	   "drain is the greatest remaining writeback distance");
  }
  {
    core_event with_unknown[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 0, CORE_DELAY_UNKNOWN },
    };
    check (core_drain_slots (with_unknown, 1, 0) == CORE_DELAY_UNKNOWN,
	   "any unknown delay makes the drain unproven");

    core_event tail_heavy[] = {
      { CSU_SIMPLE, CP_SHARED_SIMPLE_ROUND, 2, 5 },
      { CSU_STORE, CP_NONE, 2, 3 },
    };
    check (core_drain_slots (tail_heavy, 2, 2) == 5,
	   "drain measures past the last issue slot");
  }

  std::printf ("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
