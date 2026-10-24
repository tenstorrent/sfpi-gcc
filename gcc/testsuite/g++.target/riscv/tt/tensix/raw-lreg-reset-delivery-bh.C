// A reset-shaped data store is not proof that hardware executed a reset.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump-times {Lane merge: insn [0-9]+ keeps the old value of LREG mask 0x02} 3 "rvtt_lreg_livein" } }

extern volatile unsigned __instrn_buffer[];
volatile unsigned data[2];
#define RAW(W) asm volatile (".ttinsn %0" : : "n" (W))
#define WRITE(N) do { RAW (0x71120000 | (N)); __builtin_rvtt_sfprawlreg_effect (0, 2); } while (0)
#define READ() do { __builtin_rvtt_sfpencc_all_lanes (); __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (1), 4); } while (0)
#define STORED_RESET(P) do { *(P) = 0x8a00300a; *(P) = 0x910000f1; } while (0)

void unknown_address (volatile unsigned *p)
{
  STORED_RESET (p);
  WRITE (1);
  WRITE (2);
  READ ();
}

void ordinary_data ()
{
  STORED_RESET (data);
  WRITE (1);
  WRITE (2);
  READ ();
}

void wrong_fifo_offset ()
{
  STORED_RESET (((volatile unsigned *) 0xffe40004));
  WRITE (1);
  WRITE (2);
  READ ();
}

void definite_fifo ()
{
  STORED_RESET (((volatile unsigned *) 0xffe40000));
  WRITE (1);
  WRITE (2);
  READ ();
}

void canonical_fifo ()
{
  STORED_RESET (__instrn_buffer);
  WRITE (1);
  WRITE (2);
  READ ();
}

void data_store_preserves_proof (volatile unsigned *p)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  STORED_RESET (p);
  WRITE (1);
  WRITE (2);
  READ ();
}
