/* laneEK proof harness: exhaustive denotational check of the Dst
 * load->store ROUND TRIP per (SFPLOAD mod0, SFPSTORE mod0) format pair,
 * for the predicated store-sink arm of the store-fold pass
 * (gimple-rvtt-store-fold.cc).
 *
 * The sink rewrites
 *     x = sfpload(A);  v_if(M) { r = z; } v_endif;  sfpstore(A, r)
 * into
 *     x = sfpload(A);  v_if(M) { sfpstore(A, z); } v_endif;
 * On the M lanes both forms store z's conversion -- equal by
 * construction.  On the ENABLED-COMPLEMENT lanes the original form
 * writes store_convert(load_widen(d)) back over the Dst datum d while
 * the sunk form leaves d untouched: the sink is legal exactly when the
 * round trip store_convert(load_widen(d)) == d holds for EVERY Dst bit
 * pattern d the format pair can address.  This harness sweeps that
 * round trip per pair.
 *
 * Semantics lifted VERBATIM from the pinned oracle craq-sim @ 9f324140
 * (BH libttsim 32489dda..., WH 8f0079a9...):
 *   - SFPLOAD arms: src/tensix.cpp:8455-8476 (mod0=1 fp16 widen e+=112
 *     m<<13; mod0=2 bf16 decode_bf16 then <<16; mod0=3/4 decode_fp32 of
 *     the raw Dst32b word).
 *   - SFPSTORE arms: sfpstore_values src/tensix.cpp:8634-8663 (mod0=1
 *     sfpu_store_to_fp16 :8563-8575; mod0=2 denormals_as_zeros :5492
 *     then >>16; mod0=3 BH denormals_as_zeros then encode_fp32; mod0=4
 *     encode_fp32 raw -- the BH TT_VERSION=1 arm performs NO conversion).
 *   - encode_bf16/decode_bf16 and encode_fp32/decode_fp32
 *     (tensix.cpp:3565-3596) are mutually-inverse Dst bit-layout
 *     shuffles; sweeping the decoded domain is equivalent to sweeping
 *     raw Dst bits.
 *   - The 16-bit-layout arm is swept for mod0=1/2 (dest 16-bit rows);
 *     mod0=3/4 use Dst32b unconditionally in both arms.
 *
 * Doc prior: BlackholeA0 SFPSTORE.md ToBF16/ToFP16/ToFP32 all flush
 * denormals ("As part of the conversion ... denormals will be flushed
 * to zero"), so the float pairs are EXPECTED NOT-EQUAL on the denormal
 * class (the store canonicalizes Dst; eliding it preserves the
 * original bits) -- those rows ground the named refusal
 * store-sink-format-canonicalizing.  The raw INT32 pair (BH mod0=4,
 * conversion-free both directions) is EXPECTED EQUAL and licenses the
 * sink for that pair only.
 *
 * Output: per-pair mismatch census + SHA256 stream commitments
 * (round-tripped stream vs identity stream, input-order LE).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

/* ---- craq-sim lifts ---- */

static inline uint32_t denormals_as_zeros(uint32_t u) { /* tensix.cpp:5492 */
    if ((u & 0x7FFFFFFFu) < 0x800000u) u &= 0x80000000u;
    return u;
}

/* load mod0=1 (fp16 -> fp32 widen), tensix.cpp:8457-8466, on the DECODED
   fp16 datum (s:1 e:5 m:10). */
static inline uint32_t load_fp16(uint16_t v) {
    uint32_t s = v >> 15;
    uint32_t e = (v >> 10) & 31u;
    uint32_t m = v & 1023u;
    if (e) e += 112u;
    return (s << 31) | (e << 23) | (m << 13);
}

/* store mod0=1, tensix.cpp:8563-8575 */
static inline uint16_t store_fp16(uint32_t x) {
    uint32_t s = x >> 31;
    uint32_t e32 = (x >> 23) & 255u;
    uint32_t m = x & 0x7FFFFFu;
    int32_t e = (int32_t)e32 - 112;
    if (e <= 0) return (uint16_t)(s << 15);
    if (e > 31) return (uint16_t)((s << 15) | 0x7FFFu);
    return (uint16_t)((s << 15) | ((uint32_t)e << 10) | (m >> 13));
}

/* load mod0=2 (bf16 -> fp32 widen), tensix.cpp:8467-8471, decoded datum */
static inline uint32_t load_bf16(uint16_t v) {
    return ((uint32_t)v) << 16;
}

/* store mod0=2 (16-bit layout arm), tensix.cpp:8636-8641 */
static inline uint16_t store_bf16(uint32_t x) {
    return (uint16_t)(denormals_as_zeros(x) >> 16);
}

/* load mod0=3/4: decode_fp32(raw); store mod0=4: encode_fp32(value) --
   inverse shuffles, so the raw-domain round trip is value-identity;
   modelled here on the decoded domain. */
static inline uint32_t load_int32(uint32_t v) { return v; }
static inline uint32_t store_int32(uint32_t v) { return v; }

/* store mod0=3 (fp32), BH TT_VERSION=1 arm: denormals_as_zeros. */
static inline uint32_t store_fp32(uint32_t v) { return denormals_as_zeros(v); }

/* WH INT32_SM pair (mod0=12): the TT_VERSION==0 arms convert between the
   Dst sign-magnitude encoding and the LReg two's complement --
   load :8482-8487 (sign_mag_to_twos_comp :3793-3796 on bit31), store
   :8676-8681 (negate-and-set-sign on bit31).  Modelled on the decoded
   Dst domain (decode_fp32/encode_fp32 are the shared inverse shuffles). */
