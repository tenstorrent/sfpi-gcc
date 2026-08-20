/* Hand-spilled twin of ladder-body.h (lane DS acceptance arsenal).

   SAME dataflow DAG as the LADDER_N-live ladder rung -- same XOR ops on
   the same values in the same sequential order -- but values a6..a(N-1)
   are parked in scratch Dst rows with explicit exact (INT32, 32-bit
   row) store/load round-trips, keeping at most 8 values LREG-resident
   at any point.  This twin therefore COMPILES TODAY, runs on CRAQ
   today, and -- because an exact spill round-trip is lossless -- its
   output is bit-identical to what ANY correct exact-only Dst-spilling
   allocator must produce for the ladder rung, regardless of which
   values that allocator chooses to spill.  It defines the rung's
   golden.

   Parameters: LSP_NAME, LSP_N (9|10|12|16), LSP_FMT, LSP_NOINC,
   LSP_TRIPS (must equal the rung's LADDER_TRIPS), LSP_SCRATCH (byte
   address of the first of LSP_N-6 scratch rows, 2 bytes apart; must be
   disjoint from the input rows [bytes 0..2*(N-1)] and the output rows
   [bytes 192..192+2*(N-1)], which match the TWIST-0 rung's).

   Residency scheme: a0..a5 stay in LREGs; a6..a(N-1) live in scratch
   rows S0..S(P-1), P = N-6.  Per iteration:
     a0^=a1 ... a4^=a5            resident/resident   (6 live)
     t = load S0; a5 ^= t         a5 ^= a6            (8 live: a0..a5,t)
     u = load S1; t ^= u; S0 = t  a6 ^= a7            (8 live)
     t := u; u = load S2; ...     ring through the parked values
     last: t ^= a0; S(P-1) = t    a(N-1) ^= a0        (7 live)
   Epilogue folds a0..a5 then each scratch row one at a time.  */

#define LSP_P (LSP_N - 6)
#define LSP_S(j) (LSP_SCRATCH + 2 * (j))
#define LSP_LOAD(a) __builtin_rvtt_sfpload (nullptr, (a), 0, 0, LSP_FMT, \
					    LSP_NOINC)
#define LSP_STORE(v, a) __builtin_rvtt_sfpstore (nullptr, (v), (a), 0, 0, \
						 LSP_FMT, LSP_NOINC)

void LSP_NAME (void)
{
  /* Resident values from input rows 0..5.  */
  auto a0 = LSP_LOAD (0);
  auto a1 = LSP_LOAD (2);
  auto a2 = LSP_LOAD (4);
  auto a3 = LSP_LOAD (6);
  auto a4 = LSP_LOAD (8);
  auto a5 = LSP_LOAD (10);
  /* Park inputs 6..N-1 in the scratch rows, one temp at a time
     (unrolled, constant addresses -- same addressing class as the
     rung).  */
#define LSP_PARK(j)				\
  do						\
    {						\
      auto v = LSP_LOAD (12 + 2 * (j));		\
      LSP_STORE (v, LSP_S (j));			\
    }						\
  while (0)
  LSP_PARK (0);
#if LSP_P > 1
  LSP_PARK (1);
#endif
#if LSP_P > 2
  LSP_PARK (2);
#endif
#if LSP_P > 3
  LSP_PARK (3);
#endif
#if LSP_P > 4
  LSP_PARK (4);
#endif
#if LSP_P > 5
  LSP_PARK (5);
#endif
#if LSP_P > 6
  LSP_PARK (6);
#endif
#if LSP_P > 7
  LSP_PARK (7);
#endif
#if LSP_P > 8
  LSP_PARK (8);
#endif
#if LSP_P > 9
  LSP_PARK (9);
#endif

  for (unsigned ix = 0; ix != LSP_TRIPS; ++ix)
    {
      a0 = __builtin_rvtt_sfpxor (a0, a1);
      a1 = __builtin_rvtt_sfpxor (a1, a2);
      a2 = __builtin_rvtt_sfpxor (a2, a3);
      a3 = __builtin_rvtt_sfpxor (a3, a4);
      a4 = __builtin_rvtt_sfpxor (a4, a5);
      /* a5 ^= a6 (parked).  */
      auto t = LSP_LOAD (LSP_S (0));
      a5 = __builtin_rvtt_sfpxor (a5, t);
      /* Parked ring: a(6+j) ^= a(6+j+1), carrying the successor.  */
#if LSP_P > 1
      {
	auto u = LSP_LOAD (LSP_S (1));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (0));
	t = u;
      }
#endif
#if LSP_P > 2
      {
	auto u = LSP_LOAD (LSP_S (2));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (1));
	t = u;
      }
