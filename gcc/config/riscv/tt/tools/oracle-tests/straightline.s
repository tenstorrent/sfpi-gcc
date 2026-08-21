# Straight-line control fixture (no loop): dependent pair A -> C with an
# independent B emitted last, so the as-emitted order stalls once.
#   A@0 (done 1); C needs done(A)+lat(A)=2 -> one stall, issue 2, done 3;
#   B issue 3, done 4; block-end drain: B 4+1=5 -> makespan 5.
#   Lower bound: critical path A(2)+C(2)=4; words+min-sink-drain=3+1=4.
#   Expected: words=3 lower-bound=4 makespan=5 gap=1.
kernel_straight:
	sfpmad	L1, L0, L7, L1
	# xtt-effects: subunit=mad latency=1 lreg-read=0x1 lreg-write=0x2 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L2, L1, L7, L2
	# xtt-effects: subunit=mad latency=1 lreg-read=0x2 lreg-write=0x4 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L5, L4, L7, L5
	# xtt-effects: subunit=mad latency=1 lreg-read=0x10 lreg-write=0x20 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	ret
