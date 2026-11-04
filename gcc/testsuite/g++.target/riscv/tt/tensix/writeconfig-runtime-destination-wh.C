// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times {SFPCONFIG\t0, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t1, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t2, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t3, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t4, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t5, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t6, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t7, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t8, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t9, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t10, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t11, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t12, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t13, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t14, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t15, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t15, 0, 2} 1 } }
#include "writeconfig-runtime-destination-body.h"
