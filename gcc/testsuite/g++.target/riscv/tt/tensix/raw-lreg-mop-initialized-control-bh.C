// An initialized expansion retains a full physical LREG write, not a merge.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-prgm-const -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump-not {Lane merge: insn [0-9]+ keeps the old value of LREG mask 0x02} "rvtt_lreg_livein" } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+1897005057} 1 } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+1897005058} 1 } }

#define RVTT_MOP_POSITIVE_ONLY 1
#include "raw-lreg-mop-must-init-bh.C"
