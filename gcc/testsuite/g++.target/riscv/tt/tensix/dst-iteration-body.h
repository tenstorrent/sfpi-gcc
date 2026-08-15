using vec_t = __xtt_vector;

static inline vec_t
load_dst (unsigned address)
{
  return __builtin_rvtt_sfpload (nullptr, address, 0, 0, 0, DST_MODE);
}

static inline void
store_dst (vec_t value, unsigned address)
{
  __builtin_rvtt_sfpstore (nullptr, value, address, 0, 0, 0, DST_MODE);
}

#ifndef DST_INELIGIBLE_ONLY
// The synthesized instruction-buffer var/id operands differ after unrolling,
// but every semantic scalar and the internally paired live values agree.
__attribute__ ((noinline)) void
eligible_pair ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  vec_t p0 = __builtin_rvtt_sfpmul (a0, b0, 0);
  store_dst (__builtin_rvtt_sfpassign_lv (p0, p0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  vec_t p1 = __builtin_rvtt_sfpmul (a1, b1, 0);
  store_dst (__builtin_rvtt_sfpassign_lv (p1, p1), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
#endif

#ifdef DST_DYNAMIC
__attribute__ ((noinline)) void
dynamic_pair (unsigned input)
{
  unsigned base = (input & 31) * 64;
  vec_t a0 = load_dst (base);
  vec_t b0 = load_dst (base + 256);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), base + 512);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (base);
  vec_t b1 = load_dst (base + 256);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), base + 512);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

__attribute__ ((noinline)) void
dynamic_overflow (unsigned input)
{
  unsigned base = (input & 1) + DST_LIMIT - 1;
  vec_t a0 = load_dst (base);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (base);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
#endif

#ifdef DST_LATE_NEGATIVES
// These groups already have the phase-2 RWC=4 shape.  They exercise the
// late interleaver's own legality checks rather than the fusion recognizer.
__attribute__ ((noinline)) void
late_cross_half_dependency ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  vec_t p0 = __builtin_rvtt_sfpmul (a0, b0, 0);
  store_dst (p0, 128);
  vec_t a1 = load_dst (2);
  vec_t b1 = load_dst (66);
  // A value from the first half must not be treated as an iteration-local
  // counterpart of b1.
  vec_t p1 = __builtin_rvtt_sfpmul (a1, p0, 0);
  store_dst (p1, 130);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

__attribute__ ((noinline)) void
late_external_vector ()
{
  vec_t external = __builtin_rvtt_sfpreadlreg (3);
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, external, 0), 128);
  vec_t a1 = load_dst (2);
  vec_t b1 = load_dst (66);
  // Equal SSA names are not a row-local bijection.
  store_dst (__builtin_rvtt_sfpmul (a1, external, 0), 130);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

__attribute__ ((noinline)) void
late_cc_state ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  __builtin_rvtt_sfppushc (0);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_sfppopc (0);
  vec_t a1 = load_dst (2);
  vec_t b1 = load_dst (66);
  __builtin_rvtt_sfppushc (0);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 130);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

__attribute__ ((noinline)) void
late_address_alias ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  // The second row aliases the first instead of proving the exact +2 map.
  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

// Phase 2 can form this group, but row A's store at 2 aliases row B's first
// load after its +2 rewrite.  The late interleaver must leave it serial.
__attribute__ ((noinline)) void
late_store_load_alias ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 2);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 2);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
#endif

// The row bodies differ.
__attribute__ ((noinline)) void
shape_mismatch ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  vec_t c1 = load_dst (96);
  store_dst (__builtin_rvtt_sfpmul (
	       __builtin_rvtt_sfpmul (a1, b1, 0), c1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// The first row's value escapes its RWC-delimited region.
__attribute__ ((noinline)) void
live_out ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  vec_t result0 = __builtin_rvtt_sfpmul (a0, b0, 0);
  store_dst (result0, 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  store_dst (result0, 192);
}

// Opaque state between typed accesses makes the region ineligible.
__attribute__ ((noinline)) void
opaque_barrier ()
{
  vec_t a0 = load_dst (0);
  asm volatile ("" ::: "memory");
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// Only Dst may advance at the iteration boundary.
__attribute__ ((noinline)) void
rwc_mismatch ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (1, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// The +2 rewrite must remain representable in the access's own address field.
__attribute__ ((noinline)) void
address_overflow ()
{
  vec_t a0 = load_dst (DST_LIMIT);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (DST_LIMIT);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// Typed Dst accesses must use zero modifier and the target's no-increment mode.
__attribute__ ((noinline)) void
access_mode_mismatch ()
{
  vec_t a0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 1, DST_MODE);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 1, DST_MODE);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

__attribute__ ((noinline)) void
address_mode_mismatch ()
{
  vec_t a0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, DST_BAD_MODE);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, DST_BAD_MODE);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// CC state cannot cross or participate in a fused region.
__attribute__ ((noinline)) void
cc_state ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  __builtin_rvtt_sfppushc (0);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  __builtin_rvtt_sfppushc (0);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// Matching opcodes are insufficient when a scalar encoding differs.
__attribute__ ((noinline)) void
scalar_mismatch ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  vec_t k0 = __builtin_rvtt_sfpxloadi (nullptr, 1, 0, 0, -32);
  store_dst (__builtin_rvtt_sfpmul (
	       __builtin_rvtt_sfpmul (a0, b0, 0), k0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  vec_t k1 = __builtin_rvtt_sfpxloadi (nullptr, 2, 0, 0, -32);
  store_dst (__builtin_rvtt_sfpmul (
	       __builtin_rvtt_sfpmul (a1, b1, 0), k1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// Live-variant instructions carry an explicit prior destination and are not
// movable as part of an otherwise shape-identical region.
__attribute__ ((noinline)) void
live_rvtt ()
{
  vec_t external = __builtin_rvtt_sfpreadlreg (3);
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  vec_t p0 = __builtin_rvtt_sfpmul (a0, b0, 0);
  store_dst (__builtin_rvtt_sfpassign_lv (external, p0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  vec_t p1 = __builtin_rvtt_sfpmul (a1, b1, 0);
  store_dst (__builtin_rvtt_sfpassign_lv (external, p1), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

// Candidate iterations must be in one basic block.
__attribute__ ((noinline)) void
cross_basic_block (bool condition)
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  if (condition)
    asm volatile ("" ::: "memory");

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

#ifdef DST_QSR
// QSR Src-selector accesses need their own selector/address proof and are
// recognition-only until that model is implemented.
__attribute__ ((noinline)) void
qsr_selector ()
{
  vec_t a0 = __builtin_rvtt_sfploadsrcs (nullptr, 0, 0, 0, 0, 7, 1);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = __builtin_rvtt_sfploadsrcs (nullptr, 0, 0, 0, 0, 7, 1);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
#endif
