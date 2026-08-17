/* Standalone adversarial tests for the descriptor verification core.
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

/* Corrupt each component of a known-good descriptor and assert the
   verifier names exactly the failing component (never "fixes up").

     g++ -std=c++11 -Wall -Wextra -Werror -I. \
         rvtt-macro-verify-test.cc rvtt-macro-tables.cc -o <out> && <out>

   The baseline words are the frozen Min/Max BH descriptor (test
   expectations, the legitimate home for these hex values).  */

#include <cstdio>
#include <cstring>
#include "rvtt-macro-verify-core.h"

using namespace rvtt_macro;
using namespace rvtt_macro_verify;

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

static void
baseline (const caps *c, descriptor_words *d, expectations *e)
{
  memset (d, 0, sizeof (*d));
  memset (e, 0, sizeof (*e));

  d->n_templates = 2;
  d->templ[0] = 0x920002c1;
  d->templ[1] = 0x940000d6;
  d->n_seq = 2;
  d->seq[0] = 0x00dd008c;
  d->seq[1] = 0x53000000;
  d->misc = 0x00000330;
  setc16_program programs[8];
  unsigned n = 0;
  bool bank = false;
  addr_mod_program (c, 2, programs, &n, &bank);
  d->n_setc16 = n;
  for (unsigned s = 0; s != n; ++s)
    d->setc16[s] = programs[s];
  d->n_launches = 2;
  encode_launch (c, 0, 0, 0, c->no_increment_addr_mode, 0,
		 &d->launch_words[0]);
  encode_launch (c, 1, 3, 0, c->auto_increment_dst2_addr_mode, 128,
		 &d->launch_words[1]);

  e->n_accesses = 2;
  e->accesses[0] = { 0, 0, 0, 0, c->no_increment_addr_mode };
  e->accesses[1] = { 1, 3, 128, 0, c->auto_increment_dst2_addr_mode };
  e->n_templates = 2;
  e->templates[0].whole_word = false;
  e->templates[0].opcode = 0x92;
  e->templates[0].imm12 = 0;
  e->templates[0].dest_sel = 0xc;
  e->templates[0].mod1 = 1;
  e->templates[1].whole_word = true;
  e->templates[1].word = 0x940000d6;
  e->n_seq = 2;
  e->seq_words[0] = 0x00dd008c;
  e->seq_words[1] = 0x53000000;
  e->misc = 0x00000330;
  e->check_misc = true;
  e->stride = 2;
  e->planned_lregs = 0xf;
}

static const char *
run (const caps *c, void (*corrupt) (descriptor_words *, expectations *))
{
  descriptor_words d;
  expectations e;
  baseline (c, &d, &e);
  if (corrupt)
    corrupt (&d, &e);
  return verify (c, d, e);
}

