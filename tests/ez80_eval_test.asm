	.cpu ez80
	.adl 1
	ld a, low 0x123456
	ld a, high 0x123456
	ld a, upper 0x123456
	
	ld hl, 0x123456
	
	.org 0x123456
label:
	ld a, low label
	ld a, high label
	ld a, upper label