static inline uint32_t load_int32_sm_wh(uint32_t d) {
    if (d & 0x80000000u) {
        uint32_t mag = d & 0x7FFFFFFFu;
        return ~mag + 1u;
    }
    return d;
}
static inline uint32_t store_int32_sm_wh(uint32_t v) {
    if (v & 0x80000000u) {
        return 0x80000000u | (uint32_t)(-(int32_t)v);
    }
    return v;
}

struct census { uint64_t total, denorm, negzero, nan, other; };

static void classify16(struct census *c, uint16_t d, unsigned mant_bits) {
    unsigned exp_mask = (d >> mant_bits) & ((1u << (15 - mant_bits)) - 1u);
    unsigned man = d & ((1u << mant_bits) - 1u);
    c->total++;
    if (exp_mask == 0 && man) c->denorm++;
    else if (exp_mask == 0 && !man && (d & 0x8000u)) c->negzero++;
    else if (exp_mask == ((1u << (15 - mant_bits)) - 1u) && man) c->nan++;
    else c->other++;
}

static void sweep16(const char *name, uint32_t (*ld)(uint16_t),
                    uint16_t (*st)(uint32_t), unsigned mant_bits) {
    struct census c; memset(&c, 0, sizeof c);
    EVP_MD_CTX *hr = EVP_MD_CTX_new(), *hi = EVP_MD_CTX_new();
    EVP_DigestInit_ex(hr, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hi, EVP_sha256(), NULL);
    static uint16_t br[1 << 16], bi[1 << 16];
    for (uint32_t u = 0; u < (1u << 16); u++) {
        uint16_t d = (uint16_t)u;
        uint16_t rt = st(ld(d));
        br[u] = rt; bi[u] = d;
        if (rt != d) classify16(&c, d, mant_bits);
    }
    EVP_DigestUpdate(hr, br, sizeof br);
    EVP_DigestUpdate(hi, bi, sizeof bi);
    unsigned char dr[32], di[32]; unsigned int L;
    EVP_DigestFinal_ex(hr, dr, &L);
    EVP_DigestFinal_ex(hi, di, &L);
    printf("%s\n", name);
    printf("  inputs swept      : 65536\n");
    printf("  total mismatches  : %llu\n", (unsigned long long)c.total);
    printf("    denormal (exp==0, man!=0)  : %llu\n", (unsigned long long)c.denorm);
    printf("    negative zero              : %llu\n", (unsigned long long)c.negzero);
    printf("    NaN                        : %llu\n", (unsigned long long)c.nan);
    printf("    other                      : %llu\n", (unsigned long long)c.other);
    printf("  verdict           : %s\n", c.total ? "NOT-EQUAL" : "EQUAL");
    printf("  roundtrip-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", dr[i]);
    printf("\n  identity-stream  sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", di[i]);
    printf("\n");
    EVP_MD_CTX_free(hr); EVP_MD_CTX_free(hi);
}

static void sweep32(const char *name, uint32_t (*ld)(uint32_t),
                    uint32_t (*st)(uint32_t)) {
    uint64_t total = 0, denorm = 0, negzero = 0, other = 0;
    EVP_MD_CTX *hr = EVP_MD_CTX_new(), *hi = EVP_MD_CTX_new();
    EVP_DigestInit_ex(hr, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hi, EVP_sha256(), NULL);
    enum { CH = 1 << 20 };
    static uint32_t br[CH], bi[CH];
    uint64_t u = 0;
    do {
        for (uint32_t i = 0; i < CH; i++, u++) {
            uint32_t d = (uint32_t)u;
            uint32_t rt = st(ld(d));
            br[i] = rt; bi[i] = d;
            if (rt != d) {
                total++;
                uint32_t exp = (d >> 23) & 255u;
                uint32_t man = d & 0x7FFFFFu;
                if (exp == 0 && man) denorm++;
                else if (d == 0x80000000u) negzero++;
                else other++;
            }
        }
        EVP_DigestUpdate(hr, br, sizeof br);
        EVP_DigestUpdate(hi, bi, sizeof bi);
    } while (u != 0x100000000ull);
    unsigned char dr[32], di[32]; unsigned int L;
    EVP_DigestFinal_ex(hr, dr, &L);
    EVP_DigestFinal_ex(hi, di, &L);
    printf("%s\n", name);
    printf("  inputs swept      : 4294967296\n");
    printf("  total mismatches  : %llu\n", (unsigned long long)total);
    printf("    denormal (exp==0, man!=0)  : %llu\n", (unsigned long long)denorm);
    printf("    negative zero              : %llu\n", (unsigned long long)negzero);
    printf("    other                      : %llu\n", (unsigned long long)other);
    printf("  verdict           : %s\n", total ? "NOT-EQUAL" : "EQUAL");
    printf("  roundtrip-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", dr[i]);
    printf("\n  identity-stream  sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", di[i]);
    printf("\n");
    EVP_MD_CTX_free(hr); EVP_MD_CTX_free(hi);
}

int main(void) {
    sweep16("pair (load mod0=2, store mod0=2)  BF16, 16-bit Dst layout", load_bf16, store_bf16, 7);
    sweep16("pair (load mod0=1, store mod0=1)  FP16, 16-bit Dst layout", load_fp16, store_fp16, 10);
    sweep32("pair (load mod0=4, store mod0=4)  INT32 raw, Dst32b (BH)", load_int32, store_int32);
    sweep32("pair (load mod0=3, store mod0=3)  FP32, Dst32b (BH)", load_int32, store_fp32);
    sweep32("pair (load mod0=12, store mod0=12) INT32_SM, Dst32b (WH)", load_int32_sm_wh, store_int32_sm_wh);
    return 0;
}