int
main ()
{
  const caps *c = rvtt_macro_caps_for_cpu (CPU_BH);
  check (c != nullptr, "BH capability table exists");

  check (run (c, nullptr) == nullptr, "uncorrupted descriptor verifies");

  const char *tag;

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->templ[0] ^= 8;		/* flip the routing mod bit */
  });
  check (tag && !strcmp (tag, "template-mismatch"),
	 "corrupted routing mod names template-mismatch");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->templ[1] ^= 0x10;	/* corrupt the proven whole word */
  });
  check (tag && !strcmp (tag, "template-mismatch"),
	 "corrupted whole-word template names template-mismatch");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->seq[1] = 0x53000001;
  });
  check (tag && !strcmp (tag, "sequence-mismatch"),
	 "corrupted sequence word names sequence-mismatch");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->misc = 0x00000331;
  });
  check (tag && !strcmp (tag, "misc-mismatch"),
	 "corrupted misc word names misc-mismatch");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->setc16[1].value ^= 2;	/* break the Dst += 2 slot program */
  });
  check (tag && !strcmp (tag, "setc16-mismatch"),
	 "corrupted slot program names setc16-mismatch");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->launch_words[1] ^= 2;	/* move the store launch address */
  });
  check (tag && !strcmp (tag, "launch-mismatch"),
	 "corrupted launch address names launch-mismatch");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->n_launches = 1;
  });
  check (tag && !strcmp (tag, "launch-count"),
	 "missing launch names launch-count");

  tag = run (c, [] (descriptor_words *, expectations *e) {
    e->planned_lregs &= ~4u;	/* disown the hidden L2 template write */
  });
  check (tag && !strcmp (tag, "hidden-write-unowned"),
	 "unowned hidden template write names hidden-write-unowned");

  tag = run (c, [] (descriptor_words *d, expectations *) {
    d->n_setc16 = 0;		/* drop the stride programming */
  });
  check (tag && !strcmp (tag, "setc16-count"),
	 "missing stride programming names setc16-count");

  /* CC-model timing: the re-checked inequalities are keyed on the
     exchanged SLOTS alone -- never on the separator structure, a misc
     word, or a data format (the architectural constraint behind the
     2026-08-17 silicon adjudication, root-caused by craq-sim
     9f324140: the store's lane mask is live at execution, so the
     all-lanes restore must retire strictly before the store executes,
     restore_visible <= store_exec with lag 1).  */

  auto set_cc = [] (expect_cc *cc, int def_visible, int pre, int post,
		    int store_exec, int restore_visible, int interval) {
    cc->active = true;
    cc->complement = true;
    cc->def_visible_slot = def_visible;
    cc->pre_load_slot = pre;
    cc->post_load_slot = post;
    cc->store_exec_slot = store_exec;
    cc->restore_visible_slot = restore_visible;
    cc->row_interval = interval;
  };

  /* The compact 3-slot select model (separator absorbed): restore
     retires at cycle 2, one cycle before the store executes at 3.  */
  {
    descriptor_words d; expectations e;
    baseline (c, &d, &e);
    set_cc (&d.cc, 2, 1, 2, 3, 3, 3);
    set_cc (&e.cc, 2, 1, 2, 3, 3, 3);
    check (verify (c, d, e) == nullptr,
	   "compact CC model (restore exec 2 < store exec 3) verifies");
  }

  /* The 4-slot separator-kept select model exactly as derived from
     the proven delays (SETCC d0 / store d2 on slot 0, ENCC d0 on slot
     2): restore retires at cycle 3 == store exec 3 -- the silicon
     failure -- names cc-timing.  */
  {
    descriptor_words d; expectations e;
    baseline (c, &d, &e);
    set_cc (&d.cc, 2, 1, 2, 3, 4, 4);
    set_cc (&e.cc, 2, 1, 2, 3, 4, 4);
    const char *t = verify (c, d, e);
    check (t && !strcmp (t, "cc-timing"),
	   "4-slot kept model (restore exec == store exec) names cc-timing");
  }

  /* SYNTHETIC kept-separator-LIKE model that SATISFIES
     restore-before-store (interval 4, an issue slot left for an
     explicit separator, restore retiring at cycle 2 < store exec 3):
     verifies clean -- the constraint is architectural (slot-derived),
     not structural.  The compiler's scheduler cannot construct this
     schedule (the established hosting rule pins the restore to the
     LAST of the row's three load carriers, slot >= 2, and the shared
     launch VD forces the condition load first, so restore exec >= 3 ==
     store exec always; see rvtt-macro-desc.cc derive_cc_model), which
     is exactly why the refusal subsumes the retired structural
     cc-separator-kept-silicon-unproven check.  */
  {
    descriptor_words d; expectations e;
    baseline (c, &d, &e);
    set_cc (&d.cc, 2, 1, 2, 3, 3, 4);
    set_cc (&e.cc, 2, 1, 2, 3, 3, 4);
    check (verify (c, d, e) == nullptr,
	   "kept-separator-like model satisfying restore-before-store"
	   " verifies");
  }

  /* A store retiring at/after the next row's predicate definition
     (def exec + ii = 1 + 3 = 4 <= store exec 5) names cc-timing.  */
  {
    descriptor_words d; expectations e;
    baseline (c, &d, &e);
    set_cc (&d.cc, 2, 1, 2, 5, 3, 3);
    set_cc (&e.cc, 2, 1, 2, 5, 3, 3);
    const char *t = verify (c, d, e);
    check (t && !strcmp (t, "cc-timing"),
	   "store at/after next row's definition names cc-timing");
  }

  /* A synthesized model disagreeing with the re-derived expectation
     names cc-model before any timing re-check.  */
  {
    descriptor_words d; expectations e;
    baseline (c, &d, &e);
    set_cc (&d.cc, 2, 1, 2, 4, 3, 3);	/* store exec 4 != expected 3 */
    set_cc (&e.cc, 2, 1, 2, 3, 3, 3);
    const char *t = verify (c, d, e);
    check (t && !strcmp (t, "cc-model"),
	   "store-exec slot disagreement names cc-model");
  }

  std::printf ("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
