    .ez80
    .adl 0

    ; Z80 mode tests
    ld hl, 0x1234
    ld a, (0x5678)
    lea hl, (ix+10)
    mlt bc
    
    .adl 1
    ; ADL mode tests
    ld hl, 0x123456
    ld a, (0x789ABC)
    lea hl, (ix+10)
    pea (iy-5)
    stmix
    rsmix

    ; Mixed mode tests
    ld.l hl, (0x112233)
    ld.s hl, (0x4455)
    
    jp 0xDEADBE
    call 0xCAFEBA
