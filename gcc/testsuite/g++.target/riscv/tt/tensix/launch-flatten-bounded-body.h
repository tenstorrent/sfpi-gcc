/* Delivery nests whose inner trip count is only known once the outer
   loop is flattened (the topk rebuild step-N shape), and a delivery
   loop with a runtime but bounded count.  */

extern volatile unsigned long lf_instrn_buffer[];

static inline __attribute__((always_inline)) void lf_swap ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto r = __builtin_rvtt_sfpswap_indexed (a, b, a, b, 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 1);
}

#ifdef LF_STEPS
void lf_steps ()
{
  for (unsigned ss = 6; ss > 4; ss--)
    {
      unsigned dist = (ss == 5) ? 16 : 32;
      unsigned inner = dist >> 3;
      unsigned offset = 0;
      for (unsigned ii = 0; ii < inner; ii++)
	{
	  lf_instrn_buffer[0] = 0x70000000u + offset;
	  lf_instrn_buffer[0] = 0x70000000u + offset + dist;
	  lf_swap ();
	  lf_instrn_buffer[0] = 0x72000000u + offset;
	  lf_instrn_buffer[0] = 0x72000000u + offset + dist;
	  offset += 8;
	}
    }
}
#endif

#ifdef LF_RUNTIME
void lf_runtime (unsigned n)
{
  for (unsigned ii = 0; ii < (n & 3); ii++)
    {
      lf_instrn_buffer[0] = 0x70000000u + ii;
      lf_swap ();
      lf_instrn_buffer[0] = 0x72000000u + ii;
      lf_instrn_buffer[0] = 0x72000004u + ii;
      lf_instrn_buffer[0] = 0x72000008u + ii;
    }
}
#endif
