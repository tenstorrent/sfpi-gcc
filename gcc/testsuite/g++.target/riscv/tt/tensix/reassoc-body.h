/* Licensed-reassociation test shape: a straight-line accumulation
   chain of RA_N terms (volatile Dst loads, never CSE'd) combined
   left-associated by one chain operator, result stored.  Every name is
   macro-parameterized: the rebalance decision must be identical under
   renaming and under different term counts/operators (nothing may key
   on either).

   Hooks:
     RA_KERNEL    kernel name
     RA_N	  term count: 4 or 6
     RA_OP(a,b)   the chain combine (defaults to plain sfpadd)
     RA_MID()	  extra statement between the last two links
		  (CC-boundary refusal shapes)  */

#ifndef RA_ADDR_MODE
#define RA_ADDR_MODE 7		/* BH no-increment; WH tests use 3 */
#endif
#ifndef RA_OP
#define RA_OP(a, b) __builtin_rvtt_sfpadd ((a), (b), 0)
#endif
#ifndef RA_MID
#define RA_MID() do {} while (0)
#endif

void
RA_KERNEL (void)
{
  auto RA_X0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RA_ADDR_MODE);
  auto RA_X1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RA_ADDR_MODE);
  auto RA_X2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RA_ADDR_MODE);
  auto RA_X3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RA_ADDR_MODE);
#if RA_N == 6
  auto RA_X4 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RA_ADDR_MODE);
  auto RA_X5 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RA_ADDR_MODE);
#endif
  auto RA_S1 = RA_OP (RA_X0, RA_X1);
  auto RA_S2 = RA_OP (RA_S1, RA_X2);
#if RA_N == 6
  auto RA_S3 = RA_OP (RA_S2, RA_X3);
  auto RA_S4 = RA_OP (RA_S3, RA_X4);
  RA_MID ();
  auto RA_SL = RA_OP (RA_S4, RA_X5);
#else
  RA_MID ();
  auto RA_SL = RA_OP (RA_S2, RA_X3);
#endif
  __builtin_rvtt_sfpstore (nullptr, RA_SL, 0, 0, 0, 6, RA_ADDR_MODE);
}