#endif
#if LSP_P > 3
      {
	auto u = LSP_LOAD (LSP_S (3));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (2));
	t = u;
      }
#endif
#if LSP_P > 4
      {
	auto u = LSP_LOAD (LSP_S (4));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (3));
	t = u;
      }
#endif
#if LSP_P > 5
      {
	auto u = LSP_LOAD (LSP_S (5));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (4));
	t = u;
      }
#endif
#if LSP_P > 6
      {
	auto u = LSP_LOAD (LSP_S (6));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (5));
	t = u;
      }
#endif
#if LSP_P > 7
      {
	auto u = LSP_LOAD (LSP_S (7));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (6));
	t = u;
      }
#endif
#if LSP_P > 8
      {
	auto u = LSP_LOAD (LSP_S (8));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (7));
	t = u;
      }
#endif
#if LSP_P > 9
      {
	auto u = LSP_LOAD (LSP_S (9));
	t = __builtin_rvtt_sfpxor (t, u);
	LSP_STORE (t, LSP_S (8));
	t = u;
      }
#endif
      /* a(N-1) ^= a0 (the new a0).  */
      t = __builtin_rvtt_sfpxor (t, a0);
      LSP_STORE (t, LSP_S (LSP_P - 1));
    }

  /* Emit the full final state vector to the SAME output rows as the
     rung (resident values directly; parked values via one reload at a
     time): bit-identical outputs to what the allocator-compiled rung
     must produce.  */
  __builtin_rvtt_sfpstore (nullptr, a0, 192 + 0, 0, 0, LSP_FMT, LSP_NOINC);
  __builtin_rvtt_sfpstore (nullptr, a1, 192 + 2, 0, 0, LSP_FMT, LSP_NOINC);
  __builtin_rvtt_sfpstore (nullptr, a2, 192 + 4, 0, 0, LSP_FMT, LSP_NOINC);
  __builtin_rvtt_sfpstore (nullptr, a3, 192 + 6, 0, 0, LSP_FMT, LSP_NOINC);
  __builtin_rvtt_sfpstore (nullptr, a4, 192 + 8, 0, 0, LSP_FMT, LSP_NOINC);
  __builtin_rvtt_sfpstore (nullptr, a5, 192 + 10, 0, 0, LSP_FMT, LSP_NOINC);
#define LSP_EMIT(j)						\
  do								\
    {								\
      auto v = LSP_LOAD (LSP_S (j));				\
      __builtin_rvtt_sfpstore (nullptr, v, 192 + 2 * (6 + (j)), 0, 0,	\
			       LSP_FMT, LSP_NOINC);		\
    }								\
  while (0)
  LSP_EMIT (0);
#if LSP_P > 1
  LSP_EMIT (1);
#endif
#if LSP_P > 2
  LSP_EMIT (2);
#endif
#if LSP_P > 3
  LSP_EMIT (3);
#endif
#if LSP_P > 4
  LSP_EMIT (4);
#endif
#if LSP_P > 5
  LSP_EMIT (5);
#endif
#if LSP_P > 6
  LSP_EMIT (6);
#endif
#if LSP_P > 7
  LSP_EMIT (7);
#endif
#if LSP_P > 8
  LSP_EMIT (8);
#endif
#if LSP_P > 9
  LSP_EMIT (9);
#endif
}

#undef LSP_PARK
#undef LSP_EMIT
#undef LSP_P
#undef LSP_S
#undef LSP_LOAD
#undef LSP_STORE
