    .cpu ez80
    .adl 1
    public main
    extrn foo
main:
    call foo
    jp main
