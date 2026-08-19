/* Census-rooting test shape (wave-8 repro pair): the cross-call fire
   shape whose caller loop delivers a MOP launch, in a TU that programs
   a MOP template instruction slot with a raw SFPLOADI word, entered
   ONLY from outside the TU (crt0 -> main / firmware -> run_kernel).
   The TU census must root the externally-visible entry, walk the
   template-programming store, and decide by the audited SFPLOADI
   destination field: a contract destination refuses
   (crosscall-caller-mop-slot-unproven), an off-contract destination
   keeps the epoch clean and the hoist fires.  Every name, coefficient
   value, slot word, and the entry definition itself are
   macro-parameterized: nothing may key on any of them.

   Hooks:
     CCH_SLOT_WORD   the raw word programmed into instruction slot 5
     CCH_ENTRY_DEF   the entry definition (plain main, constructor-
		     rooted main, or a renamed extern "C" entry)  */

typedef volatile unsigned int vu32;

static inline void
CCH_TMPL (unsigned CCH_OUTER, unsigned CCH_INNER)
{
  ((vu32 *) 0xFFB80000)[0] = CCH_OUTER;
  ((vu32 *) 0xFFB80000)[1] = CCH_INNER;
  ((vu32 *) 0xFFB80000)[2] = 0x02000000;
  ((vu32 *) 0xFFB80000)[3] = 0x37020044;
  ((vu32 *) 0xFFB80000)[4] = 0x02000000;
  ((vu32 *) 0xFFB80000)[5] = CCH_SLOT_WORD;
  ((vu32 *) 0xFFB80000)[6] = 0x02000000;
  ((vu32 *) 0xFFB80000)[7] = 0x02000000;
  ((vu32 *) 0xFFB80000)[8] = 0x02000000;
}

__attribute__((noinline)) void
CCH_CALLEE ()
{
  auto CCH_A0 = __builtin_rvtt_sfpxloadi (nullptr, CCH_VAL_A0, 0, 0, -32);
  auto CCH_A1 = __builtin_rvtt_sfpxloadi (nullptr, CCH_VAL_A1, 0, 0, -32);
  auto CCH_A2 = __builtin_rvtt_sfpxloadi (nullptr, CCH_VAL_A2, 0, 0, -32);
  auto CCH_B0 = __builtin_rvtt_sfpxloadi (nullptr, CCH_VAL_B0, 0, 0, -32);
  auto CCH_B1 = __builtin_rvtt_sfpxloadi (nullptr, CCH_VAL_B1, 0, 0, -32);
  auto CCH_B2 = __builtin_rvtt_sfpxloadi (nullptr, CCH_VAL_B2, 0, 0, -32);
  for (int CCH_ROW = 0; CCH_ROW != 8; ++CCH_ROW)
    {
      auto CCH_X = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto CCH_R = __builtin_rvtt_sfplutfp32_6r (CCH_A0, CCH_A1, CCH_A2,
						 CCH_B0, CCH_B1, CCH_B2,
						 CCH_X, 4);
      __builtin_rvtt_sfpstore (nullptr, CCH_R, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void
CCH_CALLER (int CCH_TILES)
{
  for (int CCH_T = 0; CCH_T != CCH_TILES; ++CCH_T)
    {
      /* The MOP launch: expands the programmed template slots.  */
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));
      CCH_CALLEE ();
    }
}

CCH_ENTRY_DEF
