/* laneCU proof harness: exhaustive 2^32 denotational comparison for the
 * int-abs proof-carrying peephole (proposal P2, sibling of laneCT's
 * cast-fp16a-rne proof).
 *
 * CUT denotation: the fresh-body conditional-negate CC region, transcribed
 * from the pin-13 gimple and emission of
 *   tt-metal tt_metal/tt-llk/tests/helpers/include/fresh_cpp/absint32.h
 * (dump absint32_tu.cpp.275t.optimized; emitted row stream
 *  SFPLOAD mod0=4 / SFPSETCC mod1=0 / SFPIADD L9 mod1=6 / SFPENCC 3,10 /
 *  SFPSTORE mod0=4):
 *   cc  = raw bit31 of w            (SFPSETCC mod1=0, is_negative raw bit,
 *                                    craq-sim tensix.cpp:8969-8996)
 *   neg = 0 - w  (wrap mod 2^32)    (SFPIADD mod1=6 = ARG_2SCOMP|CC_NONE,
 *                                    src = LCONST_0 - w, tensix.cpp:8894-8929)
 *   cut = cc ? neg : w              (predicated write + ENCC restore)
 * Load/store are mod0=4 (INT32_2S_COMP), raw 32-bit on BH
 * (tensix.cpp:8465-8466 load, :8656-8658 store) -- the chain is closed
 * over the raw Dst word.
 *
 * HW denotation: SFPABS instr_mod1=0 (integer), lifted VERBATIM from the
 * pinned oracle craq-sim @ 9f324140, src/tensix.cpp:9030-9056 (the
 * TT_VERSION<=1 arm shared by WH and BH):
 *   if (src >= 0x80000000) src = -src;
 *
 * Output: per-class mismatch census (expected all-zero => EQUAL) + SHA256
 * over the full 2^32 output streams of both denotations (input-order,
 * little-endian u32).  Classes partition the input space so `other`
 * cannot occur; the sweep proves the counts.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

/* ---- fresh-body cut denotation (region lowering) ---- */
static inline uint32_t cut_negate_select(uint32_t w) {
    uint32_t cc = w & 0x80000000u;          /* SFPSETCC mod1=0: raw sign bit */
    uint32_t neg = 0u - w;                  /* SFPIADD mod1=6: 0 - w, wrap */
    return cc ? neg : w;                    /* predicated merge */
}

/* ---- craq-sim lift (verbatim semantics, tensix.cpp:9030-9056) ---- */
static inline uint32_t hw_sfpabs_int(uint32_t src) {
    if (src >= 0x80000000u) {
        src = -src;
    }
    return src;
}

int main(void) {
    uint64_t total_mismatch = 0;
    uint64_t mm_mostneg = 0;   /* w == 0x80000000 (INT32_MIN)   */
    uint64_t mm_neg = 0;       /* bit31 set, w != 0x80000000    */
    uint64_t mm_zero = 0;      /* w == 0                        */
    uint64_t mm_pos = 0;       /* bit31 clear, w != 0           */
    EVP_MD_CTX *hc = EVP_MD_CTX_new(), *hh = EVP_MD_CTX_new();
    EVP_DigestInit_ex(hc, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hh, EVP_sha256(), NULL);
    enum { CH = 1 << 20 };
    static uint32_t bc[CH], bh[CH];
    uint64_t u = 0;
    do {
        for (uint32_t i = 0; i < CH; i++, u++) {
            uint32_t x = (uint32_t)u;
            uint32_t c = cut_negate_select(x);
            uint32_t h = hw_sfpabs_int(x);
            bc[i] = c; bh[i] = h;
            if (c != h) {
                total_mismatch++;
                if (x == 0x80000000u) mm_mostneg++;
                else if (x & 0x80000000u) mm_neg++;
                else if (x == 0) mm_zero++;
                else mm_pos++;
            }
        }
        EVP_DigestUpdate(hc, bc, sizeof bc);
        EVP_DigestUpdate(hh, bh, sizeof bh);
    } while (u != 0x100000000ull);
    unsigned char dc[32], dh[32]; unsigned int L;
    EVP_DigestFinal_ex(hc, dc, &L);
    EVP_DigestFinal_ex(hh, dh, &L);
    printf("inputs swept          : 4294967296\n");
    printf("total mismatches      : %llu\n", (unsigned long long)total_mismatch);
    printf("  INT32_MIN (0x80000000)      : %llu\n", (unsigned long long)mm_mostneg);
    printf("  negative (bit31 set)        : %llu\n", (unsigned long long)mm_neg);
    printf("  zero                        : %llu\n", (unsigned long long)mm_zero);
    printf("  positive (bit31 clear)      : %llu\n", (unsigned long long)mm_pos);
    printf("verdict               : %s\n",
           total_mismatch ? "NOT-EQUAL (proof obligation FAILS)" : "EQUAL");
    char hex[65];
    for (int k = 0; k < 2; k++) {
        const unsigned char *d = k ? dh : dc;
        for (int i = 0; i < 32; i++) sprintf(hex + 2*i, "%02x", d[i]);
        printf("%s sha256 = %s\n", k ? "hw-stream " : "cut-stream", hex);
    }
    return 0;
}
