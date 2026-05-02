#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <regex.h>

char *opt_symbols_regex = NULL;
int opt_color_output = 0;
#define CLR_RESET "\033[0m"
#define CLR_RED   "\033[31m"
#define CLR_GREEN "\033[32m"
#define CLR_YEL   "\033[33m"
#define CLR_CYAN  "\033[36m"

/*
 * lk80 — Linker for LINK-80 / Nestor80 compatible .REL files.
 * Also supports ASlink XL3 text format (.REL files from SDCC / Nestor80 --build-type sdcc).
 *
 * Supported arguments (Linkstor80 compatible):
 *   --working-dir <dir>      chdir before processing any files
 *   --output-file <file>     output binary (default: output.bin)
 *   --symbols-file <file>    write symbol table (XXXX NAME format)
 *   --code <addr>            set CSEG base for following files (hex, '4000h' or '0x4000')
 *   --data <addr>            set DSEG base (hex)
 *   -o <file>                alias for --output-file
 *   -v                       verbose
 *   --no-show-banner         ignored
 *   --verbosity <n>          0 = quiet (default)
 *   --output-file-case lower ignored
 *   --output-format bin      ignored (always writes raw binary)
 *
 * Environment variables LK80_ARGS / LINKSTOR80_ARGS are parsed before
 * command-line arguments.
 */

#define MAX_SYMBOLS       16384
#define MAX_MODULES        1024
#define MAX_COMMONS         256
#define MAX_EXTERNAL_CHAINS 32768
#define OUTPUT_SIZE       65536
#define RPN_STACK_SIZE      256

/* MS-REL Extension Link Item Subtypes */
#define EXT_ARITHMETIC_OP  0x41
#define EXT_REF_EXTERNAL   0x42
#define EXT_ADDRESS        0x43

/* MS-REL Arithmetic Operator Codes */
#define OP_STORE_AS_BYTE   1
#define OP_STORE_AS_WORD   2
#define OP_HIGH            3
#define OP_LOW             4
#define OP_NOT             5
#define OP_UNARY_MINUS     6
#define OP_MINUS           7
#define OP_PLUS            8
#define OP_MULTIPLY        9
#define OP_DIVIDE          10
#define OP_MOD             11
#define OP_SHR             16
#define OP_SHL             17
#define OP_EQ              18
#define OP_NE              19
#define OP_LT              20
#define OP_LE              21
#define OP_GT              22
#define OP_GE              23
#define OP_AND             24
#define OP_OR              25
#define OP_XOR             26

/* XL3 area flags */
#define XLF_AREA_OVR  0x04
#define XLF_AREA_ABS  0x08

/* XL3 relocation entry flags */
#define XLF_REL_BYTE  0x01  /* byte output (3-byte T-line group) */
#define XLF_REL_SYM   0x02  /* symbol reference (vs area reference) */
#define XLF_REL_TWOB  0x08  /* 2-byte object format (used with BYTE for XL3) */
#define XLF_REL_MSB   0x80  /* use high byte (for BYTE relocations) */

/* XL3 limits */
#define MAX_SDCC_AREAS        512
#define MAX_AREAS_PER_MODULE   64
#define MAX_EXTSYMS_PER_MOD    64
#define XLF_LINE_MAX          512

static const unsigned char extended_header[16] = {
    0x85, 0xD3, 0x13, 0x92, 0xD4, 0xD5, 0x13, 0xD4,
    0xA5, 0x00, 0x00, 0x13, 0x8F, 0xFF, 0xF0, 0x9E
};

typedef enum {
    ADDR_ASEG   = 0,
    ADDR_CSEG   = 1,
    ADDR_DSEG   = 2,
    ADDR_COMMON = 3,
    ADDR_SDCC   = 4   /* SDCC area-relative symbol */
} AddressType;

typedef struct {
    char name[256];
    unsigned int value;
    int segment;          /* ADDR_* ; ADDR_SDCC for XL3 symbols */
    int module_index;
    int common_index;
    int sdcc_area_local;  /* index into mod_area_insts[module_index][], or -1 */
} Symbol;

typedef struct {
    char name[256];
    unsigned int size;
    unsigned int base_addr;
} CommonBlock;

typedef struct {
    char name[256];
    unsigned int cseg_size;
    unsigned int dseg_size;
    unsigned int cseg_offset;
    unsigned int dseg_offset;
    unsigned int section_base;
    int common_indices[MAX_COMMONS];
    int num_commons;
    int has_data;
    int is_sdcc;          /* 1 = XL3 format module */
} Module;

/* ---- SDCC / XL3 area structures ---- */

typedef struct {
    char         name[64];
    int          is_abs;
    int          is_ovr;
    unsigned int size;    /* CON: accumulated; OVR/ABS: max size */
    unsigned int base;    /* resolved absolute base (ABS areas: 0, T offsets are absolute) */
} SdccGlobalArea;

typedef struct {
    int          global_idx;   /* index into sdcc_global_areas[] */
    unsigned int offset;       /* offset within global area (CON only) */
    unsigned int contrib;      /* bytes contributed by this module */
    unsigned int base;         /* resolved absolute base for this instance */
} SdccAreaInst;

static SdccGlobalArea sdcc_global_areas[MAX_SDCC_AREAS];
static int            sdcc_global_area_count = 0;

static SdccAreaInst   mod_area_insts[MAX_MODULES][MAX_AREAS_PER_MODULE];
static int            mod_area_inst_count[MAX_MODULES];

/* Symbol names per SDCC module, indexed exactly as the module's S records.
 * Relocation records reference this local symbol table directly. */
static char mod_ext_syms[MAX_MODULES][MAX_EXTSYMS_PER_MOD][64];
static int  mod_ext_sym_count[MAX_MODULES];

/* ---- Main tables ---- */

typedef struct {
    char name[64];
    unsigned int addr;
} AreaArg;
#define MAX_AREA_ARGS 64
static AreaArg area_args[MAX_AREA_ARGS];
static int area_arg_count = 0;

#define MAX_LIB_DIRS 64
static char *lib_dirs[MAX_LIB_DIRS];
static int lib_dir_count = 0;

static Symbol     symbols[MAX_SYMBOLS];
static int        symbol_count = 0;

static Module     modules[MAX_MODULES];
static int        module_count = 0;
static int        file_num_modules[MAX_MODULES];

static CommonBlock commons[MAX_COMMONS];
static int         common_count = 0;

static unsigned int entry_point = 0;
static int          entry_point_found = 0;
static int          entry_seg = 0;

static unsigned char output_buffer[OUTPUT_SIZE];
static unsigned char output_touched[OUTPUT_SIZE];

typedef struct {
    char name[256];
    unsigned int val;
    unsigned int seg_offset;
    unsigned int seg_limit;
    int pending_offset;
} ExternalChain;

static ExternalChain external_chains[MAX_EXTERNAL_CHAINS];
static int external_chain_count = 0;

typedef enum { FMT_BIN, FMT_HEX } OutputFormat;
typedef enum { SYM_L80, SYM_JSON, SYM_EQUS } SymbolFormat;

static int verbose = 0;
static unsigned int align_code = 1;
static unsigned int align_data = 1;
static unsigned int fill_byte = 0;
static int fill_set = 0;
static unsigned int start_limit = 0x0000;
static int start_set = 0;
static unsigned int end_limit = 0xFFFF;
static int end_set = 0;
static OutputFormat out_fmt = FMT_BIN;
static SymbolFormat sym_fmt = SYM_L80;
static int opt_show_banner = 1;
static int opt_file_case_lower = 0;
static int opt_file_explicit = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static unsigned int align_addr(unsigned int addr, unsigned int alignment) {
    if (alignment <= 1) return addr;
    return (addr + (alignment - 1)) & ~(alignment - 1);
}

