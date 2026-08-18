/* Cross-call prefix hoist test shape: a noinline per-tile callee whose
   row loop consumes six call-invariant 32-bit immediate
   materializations through the SFPLUTFP32 pinned coefficient operands
   (the md constraints pin them to L0-L2/L4-L6), called from a purely
   scalar tile loop.  Every name and every coefficient value is
   macro-parameterized: the hoist decision must be identical under
   renaming and under different values (nothing may key on either).

   Hooks:
     CCH_CALLEE_EXTRA(r)  extra statement inside the callee's row loop
     CCH_LOOP_EXTRA()     extra statement inside the caller's tile loop
     CCH_AFTER_DEFS()     file-scope extra after the definitions  */

#ifndef CCH_ADDR_MODE
#define CCH_ADDR_MODE 7		/* BH no-increment; WH tests use 3 */
#endif
#ifndef CCH_CALLEE_EXTRA
#define CCH_CALLEE_EXTRA(r) do {} while (0)
#endif
#ifndef CCH_LOOP_EXTRA
#define CCH_LOOP_EXTRA() do {} while (0)
#endif
#ifndef CCH_LUT_MOD
#define CCH_LUT_MOD 4		/* FP32 3-entry, SGN_RETAIN */
#endif

__attribute__((noinline)) void
CCH_CALLEE ()
{
  auto CCH_A0 = __builtin_rvtt_sfpxloadi (CCH_VAL_A0, -32);
  auto CCH_A1 = __builtin_rvtt_sfpxloadi (CCH_VAL_A1, -32);
  auto CCH_A2 = __builtin_rvtt_sfpxloadi (CCH_VAL_A2, -32);
  auto CCH_B0 = __builtin_rvtt_sfpxloadi (CCH_VAL_B0, -32);
  auto CCH_B1 = __builtin_rvtt_sfpxloadi (CCH_VAL_B1, -32);
  auto CCH_B2 = __builtin_rvtt_sfpxloadi (CCH_VAL_B2, -32);
  for (int CCH_ROW = 0; CCH_ROW != 8; ++CCH_ROW)
    {
      auto CCH_X = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6,
					   CCH_ADDR_MODE);
      auto CCH_R = __builtin_rvtt_sfplutfp32_6r (CCH_A0, CCH_A1, CCH_A2,
						 CCH_B0, CCH_B1, CCH_B2,
						 CCH_X, CCH_LUT_MOD);
      CCH_CALLEE_EXTRA (CCH_R);
      __builtin_rvtt_sfpstore (nullptr, CCH_R, 0, 0, 0, 6, CCH_ADDR_MODE);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
CCH_CALLER (int CCH_TILES)
{
  for (int CCH_T = 0; CCH_T != CCH_TILES; ++CCH_T)
    {
      CCH_LOOP_EXTRA ();
      CCH_CALLEE ();
    }
}

#ifdef CCH_AFTER_DEFS
CCH_AFTER_DEFS
#endif
