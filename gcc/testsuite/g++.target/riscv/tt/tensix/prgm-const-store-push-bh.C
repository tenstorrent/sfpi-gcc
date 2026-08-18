// The volatile-store blind spot is closed: instruction-FIFO pushes by
// store classify their word through the audited table; the blocking
// store-load-consume asm idiom classifies as the store it is; PC_BUF
// sync words and other MMIO are inert by the decoder census; unproven
// targets refuse by name.  Fire leg: a TU whose pushes are all
// audited (a claiming SFPCONFIG push included) allocates around the
// pushed claim.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L13 for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

typedef volatile unsigned int vu32;
extern volatile unsigned int __instrn_buffer[];

void audited_pushes_then_math ()
{
  // A runtime-composed SETC16 push (thread-config class, admitted for
  // every field value) and a constant SFPCONFIG push CLAIMING L12
  // (word 0x910000C0: dest 12) -- the allocator must skip to L13.
  unsigned v = 42;
  __instrn_buffer[0] = 0xb2000000 + (v & 0xffff);
  __instrn_buffer[0] = 0x910000C0;

  // The blocking-store idiom at the PC_BUF MOP_SYNC word: inert.
  unsigned raw = 0;
  vu32 *sync_word = (vu32 *) 0xFFE80008;
  asm volatile ("sw %0, (%1)\n\tlw %0, (%1)\n\tand x0, x0, %0"
		: "+r" (raw) : "r" (sync_word) : "memory");

  // An ordinary L1 mailbox store at a constant address: inert MMIO.
  *(vu32 *) 0x0001FFBC = 1;

  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpwriteconfig_v (s, 14);   // typed claim of L14
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
