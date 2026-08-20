/* Graded-pressure ladder body (raw rvtt builtins, no headers).
   Lane DS acceptance arsenal for the LREG allocator.

   Parameters (define before including):
     LADDER_NAME   function name
     LADDER_N      simultaneously live vector values: 8, 9, 10, 12 or 16
     LADDER_FMT    SFPLOAD/SFPSTORE mod0 data format (4 = INT32, 3 = FP32,
                   2 = FP16B/bf16)
     LADDER_NOINC  no-increment addr_mode (BH/QSR 7, WH 3)
     LADDER_TRIPS  loop trip count (kept rolled with -fno-unroll-loops)
     LADDER_TWIST  0 = up-ring over rows 0,2,..; 1 = renamed/varied twin:
                   down-ring over rows 64,66,.. (generality bar)

   Liveness shape: LADDER_N values are initialized from LADDER_N distinct
   Dst rows (volatile loads -- neither sinkable nor reorderable), then a
   counted loop redefines each value from itself and its ring successor:

       a[i] = a[i] XOR a[i+1 mod N]      (TWIST 0)
       a[i] = a[i] XOR a[i-1 mod N]      (TWIST 1)

   Every value is consumed and redefined each iteration, so all LADDER_N
   are live across the backedge at every program point in the loop: the
   maximum simultaneous liveness is exactly LADDER_N and no legal
   rescheduling can lower it (ring dependence).  The epilogue folds all
   values into one result and stores it to a Dst row.

   XOR only, on purpose: the builtins are opaque unspecs (no folding),
   XOR carries no CC state, is format-independent, and is exactly
   reproducible on the host in int32 -- the CRAQ golden needs no
   floating-point rounding model.  No literal constants appear anywhere,
   so no value can be parked in a constant LREG (CREG 8..15) by
   const-remat/const-residency: the ladder measures the allocatable
   8-register file and nothing else.

   Expected verdicts are declared in each including test and in
   ARSENAL.md.  */

#if LADDER_TWIST
#define LADDER_ROW(i) (64 + 2 * (i))
#define LADDER_OUT(i) (256 + 2 * (i))
#else
#define LADDER_ROW(i) (2 * (i))
#define LADDER_OUT(i) (192 + 2 * (i))
#endif

void LADDER_NAME (void)
{
  auto a0 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (0), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a1 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (1), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a2 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (2), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a3 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (3), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a4 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (4), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a5 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (5), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a6 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (6), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
  auto a7 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (7), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
#if LADDER_N > 8
  auto a8 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (8), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
#endif
#if LADDER_N > 9
  auto a9 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (9), 0, 0,
				    LADDER_FMT, LADDER_NOINC);
#endif
#if LADDER_N > 10
  auto a10 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (10), 0, 0,
				     LADDER_FMT, LADDER_NOINC);
  auto a11 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (11), 0, 0,
				     LADDER_FMT, LADDER_NOINC);
#endif
#if LADDER_N > 12
  auto a12 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (12), 0, 0,
				     LADDER_FMT, LADDER_NOINC);
  auto a13 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (13), 0, 0,
				     LADDER_FMT, LADDER_NOINC);
  auto a14 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (14), 0, 0,
				     LADDER_FMT, LADDER_NOINC);
  auto a15 = __builtin_rvtt_sfpload (nullptr, LADDER_ROW (15), 0, 0,
				     LADDER_FMT, LADDER_NOINC);
#endif

