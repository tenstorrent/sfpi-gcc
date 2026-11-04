// { dg-do compile }
// { dg-additional-options "-mcpu=tt-qsr32-tensix -O2" }
// tt-qsr32 has Zbb, not Zbc.

int clz (unsigned a) {
  return __builtin_clz (a);
}

int ctz (unsigned a) {
  return __builtin_ctz (a);
}

int popcount (unsigned a) {
  return __builtin_popcount (a);
}

// GCC 15's CRC pass turns this CRC-16/CCITT step into clmul when Zbc
// is enabled.  Keep the if/else form: a ternary is if-converted before
// the CRC pass runs and is never recognised.
unsigned short crc16 (unsigned short crc, unsigned char data) {
  crc ^= (unsigned short) data << 8;
  for (int i = 0; i < 8; i++)
    {
      if (crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  return crc;
}

// { dg-final { scan-assembler "\tclz" } }
// { dg-final { scan-assembler "\tctz" } }
// { dg-final { scan-assembler "\tcpop" } }
// { dg-final { scan-assembler-not "clmul" } }
