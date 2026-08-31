/* One emission helper for the Tensix backend's named refusals.
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

/* See rvtt-refuse.h for the dual-emission contract.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "function.h"
#include "dumpfile.h"
#include "tree-pass.h"
#include "hash-map.h"
#include "rvtt-refuse.h"

/* The registry tables, expanded from the .def.  */

static const char *const rvtt_refusal_names[] =
{
#define RVTT_REFUSAL(ID, NAME, CONTRACT) NAME,
#include "rvtt-refusals.def"
#undef RVTT_REFUSAL
};

static const char *const rvtt_refusal_contracts[] =
{
#define RVTT_REFUSAL(ID, NAME, CONTRACT) CONTRACT,
#include "rvtt-refusals.def"
#undef RVTT_REFUSAL
};

const char *
rvtt_refusal_name (enum rvtt_refusal r)
{
  gcc_checking_assert (r < RVTT_REF_COUNT_);
  return rvtt_refusal_names[r];
}

const char *
rvtt_refusal_contract (enum rvtt_refusal r)
{
  gcc_checking_assert (r < RVTT_REF_COUNT_);
  return rvtt_refusal_contracts[r];
}

/* name -> enum lookup, built lazily on first string-keyed fire.  */

struct rvtt_refusal_name_hash : nofree_string_hash {};

static hash_map<rvtt_refusal_name_hash, int> *rvtt_refusal_index;

enum rvtt_refusal
rvtt_refusal_lookup (const char *name)
{
  if (!rvtt_refusal_index)
    {
      rvtt_refusal_index = new hash_map<rvtt_refusal_name_hash, int> (512);
      for (int i = 0; i < (int) RVTT_REF_COUNT_; i++)
	rvtt_refusal_index->put (rvtt_refusal_names[i], i);
    }
  if (int *slot = rvtt_refusal_index->get (name))
    return (enum rvtt_refusal) *slot;
  return RVTT_REF_COUNT_;
}

/* Per-name fire counters.  Registered names count in a flat array;
   unregistered string-keyed fires (possible only through the by-name /
   composed entry points) are counted under their own xstrdup'd key so
   nothing is ever silently dropped.  */

static unsigned rvtt_refusal_counts[RVTT_REF_COUNT_];
static hash_map<rvtt_refusal_name_hash, unsigned> *rvtt_unregistered_counts;

unsigned
rvtt_refusal_count (enum rvtt_refusal r)
{
  gcc_checking_assert (r < RVTT_REF_COUNT_);
  return rvtt_refusal_counts[r];
}

void
rvtt_refusal_print_counts (FILE *f)
{
  for (int i = 0; i < (int) RVTT_REF_COUNT_; i++)
    if (rvtt_refusal_counts[i])
      fprintf (f, "%s\t%u\n", rvtt_refusal_names[i], rvtt_refusal_counts[i]);
  if (rvtt_unregistered_counts)
    for (hash_map<rvtt_refusal_name_hash, unsigned>::iterator
	   it = rvtt_unregistered_counts->begin ();
	 it != rvtt_unregistered_counts->end (); ++it)
      fprintf (f, "%s\t%u\t(unregistered)\n", (*it).first, (*it).second);
}

/* Optional whole-process summary to stderr at exit, for interactive
   archaeology (RVTT_REFUSAL_SUMMARY=1 in the environment).  Nothing in
   any default stream: any new line in a pass dump would perturb the
   pinned twin-suite bytes, so the counters surface only here and
   through -fopt-info.  */

static void
rvtt_refusal_summary_atexit (void)
{
  fprintf (stderr, "=== tt refusal fire counts ===\n");
  rvtt_refusal_print_counts (stderr);
}

static void
rvtt_refusal_maybe_register_summary (void)
{
  static int state = 0;	/* 0 = unknown, 1 = on, -1 = off */
  if (state == 0)
    {
      state = getenv ("RVTT_REFUSAL_SUMMARY") ? 1 : -1;
      if (state == 1)
	atexit (rvtt_refusal_summary_atexit);
    }
}

/* The -fopt-info routing.  The structured line goes to the -fopt-info
   destinations ONLY: -fdump-...-details also enables the MSG_* kinds
   on the pass dump stream, and those bytes are pinned by the twin
   suite (including scan-dump-nots on the word "refus"), so the pass
   dump file is parked while the line is emitted.  */