static unsigned int parse_hex_arg(const char *s) {
    if (!s || !*s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (unsigned int)strtoul(s + 2, NULL, 16);
    size_t len = strlen(s);
    if (len > 0 && (s[len - 1] == 'h' || s[len - 1] == 'H')) {
        char buf[32];
        if (len - 1 < sizeof(buf)) {
            memcpy(buf, s, len - 1);
            buf[len - 1] = '\0';
            return (unsigned int)strtoul(buf, NULL, 16);
        }
    }
    return (unsigned int)strtoul(s, NULL, 16);
}

static FILE *open_rel_file(const char *fname, const char *mode) {
    FILE *f = fopen(fname, mode);
    if (f) return f;

    if (fname[0] == '/' || fname[0] == '.') return NULL;

    for (int i = 0; i < lib_dir_count; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", lib_dirs[i], fname);
        f = fopen(path, mode);
        if (f) return f;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* BitReader                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    FILE *f;
    unsigned char current_byte;
    int bits_left;
} BitReader;

static int read_bit(BitReader *r) {
    if (r->bits_left == 0) {
        if (fread(&r->current_byte, 1, 1, r->f) != 1)
            return -1;
        r->bits_left = 8;
    }
    int bit = (r->current_byte >> (r->bits_left - 1)) & 1;
    r->bits_left--;
    return bit;
}

static unsigned int read_bits(BitReader *r, int n) {
    unsigned int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = read_bit(r);
        if (bit == -1) return 0;
        val = (val << 1) | bit;
    }
    return val;
}

static unsigned int read_16le(BitReader *r) {
    unsigned int lo = read_bits(r, 8);
    unsigned int hi = read_bits(r, 8);
    return lo | (hi << 8);
}

static void read_symbol(BitReader *r, char *dest, int dest_size) {
    int len = (int)read_bits(r, 3);
    if (len >= 2 && len <= 5) {
        unsigned int b1 = read_bits(r, 8);
        if (b1 == 0xFF) {
            int actual_len_bytes = len - 1;
            unsigned int actual_len = 0;
            for (int j = 0; j < actual_len_bytes; j++)
                actual_len |= (read_bits(r, 8) << (j * 8));
            len = (int)actual_len;
            if (len >= 256) r->bits_left = 0;
            int i;
            for (i = 0; i < len; i++) {
                char c = (char)read_bits(r, 8);
                if (i < dest_size - 1) dest[i] = c;
            }
            dest[(i < dest_size - 1) ? i : dest_size - 1] = '\0';
            return;
        } else {
            if (dest_size > 1) dest[0] = (char)b1;
            int i;
            for (i = 1; i < len; i++) {
                char c = (char)read_bits(r, 8);
                if (i < dest_size - 1) dest[i] = c;
            }
            dest[(i < dest_size - 1) ? i : dest_size - 1] = '\0';
            return;
        }
    }
    int i;
    for (i = 0; i < len; i++) {
        char c = (char)read_bits(r, 8);
        if (i < dest_size - 1) dest[i] = c;
    }
    dest[(i < dest_size - 1) ? i : dest_size - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Lookup helpers                                                      */
/* ------------------------------------------------------------------ */

static Symbol *find_symbol(const char *name) {
    for (int i = 0; i < symbol_count; i++)
        if (strcasecmp(symbols[i].name, name) == 0)
            return &symbols[i];
    return NULL;
}

static int find_common(const char *name) {
    for (int i = 0; i < common_count; i++)
        if (strcasecmp(commons[i].name, name) == 0)
            return i;
    return -1;
}

static void init_module(Module *mod, const char *filename, unsigned int sec_base) {
    snprintf(mod->name, sizeof(mod->name), "%s", filename);
    mod->cseg_size   = 0;
    mod->dseg_size   = 0;
    mod->num_commons = 0;
    mod->section_base = sec_base;
    mod->has_data = 0;
    mod->is_sdcc = 0;
}

static int find_area_arg(const char *name) {
    for (int i = 0; i < area_arg_count; i++)
        if (strcasecmp(area_args[i].name, name) == 0)
            return i;
    return -1;
}

static int resolve_sdcc_area_symbol(const char *name, unsigned int *value) {
    if (!name || strlen(name) < 4 || name[1] != '_' || name[2] != '_')
        return 0;
    char area_name[64];
    snprintf(area_name, sizeof(area_name), "_%s", name + 3);
    for (int i = 0; i < sdcc_global_area_count; i++) {
        if (strcasecmp(sdcc_global_areas[i].name, area_name) == 0) {
            if (name[0] == 's' || name[0] == 'S') {
                *value = sdcc_global_areas[i].base;
                return 1;
            }
            if (name[0] == 'l' || name[0] == 'L') {
                *value = sdcc_global_areas[i].size;
                return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Resolve absolute value of a symbol (for pass-2 and symbol output)  */
/* ------------------------------------------------------------------ */

static unsigned int resolve_symbol_abs(const Symbol *s) {
    unsigned int val = s->value;
    if (s->segment == ADDR_SDCC) {
        if (s->module_index < module_count && s->sdcc_area_local >= 0)
            val += mod_area_insts[s->module_index][s->sdcc_area_local].base;
    } else if (s->segment == ADDR_CSEG && s->module_index < module_count) {
        val += modules[s->module_index].cseg_offset;
    } else if (s->segment == ADDR_DSEG && s->module_index < module_count) {
        val += modules[s->module_index].dseg_offset;
    } else if (s->segment == ADDR_COMMON && s->common_index >= 0) {
        val += commons[s->common_index].base_addr;
    }
    return val;
}

/* ------------------------------------------------------------------ */
/* Argument Management                                                */
/* ------------------------------------------------------------------ */

#define ARG_CAPACITY 1024
static char *merged_argv[ARG_CAPACITY];
static int merged_argc = 0;

static void add_arg(char *arg) {
    if (merged_argc < ARG_CAPACITY - 1) {
        merged_argv[merged_argc++] = arg;
        merged_argv[merged_argc] = NULL;
    }
}

static void parse_arg_string(char *s) {
    char *p = s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char *token_start = p;
        char *src = p;
        char *dst = p;
        int in_quotes = 0;

        while (*src) {
            if (*src == '\\' && src[1]) {
                src++;
                *dst++ = *src++;
            } else if (*src == '"') {
                in_quotes = !in_quotes;
                src++;
            } else if (!in_quotes && isspace((unsigned char)*src)) {
                break;
            } else {
                *dst++ = *src++;
            }
        }

        char next_char = *src;
        *dst = '\0';
        add_arg(token_start);
        
        if (next_char == '\0') break;
        p = src + 1;
    }
}

static void parse_arg_file(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (fread(buf, 1, sz, f) == (size_t)sz) {
        buf[sz] = 0;
        parse_arg_string(buf);
    }
    fclose(f);
}

static int has_arg(int argc, char **argv, const char *pattern) {
    for (int i = 1; i < argc; i++)
        if (argv[i] && strcmp(argv[i], pattern) == 0) return 1;
    return 0;
}
static void reset_lk80_config(unsigned int *code, unsigned int *data, int *data_set, char **out, char **syms) {
    *code = 0x0100;
    *data = 0;
    *data_set = 0;
    *out = "output.bin";
    *syms = NULL;
    align_code = 1;
    align_data = 1;
    fill_byte = 0;
    fill_set = 0;
    start_limit = 0x0000;
    start_set = 0;
    end_limit = 0xFFFF;
    end_set = 0;
    out_fmt = FMT_BIN;
    sym_fmt = SYM_L80;
    opt_file_case_lower = 0;
    opt_show_banner = 1;
    verbose = 0;
    area_arg_count = 0;
    lib_dir_count = 0;
}

/* ------------------------------------------------------------------ */
/* Skip MS-REL extended header if present                             */
/* ------------------------------------------------------------------ */

static void skip_header(BitReader *r) {
    if (r->bits_left > 0) r->bits_left = 0;
    long pos = ftell(r->f);
    unsigned char head[16];
    if (fread(head, 1, 16, r->f) == 16 && memcmp(head, extended_header, 16) == 0) {
        /* skipped */
    } else {
        fseek(r->f, pos, SEEK_SET);
    }
}

/* ================================================================== */
/* XL3 / ASlink format support                                        */
/* ================================================================== */

/* Returns 1 if the file starts with "XL" (XL3 text format) */
static int is_xlf_file(const char *fname) {
    FILE *f = open_rel_file(fname, "r");
    if (!f) return 0;
    char buf[4] = {0};
    int r = (fread(buf, 1, 2, f) == 2 && buf[0] == 'X' && buf[1] == 'L');
    fclose(f);
    return r;
}

/* Find or create a global SDCC area entry */
static int find_or_create_sdcc_area(const char *name, int is_abs, int is_ovr) {
    for (int i = 0; i < sdcc_global_area_count; i++)
        if (strcasecmp(sdcc_global_areas[i].name, name) == 0)
            return i;
    if (sdcc_global_area_count >= MAX_SDCC_AREAS) return 0;
    int i = sdcc_global_area_count++;
    snprintf(sdcc_global_areas[i].name, sizeof(sdcc_global_areas[i].name), "%s", name);
    sdcc_global_areas[i].is_abs = is_abs;
    sdcc_global_areas[i].is_ovr = is_ovr;
    sdcc_global_areas[i].size = 0;
    sdcc_global_areas[i].base = 0;
    return i;
}

/* Parse hex bytes from a T or R line (e.g. "3E 2A 00") into bytes[].
 * Returns count. line points past the line-type prefix (e.g. past "T "). */
static int xlf_parse_hex_bytes(const char *line, unsigned char *bytes, int max) {
    int count = 0;
    const char *p = line;
    while (*p && count < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!isxdigit((unsigned char)*p)) break;
        char buf[3] = {p[0], isxdigit((unsigned char)p[1]) ? p[1] : '0', '\0'};
        bytes[count++] = (unsigned char)strtoul(buf, NULL, 16);
        p += (isxdigit((unsigned char)p[1]) ? 2 : 1);
    }
    return count;
}

/*
 * XL3 Pass 1: scan one file, collecting module/area/symbol metadata.
 * Does NOT process T/R lines. Fills modules[], symbols[], sdcc_global_areas[],
 * mod_area_insts[][], mod_ext_syms[][].
 * Returns number of modules found in this file.
 */
static int xlf_pass1(const char *fname, unsigned int code_base) {
    FILE *f = open_rel_file(fname, "r");
    if (!f) { fprintf(stderr, "lk80: cannot open '%s'\n", fname); return 0; }

    char line[XLF_LINE_MAX];
    int mods_found = 0;
    int in_module = 0;
    int current_area_local = -1;  /* local area index within current module */
    int m = -1;

    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';

        if (strncmp(line, "XL", 2) == 0 && (line[2] == '1'||line[2]=='2'||line[2]=='3'||line[2]=='4')) {
            /* New module starts */
            if (module_count >= MAX_MODULES) break;
            m = module_count++;
            mods_found++;
            in_module = 1;
            current_area_local = -1;
            init_module(&modules[m], fname, code_base);
            modules[m].is_sdcc = 1;
            mod_area_inst_count[m] = 0;
            mod_ext_sym_count[m] = 0;
            continue;
        }

        if (!in_module || m < 0) continue;

        if (line[0] == 'M' && line[1] == ' ') {
            char mname[256];
            if (sscanf(line + 2, "%255s", mname) == 1)
                snprintf(modules[m].name, sizeof(modules[m].name), "%s", mname);
        } else if (line[0] == 'A' && line[1] == ' ') {
            /* A name size SS flags FF addr AA */
            char aname[64] = {0};
            unsigned int asize = 0;
            unsigned int aflags = 0;
            unsigned int aaddr = 0;
            if (sscanf(line + 2, "%63s size %x flags %x addr %x", aname, &asize, &aflags, &aaddr) < 3)
                sscanf(line + 2, "%63s size %x flags %x", aname, &asize, &aflags);

            int is_abs = (aflags & XLF_AREA_ABS) ? 1 : 0;
            int is_ovr = (aflags & XLF_AREA_OVR) ? 1 : 0;
            int gi = find_or_create_sdcc_area(aname, is_abs, is_ovr);
            SdccGlobalArea *ga = &sdcc_global_areas[gi];

            if (mod_area_inst_count[m] >= MAX_AREAS_PER_MODULE) continue;
            int li = mod_area_inst_count[m]++;
            SdccAreaInst *inst = &mod_area_insts[m][li];
            inst->global_idx = gi;
            inst->contrib    = asize;
            inst->base       = 0; /* resolved later */

            if (is_abs) {
                /* ABS areas: T line offsets are absolute. inst->base stays 0.
                 * ga->size tracks the max span. */
                if (asize > ga->size) ga->size = asize;
                inst->offset = 0;
            } else if (is_ovr) {
                /* OVR: all modules share the same base, take the max size */
                if (asize > ga->size) ga->size = asize;
                inst->offset = 0;
            } else {
                /* CON: concatenate contributions */
                inst->offset = ga->size;
                ga->size += asize;
            }

            current_area_local = li;
            modules[m].has_data = 1;
        } else if (line[0] == 'S' && line[1] == ' ') {
            /* S name Defnnnnnn  or  S name Refnnnnnn */
            char sname[64] = {0};
            char defref[16] = {0};
            if (sscanf(line + 2, "%63s %15s", sname, defref) != 2) continue;

            if (mod_ext_sym_count[m] < MAX_EXTSYMS_PER_MOD) {
                snprintf(mod_ext_syms[m][mod_ext_sym_count[m]], 64, "%s", sname);
                mod_ext_sym_count[m]++;
            }

            if (strncasecmp(defref, "Ref", 3) == 0) {
                /* References are resolved during pass 2. */
            } else if (strncasecmp(defref, "Def", 3) == 0) {
                /* Public definition */
                if (strcasecmp(sname, ".__.ABS.") == 0) continue;
                unsigned int sval = (unsigned int)strtoul(defref + 3, NULL, 16);
                if (symbol_count >= MAX_SYMBOLS) continue;
                Symbol *s = &symbols[symbol_count++];
                snprintf(s->name, sizeof(s->name), "%s", sname);
                s->value            = sval;
                s->segment          = ADDR_SDCC;
                s->module_index     = m;
                s->common_index     = -1;
                s->sdcc_area_local  = current_area_local; /* -1 if before first A line */
            }
        }
        /* T and R lines are skipped in pass 1 */
    }

    fclose(f);
    return mods_found;
}

/*
 * XL3 Pass 2: re-read one file, process T/R lines, write to output_buffer.
 * mod_start is the index of the first module from this file in modules[].
 * num_mods is the count of modules from this file.
 * min_addr / max_addr are updated.
 */
static void xlf_pass2(const char *fname, int mod_start, int num_mods,
                      unsigned int *min_addr, unsigned int *max_addr)
{
    FILE *f = open_rel_file(fname, "r");
    if (!f) return;

    char line[XLF_LINE_MAX];
    int m = mod_start - 1;
    int mods_seen = 0;
    int t_len = 0;
    int have_t = 0;
    unsigned char t_bytes[XLF_LINE_MAX];
    unsigned char r_bytes[XLF_LINE_MAX];
    int xlf_addr_size = 3;

    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';

        if (strncmp(line, "XL", 2) == 0 && (line[2]=='1'||line[2]=='2'||line[2]=='3'||line[2]=='4')) {
            if (mods_seen >= num_mods) break;
            m = mod_start + mods_seen;
            mods_seen++;
            xlf_addr_size = line[2] - '0';
            have_t = 0;
            continue;
        }

        if (m < mod_start || m >= mod_start + num_mods) continue;

        if (line[0] == 'T' && line[1] == ' ') {
            /* Store T line bytes for the upcoming R line */
            t_len = xlf_parse_hex_bytes(line + 2, t_bytes, (int)(sizeof(t_bytes)));
            have_t = 1;
        } else if (line[0] == 'R' && line[1] == ' ' && have_t) {
            int r_len = xlf_parse_hex_bytes(line + 2, r_bytes, (int)(sizeof(r_bytes)));
            have_t = 0;

            if (t_len < xlf_addr_size || r_len < 4) continue;

            /* T line starts with a little-endian address field:
             * XL3 uses 3 bytes, XL4 uses 4 bytes. */
            unsigned int t_addr = 0;
            for (int bi = 0; bi < xlf_addr_size && bi < 4; bi++)
                t_addr |= (unsigned int)t_bytes[bi] << (8 * bi);

            /* R line: [00 00 ai_lo ai_hi [flags off si_lo si_hi] ...] */
            int area_local_idx = (int)((unsigned int)r_bytes[2] | ((unsigned int)r_bytes[3] << 8));
            if (area_local_idx < 0 || area_local_idx >= mod_area_inst_count[m]) continue;

            unsigned int area_base = mod_area_insts[m][area_local_idx].base;

            /* Build reloc lookup: for each reloc entry in R line, key = T-byte offset */
            typedef struct { int offset; int flags; int idx; } Reloc;
            Reloc relocs[64];
            int nrelocs = 0;
            for (int ri = 4; ri + 3 < r_len && nrelocs < 64; ri += 4) {
                relocs[nrelocs].flags  = r_bytes[ri];
                relocs[nrelocs].offset = r_bytes[ri + 1];
                relocs[nrelocs].idx    = (int)((unsigned int)r_bytes[ri + 2] | ((unsigned int)r_bytes[ri + 3] << 8));
                nrelocs++;
            }

            /* Iterate through T data bytes after the address field. */
            int out_offset = 0;  /* byte offset in output from area_base + t_addr */
            int ti = xlf_addr_size;
            while (ti < t_len) {
                int found_reloc = -1;
                for (int ri = 0; ri < nrelocs; ri++) {
                    if (relocs[ri].offset == ti) { found_reloc = ri; break; }
                }

                if (found_reloc >= 0) {
                    int rflags = relocs[found_reloc].flags;
                    int ridx   = relocs[found_reloc].idx;
                    int is_byte = (rflags & XLF_REL_BYTE) && (rflags & XLF_REL_TWOB);
                    int is_sym  = (rflags & XLF_REL_SYM) ? 1 : 0;
                    int is_msb  = (rflags & XLF_REL_MSB) ? 1 : 0;

                    unsigned int raw = 0;
                    if (ti + 1 < t_len) raw = (unsigned int)t_bytes[ti] | ((unsigned int)t_bytes[ti + 1] << 8);

                    unsigned int base = 0;
                    if (is_sym) {
                        if (ridx >= 0 && ridx < mod_ext_sym_count[m]) {
                            if (strcasecmp(mod_ext_syms[m][ridx], ".__.ABS.") != 0) {
                                unsigned int area_sym_value = 0;
                                if (resolve_sdcc_area_symbol(mod_ext_syms[m][ridx], &area_sym_value)) {
                                    base = area_sym_value;
                                } else {
                                    Symbol *s = find_symbol(mod_ext_syms[m][ridx]);
                                    if (s) {
                                        base = resolve_symbol_abs(s);
                                    } else if (verbose) {
                                        fprintf(stderr, "lk80: unresolved symbol '%s' in %s\n",
                                                mod_ext_syms[m][ridx], fname);
                                    }
                                }
                            }
                        }
                    } else {
                        if (ridx < mod_area_inst_count[m])
                            base = mod_area_insts[m][ridx].base;
                    }

                    unsigned int resolved = raw + base;
                    unsigned int out_addr = area_base + t_addr + (unsigned int)out_offset;

                    if (is_byte) {
                        unsigned char byte_val = is_msb
                            ? (unsigned char)((resolved >> 8) & 0xFF)
                            : (unsigned char)(resolved & 0xFF);
                        if (out_addr < OUTPUT_SIZE) {
                            output_buffer[out_addr] = byte_val;
                            output_touched[out_addr] = 1;
                            if (out_addr < *min_addr) *min_addr = out_addr;
                            if (out_addr > *max_addr) *max_addr = out_addr;
                        }
                        out_offset++;
                        ti += xlf_addr_size;
                    } else {
                        if (out_addr + 1 < OUTPUT_SIZE) {
                            output_buffer[out_addr]     = (unsigned char)(resolved & 0xFF);
                            output_buffer[out_addr + 1] = (unsigned char)(resolved >> 8);
                            output_touched[out_addr] = 1;
                            output_touched[out_addr + 1] = 1;
                            if (out_addr < *min_addr)     *min_addr = out_addr;
                            if (out_addr + 1 > *max_addr) *max_addr = out_addr + 1;
                        }
                        out_offset += 2;
                        ti += 2;
                    }
                } else {
                    unsigned int out_addr = area_base + t_addr + (unsigned int)out_offset;
                    if (out_addr < OUTPUT_SIZE) {
                        output_buffer[out_addr] = t_bytes[ti];
                        output_touched[out_addr] = 1;
                        if (out_addr < *min_addr) *min_addr = out_addr;
                        if (out_addr > *max_addr) *max_addr = out_addr;
                    }
                    out_offset++;
                    ti++;
                }
            }
        }
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void write_hex_record(FILE *f, int type, int addr, unsigned char *data, int len) {
    unsigned char sum = (unsigned char)(len + (addr >> 8) + (addr & 0xFF) + type);
    fprintf(f, ":%02X%04X%02X", len, addr & 0xFFFF, type);
    for (int i = 0; i < len; i++) {
        fprintf(f, "%02X", data[i]);
        sum += data[i];
    }
    fprintf(f, "%02X\n", (unsigned char)(0x100 - sum));
}

static void write_intel_hex(FILE *f, unsigned int min, unsigned int max) {
    unsigned char line[16];
    int len = 0, last_addr = -2;
    for (unsigned int a = min; a <= max; a++) {
        if (!output_touched[a]) {
            if (len > 0) { write_hex_record(f, 0, a - len, line, len); len = 0; }
            continue;
        }
        if (len == 16 || (len > 0 && a != (unsigned int)(last_addr + 1))) {
            write_hex_record(f, 0, a - len, line, len);
            len = 0;
        }
        line[len++] = output_buffer[a];
        last_addr = (int)a;
    }
    if (len > 0) write_hex_record(f, 0, (unsigned int)(last_addr - len + 1), line, len);
    write_hex_record(f, 1, 0, NULL, 0);
}

int main(int argc, char *argv[]) {
	merged_argc = 0;
	add_arg(argv[0]);

	int use_env = !has_arg(argc, argv, "--no-env-args");
	int use_def_file = !has_arg(argc, argv, "--no-default-file-args") && !has_arg(argc, argv, "--no-def-file-args");

	if (use_env) {
		const char *env = getenv("LK80_ARGS");
		if (!env) env = getenv("LINKSTOR80_ARGS");
		if (env) {
			char *ecopy = strdup(env);
			parse_arg_string(ecopy);
		}
	}

	if (use_def_file) {
		parse_arg_file(".LK80");
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--arguments-file") == 0 || strcmp(argv[i], "--args-file") == 0) {
			if (i + 1 < argc) {
				parse_arg_file(argv[++i]);
			}
		} else {
			add_arg(argv[i]);
		}
	}

    if (merged_argc < 2 || (merged_argv[1] && (strcmp(merged_argv[1], "-h") == 0 || strcmp(merged_argv[1], "--help") == 0))) {
        printf("lk80 - Linker for .REL files (Nestor80/LINK-80 compatible)\n\n");
        printf("Usage:\n");
        printf("  lk80 [options] <rel-files...> [-o <output-file>]\n\n");
        printf("Linker Options:\n");
        printf("  -o, --output-file <file>      Name of the output file (default: output.bin)\n");
        printf("  --output-format <bin|hex>     Output file format (binary or Intel HEX)\n");
        printf("  --code <addr>                 Base address for CSEG (hex, e.g. 0100 or 100h)\n");
        printf("  --data <addr>                 Base address for DSEG (hex)\n");
        printf("  --fill <byte>                 Byte used to fill memory gaps (hex)\n");
        printf("  --start <addr>                Force output to start at this address (hex)\n");
        printf("  --end <addr>                  Force output to end at this address (hex)\n");
        printf("  --align-code <val>            Alignment for code segments (hex)\n");
        printf("  --align-data <val>            Alignment for data segments (hex)\n");
        printf("  --area <name> <addr>          Force base address for a specific SDCC area\n\n");
        printf("Symbol Options:\n");
        printf("  --symbols-file <file>         Generate a symbol table file\n");
        printf("  --symbols-file-format <fmt>   Format: l80 (default), json, or equs\n\n");
        printf("Global Options:\n");
        printf("  --arguments-file <file>       Include arguments from a file\n");
        printf("  --library-dir <dir>           Directory to search for missing REL files\n");
        printf("  --working-dir <dir>           Change directory before linking\n");
        printf("  --output-file-case lower      Lowercase the output filename\n");
        printf("  --no-show-banner              Suppress the startup greeting\n");
        printf("  --reset-config                Reset configuration to defaults\n");
        printf("  -v, --verbosity <n>           Set log level (0=quiet, 1=normal)\n");
        return 0;
    }

    unsigned int current_code_base = 0x0100;
    unsigned int data_base = 0;
    int data_base_set = 0;
    char *output_file  = "output.bin";
    char *symbols_file = NULL;

    int          file_indices  [MAX_MODULES];
    unsigned int file_code_base[MAX_MODULES];
    int          file_count = 0;

    for (int i = 1; i < merged_argc; i++) {
        const char *a = merged_argv[i];
        if (strcmp(a, "--reset-config") == 0) {
            reset_lk80_config(&current_code_base, &data_base, &data_base_set, &output_file, &symbols_file);
        } else if (strcmp(a, "--no-env-args") == 0 || strcmp(a, "--no-default-file-args") == 0 || strcmp(a, "--no-def-file-args") == 0) {
            /* handled in pre-scan */
        } else if (strcmp(a, "--working-dir") == 0 && i + 1 < merged_argc) {
            if (chdir(merged_argv[++i]) != 0) { perror("lk80: chdir"); return 1; }
        } else if ((strcmp(a, "--output-file") == 0 || strcmp(a, "-o") == 0) && i + 1 < merged_argc) {
            output_file = merged_argv[++i];
            opt_file_explicit = 1;
        } else if (strcmp(a, "--symbols-file") == 0 && i + 1 < merged_argc) {
            symbols_file = merged_argv[++i];
        } else if (strcmp(a, "--symbols-file-regex") == 0 && i + 1 < merged_argc) {
            opt_symbols_regex = merged_argv[++i];
        } else if (strcmp(a, "--symbols-file-format") == 0 && i + 1 < merged_argc) {
            const char *fmt = merged_argv[++i];
            if (strcasecmp(fmt, "json") == 0) sym_fmt = SYM_JSON;
            else if (strcasecmp(fmt, "equs") == 0) sym_fmt = SYM_EQUS;
            else sym_fmt = SYM_L80;
        } else if (strcmp(a, "--color-output") == 0) {
            opt_color_output = 1;
        } else if (strcmp(a, "--output-format") == 0 && i + 1 < merged_argc) {
            const char *fmt = merged_argv[++i];
            if (strcasecmp(fmt, "hex") == 0) out_fmt = FMT_HEX;
            else out_fmt = FMT_BIN;
        } else if (strcmp(a, "--fill") == 0 && i + 1 < merged_argc) {
            fill_byte = parse_hex_arg(merged_argv[++i]) & 0xFF;
            fill_set = 1;
        } else if (strcmp(a, "--start") == 0 && i + 1 < merged_argc) {
            start_limit = parse_hex_arg(merged_argv[++i]);
            start_set = 1;
        } else if (strcmp(a, "--end") == 0 && i + 1 < merged_argc) {
            end_limit = parse_hex_arg(merged_argv[++i]);
            end_set = 1;
        } else if (strcmp(a, "--code") == 0 && i + 1 < merged_argc) {
            current_code_base = parse_hex_arg(merged_argv[++i]);
        } else if (strcmp(a, "--data") == 0 && i + 1 < merged_argc) {
            data_base = parse_hex_arg(merged_argv[++i]);
            data_base_set = 1;
        } else if (strcmp(a, "--library-dir") == 0 && i + 1 < merged_argc) {
            if (lib_dir_count < MAX_LIB_DIRS) {
                lib_dirs[lib_dir_count++] = merged_argv[++i];
            } else {
                fprintf(stderr, "lk80: too many library directories\n");
                i++;
            }
        } else if (strcmp(a, "--align-code") == 0 && i + 1 < merged_argc) {
            align_code = parse_hex_arg(merged_argv[++i]);
        } else if (strcmp(a, "--align-data") == 0 && i + 1 < merged_argc) {
            align_data = parse_hex_arg(merged_argv[++i]);
        } else if (strcmp(a, "--area") == 0 && i + 2 < merged_argc) {
            if (area_arg_count < MAX_AREA_ARGS) {
                snprintf(area_args[area_arg_count].name, 64, "%s", merged_argv[++i]);
                area_args[area_arg_count].addr = parse_hex_arg(merged_argv[++i]);
                area_arg_count++;
            }
        } else if (strcmp(a, "-v") == 0) {
            verbose = 1;
        } else if (strcmp(a, "--verbosity") == 0 && i + 1 < merged_argc) {
            verbose = (atoi(merged_argv[++i]) > 0);
        } else if (strcmp(a, "--no-show-banner") == 0) {
            opt_show_banner = 0;
        } else if (strcmp(a, "--output-file-case") == 0 && i + 1 < merged_argc) {
            if (strcasecmp(merged_argv[++i], "lower") == 0) opt_file_case_lower = 1;
        } else if (a[0] == '-') {
            if (verbose) fprintf(stderr, "lk80: ignoring unknown option '%s'\n", a);
        } else {
            if (file_count < MAX_MODULES) {
                file_indices [file_count] = i;
                file_code_base[file_count] = current_code_base;
                file_count++;
            }
        }
    }

    /* ---- PASS 1 ---- */
    for (int i = 0; i < file_count; i++) {
        int start_module_count = module_count;
        const char *fname = merged_argv[file_indices[i]];

        if (is_xlf_file(fname)) {
            int n = xlf_pass1(fname, file_code_base[i]);
            file_num_modules[i] = n;
            continue;
        }

        /* MS-REL binary format */
        FILE *f = open_rel_file(fname, "rb");
        if (!f) { fprintf(stderr, "lk80: cannot open '%s'\n", fname); continue; }

        BitReader r = {f, 0, 0};
        skip_header(&r);

        Module *mod = &modules[module_count++];
        init_module(mod, fname, file_code_base[i]);

        int need_new_module = 0;
        int current_pc = 0;
        int max_pc = 0;
        int has_cseg = 0;

        while (1) {
            if (need_new_module) {
                skip_header(&r);
                if (module_count >= MAX_MODULES) break;
                mod = &modules[module_count++];
                init_module(mod, fname, file_code_base[i]);
                need_new_module = 0;
                current_pc = 0;
                max_pc = 0;
                has_cseg = 0;
            }

            int first_bit = read_bit(&r);
            if (first_bit == -1) break;

            if (first_bit == 0) {
                read_bits(&r, 8);
                mod->has_data = 1;
                has_cseg = 1;
                current_pc++;
                if (current_pc > max_pc) max_pc = current_pc;
            } else {
                int t2 = (int)read_bits(&r, 2);
                if (t2 == 0) {
                    int ctrl = (int)read_bits(&r, 4);
                    char sym[1024] = {0};
                    if (ctrl <= 4) {
                        read_symbol(&r, sym, sizeof(sym));
                        if (ctrl == 2) snprintf(mod->name, sizeof(mod->name), "%s", sym);
                        else if (ctrl == 4 && (unsigned char)sym[0] == 0x41) {
                            int op = (unsigned char)sym[1];
                            if (op == OP_STORE_AS_BYTE || op == OP_STORE_AS_WORD) {
                                has_cseg = 1;
                                current_pc += (op == OP_STORE_AS_WORD ? 2 : 1);
                                if (current_pc > max_pc) max_pc = current_pc;
                            }
                        }
                    } else if (ctrl <= 7) {
                        int seg = (int)read_bits(&r, 2);
                        unsigned int val = read_16le(&r);
                        read_symbol(&r, sym, sizeof(sym));
                        if (ctrl == 5) {
                            int ci = find_common(sym);
                            if (ci < 0 && common_count < MAX_COMMONS) {
                                ci = common_count++;
                                snprintf(commons[ci].name, sizeof(commons[ci].name), "%s", sym);
                                commons[ci].size = val;
                            } else if (ci >= 0 && val > commons[ci].size) {
                                commons[ci].size = val;
                            }
                            if (ci >= 0 && mod->num_commons < MAX_COMMONS)
                                mod->common_indices[mod->num_commons++] = ci;
                        } else if (ctrl == 7) {
                            if (symbol_count < MAX_SYMBOLS) {
                                Symbol *s = &symbols[symbol_count++];
                                snprintf(s->name, sizeof(s->name), "%s", sym);
                                s->value = val;
                                s->segment = seg;
                                s->module_index = module_count - 1;
                                s->sdcc_area_local = -1;
                            }
                        }
                    } else if (ctrl <= 14) {
                        int seg = (int)read_bits(&r, 2);
                        unsigned int val = read_16le(&r);
                        if (ctrl == 10) mod->dseg_size = val;
                        else if (ctrl == 11 && (seg == ADDR_CSEG || seg == ADDR_ASEG)) current_pc = val;
                        else if (ctrl == 13) mod->cseg_size = val;
                        else if (ctrl == 14) {
                            if (mod->cseg_size == 0 && has_cseg) mod->cseg_size = max_pc;
                            if (!entry_point_found && (val != 0 || seg != ADDR_ASEG) && val != 0xFFFF) {
                                entry_point = val;
                                entry_seg = seg;
                                entry_point_found = 1;
                            }
                            need_new_module = 1;
                        }
                    } else break;
                } else {
                    read_bits(&r, 16);
                    mod->has_data = 1;
                    has_cseg = 1;
                    current_pc += 2;
                    if (current_pc > max_pc) max_pc = current_pc;
                }
            }
        }
        fclose(f);
        file_num_modules[i] = module_count - start_module_count;
    }

    {
        unsigned int cur_section_base = (module_count > 0) ? modules[0].section_base : current_code_base;
        unsigned int cur_code = align_addr(cur_section_base, align_code);

        for (int i = 0; i < module_count; i++) {
            if (modules[i].is_sdcc) continue;
            if (modules[i].section_base != cur_section_base) {
                cur_section_base = modules[i].section_base;
                cur_code = align_addr(cur_section_base, align_code);
            } else if (i > 0) {
                cur_code = align_addr(cur_code, align_code);
            }
            modules[i].cseg_offset = cur_code;
            cur_code += modules[i].cseg_size;
        }

        unsigned int common_base = align_addr(cur_code, align_code);
        for (int i = 0; i < module_count; i++) {
            if (!modules[i].is_sdcc) {
                unsigned int end = modules[i].cseg_offset + modules[i].cseg_size;
                if (end > common_base) common_base = end;
            }
        }

        common_base = align_addr(common_base, align_data);
        for (int i = 0; i < common_count; i++) {
            commons[i].base_addr = align_addr(common_base, align_data);
            common_base = commons[i].base_addr + commons[i].size;
        }

        if (!data_base_set) data_base = align_addr(common_base, align_data);
        else data_base = align_addr(data_base, align_data);

        for (int i = 0; i < module_count; i++) {
            if (!modules[i].is_sdcc) {
                if (i > 0) data_base = align_addr(data_base, align_data);
                modules[i].dseg_offset = data_base;
                data_base += modules[i].dseg_size;
            }
        }

        for (int s = 0; s < symbol_count; s++) {
            if (symbols[s].segment == ADDR_COMMON) {
                int mi = symbols[s].module_index;
                if (mi < module_count && modules[mi].num_commons > 0)
                    symbols[s].common_index = modules[mi].common_indices[0];
            }
        }

        unsigned int sdcc_next = align_addr(cur_code, align_code);
        if (cur_code == (module_count > 0 ? (align_addr(modules[0].section_base, align_code)) : align_addr(current_code_base, align_code)))
            sdcc_next = align_addr(current_code_base, align_code);

        for (int i = 0; i < sdcc_global_area_count; i++) {
            SdccGlobalArea *ga = &sdcc_global_areas[i];
            int arg_idx = find_area_arg(ga->name);
            if (arg_idx >= 0) {
                ga->base = area_args[arg_idx].addr;
                if (!ga->is_ovr) sdcc_next = ga->base + ga->size;
            } else if (ga->is_abs) {
                ga->base = 0;
            } else {
                ga->base = align_addr(sdcc_next, align_code);
                if (!ga->is_ovr) sdcc_next = ga->base + ga->size;
            }
        }

        for (int mi = 0; mi < module_count; mi++) {
            if (!modules[mi].is_sdcc) continue;
            for (int a = 0; a < mod_area_inst_count[mi]; a++) {
                SdccAreaInst *inst = &mod_area_insts[mi][a];
                SdccGlobalArea *ga = &sdcc_global_areas[inst->global_idx];
                if (ga->is_abs || ga->is_ovr) inst->base = ga->base;
                else inst->base = ga->base + inst->offset;
            }
        }
    }

    memset(output_buffer, fill_byte, sizeof(output_buffer));
    memset(output_touched, 0, sizeof(output_touched));
    unsigned int min_addr = 0xFFFF, max_addr = 0;
    int mod_idx = 0;

    for (int i = 0; i < file_count; i++) {
        const char *fname = merged_argv[file_indices[i]];
        if (is_xlf_file(fname)) {
            int n = file_num_modules[i];
            xlf_pass2(fname, mod_idx, n, &min_addr, &max_addr);
            mod_idx += n;
            continue;
        }

        /* MS-REL binary format */
        FILE *f = open_rel_file(fname, "rb");
        if (!f) continue;
        BitReader r = {f, 0, 0};
        skip_header(&r);

        int mods_in_this_file = file_num_modules[i];
        int mods_processed = 0;
        Module *mod = NULL;
        unsigned int current_pc = 0;
        if (mods_processed < mods_in_this_file) {
            mod = &modules[mod_idx++];
            mods_processed++;
            current_pc = mod->cseg_offset;
        }

        unsigned int rpn_stack[RPN_STACK_SIZE];
        int rpn_sp = 0;
        int current_common_idx = -1;
        int pending_offset = 0;
        int need_new_module = 0;

        while (1) {
            if (need_new_module) {
                skip_header(&r);
                if (mods_processed < mods_in_this_file) {
                    mod = &modules[mod_idx++];
                    mods_processed++;
                    rpn_sp = 0;
                    current_common_idx = -1;
                    current_pc = mod->cseg_offset;
                    pending_offset = 0;
                } else mod = NULL;
                need_new_module = 0;
            }

            int first_bit = read_bit(&r);
            if (first_bit == -1) break;

            if (first_bit == 0) {
                unsigned char val = (unsigned char)read_bits(&r, 8);
                if (current_pc < OUTPUT_SIZE) {
                    output_buffer[current_pc] = val;
                    output_touched[current_pc] = 1;
                    if (current_pc < min_addr) min_addr = current_pc;
                    if (current_pc > max_addr) max_addr = current_pc;
                }
                current_pc++;
            } else {
                int t2 = (int)read_bits(&r, 2);
                if (t2 == 0) {
                    int ctrl = (int)read_bits(&r, 4);
                    char sym[1024] = {0};
                    if (ctrl <= 4) {
                        read_symbol(&r, sym, sizeof(sym));
                        if (ctrl == 1) {
                            current_common_idx = find_common(sym);
                            if (current_common_idx >= 0) current_pc = commons[current_common_idx].base_addr;
                        } else if (ctrl == 4) {
                            unsigned char ext_type = (unsigned char)sym[0];
                            if (ext_type == EXT_REF_EXTERNAL) {
                                Symbol *s = find_symbol(sym + 1);
                                unsigned int sym_val = s ? resolve_symbol_abs(s) : 0;
                                if (rpn_sp < RPN_STACK_SIZE) rpn_stack[rpn_sp++] = sym_val;
                            } else if (ext_type == EXT_ADDRESS) {
                                int addr_seg = (unsigned char)sym[1];
                                unsigned int addr_val = (unsigned char)sym[2] | ((unsigned char)sym[3] << 8);
                                if (addr_seg == ADDR_CSEG) addr_val += mod->cseg_offset;
                                else if (addr_seg == ADDR_DSEG) addr_val += mod->dseg_offset;
                                else if (addr_seg == ADDR_COMMON && current_common_idx >= 0) addr_val += commons[current_common_idx].base_addr;
                                if (rpn_sp < RPN_STACK_SIZE) rpn_stack[rpn_sp++] = addr_val;
                            } else if (ext_type == EXT_ARITHMETIC_OP) {
                                int op = (unsigned char)sym[1];
                                if (op == OP_STORE_AS_BYTE || op == OP_STORE_AS_WORD) {
                                    unsigned int res = (rpn_sp > 0) ? rpn_stack[--rpn_sp] : 0;
                                    if (current_pc < OUTPUT_SIZE) {
                                        output_buffer[current_pc] = (unsigned char)(res & 0xFF);
                                        output_touched[current_pc] = 1;
                                        if (current_pc < min_addr) min_addr = current_pc;
                                        if (current_pc > max_addr) max_addr = current_pc;
                                        if (op == OP_STORE_AS_WORD && current_pc + 1 < OUTPUT_SIZE) {
                                            output_buffer[current_pc + 1] = (unsigned char)(res >> 8);
                                            output_touched[current_pc + 1] = 1;
                                            if (current_pc + 1 > max_addr) max_addr = current_pc + 1;
                                        }
                                    }
                                    current_pc += (op == OP_STORE_AS_WORD ? 2 : 1);
                                } else {
                                    unsigned int b = rpn_sp > 0 ? rpn_stack[--rpn_sp] : 0;
                                    unsigned int a = rpn_sp > 0 ? rpn_stack[--rpn_sp] : 0;
                                    unsigned int res = 0;
                                    if (op == OP_PLUS) res = a + b;
                                    else if (op == OP_MINUS) res = a - b;
                                    else if (op == OP_MULTIPLY) res = a * b;
                                    else if (op == OP_DIVIDE) res = (b != 0) ? a / b : 0;
                                    else if (op == OP_MOD) res = (b != 0) ? a % b : 0;
                                    else if (op == OP_NOT) { rpn_stack[rpn_sp++] = a; res = ~b; }
                                    else if (op == OP_UNARY_MINUS) { rpn_stack[rpn_sp++] = a; res = (unsigned int)-(int)b; }
                                    else if (op == OP_HIGH) { rpn_stack[rpn_sp++] = a; res = (b >> 8) & 0xFF; }
                                    else if (op == OP_LOW) { rpn_stack[rpn_sp++] = a; res = b & 0xFF; }
                                    else if (op == OP_SHR) res = a >> (b & 0x0F);
                                    else if (op == OP_SHL) res = a << (b & 0x0F);
                                    else if (op == OP_EQ) res = (a == b) ? 0xFFFF : 0;
                                    else if (op == OP_NE) res = (a != b) ? 0xFFFF : 0;
                                    else if (op == OP_LT) res = (a < b) ? 0xFFFF : 0;
                                    else if (op == OP_LE) res = (a <= b) ? 0xFFFF : 0;
                                    else if (op == OP_GT) res = (a > b) ? 0xFFFF : 0;
                                    else if (op == OP_GE) res = (a >= b) ? 0xFFFF : 0;
                                    else if (op == OP_AND) res = a & b;
                                    else if (op == OP_OR) res = a | b;
                                    else if (op == OP_XOR) res = a ^ b;
                                    if (rpn_sp < RPN_STACK_SIZE) rpn_stack[rpn_sp++] = res;
                                }
                            }
                        }
                    } else if (ctrl <= 7) {
                        int seg = (int)read_bits(&r, 2); unsigned int val = read_16le(&r); read_symbol(&r, sym, sizeof(sym));
                        if (ctrl == 6) {
                            if (external_chain_count < MAX_EXTERNAL_CHAINS) {
                                ExternalChain *chain = &external_chains[external_chain_count++];
                                snprintf(chain->name, sizeof(chain->name), "%s", sym);
                                chain->val = val;
                                chain->pending_offset = pending_offset;
                                if (seg == ADDR_CSEG) {
                                    chain->seg_offset = mod->cseg_offset;
                                    chain->seg_limit = mod->cseg_offset + mod->cseg_size;
                                } else if (seg == ADDR_DSEG) {
                                    chain->seg_offset = mod->dseg_offset;
                                    chain->seg_limit = mod->dseg_offset + mod->dseg_size;
                                } else if (seg == ADDR_COMMON && current_common_idx >= 0) {
                                    chain->seg_offset = commons[current_common_idx].base_addr;
                                    chain->seg_limit = commons[current_common_idx].base_addr + commons[current_common_idx].size;
                                } else {
                                    chain->seg_offset = 0;
                                    chain->seg_limit = OUTPUT_SIZE;
                                }
                            }
                            pending_offset = 0;
                        }
                    } else if (ctrl <= 14) {
                        int seg = (int)read_bits(&r, 2); unsigned int val = read_16le(&r);
                        if (ctrl == 8) pending_offset = -(int)(short)val;
                        else if (ctrl == 9) pending_offset = (int)(short)val;
                        else if (ctrl == 11) {
                            if (seg == ADDR_ASEG) current_pc = val;
                            else if (seg == ADDR_CSEG) current_pc = val + (mod ? mod->cseg_offset : 0);
                            else if (seg == ADDR_DSEG) current_pc = val + (mod ? mod->dseg_offset : 0);
                            else if (seg == ADDR_COMMON && current_common_idx >= 0) current_pc = val + commons[current_common_idx].base_addr;
                        } else if (ctrl == 14) need_new_module = 1;
                    } else break;
                } else {
                    unsigned int addr = read_bits(&r, 16);
                    if (t2 == ADDR_CSEG) addr += mod->cseg_offset;
                    else if (t2 == ADDR_DSEG) addr += mod->dseg_offset;
                    else if (t2 == ADDR_COMMON && current_common_idx >= 0) addr += commons[current_common_idx].base_addr;
                    if (current_pc + 1 < OUTPUT_SIZE) {
                        output_buffer[current_pc] = (unsigned char)(addr & 0xFF);
                        output_buffer[current_pc + 1] = (unsigned char)(addr >> 8);
                        output_touched[current_pc] = 1;
                        output_touched[current_pc + 1] = 1;
                        if (current_pc < min_addr) min_addr = current_pc;
                        if (current_pc + 1 > max_addr) max_addr = current_pc + 1;
                    }
                    current_pc += 2;
                }
            }
        }
        fclose(f);
    }

    {
        unsigned char chain_links[OUTPUT_SIZE];
        memcpy(chain_links, output_buffer, sizeof(chain_links));

        for (int i = 0; i < external_chain_count; i++) {
            ExternalChain *chain = &external_chains[i];
            Symbol *s = find_symbol(chain->name);
            if (!s) continue;

            unsigned int patch_val = resolve_symbol_abs(s) + (unsigned int)(int)chain->pending_offset;
            unsigned int chain_addr = chain->val + chain->seg_offset;
            unsigned char chain_seen[OUTPUT_SIZE] = {0};

            while (1) {
                if (chain_addr + 1 >= OUTPUT_SIZE) break;
                if (chain_seen[chain_addr]) {
                    fprintf(stderr, "lk80 cycle detected: sym='%s' sym_val=%04X chain_addr=%04X seg_offset=%04X\n", s->name, patch_val, chain_addr, chain->seg_offset);
                    break;
                }
                chain_seen[chain_addr] = 1;

                unsigned int nxt = (unsigned int)chain_links[chain_addr] | ((unsigned int)chain_links[chain_addr + 1] << 8);
                output_buffer[chain_addr] = (unsigned char)(patch_val & 0xFF);
                output_buffer[chain_addr + 1] = (unsigned char)(patch_val >> 8);
                output_touched[chain_addr] = 1;
                output_touched[chain_addr + 1] = 1;
                if (chain_addr < min_addr) min_addr = chain_addr;
                if (chain_addr + 1 > max_addr) max_addr = chain_addr + 1;

                if (chain->pending_offset != 0 || nxt == 0) break;
                unsigned int next_addr = nxt + chain->seg_offset;
                if (next_addr < chain->seg_offset || next_addr + 1 >= chain->seg_limit) break;
                chain_addr = next_addr;
            }
        }
    }

    if (start_set && min_addr > start_limit) min_addr = start_limit;
    if (max_addr < min_addr) max_addr = min_addr;
    if (end_set && max_addr > end_limit) max_addr = end_limit;

    if (opt_show_banner) {
        printf("lk80 - Linker v1.1 (LINK-80 / Nestor80 compatible)\n");
    }

    if (output_file) {
        char output_filename[1024];
        strncpy(output_filename, output_file, sizeof(output_filename)-1);
        output_filename[sizeof(output_filename)-1] = '\0';
        if (opt_file_case_lower && !opt_file_explicit) {
            char *p = output_filename;
            while (*p) { *p = tolower((unsigned char)*p); p++; }
        }
        FILE *out = fopen(output_filename, out_fmt == FMT_HEX ? "w" : "wb");
        if (out) {
            if (out_fmt == FMT_HEX) write_intel_hex(out, min_addr, max_addr);
            else fwrite(&output_buffer[min_addr], 1, max_addr - min_addr + 1, out);
            fclose(out);
        }
        if (opt_color_output) {
            printf("%sOutput:%s %s%s%s  %s%04X-%04X%s (%s%s%s)\n", 
                CLR_GREEN, CLR_RESET, CLR_CYAN, output_filename, CLR_RESET,
                CLR_YEL, min_addr, max_addr, CLR_RESET,
                CLR_CYAN, out_fmt == FMT_HEX ? "HEX" : "BIN", CLR_RESET);
        } else {
            printf("Output: %s  %04X-%04X (%s)\n", output_filename, min_addr, max_addr, out_fmt == FMT_HEX ? "HEX" : "BIN");
        }
    }

    if (symbols_file) {
        regex_t regex;
        int use_regex = 0;
        if (opt_symbols_regex) {
            if (regcomp(&regex, opt_symbols_regex, REG_EXTENDED | REG_NOSUB) == 0) {
                use_regex = 1;
            } else {
                fprintf(stderr, "lk80: invalid regex '%s'\n", opt_symbols_regex);
            }
        }

        FILE *sf = fopen(symbols_file, "w");
        if (sf) {
            if (sym_fmt == SYM_JSON) {
                fprintf(sf, "{\n");
                int first = 1;
                for (int s = 0; s < symbol_count; s++) {
                    if (use_regex && regexec(&regex, symbols[s].name, 0, NULL, 0) != 0)
                        continue;
                    unsigned int fv = resolve_symbol_abs(&symbols[s]);
                    fprintf(sf, "%s  \"%s\": %u", first ? "" : ",\n", symbols[s].name, fv);
                    first = 0;
                }
                fprintf(sf, "\n}\n");
            } else if (sym_fmt == SYM_EQUS) {
                for (int s = 0; s < symbol_count; s++) {
                    if (use_regex && regexec(&regex, symbols[s].name, 0, NULL, 0) != 0)
                        continue;
                    unsigned int fv = resolve_symbol_abs(&symbols[s]);
                    fprintf(sf, "%s: EQU 0%04XH\n", symbols[s].name, fv);
                }
            } else {
                int col = 0;
                for (int s = 0; s < symbol_count; s++) {
                    if (use_regex && regexec(&regex, symbols[s].name, 0, NULL, 0) != 0)
                        continue;
                    unsigned int fv = resolve_symbol_abs(&symbols[s]);
                    char uname[256];
                    int k;
                    for (k = 0; symbols[s].name[k]; k++) {
                        unsigned char c = (unsigned char)symbols[s].name[k];
                        uname[k] = (c < 128) ? (char)toupper(c) : (char)c;
                    }
                    uname[k] = '\0';
                    fprintf(sf, "%04X %s\t", fv, uname);
                    if (++col >= 4) { fprintf(sf, "\n"); col = 0; }
                }
                if (col > 0) fprintf(sf, "\n");
            }
            fclose(sf);
        }
        if (use_regex) regfree(&regex);
    }
    return 0;
}
