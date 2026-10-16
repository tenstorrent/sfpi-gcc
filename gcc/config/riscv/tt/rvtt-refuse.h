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

/* Named refusals.  A pass that declines a transformation reports it in two
   independent ways:

   1. In its own dump file, with its usual message.  The rvtt_refuse macros
      below print with fprintf only when the dump stream is non-null and,
      like the usual "if (dump_file) fprintf (...)" idiom, do not evaluate
      the format arguments otherwise.

   2. Through -fopt-info: rvtt_refusal_fire emits

	tt-refusal: <name> [<pass>]

      with dump_printf_loc (MSG_MISSED_OPTIMIZATION, ...), so
      -fopt-info-missed lists every refusal.  This line goes only to the
      -fopt-info destinations; rvtt-refuse.cc detaches the pass dump file
      around the call so that -details dumps, which tests scan, are not
      changed.

   The _at variants take the statement or insn the refusal is about; the
   others use the location of the current function.  Refusal names are
   registered in rvtt-refusals.def.  */

#ifndef GCC_RVTT_REFUSE_H
#define GCC_RVTT_REFUSE_H

enum rvtt_refusal
{
#define RVTT_REFUSAL(ID, NAME) RVTT_REF_##ID,
#include "rvtt-refusals.def"
#undef RVTT_REFUSAL
  RVTT_REF_COUNT_
};

extern const char *rvtt_refusal_name (enum rvtt_refusal);

/* Emit the -fopt-info line for a refusal (never into a pass dump).  */
extern void rvtt_refusal_fire (enum rvtt_refusal);
extern void rvtt_refusal_fire (enum rvtt_refusal, const gimple *);
extern void rvtt_refusal_fire (enum rvtt_refusal, const rtx_insn *);
extern void rvtt_refusal_fire_by_name (const char *name);
extern void rvtt_refusal_fire_by_name (const char *name, const gimple *);
extern void rvtt_refusal_fire_by_name (const char *name, const rtx_insn *);
/* Fire the refusal whose name is PREFIX followed by SUFFIX.  */
extern void rvtt_refusal_fire_composed (const char *prefix,
					const char *suffix);

/* Return true if a transformation guarded by refusal STANDING may
   proceed, i.e. TOKEN_PRESENT and PROOF_IN_SCOPE are both true.
   Otherwise fire STANDING, print FMT to STREAM if non-null, and return
   false; the caller must then change nothing.  Unlike the macros below
   this is a real variadic function, so the format arguments are always
   evaluated.  */
extern bool rvtt_refuse_licensed (enum rvtt_refusal standing,
				  bool token_present, bool proof_in_scope,
				  FILE *stream, const char *fmt, ...)
  ATTRIBUTE_PRINTF_5;

/* Fire a refusal and print the fprintf-style arguments to STREAM, the
   pass's dump file (which may be null).  These are macros so that the
   arguments are evaluated only when dumping.  */

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
