/* Macro-planner in-tree descriptor verifier (Layer 7a).
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

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "tm_p.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-macro-verify-core.h"

/* Decode every synthesized word back through the capability tables and
   compare against expectations assembled from the region's explicit
   facts.  A mismatch refuses with descriptor-verification-failed and the
   failing component's stable tag -- the descriptor is never "fixed up".
   Enabled by -mtt-tensix-macro-planner-verify and always under
   checking.  */

void
rvtt_macro_verify_descriptor (const macro_region &region,
			      const macro_schedule &schedule,
			      const macro_descriptor &desc, FILE *dump)
{
  if (desc.refusal)
    return;			/* nothing was synthesized */

  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c)
    return;

  rvtt_macro_verify::expectations expect;
  if (!rvtt_macro_build_expectations (region, schedule, &expect))
    {
      if (dump)
	fprintf (dump, "Macro-planner refusal: %s (expectations)\n",
		 macro_desc_refusal_verification_failed);
      return;
    }

  rvtt_macro_verify::descriptor_words words;
  memset (&words, 0, sizeof (words));
  words.n_templates = desc.n_templates;
  for (unsigned t = 0; t != desc.n_templates; ++t)
    words.templ[t] = desc.templ[t];
  words.n_seq = desc.n_seq;
  for (unsigned m = 0; m != desc.n_seq; ++m)
    words.seq[m] = desc.seq[m];
  words.misc = desc.misc;
  words.n_setc16 = desc.n_setc16;
  for (unsigned s = 0; s != desc.n_setc16; ++s)
    words.setc16[s] = desc.setc16[s];
  words.n_launches = desc.launches.length ();
  for (unsigned l = 0; l != desc.launches.length (); ++l)
    words.launch_words[l] = desc.launches[l].word;

  const char *component = rvtt_macro_verify::verify (c, words, expect);
  if (dump)
    {
      if (component)
	fprintf (dump, "Macro-planner refusal: %s (%s)\n",
		 macro_desc_refusal_verification_failed, component);
      else
	fprintf (dump, "Macro-planner verify: ok\n");
    }
}
