/* laneCU proof harness: exhaustive denotational comparison for the proposed
 * SM32 cast-elision peephole on the binary left shift (leftshift-fresh row).
 *
 * FRESH chain (pin-13 emission of calculate_left_shift_fresh_cpp,
 * fresh_cpp_operations.h; dump leftshift_tu.s: TTREPLAY 0,12 row):
 *   SFPLOAD  L0 mod0=4       ; w  raw          (BH raw, tensix.cpp:8465)
 *   SFPCAST  L0,L0,3         ; v = smag2int(w) (tensix.cpp:9601-9641 mod1=3)
 *   SFPLOAD  L1 mod0=4       ; a  raw
 *   SFPCAST  L1,L1,3         ; s = smag2int(a)
 *   SFPSHFT  L0,L1,0,0       ; r = s>=0 ? v<<(s&31) : v>>((-s)&31)  (:8931-8968)
 *   SFPSETCC L1,0,4          ; cc = !sign(s)
 *   SFPIADD  L1,L1,-32,1     ; cc &= sign(s-32)  (enabled lanes)
 *   SFPCOMPC ; SFPMOV L0,L9 ; SFPENCC 3,10      ; r = in_range ? r : 0
 *   SFPCAST  L0,L0,3         ; out = int2smag(r)  (same self-inverse mod1=3)
 *   SFPSTORE L0 mod0=4
 * Denotation: f(w,a) = cast3( inrange(s) ? shft(cast3(w), s) : 0 ),
 *             s = cast3(a), inrange(s) = 0 <= (int32)s < 32.
 *
 * ELIDED chain (the proposed rule: drop the three SFPCASTs = the hand
 * kernel's 9-word INT32_2S_COMP conversion-in-load form,
 * ckernel_sfpu_shift.h calculate_binary_left_shift; INT32_2S_COMP loads and
 * stores are architecturally inert on BH -- tensix.cpp:8465/8486 load
 * mod0 4 == 12 raw, :8656/8670 store raw -- the laneCI inertness fact):
 * Denotation: h(w,a) = inrange_raw(a) ? shft(w, a) : 0,
 *             inrange_raw(a) = !sign(a) && sign(a-32)  (== 0 <= a < 32).
 *
 * Space: value x shift-amount = 2^64.  Decomposition per the task rule:
 *   PART A (2^32): the amount dimension.  For every a, both chains'
 *     selector (zero vs in-range) and effective shift count are computed
 *     and compared.  EQUAL here proves every mismatch lives in the value
 *     dimension at some k in [0,31].
 *   PART B (31 x 2^32 stratified-exhaustive, k=1..31; k=0 is the cast3
 *     self-inverse identity, swept too): census of
 *     f_k(w) = cast3(cast3(w)<<k)  vs  h_k(w) = w<<k, with the two-class
 *     partition by bit31(w) (complete: other cannot occur) and per-class
 *     closed forms checked exactly:
 *       P (bit31(w)=0): mismatch iff bit31(w<<k)=1 and (w<<k) % 2^30 != 0
 *          count = 2^30 - 2^k  for k=1..30;  2^30 - 2^30 = 0 ... see check
 *       N (bit31(w)=1): mismatch iff t=((-w)<<k) mod 2^32 has
 *          neither t==0 nor t==2^31
 *     The harness computes the closed forms independently and prints
 *     OK/FAIL per k; the census is the ground truth.
 * SHA256 commitment: over the per-k census table records.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

/* craq-sim tensix.cpp:9619-9621, TT_VERSION==1, SFPCAST instr_mod1==3 */
static inline uint32_t cast3(uint32_t src) {
    uint32_t sign = src & 0x80000000u;
    return sign | (sign ? (uint32_t)(0u - src) : src);
}
/* craq-sim tensix.cpp:8952-8961, SFPSHFT reg form (mod1=0) */
static inline uint32_t shft(uint32_t src, uint32_t amt_reg) {
    int32_t s = (int32_t)amt_reg;
    if (s >= 0) return src << (s & 31);
    return src >> ((-s) & 31);
}

