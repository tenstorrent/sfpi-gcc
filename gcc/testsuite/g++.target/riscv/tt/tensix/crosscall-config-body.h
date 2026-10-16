/* crosscall-hoist-body.h callee that also programs a programmable-constant
   register (CCH_CFG_DEST, value CCH_CFG_VAL) and reads it in the row loop.  */

#ifndef CCH_CFG_DEST
#define CCH_CFG_DEST 12
#endif
#ifndef CCH_CFG_VAL
#define CCH_CFG_VAL 16128
#endif
#ifndef CCH_CFG_V
#define CCH_CFG_V cfg_v
#endif
#ifndef CCH_CFG_R
#define CCH_CFG_R cfg_r
#endif

#define CCH_CALLEE_HEAD_CONFIG()					\
  do									\
    {									\
      auto CCH_CFG_V = __builtin_rvtt_sfploadi (nullptr, CCH_CFG_VAL,	\
						0, 0, 0);		\
      __builtin_rvtt_sfpwriteconfig_v (CCH_CFG_V, 0, CCH_CFG_DEST);	\
    }									\
  while (0)

#define CCH_ROW_CONFIG_READ(r)						\
  do									\
    {									\
      auto CCH_CFG_R = __builtin_rvtt_sfpreadlreg (CCH_CFG_DEST);	\
      (r) = __builtin_rvtt_sfpmul ((r), CCH_CFG_R, 0);			\
    }									\
  while (0)
