// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988572674" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989817856" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { check-function-bodies "**" "" } }

#include "loadmacro-staged-body.h"

/*
**_Z11staged_bodyv:
**	SFPENCC	3, 10
**	SFPLOADI	L0, 4294, 2
**	SFPLOADI	L0, 38142, 8	# LV:L0
**	SFPCONFIG	0, 0, 0	# R:L0 CFG:0
**	SFPLOADI	L0, 208, 2
**	SFPLOADI	L0, 36864, 8	# LV:L0
**	SFPCONFIG	1, 0, 0	# R:L0 CFG:1
**	SFPLOADI	L0, 77, 2
**	SFPLOADI	L0, 21380, 8	# LV:L0
**	SFPCONFIG	4, 0, 0	# R:L0 CFG:4
**	SFPLOADI	L0, 272, 4
**	SFPCONFIG	8, 0, 0	# R:L0 CFG:8
**	SFPNOP
**	SFPNOP
**	SFPNOP
**	ret
*/
