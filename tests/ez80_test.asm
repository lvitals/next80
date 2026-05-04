	.adl 1
	ld hl, 0x123456
	mlt bc
	lea hl, (ix+10)
	.adl 0
	ld hl, 0x1234
	mlt de
	.adl 1
	jp 0x654321
	.z80
	jp 0x4321
