# tt/proofs — exhaustive denotational proof artifacts (proposal P2)

Each subdirectory carries the proof obligation record for one proposed
proof-carrying peephole: the harness (host C, oracle semantics lifted
verbatim from the pinned craq-sim with file:line provenance), the swept
result with SHA256 stream commitments, and the matched cut's gimple.
A rule may ship in rvtt.gc ONLY citing a directory here whose RESULT is
EQUAL over the full input space; a NOT-EQUAL result is a standing named
refusal (see the NOTES-*-refusal-*.md records) so the cut is never
re-mined.

- cast-fp16a-rne/ — castfp32tofp16a software-RNE cut vs SFP_STOCH_RND
  mod1=0 rnd=0. NOT-EQUAL (33,810,429/2^32). Refusal:
  cast-cut-equivalence-refuted. laneCT 2026-08-20.
