/* Config-prefix pair test shape: the crosscall-hoist-body.h
   callee additionally programs a programmable-constant register from an
   all-constant immediate (the sfpi vConstFloatPrgm idiom the licensed
   fp16-LUT bodies emit) and reads it back inside the row loop.  With
   -mtt-tensix-optimize-crosscall-config-prefix the pair joins the
   coefficient contract; without it the pair is a foreign vector
   statement in the liveness-extension tail and the WHOLE contract
   refuses (crosscall-callee-vector-outside-loop), byte-identically to
   the pre-flag pass.

   Macro-parameterized like the base body: names and values must not
   matter.  CCH_CFG_DEST selects the programmed register (12 = the
   vConstFloatPrgm0 row); CCH_CFG_VAL the 16-bit immediate.  */

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
      __builtin_rvtt_sfpwriteconfig_v (CCH_CFG_V, CCH_CFG_DEST);	\
    }									\
  while (0)

#define CCH_ROW_CONFIG_READ(r)						\
  do									\
    {									\
      auto CCH_CFG_R = __builtin_rvtt_sfpreadlreg (CCH_CFG_DEST);	\
      (r) = __builtin_rvtt_sfpmul ((r), CCH_CFG_R, 0);			\
    }									\
  while (0)
