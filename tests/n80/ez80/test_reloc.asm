    .ez80
    .adl 1

    public _main
    extrn _helper

_main:
    ld hl, _main
    ld a, (_data_val)
    call _helper
    jp _main

    .area _DATA
_data_val:
    db 0x55
