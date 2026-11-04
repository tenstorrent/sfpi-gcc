/* Runtime materialization must not replace a trailing destination with
   the preceding modifier.  Exercise every destination and distinct mods.  */
void runtime_configuration_destinations (volatile unsigned long *buffer,
				       unsigned value)
{
#define CONFIG(D) __builtin_rvtt_sfpwriteconfig_i (buffer, value, 0, 0, 0, D)
  CONFIG (0); CONFIG (1); CONFIG (2); CONFIG (3);
  CONFIG (4); CONFIG (5); CONFIG (6); CONFIG (7);
  CONFIG (8); CONFIG (9); CONFIG (10); CONFIG (11);
  CONFIG (12); CONFIG (13); CONFIG (14); CONFIG (15);
#undef CONFIG
  __builtin_rvtt_sfpwriteconfig_i (buffer, value, 0, 0, 2, 15);
}
