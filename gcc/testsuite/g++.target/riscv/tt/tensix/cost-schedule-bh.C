// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap" }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

/* Check that a MAD feeding a dependent op keeps its pipeline bubble.  */
void
cost_model_preserves_mad_bubble ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto neg1 = __builtin_rvtt_sfpreadlreg (11);
  auto product = __builtin_rvtt_sfpmad (neg1, a, b, 0);
  auto result = __builtin_rvtt_sfpand (product, b);
  __builtin_rvtt_sfpwritelreg (result, 3);
}

// The dependent use is across a CFG edge: one SFPNOP is inserted after the
// producer, before the branch.
extern volatile unsigned iptr[];

void cost_model_preserves_cross_block_bubble (bool predicate) {
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto product = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  if (predicate)
    iptr[0] = 0;
  auto result = __builtin_rvtt_sfpand(product, b);
  __builtin_rvtt_sfpwritelreg(result, 3);
}

// A nondependent zero-length LREG marker between producer and consumer
// must not hide the dependent use.
void cost_model_preserves_ghost_skip () {
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto product = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  auto marker = __builtin_rvtt_sfpreadlreg(2);
  __builtin_rvtt_sfpwritelreg(marker, 2);
  auto result = __builtin_rvtt_sfpand(product, b);
  __builtin_rvtt_sfpwritelreg(result, 3);
}
