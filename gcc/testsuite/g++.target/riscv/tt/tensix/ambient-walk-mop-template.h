/* Give the positive/poison pair the same complete, concrete template.
   An unowned MOP launch cannot prove absence of CC/config/recorder effects. */
static inline void program_ambient_nop_mop ()
{
  volatile unsigned *slots = (volatile unsigned *) 0xFFB80000u;
  slots[0] = 1;
  slots[1] = 1;
  slots[2] = 0x02000000u;
  slots[3] = 0x02000000u;
  slots[4] = 0x02000000u;
  slots[5] = 0x02000000u;
  slots[6] = 0x02000000u;
  slots[7] = 0x02000000u;
  slots[8] = 0x02000000u;
}
