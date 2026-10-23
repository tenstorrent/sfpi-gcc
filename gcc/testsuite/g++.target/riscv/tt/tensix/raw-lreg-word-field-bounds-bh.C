// Runtime fields are not assumed to fit merely because metadata is present.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump-times {Lane merge: insn [0-9]+ keeps the old value of LREG mask 0x02} 2 "rvtt_lreg_livein" } }

#define RAW(W) asm volatile (".ttinsn %0" : : "n" (W))
#define RESET() do { __builtin_rvtt_sfpencc_all_lanes ();  __builtin_rvtt_sfpconfig_i (0, 15, 1); } while (0)
#define WRITE(N) do { RAW (0x71120000 | (N));  __builtin_rvtt_sfprawlreg_effect (0, 2); } while (0)
#define READ() do { __builtin_rvtt_sfpencc_all_lanes ();  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (1), 4); } while (0)

void unbounded_add (volatile unsigned *buffer, unsigned value)
{
  RESET ();
  WRITE (1);
  *buffer = 0x71020000u + value;
  WRITE (2);
  READ ();
}

void unbounded_or (volatile unsigned *buffer, unsigned value)
{
  RESET ();
  WRITE (1);
  *buffer = 0x71020000u | value;
  WRITE (2);
  READ ();
}

void bounded_low_half (volatile unsigned *buffer, unsigned value)
{
  RESET ();
  WRITE (1);
  *buffer = 0x71020000u | (value & 0xffffu);
  WRITE (2);
  READ ();
}
