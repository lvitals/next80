# Next80

> *A 8080 / Z80 / R800 / Z280 assembler toolchain for the universe of machines that speak C.*

**Next80** is a native C port of [Nestor80](https://github.com/Konamiman/Nestor80) — the modern, MACRO-80 compatible assembler originally written in C# by [Konamiman](https://github.com/Konamiman). If you have ever written Z80 assembly and wanted a fast, dependency-free toolchain that compiles from source on virtually any machine alive today, this is it.

The toolchain ships three tools:

| Tool | Replaces | Purpose |
|------|----------|---------|
| **n80** | MACRO-80 / N80 | Z80/8080/R800/Z280 assembler |
| **lk80** | LINK-80 / Linkstor80 | Relocatable linker |
| **lb80** | LIB-80 / Libstor80 | Library manager |

---

## Why a C port?

Nestor80 is an excellent piece of software. It is accurate, well-documented and feature-rich. But it requires the [.NET runtime](https://dotnet.microsoft.com/en-us/download/dotnet/) — which is perfectly fine on Windows, macOS and mainstream Linux, but becomes a real obstacle elsewhere:

* **Embedded Linux systems** (routers, SBCs, industrial controllers) rarely ship with .NET.
* **BSD variants** (FreeBSD, OpenBSD, NetBSD) — widely used in servers and embedded firmware — have no first-class .NET support.
* **Old or minimal Linux environments** used precisely by the retrocomputing community that needs a Z80 assembler.
* **Cross-compilation toolchains** that produce binaries for a different target architecture.
* **Sandboxed or air-gapped build environments** where installing a runtime is not an option.

C solves all of this. It has been the *lingua franca* of computing since 1972. A C99 compiler is present — or easily bootstrappable — on every platform that matters: Linux, macOS, Windows (MinGW/MSVC), FreeBSD, OpenBSD, Haiku, Plan 9, and anything else you can name. There is no runtime to install, no package manager to invoke, no internet connection required. You get a single small binary that does its job.

> C is not fashionable. It does not have generics or pattern matching or async/await. What it has is a half-century of compilers, linkers, and operating systems built on top of it — and that is a kind of portability no managed runtime can match.

next80 preserves 100% of the Nestor80 feature set and is fully wire-compatible with its file formats. Relocatable `.rel` files produced by `n80` can be linked by both `lk80` and the original Linkstor80. SDCC XL3 files can be consumed by both `lk80` and SDLD. There is no lock-in: the two implementations speak the same language.

---

## A heartfelt thank-you

This project would not exist without the extraordinary work of **[Konamiman](https://github.com/Konamiman)**. Nestor80 is not a quick hack: it is a carefully researched, meticulously documented assembler that takes the original MACRO-80 manual seriously and then goes further — adding Z280 support, SDCC compatibility, UTF-8 symbols, expression interpolation, and dozens of quality-of-life improvements that make writing Z80 assembly in 2025 genuinely pleasant.

**Thank you, Konamiman.** next80 is a tribute to that work.

If you find Nestor80 useful — and you should — consider [donating to Konamiman](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=konamiman@konamiman.com&item_name=Donation+to+Konamiman+for+Nestor80).

---

## Features

Everything that made Nestor80 great, now as a native binary:

* **No runtime required.** One binary, zero dependencies. Build once, run anywhere.

* Fully **compatible with [Microsoft MACRO-80](https://en.wikipedia.org/wiki/Microsoft_MACRO-80)** for both Z80 and Intel 8080 code (`.8080` / `.cpu 8080`).

* Produces **absolute and relocatable binary files**. Relocatable files conform to the extended MACRO-80/LINK-80 format or to the SDCC XL3 format — your choice.

* Full support for **Z80 undocumented instructions** (the ones that "don't exist" but totally do).

* **8080, Z80, R800 and Z280** CPU support, including all extended addressing modes, privileged instruction control, 32-bit accumulator operations (`MULTW`, `DIVW`...) and flag jumps (`JAF`, `JAR`).

* **UTF-8 symbol names** — labels and module names can be any Unicode text:

```asm
このラベルは誇張されていますがnext80がいかに強力であるかを示しています equ 34
```

* **Modern string handling**: choose the encoding for string literals (ASCII, UTF-8, Latin-1, CP1252, Shift-JIS...) and use C-style escape sequences inside strings:

```asm
    .STRENC shift_jis
HELLO_JP:
    defb "日本語でこんにちは！\0"

    .STRENC default
JOKE:
    defb "A tab,\ta newline\nand a null\0"
```

* **Expression interpolation** in user-generated messages:

```asm
if $ gt 7FFFh
.error ROM page boundary crossed — location pointer is {$:H4}h
endif
```

* **Nested `INCLUDE` files** (MACRO-80 allows only one level).

* **Modules and relative labels** in the style of [Sjasm](https://github.com/Konamiman/Sjasm):

```asm
    module GRAPHICS
INIT:
    ;...
    endmod

    module SOUND
INIT:   ; no conflict with GRAPHICS.INIT
    ;...
    endmod
```

* **50+ command-line arguments** for fine-grained control. All have sensible defaults, so you can ignore all of them until you need one.

* **Detailed, comprehensive error reporting** — unlike MACRO-80, which was famously terse.

---

## Getting started

1. **Build** the tools (see [Building](#building) below).

2. Paste this Z80 "Hello World" MSX ROM into a file named `hello.asm`:

```asm
CHGET: equ 009Fh
CHPUT: equ 00A2h

    org 4000h

    db 41h,42h
    dw START
    ds 4010h-$

START:
    call PRINT
    call CHGET
    ret

PRINT:
    ld hl,HELLO
loop:
    ld a,(hl)
    or a
    ret z
    call CHPUT
    inc hl
    jr loop

HELLO:
    db "Hello, world!\r\n"
    db "Press any key...\0"

    ds 8000h-$   ; padding to 16K
```

3. Assemble it:

```
n80 hello.asm
```

This produces `hello.bin`. To specify the output file name explicitly:

```
n80 hello.asm hello.rom
```

4. Burn to a flash cartridge, load in an MSX emulator such as [WebMSX](http://webmsx.org/), or hand it to your favourite Z80 target. Done.

---

## Building

Requirements: a C99-compatible compiler (`gcc` or `clang`) and `make`. That is all.

```sh
# Clone the repository
git clone https://github.com/lvitals/next80
cd next80

# Build all three tools
make

# Run the test suite (optional but recommended)
make test
```

### Installing

```sh
# Install to /usr/local/bin  (default)
sudo make install

# Install to a custom prefix
make PREFIX=$HOME/.local install

# With DESTDIR for packaging
make DESTDIR=/tmp/pkg PREFIX=/usr install
```

### Uninstalling

```sh
sudo make uninstall
```

### Building individual tools

```sh
make -C n80
make -C lk80
make -C lb80
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [Language reference](docs/LanguageReference.md) | Full assembly language syntax and all pseudo-operators |
| [Writing relocatable code](docs/WritingRelocatableCode.md) | Multi-file projects, public/external symbols, libraries |
| [Relocatable file format](docs/RelocatableFileFormat.md) | Byte-level format of LINK-80 `.rel` files |
| [Intel 8080 support](docs/Intel8080Support.md) | 8080-specific features and implementation details |
| [Z280 support](docs/Z280Support.md) | Z280-specific features, addressing modes, index modes |
| [SDCC file format support](docs/SdccFileFormatSupport.md) | Producing XL3 files for use with SDCC |

For command-line help:

```
n80  --help
lk80 --help
lb80 --help
```

---

## Compatibility

next80 is wire-compatible with Nestor80 and LINK-80:

* Relocatable `.rel` files produced by `n80` are accepted by the original Linkstor80, by LINK-80, and by `lk80`.
* Library `.lib` files are interchangeable between `lb80` and LIB-80.
* SDCC XL3 files produced by `n80 --build-type sdcc` are accepted by both `lk80` and SDLD.

The extended relocatable file format (files starting with the `LNKSTOR` header) is supported by `lk80` just as Linkstor80 supports it.

---

## Project origins

next80 is a port of **[Nestor80](https://github.com/Konamiman/Nestor80)**, itself a modern reimplementation of [Microsoft MACRO-80](https://en.wikipedia.org/wiki/Microsoft_MACRO-80) — the classic Z80 assembler from the CP/M era that shipped alongside LINK-80 and LIB-80. The Nestor80 project brought MACRO-80 semantics forward into the 21st century with proper Unicode support, SDCC integration, and a clean, extensible codebase. next80 takes that foundation and compiles it to metal.

---

## Bugs

The assembler language supported by MACRO-80 is genuinely complex — macros, conditional blocks, two-pass semantics, relocatable expressions. Expect bugs in edge cases. If you find one, please [open an issue](https://github.com/lvitals/next80/issues) with as much detail as possible: the error message, the offending source snippet, and ideally a minimal reproduction.

---

## License

See [LICENSE](https://github.com/lvitals/next80/blob/main/LICENSE).
