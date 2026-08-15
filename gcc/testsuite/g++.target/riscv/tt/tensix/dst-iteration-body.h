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

__attribute__ ((noinline)) void
eligible_pair ()
{
  vec_t a0 = load_dst (0);
  vec_t b0 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a0, b0, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);

  vec_t a1 = load_dst (0);
  vec_t b1 = load_dst (64);
  store_dst (__builtin_rvtt_sfpmul (a1, b1, 0), 128);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

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
