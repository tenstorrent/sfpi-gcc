/* All written payload words are inert. The negatives test MUST initialization,
   not a TU-wide bad-word refusal that would also poison the positive.
   Template 1 with outer=inner=1 executes Loop0Last (slot 7), so omit that
   genuinely consumed slot rather than an unused slot for these counts. */
#define RAW(W) asm volatile (".ttinsn %0" : : "n" (W))
#define SLOT(N, V) (((volatile unsigned *) 0xffb80000u)[N] = (V))
#define EIGHT_SLOTS() do { \
  SLOT (0, 1); SLOT (1, 1); \
  SLOT (2, 0x02000000u); SLOT (3, 0x02000000u); \
  SLOT (4, 0x02000000u); SLOT (5, 0x02000000u); \
  SLOT (6, 0x02000000u); SLOT (8, 0x02000000u); \
} while (0)
#define NINTH_SLOT() SLOT (7, 0x02000000u)
#define RESET() do { __builtin_rvtt_sfpencc_all_lanes (); \
  __builtin_rvtt_sfpconfig_i (0, 15, 1); } while (0)

#ifndef RVTT_MOP_NEGATIVE_ONLY
void initialized_template ()
{
  EIGHT_SLOTS ();
  NINTH_SLOT ();
  RAW (0x01800000);
  RESET ();
  CHECK_LANES ();
}
#endif

#ifndef RVTT_MOP_POSITIVE_ONLY
void missing_template_slot ()
{
  EIGHT_SLOTS ();
  RAW (0x01800000);
  RESET ();
  CHECK_LANES ();
}

void partial_template_join (bool condition)
{
  EIGHT_SLOTS ();
  if (condition)
    NINTH_SLOT ();
  RAW (0x01800000);
  RESET ();
  CHECK_LANES ();
}
void launch_before_initialization ()
{
  RAW (0x01800000);
  EIGHT_SLOTS ();
  NINTH_SLOT ();
  // An earlier unknown template may have started a recorder. Programming
  // its slots and emitting a reset later cannot prove physical delivery.
  RESET ();
  CHECK_LANES ();
}
#endif
