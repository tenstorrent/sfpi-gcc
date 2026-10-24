// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that the du-chain rename is name-independent and renames a third chain through a non-destructive OR consumer.
// { dg-final { scan-rtl-dump-times "Lreg chain rename: L\\d+ -> L\\d+" 3 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=3" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-no-free-lreg" "rvtt_lreg_rename_chains" } }
#define RENC_FN wombat_chains
#define RENC_TRIPS 12
#define RENC_K1 quokka
#define RENC_K2 dingo
#define RENC_X numbat
#define RENC_T tuatara
#define RENC_P pademelon
#define RENC_R rosella
#define RENC_U uakari
#define RENC_T2 quoll
#define RENC_R2 lorikeet
#define RENC_U2 bilby
#define RENC_R3 kea
#define RENC_ROW lap
#define RENC_CONSUME(a, b) __builtin_rvtt_sfpor ((a), (b))
#include "lreg-rename-chains-body.h"