#define LADDER_STEP_2(i, j) a##i = __builtin_rvtt_sfpxor (a##i, a##j)

  for (unsigned ix = 0; ix != LADDER_TRIPS; ++ix)
    {
#if LADDER_TWIST
#if LADDER_N == 8
      LADDER_STEP_2 (0, 7); LADDER_STEP_2 (1, 0); LADDER_STEP_2 (2, 1);
      LADDER_STEP_2 (3, 2); LADDER_STEP_2 (4, 3); LADDER_STEP_2 (5, 4);
      LADDER_STEP_2 (6, 5); LADDER_STEP_2 (7, 6);
#elif LADDER_N == 9
      LADDER_STEP_2 (0, 8); LADDER_STEP_2 (1, 0); LADDER_STEP_2 (2, 1);
      LADDER_STEP_2 (3, 2); LADDER_STEP_2 (4, 3); LADDER_STEP_2 (5, 4);
      LADDER_STEP_2 (6, 5); LADDER_STEP_2 (7, 6); LADDER_STEP_2 (8, 7);
#elif LADDER_N == 10
      LADDER_STEP_2 (0, 9); LADDER_STEP_2 (1, 0); LADDER_STEP_2 (2, 1);
      LADDER_STEP_2 (3, 2); LADDER_STEP_2 (4, 3); LADDER_STEP_2 (5, 4);
      LADDER_STEP_2 (6, 5); LADDER_STEP_2 (7, 6); LADDER_STEP_2 (8, 7);
      LADDER_STEP_2 (9, 8);
#elif LADDER_N == 12
      LADDER_STEP_2 (0, 11); LADDER_STEP_2 (1, 0); LADDER_STEP_2 (2, 1);
      LADDER_STEP_2 (3, 2); LADDER_STEP_2 (4, 3); LADDER_STEP_2 (5, 4);
      LADDER_STEP_2 (6, 5); LADDER_STEP_2 (7, 6); LADDER_STEP_2 (8, 7);
      LADDER_STEP_2 (9, 8); LADDER_STEP_2 (10, 9); LADDER_STEP_2 (11, 10);
#elif LADDER_N == 16
      LADDER_STEP_2 (0, 15); LADDER_STEP_2 (1, 0); LADDER_STEP_2 (2, 1);
      LADDER_STEP_2 (3, 2); LADDER_STEP_2 (4, 3); LADDER_STEP_2 (5, 4);
      LADDER_STEP_2 (6, 5); LADDER_STEP_2 (7, 6); LADDER_STEP_2 (8, 7);
      LADDER_STEP_2 (9, 8); LADDER_STEP_2 (10, 9); LADDER_STEP_2 (11, 10);
      LADDER_STEP_2 (12, 11); LADDER_STEP_2 (13, 12); LADDER_STEP_2 (14, 13);
      LADDER_STEP_2 (15, 14);
#else
#error unsupported LADDER_N
#endif
#else /* !LADDER_TWIST */
#if LADDER_N == 8
      LADDER_STEP_2 (0, 1); LADDER_STEP_2 (1, 2); LADDER_STEP_2 (2, 3);
      LADDER_STEP_2 (3, 4); LADDER_STEP_2 (4, 5); LADDER_STEP_2 (5, 6);
      LADDER_STEP_2 (6, 7); LADDER_STEP_2 (7, 0);
#elif LADDER_N == 9
      LADDER_STEP_2 (0, 1); LADDER_STEP_2 (1, 2); LADDER_STEP_2 (2, 3);
      LADDER_STEP_2 (3, 4); LADDER_STEP_2 (4, 5); LADDER_STEP_2 (5, 6);
      LADDER_STEP_2 (6, 7); LADDER_STEP_2 (7, 8); LADDER_STEP_2 (8, 0);
#elif LADDER_N == 10
      LADDER_STEP_2 (0, 1); LADDER_STEP_2 (1, 2); LADDER_STEP_2 (2, 3);
      LADDER_STEP_2 (3, 4); LADDER_STEP_2 (4, 5); LADDER_STEP_2 (5, 6);
      LADDER_STEP_2 (6, 7); LADDER_STEP_2 (7, 8); LADDER_STEP_2 (8, 9);
      LADDER_STEP_2 (9, 0);
#elif LADDER_N == 12
      LADDER_STEP_2 (0, 1); LADDER_STEP_2 (1, 2); LADDER_STEP_2 (2, 3);
      LADDER_STEP_2 (3, 4); LADDER_STEP_2 (4, 5); LADDER_STEP_2 (5, 6);
      LADDER_STEP_2 (6, 7); LADDER_STEP_2 (7, 8); LADDER_STEP_2 (8, 9);
      LADDER_STEP_2 (9, 10); LADDER_STEP_2 (10, 11); LADDER_STEP_2 (11, 0);
#elif LADDER_N == 16
      LADDER_STEP_2 (0, 1); LADDER_STEP_2 (1, 2); LADDER_STEP_2 (2, 3);
      LADDER_STEP_2 (3, 4); LADDER_STEP_2 (4, 5); LADDER_STEP_2 (5, 6);
      LADDER_STEP_2 (6, 7); LADDER_STEP_2 (7, 8); LADDER_STEP_2 (8, 9);
      LADDER_STEP_2 (9, 10); LADDER_STEP_2 (10, 11); LADDER_STEP_2 (11, 12);
      LADDER_STEP_2 (12, 13); LADDER_STEP_2 (13, 14); LADDER_STEP_2 (14, 15);
      LADDER_STEP_2 (15, 0);
#else
#error unsupported LADDER_N
#endif
#endif
    }

  /* Store every final value to its own output row: the golden is the
     FULL state vector -- maximally discriminating (a spill that swaps,
     clobbers or truncates any single value fails the compare; a folded
     XOR would let errors cancel).  Kills values one at a time.  */
#define LADDER_EMIT(i) \
  __builtin_rvtt_sfpstore (nullptr, a##i, LADDER_OUT (i), 0, 0, \
			   LADDER_FMT, LADDER_NOINC)
  LADDER_EMIT (0);
  LADDER_EMIT (1);
  LADDER_EMIT (2);
  LADDER_EMIT (3);
  LADDER_EMIT (4);
  LADDER_EMIT (5);
  LADDER_EMIT (6);
  LADDER_EMIT (7);
#if LADDER_N > 8
  LADDER_EMIT (8);
#endif
#if LADDER_N > 9
  LADDER_EMIT (9);
#endif
#if LADDER_N > 10
  LADDER_EMIT (10);
  LADDER_EMIT (11);
#endif
#if LADDER_N > 12
  LADDER_EMIT (12);
  LADDER_EMIT (13);
  LADDER_EMIT (14);
  LADDER_EMIT (15);
#endif
}

#undef LADDER_ROW
#undef LADDER_OUT
#undef LADDER_STEP_2
#undef LADDER_EMIT