static void
rvtt_refusal_route_optinfo (const char *name, dump_user_location_t loc)
{
  if (!dumps_are_enabled)
    return;
  FILE *saved = dump_file;
  if (saved)
    set_dump_file (NULL);
  if (dump_enabled_p ())
    {
      if (current_pass && current_pass->name)
	dump_printf_loc (MSG_MISSED_OPTIMIZATION, loc, "tt-refusal: %s [%s]\n",
			 name, current_pass->name);
      else
	dump_printf_loc (MSG_MISSED_OPTIMIZATION, loc, "tt-refusal: %s\n",
			 name);
    }
  if (saved)
    set_dump_file (saved);
}

/* Default location: the current function's declaration, so opt-info
   lines always carry a usable anchor even from mechanically migrated
   sites that do not thread a stmt/insn.  */

static dump_user_location_t
rvtt_refusal_default_loc (void)
{
  if (cfun && cfun->decl)
    return dump_user_location_t::from_function_decl (cfun->decl);
  return dump_user_location_t ();
}

static void
rvtt_refusal_fire_1 (const char *name, int index, dump_user_location_t loc)
{
  rvtt_refusal_maybe_register_summary ();
  if (index >= 0)
    rvtt_refusal_counts[index]++;
  else
    {
      if (!rvtt_unregistered_counts)
	rvtt_unregistered_counts
	  = new hash_map<rvtt_refusal_name_hash, unsigned> (16);
      bool existed;
      unsigned &n
	= rvtt_unregistered_counts->get_or_insert (name, &existed);
      if (!existed)
	{
	  /* Key must outlive the caller's buffer (composed names).  */
	  rvtt_unregistered_counts->remove (name);
	  rvtt_unregistered_counts->put (xstrdup (name), 1);
	}
      else
	n++;
    }
  rvtt_refusal_route_optinfo (name, loc);
}

void
rvtt_refusal_fire (enum rvtt_refusal r)
{
  rvtt_refusal_fire_1 (rvtt_refusal_name (r), r, rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire (enum rvtt_refusal r, const gimple *stmt)
{
  rvtt_refusal_fire_1 (rvtt_refusal_name (r), r,
		       stmt ? dump_user_location_t (stmt)
			    : rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire (enum rvtt_refusal r, const rtx_insn *insn)
{
  rvtt_refusal_fire_1 (rvtt_refusal_name (r), r,
		       insn ? dump_user_location_t (insn)
			    : rvtt_refusal_default_loc ());
}

/* String-keyed fires: the reason-carrier idiom.  An unregistered name
   is never dropped -- it counts under its own string key and shows up
   marked "(unregistered)" in the counters, and the t-riscv-tt
   catalogue rule fails the build for unregistered name LITERALS at
   by-name emission sites; a reason assembled at runtime that the
   census missed surfaces through the counters rather than an assert
   (the registry is append-only: the fix is always a new .def row).  */

static int
rvtt_refusal_index_of (const char *name)
{
  enum rvtt_refusal r = rvtt_refusal_lookup (name);
  return r == RVTT_REF_COUNT_ ? -1 : (int) r;
}

void
rvtt_refusal_fire_by_name (const char *name)
{
  rvtt_refusal_fire_1 (name, rvtt_refusal_index_of (name),
		       rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire_by_name (const char *name, const gimple *stmt)
{
  rvtt_refusal_fire_1 (name, rvtt_refusal_index_of (name),
		       stmt ? dump_user_location_t (stmt)
			    : rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire_by_name (const char *name, const rtx_insn *insn)
{
  rvtt_refusal_fire_1 (name, rvtt_refusal_index_of (name),
		       insn ? dump_user_location_t (insn)
			    : rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire_composed (const char *prefix, const char *suffix)
{
  char buf[192];
  snprintf (buf, sizeof (buf), "%s-%s", prefix, suffix);
  rvtt_refusal_fire_1 (buf, rvtt_refusal_index_of (buf),
		       rvtt_refusal_default_loc ());
}

/* The licensed-refusal invariants, mechanically enforced; see
   rvtt-refuse.h.  */

bool
rvtt_refuse_licensed (enum rvtt_refusal standing, bool token_present,
		      bool proof_in_scope, FILE *stream,
		      const char *fmt, ...)
{
  if (token_present && proof_in_scope)
    return true;
  rvtt_refusal_fire (standing);
  if (stream)
    {
      va_list ap;
      va_start (ap, fmt);
      vfprintf (stream, fmt, ap);
      va_end (ap);
    }
  return false;
}
