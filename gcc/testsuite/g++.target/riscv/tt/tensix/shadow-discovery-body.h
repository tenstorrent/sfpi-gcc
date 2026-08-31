/* Shared body for the item-#9 stage-A shadow-discovery twins.  RUN is
   one six-word SFPU row whose recurrence on a/b/c keeps every clone's
   register encoding identical, so the discovery's generation-aged class
   hash maps the clones onto one symbol string.  */

#define RUN(a, b, c)				\
  a = __builtin_rvtt_sfpmul (a, a, 0);		\
  b = __builtin_rvtt_sfpmul (b, b, 0);		\
  c = __builtin_rvtt_sfpmul (c, c, 0);		\
  a = __builtin_rvtt_sfpmul (a, b, 0);		\
  b = __builtin_rvtt_sfpmul (b, c, 0);		\
  c = __builtin_rvtt_sfpmul (c, a, 0);
