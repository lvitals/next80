	.cpu ez80
	.adl 1
	tst a
	tst (hl)
	tst 0x55
	in0 a, (0x10)
	out0 (0x20), b
	
	.adl 0
	tst c
	in0 e, (0x30)
	
	.adl 1
	ld.s hl, 0x1234
	ld.is hl, 0x5678
	ld.il hl, 0x123456
	ld.l hl, 0xABCDEF
	
	jp.s 0x1234
	jp.is 0x5678
	jp.il 0x123456
	jp.l 0xABCDEF
