// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap" }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

/* The default F1 model must preserve the existing BH MAD-pipeline bubble.
   This assembly check keeps the established F1.2 placement contract.  */
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

// The scheduler must follow a dependent use across a CFG edge.  This is also
// the distance-one boundary case: one SFPNOP is inserted after the producer,
// before the scalar branch/merge machinery.
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

// Preserve the legacy DYNAMIC rule: a nondependent zero-length LREG marker
// between producer and consumer is skipped, so it does not hide the actual
// dependent use.  (STATIC intentionally has different legacy semantics.)
void cost_model_preserves_ghost_skip () {
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto product = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  auto marker = __builtin_rvtt_sfpreadlreg(2);
  __builtin_rvtt_sfpwritelreg(marker, 2);
  auto result = __builtin_rvtt_sfpand(product, b);
  __builtin_rvtt_sfpwritelreg(result, 3);
}
