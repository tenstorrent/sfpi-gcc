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

/* Emission of the Tensix backend's named refusals; see rvtt-refuse.h.  */

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
#include "rvtt-refuse.h"

/* Refusal names, indexed by enum rvtt_refusal.  */

static const char *const rvtt_refusal_names[] =
{
#define RVTT_REFUSAL(ID, NAME) NAME,
#include "rvtt-refusals.def"
#undef RVTT_REFUSAL
};

/* Return the registered name of refusal R, as printed in the opt-info
   line.  */

const char *
rvtt_refusal_name (enum rvtt_refusal r)
{
  gcc_checking_assert (r < RVTT_REF_COUNT_);
  return rvtt_refusal_names[r];
}

/* Emit the "tt-refusal: NAME [PASS]" line for NAME at LOC to the
   -fopt-info destinations only.  -fdump-...-details also enables the
   MSG_* kinds on the pass dump, which tests scan (including
   scan-dump-not of "refus"), so the pass dump file is detached while
   the line is emitted.  */

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

/* Return the current function's declaration as a location, so opt-info
   lines have an anchor even when the caller has no stmt or insn.  */

static dump_user_location_t
rvtt_refusal_default_loc (void)
{
  if (cfun && cfun->decl)
    return dump_user_location_t::from_function_decl (cfun->decl);
  return dump_user_location_t ();
}

/* Emit the opt-info line for refusal NAME at LOC.  */

static void
rvtt_refusal_fire_1 (const char *name, dump_user_location_t loc)
{
  rvtt_refusal_route_optinfo (name, loc);
}

/* Emit the opt-info line for registered refusal R.  The overloads
   differ only in the location: the current function, STMT or INSN (a
   null STMT or INSN falls back to the function).  */

void
rvtt_refusal_fire (enum rvtt_refusal r)
{
  rvtt_refusal_fire_1 (rvtt_refusal_name (r), rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire (enum rvtt_refusal r, const gimple *stmt)
{
  rvtt_refusal_fire_1 (rvtt_refusal_name (r),
		       stmt ? dump_user_location_t (stmt)
			    : rvtt_refusal_default_loc ());
}

void
rvtt_refusal_fire (enum rvtt_refusal r, const rtx_insn *insn)
{
  rvtt_refusal_fire_1 (rvtt_refusal_name (r),
		       insn ? dump_user_location_t (insn)
			    : rvtt_refusal_default_loc ());
}

/* Emit refusal NAME, registered or not, at the current function.  */

void
rvtt_refusal_fire_by_name (const char *name)
{
  rvtt_refusal_fire_1 (name,
		       rvtt_refusal_default_loc ());
}

/* As above, anchoring the opt-info line at STMT's location when STMT
   is non-null.  */

void
rvtt_refusal_fire_by_name (const char *name, const gimple *stmt)
{
  rvtt_refusal_fire_1 (name,
		       stmt ? dump_user_location_t (stmt)
			    : rvtt_refusal_default_loc ());
}

/* As above, anchoring the opt-info line at INSN's location when INSN
   is non-null.  */

void
rvtt_refusal_fire_by_name (const char *name, const rtx_insn *insn)
{
  rvtt_refusal_fire_1 (name,
		       insn ? dump_user_location_t (insn)
			    : rvtt_refusal_default_loc ());
}

/* Emit the refusal named "PREFIX-SUFFIX" (see rvtt-refuse.h).  The name
   is built in a fixed buffer and truncated past 191 bytes.  */

void
rvtt_refusal_fire_composed (const char *prefix, const char *suffix)
{
  char buf[192];
  snprintf (buf, sizeof (buf), "%s-%s", prefix, suffix);
  rvtt_refusal_fire_1 (buf,
		       rvtt_refusal_default_loc ());
}

/* Return true if TOKEN_PRESENT and PROOF_IN_SCOPE both hold.  Otherwise
   emit refusal STANDING, print FMT to STREAM if non-null, and return
   false.  See rvtt-refuse.h.  */

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
