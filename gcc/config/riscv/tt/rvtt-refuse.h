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

/* FABLE_GOES_BURR.md item #1: the refusal registry plus one emission
   helper, routed through -fopt-info.

   THE DUAL-EMISSION CONTRACT
   --------------------------
   Every fire does two independent things:

   1. LEGACY STREAM (byte-stable).  The pass's own dump stream keeps its
      exact pre-registry message spelling: the emitting site passes its
      dump FILE* and its verbatim format string through the rvtt_refuse
      macros below, which print with plain fprintf only when the stream
      is non-null.  The macros preserve the historical
      `if (dump_file) fprintf (dump_file, ...)` laziness exactly:
      format arguments are NOT evaluated when the stream is null (many
      sites pass expressions that are only legal, or only cheap, while
      dumping).  The ~1,088-test Tcl-regex suite and the board's
      dump-mining tooling therefore keep working unmodified.

   2. -fopt-info ROUTING (additive).  rvtt_refusal_fire bumps the
      per-name fire counter and, when -fopt-info destinations are
      active, emits one structured line

        tt-refusal: <name> [<pass>]

      via dump_printf_loc (MSG_MISSED_OPTIMIZATION, ...) -- so
      `-fopt-info-missed` surfaces every named refusal for the first
      time.  The structured line is routed to the -fopt-info
      destinations ONLY, never into the pass dump stream:
      -fdump-...-details enables the MSG_* kinds on the pass stream
      too, and the twin suite pins those dump bytes (including
      scan-dump-nots on the literal word "refus").  rvtt-refuse.cc
      temporarily parks the pass dump file around the dump_printf_loc
      call to keep that stream byte-identical.

   Location: the _at variants take the gimple stmt / RTL insn the
   refusal is about; the plain variants fall back to the current
   function's declaration location, so opt-info lines always carry a
   usable source anchor.

   Registration: names live in rvtt-refusals.def (append-only; see the
   header there).  Enum-keyed fires are checked at compile time by
   construction.  String-keyed fires (the reason-carrier idiom the tt
   passes use everywhere) are checked against the registry at runtime
   in checking builds, and unregistered names are still counted, under
   their own string key, in release builds.  */

#ifndef GCC_RVTT_REFUSE_H
#define GCC_RVTT_REFUSE_H

enum rvtt_refusal
{
#define RVTT_REFUSAL(ID, NAME, CONTRACT) RVTT_REF_##ID,
#include "rvtt-refusals.def"
#undef RVTT_REFUSAL
  RVTT_REF_COUNT_
};

/* Registry queries.  */
extern const char *rvtt_refusal_name (enum rvtt_refusal);
extern const char *rvtt_refusal_contract (enum rvtt_refusal);
/* Returns RVTT_REF_COUNT_ when NAME is not registered.  */
extern enum rvtt_refusal rvtt_refusal_lookup (const char *name);

/* Fire = count the refusal + route the structured line to the
   -fopt-info destinations (never into a pass dump stream).  */
extern void rvtt_refusal_fire (enum rvtt_refusal);
extern void rvtt_refusal_fire (enum rvtt_refusal, const gimple *);
extern void rvtt_refusal_fire (enum rvtt_refusal, const rtx_insn *);
extern void rvtt_refusal_fire_by_name (const char *name);
extern void rvtt_refusal_fire_by_name (const char *name, const gimple *);
extern void rvtt_refusal_fire_by_name (const char *name, const rtx_insn *);
/* For prefix+suffix assembled spellings (record-hoist-peel-%s class):
   fires the composed name.  */
extern void rvtt_refusal_fire_composed (const char *prefix,
					const char *suffix);

/* Per-name fire counters (this compilation process).  */
extern unsigned rvtt_refusal_count (enum rvtt_refusal);
extern void rvtt_refusal_print_counts (FILE *);

/* The licensed-refusal invariants (AUDIT-licensed-folds.md section
   1.6), mechanically enforced: token absent => the standing named
   refusal fires and the caller must change nothing; token present but
   proof class out of scope => the same standing refusal fires
   regardless (the WH INT32_SM pattern).  Returns true only when the
   transform may proceed; fires STANDING and prints the legacy line
   otherwise.  NOTE: unlike the macros below this is a true variadic
   function, so the format arguments are evaluated unconditionally --
   licensed sites are hand-migrated and must pass only always-legal
   arguments.  */
extern bool rvtt_refuse_licensed (enum rvtt_refusal standing,
				  bool token_present, bool proof_in_scope,
				  FILE *stream, const char *fmt, ...)
  ATTRIBUTE_PRINTF_5;

/* The one emission helper, as macros so that the legacy stream keeps
   the historical lazy evaluation (see the file header).  STREAM is the
   pass's dump FILE* (may be null); the remaining arguments are the
   verbatim legacy fprintf format string and its arguments.  */

#define RVTT_REFUSE_EMIT_(STREAM, ...)				\
  do								\
    {								\
      FILE *rvtt_refuse_stream__ = (STREAM);			\
      if (rvtt_refuse_stream__)					\
	fprintf (rvtt_refuse_stream__, __VA_ARGS__);		\
    }								\
  while (0)

#define rvtt_refuse(ID, STREAM, ...)				\
  do								\
    {								\
      rvtt_refusal_fire (ID);					\
      RVTT_REFUSE_EMIT_ ((STREAM), __VA_ARGS__);		\
    }								\
  while (0)

#define rvtt_refuse_at(ID, LOC, STREAM, ...)			\
  do								\
    {								\
      rvtt_refusal_fire ((ID), (LOC));				\
      RVTT_REFUSE_EMIT_ ((STREAM), __VA_ARGS__);		\
    }								\
  while (0)

#define rvtt_refuse_by_name(NAME, STREAM, ...)			\
  do								\
    {								\
      rvtt_refusal_fire_by_name (NAME);				\
      RVTT_REFUSE_EMIT_ ((STREAM), __VA_ARGS__);		\
    }								\
  while (0)

#define rvtt_refuse_by_name_at(NAME, LOC, STREAM, ...)		\
  do								\
    {								\
      rvtt_refusal_fire_by_name ((NAME), (LOC));		\
      RVTT_REFUSE_EMIT_ ((STREAM), __VA_ARGS__);		\
    }								\
  while (0)

#endif /* GCC_RVTT_REFUSE_H */
