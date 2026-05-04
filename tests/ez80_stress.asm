	.cpu ez80
	.adl 1

	; 1. Mistura de modos (CALL/RET)
	call foo_z80		; deve empilhar 24-bit (3 bytes)
	
	.adl 0
foo_z80:
	ret			; deve desempilhar 16-bit (CUIDADO: isso causa crash real, mas assembler deve permitir)

	.adl 1
	; 2. Stack pointer e push/pop
	push hl			; 3 bytes em ADL
	.adl 0
	pop hl			; 2 bytes em Z80 (SP fica desalinhado)

	.adl 1
	; 3. Instruções que não escalam (sempre 8-bit)
	jr start_loop
	djnz start_loop
	in0 a, (0x10)		; port 8-bit
	tst 0x55		; val 8-bit
start_loop:
	nop

	; 4. Prefix stacking (ADL + Index)
	ld.s a, (ix+10)		; SIS + DD + 46 + 0A
	ld.l a, (iy+20)		; LIL (none if ADL=1) + FD + 46 + 14
	
	; 5. LEA / PEA
	lea hl, (ix-5)		; ED 32 FB
	lea de, (iy+127)	; ED 21 7F
	pea (ix+10)		; ED 65 0A
	pea (iy-10)		; ED 66 F6

	; 6. Relocação 24-bit e Operadores
	ld hl, big_label
	ld a, low big_label
	ld a, high big_label
	ld a, upper big_label

	; 7. Limites de endereçamento
	.org 0x00FFFF
limit_16:
	nop
	.org 0x010000
above_16:
	nop

	; 8. Forward references + Tamanho variável
	jp forward_label	; ADL=1 -> 4 bytes
	ld a, (forward_label)	; ADL=1 -> 4 bytes
	
	.org 0x100000
forward_label:
	nop

	; 9. Compatibilidade silenciosa
	ld hl, 0x1234		; Deve ser 4 bytes em ADL=1 (21 34 12 00)
	ld bc, 0x5678		; 
	
big_label EQU 0xABCDEF