int main(void) {
    /* ---- PART A: amount dimension, all 2^32 a ---- */
    uint64_t a_selector_mismatch = 0;
    for (uint64_t ua = 0; ua < 0x100000000ull; ua++) {
        uint32_t a = (uint32_t)ua;
        uint32_t s = cast3(a);
        int zero_f = ((int32_t)s < 0) || ((int32_t)s >= 32);
        int zero_h = ((a & 0x80000000u) != 0)
                     || !(((a - 32u) & 0x80000000u) != 0); /* !sign(a-32) */
        if (zero_f != zero_h) { a_selector_mismatch++; continue; }
        if (!zero_f && s != a) a_selector_mismatch++;
    }
    printf("PART A (amount selector, 2^32): mismatches = %llu  %s\n",
           (unsigned long long)a_selector_mismatch,
           a_selector_mismatch ? "NOT-EQUAL" : "EQUAL");

    /* ---- PART B: value dimension per k ---- */
    EVP_MD_CTX *hm = EVP_MD_CTX_new();
    EVP_DigestInit_ex(hm, EVP_sha256(), NULL);
    uint64_t grand_total = 0, grand_P = 0, grand_N = 0;
    int all_closed_forms_ok = 1;
    printf("PART B (value strata, 32 x 2^32):\n");
    printf("  k   mismatches      P(sign-clear w)  N(sign-set w)   closed-form\n");
    for (uint32_t k = 0; k <= 31; k++) {
        uint64_t mmP = 0, mmN = 0;
        uint32_t ex_P[3] = {0,0,0}, ex_N[3] = {0,0,0};
        int have_P = 0, have_N = 0;
        for (uint64_t uw = 0; uw < 0x100000000ull; uw++) {
            uint32_t w = (uint32_t)uw;
            uint32_t f = cast3(cast3(w) << k);
            uint32_t h = w << k;
            if (f != h) {
                if (w & 0x80000000u) {
                    if (!have_N) { have_N=1; ex_N[0]=w; ex_N[1]=f; ex_N[2]=h; }
                    mmN++;
                } else {
                    if (!have_P) { have_P=1; ex_P[0]=w; ex_P[1]=f; ex_P[2]=h; }
                    mmP++;
                }
            }
        }
        /* closed forms:
           P: #{w<2^31: bit31(w<<k)=1} - #{w<2^31: (w<<k)%2^30==0, bit31 set}
              k=0: 0.  1<=k<=31: bit31 count = 2^30;
              exceptions w<<k in {0x80000000,0xC0000000}:
                0x80000000: 2^(k-1);  0xC0000000: k<=30 ? 2^(k-1) : 0
           N: total sign-set = 2^31; equal cases: t=((0-w)<<k) mod 2^32 in
              {0, 2^31}:  k=0: all equal (identity) -> 0 mismatches.
              k>=1: w=0x80000000 gives t=0 (equal).  For m=(0-w) in (0,2^31):
                t==0     : m % 2^(32-k) == 0        -> count 2^(k-1)-1  (k>=2; k=1: 0)
                t==2^31  : m % 2^(32-k) == 2^(31-k) -> count 2^(k-1)
              mismatches = 2^31 - 1 - (t0_count) - (t31_count)           */
        uint64_t expP, expN;
        if (k == 0) { expP = 0; expN = 0; }
        else {
            uint64_t exc80 = 1ull << (k-1);
            uint64_t excC0 = (k <= 30) ? (1ull << (k-1)) : 0;
            expP = (1ull << 30) - exc80 - excC0;
            uint64_t t0  = (k >= 2) ? ((1ull << (k-1)) - 1) : 0;
            uint64_t t31 = 1ull << (k-1);
            expN = (1ull << 31) - 1 - t0 - t31;
        }
        int ok = (mmP == expP && mmN == expN);
        if (!ok) all_closed_forms_ok = 0;
        printf("  %2u  %-14llu  %-15llu  %-14llu  %s\n", k,
               (unsigned long long)(mmP+mmN), (unsigned long long)mmP,
               (unsigned long long)mmN, ok ? "OK" : "FAIL");
        if (have_P)
            printf("      first P: w=0x%08x elided=0x%08x fresh=0x%08x\n",
                   ex_P[0], ex_P[2], ex_P[1]);
        if (have_N)
            printf("      first N: w=0x%08x elided=0x%08x fresh=0x%08x\n",
                   ex_N[0], ex_N[2], ex_N[1]);
        uint64_t rec[3] = { k, mmP, mmN };
        EVP_DigestUpdate(hm, rec, sizeof rec);
        grand_total += mmP + mmN; grand_P += mmP; grand_N += mmN;
    }
    unsigned char d[32]; unsigned int L;
    EVP_DigestFinal_ex(hm, d, &L);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2*i, "%02x", d[i]);
    printf("grand total mismatches: %llu (P=%llu N=%llu, other=0 by partition)\n",
           (unsigned long long)grand_total, (unsigned long long)grand_P,
           (unsigned long long)grand_N);
    printf("census sha256 = %s\n", hex);
    printf("closed forms          : %s\n",
           all_closed_forms_ok ? "ALL OK" : "SOME FAIL (census is ground truth)");
    printf("verdict               : %s\n",
           (a_selector_mismatch || grand_total)
           ? "NOT-EQUAL (cast elision REFUTED)" : "EQUAL");
    return 0;
}
