/* A counted DELIVERY loop shaped like the topk bitonic phase driver:
   per trip a direction flip-flop selects a fixed raw config word, an
   init guard records the typed window once and launches it thereafter,
   and a second launch follows unconditionally.  Every tested value is a
   proven function of the trip number, so the launch-flatten request
   lets the complete unroller flatten the delivery and fold the per-trip
   conditionals at their proven values.  */

#ifndef LF_KERNEL
#define LF_KERNEL lf_kernel
#endif
#ifndef LF_TRIPS
#define LF_TRIPS 16
#endif
#ifndef LF_START
#define LF_START 16
#endif
#ifndef LF_LEN
#define LF_LEN 9
#endif
#ifndef LF_CFGWORD
#define LF_CFGWORD 0x91800104u
#endif
/* QSR cannot exec-while-load: its twin records without executing.  */
#ifndef LF_EXEC
#define LF_EXEC 1
#endif

void LF_KERNEL ()
{
  bool dir = false;
  bool init = true;
  for (int d = 0; d < LF_TRIPS; ++d)
    {
      if (dir)
	asm volatile (".ttinsn %0" :: "n" (LF_CFGWORD));
      if (init)
	{
	  __builtin_rvtt_ttreplay (nullptr, LF_LEN, 0, 0, LF_START, LF_EXEC, 1);
	  auto a = __builtin_rvtt_sfpreadlreg (0);
	  auto b = __builtin_rvtt_sfpreadlreg (1);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpadd (a, b, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  a = __builtin_rvtt_sfpadd (a, b, 0);
#if LF_LEN >= 6
	  a = __builtin_rvtt_sfpmul (a, b, 0);
#endif
#if LF_LEN >= 7
	  a = __builtin_rvtt_sfpadd (a, b, 0);
#endif
#if LF_LEN >= 8
	  a = __builtin_rvtt_sfpmul (a, b, 0);
#endif
#if LF_LEN >= 9
	  a = __builtin_rvtt_sfpadd (a, b, 0);
#endif
	  __builtin_rvtt_sfpwritelreg (a, 2);
	  init = false;
	}
      else
	__builtin_rvtt_ttreplay (nullptr, LF_LEN, 0, 0, LF_START, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, LF_LEN, 0, 0, LF_START, 0, 0);
      dir = !dir;
    }
}
