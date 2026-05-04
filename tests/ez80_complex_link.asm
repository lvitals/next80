    .cpu ez80
    .adl 1
    extrn foo_z80
    extrn foo_adl
    public start
start:
    call foo_z80    ; call to 16-bit code from 24-bit (needs 24-bit address)
    call foo_adl    ; call to 24-bit code from 24-bit
    ret
