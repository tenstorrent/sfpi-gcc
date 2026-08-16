// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed-equivalent form of the explicit positive with a different Dst
// address: the transform must key on structure, never on names or specific
// constants.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 8, 0, 6" 8 } }

using rechenvektor = __xtt_vector;

static inline void
zeile (unsigned platz)
{
  rechenvektor eingabe = __builtin_rvtt_sfpload (nullptr, platz, 0, 0, 0, 7);
  rechenvektor produkt = __builtin_rvtt_sfpmul (eingabe, eingabe, 0);
  __builtin_rvtt_sfpstore (nullptr, produkt, platz, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
acht_zeilen ()
{
  zeile (8);
  zeile (8);
  zeile (8);
  zeile (8);
  zeile (8);
  zeile (8);
  zeile (8);
  zeile (8);
}
