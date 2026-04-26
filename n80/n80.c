#include <stdio.h>	// printf,fopen...
#include <stdlib.h> // malloc,realloc...
#include <string.h> // strchr,strcasecmp...
#include <ctype.h>	// tolower

#ifndef _WIN32
int strcasecmp(const char *s1, const char *s2)
{
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	int result;
	if (p1 == p2)
		return 0;
	while ((result = tolower(*p1) - tolower(*p2++)) == 0)
		if (*p1++ == '\0')
			break;
	return result;
}
#endif

#ifdef _WIN32
#define PATHCHAR '\\'
#define strcasecmp _stricmp
#else
#define PATHCHAR '/'
#endif
#define INLINE 

typedef struct
{
	char name[16];
	int code;
	int type;
} t_opcode;

typedef struct
{
	char name[16];
	int type;
} t_parmtr;

#define SEG_ASEG 0
#define SEG_CSEG 1
#define SEG_DSEG 2
#define SEG_COMMON 3
#define SEG_EXTRN 4
#define SEG_COMPLEX 5

/* Microsoft Extension Link Item Subtypes */
#define EXT_ARITHMETIC_OP  0x41
#define EXT_REF_EXTERNAL   0x42
#define EXT_ADDRESS        0x43

/* Arithmetic Operator Codes for REL Type 4 */
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

#define MAX_COMMONS 256
char common_names[MAX_COMMONS][64];
int common_sizes[MAX_COMMONS];
int common_targets[MAX_COMMONS];
int num_commons = 0;
int current_common = -1;

#define MAX_SDCC_AREAS 256
typedef struct {
	int offset;
	int flags;
	int target_index;
	int raw_value;
} SdccReloc;

typedef struct {
	char name[64];
	int is_abs;
	int is_ovr;
	int address;
	int max_offset;
	unsigned char *data;
	unsigned char *used;
	SdccReloc relocs[4096];
	int reloc_count;
} SdccArea;
SdccArea sdcc_areas[MAX_SDCC_AREAS];
int sdcc_area_count = 0;
int current_sdcc_area = -1;
int *sdcc_symbol_index_by_label;

int flag_v = 0;
int flag_string_escapes = 1; // Enabled by default; disable with --no-string-escapes
int flag_relab = 0; // Disabled by default
int flag_extroot = 0; // Disabled by default
int flag_contm = 0; // Signal to skip iteration
int opt_show_banner = 1;
int opt_file_case_lower = 0;
int opt_output_file_explicit = 0;
int opt_unknown_symbols_external = 0;
int opt_allow_bare_expressions = 0;
typedef enum { CPU_Z80, CPU_R800, CPU_Z280, CPU_8080 } CpuType;
CpuType current_cpu = CPU_Z80;

typedef enum {
	ENC_ASCII,
	ENC_UTF8,
	ENC_LATIN1,
	ENC_CP1252,
	ENC_CP850,
	ENC_CP437,
	ENC_SJIS
} Encoding;
static Encoding current_encoding = ENC_ASCII;
static Encoding default_encoding = ENC_ASCII;

// Globals
int current_seg = SEG_ASEG;
int seg_target[4] = {0, 0, 0, 0};
int seg_max[4] = {0, 0, 0, 0};
int target, origin, remote, dollar, dollar_seg, flag_dollar = 0, flag_z = 1;
int flag_sdcc = 0;
int eval_res_seg, eval_res_lbl;
int entry_point = 0, entry_seg = SEG_ASEG;
int phase_active = 0, phase_target = 0, phase_seg = SEG_ASEG;
int pass = 1;
#define SIZEOF_REL_BUFFER (2 << 20) // 2MB
unsigned char *rel_buffer, *rel_buffer_copy;
int rel_bits = 0;
long rel_pos = 0;

#define INPUT_MAXIMUM 256
#define PARAM_SIZE 2048
#define SIZEOF_OUTPUT (1 << 16)
unsigned char output[SIZEOF_OUTPUT + 512], source[4096], input0[4096], folder[1 << 9], incpath[1 << 9], newpath[1 << 9];
unsigned char *split_symbol, *split_opcode, *split_parmtr;
static unsigned char split_opcode_buf[64];
char saved_p;

#define LOCAL_MAXIMUM 4096
int local_name[LOCAL_MAXIMUM], local_macro[LOCAL_MAXIMUM];
FILE *input[INPUT_MAXIMUM];
int inputs, input_source[INPUT_MAXIMUM], macro0[INPUT_MAXIMUM], local0[INPUT_MAXIMUM];
int cond0[INPUT_MAXIMUM]; // Saved condition depth for each input level
int macro_stack[INPUT_MAXIMUM];
int rept_remain[INPUT_MAXIMUM]; // remaining REPT iterations for each input stack level (0 for non-REPT)
char param0[INPUT_MAXIMUM * PARAM_SIZE];
int param[INPUT_MAXIMUM][10], params, locals;

void printerror(char *s);
int get_label(char *s);
int add_label(char *s, int n, int seg);
int get_opcode(char *s);
int get_parmtr(char *s);
int split_param(char *s);
int open_macro(int i);
int eval(char *s);
void eval_start_rpn(void);
extern char *eval_cursor;
void write_rel_control(int type, int a_val, int a_seg, char *symbol, int sym_len);
void add_external_usage(int lbl, int offset);
static int sdcc_external_index_for_label(int lbl);

int LABEL_MAXIMUM = 1 << 14;
int *label, labels, *value, *label_seg, *label_sdcc_area;
int CACHE_MAXIMUM = 1 << 14;
int *cache, caches, *macro, macros, *macro_source, *rept_iterations, *macro_irp_list;
int rept_count = 0, macro_local_count = 0;
int macro_recording_idx = -1;
int irp_pos[INPUT_MAXIMUM];
int DOUBT_MAXIMUM = 1 << 14;
int *doubt, doubts, *doubt_target, *doubt_source;
int CHAIN_MAXIMUM = 1 << 14;
int *chain, chains;
#define CREATE_ARRAY(x, y) (x = (int *)malloc(sizeof(int[y])))
#define RESIZE_ARRAY(x, y) (x = (int *)realloc(x, sizeof(int[y])))

int ascizs, ASCIZ_MAXIMUM = 1 << 18;
char *asciz;

#define RPN_MAX 2048
typedef struct {
	int type; // 0=VAL, 1=EXT, 2=OP
	int val;
	int seg;
	char sym[256];
} rpn_item;
rpn_item rpn_buffer[RPN_MAX];
int rpn_count = 0;
int eval_has_extrn = 0;
int eval_in_operate = 0;

static int sdcc_find_area(const char *name)
{
	for (int i = 0; i < sdcc_area_count; i++)
		if (!strcasecmp(sdcc_areas[i].name, name))
			return i;
	return -1;
}

static int sdcc_ensure_area(const char *name, int is_abs, int is_ovr)
{
	int i = sdcc_find_area(name);
	if (i >= 0) {
		if (is_abs) sdcc_areas[i].is_abs = 1;
		if (is_ovr) sdcc_areas[i].is_ovr = 1;
		return i;
	}
	if (sdcc_area_count >= MAX_SDCC_AREAS)
		return -1;
	i = sdcc_area_count++;
	memset(&sdcc_areas[i], 0, sizeof(sdcc_areas[i]));
	strncpy(sdcc_areas[i].name, name, sizeof(sdcc_areas[i].name) - 1);
	sdcc_areas[i].is_abs = is_abs;
	sdcc_areas[i].is_ovr = is_ovr;
	sdcc_areas[i].address = 0;
	sdcc_areas[i].max_offset = 0;
	sdcc_areas[i].data = (unsigned char *)calloc(SIZEOF_OUTPUT, 1);
	sdcc_areas[i].used = (unsigned char *)calloc(SIZEOF_OUTPUT, 1);
	if (!sdcc_areas[i].data || !sdcc_areas[i].used)
		return -1;
	return i;
}

static void sdcc_select_area(const char *name, int is_abs, int is_ovr)
{
	int i = sdcc_ensure_area(name, is_abs, is_ovr);
	if (i >= 0)
		current_sdcc_area = i;
}

static void sdcc_record_byte(int offset, unsigned char value)
{
	if (!flag_sdcc || pass != 2)
		return;
	if (current_sdcc_area < 0)
		sdcc_select_area("_CODE", 0, 0);
	if (current_sdcc_area < 0 || offset < 0 || offset >= SIZEOF_OUTPUT)
		return;
	SdccArea *area = &sdcc_areas[current_sdcc_area];
	area->data[offset] = value;
	area->used[offset] = 1;
	if (offset + 1 > area->max_offset)
		area->max_offset = offset + 1;
}

static int sdcc_area_for_segment(int seg)
{
	if (seg == SEG_CSEG)
		return sdcc_ensure_area("_CODE", 0, 0);
	if (seg == SEG_DSEG)
		return sdcc_ensure_area("_DATA", 0, 0);
	if (seg == SEG_ASEG)
		return sdcc_ensure_area("_ABS", 1, 1);
	if (seg == SEG_COMMON && current_common >= 0)
		return sdcc_ensure_area(common_names[current_common], 0, 1);
	return -1;
}

static void sdcc_record_reloc(int offset, int flags, int target_index, int raw_value)
{
	if (!flag_sdcc || pass != 2)
		return;
	if (current_sdcc_area < 0 || current_sdcc_area >= sdcc_area_count)
		return;
	SdccArea *area = &sdcc_areas[current_sdcc_area];
	if (area->reloc_count >= (int)(sizeof(area->relocs) / sizeof(area->relocs[0])))
		return;
	area->relocs[area->reloc_count].offset = offset;
	area->relocs[area->reloc_count].flags = flags;
	area->relocs[area->reloc_count].target_index = target_index;
	area->relocs[area->reloc_count].raw_value = raw_value & 0xFFFF;
	area->reloc_count++;
}

char current_module[256] = "";
char last_global_label[256] = "";
void apply_module_scope(char *dest, const char *src) {
	if (src && src[0] == '.' && flag_relab && last_global_label[0]) {
		snprintf(dest, 1023, "%s%s", last_global_label, src);
	} else if (src && src[0] && current_module[0] && src[0] != '.' && !strchr(src, '.')) {
		snprintf(dest, 1023, "%s.%s", current_module, src);
	} else {
		if (src) strncpy(dest, src, 1023);
		else dest[0] = 0;
	}
	dest[1023] = 0;
}

INLINE int isnumber(int i) { return (i >= '0' && i <= '9'); }
INLINE int isletter(int i) { return (i >= 'A' && i <= 'Z') || (i >= 'a' && i <= 'z') || i == '_' || i == '$' || i == '.' || i == '?' || i == '@' || ((unsigned char)i >= 128); }
INLINE int iseither(int i) { return (i >= '0' && i <= '9') || (i >= 'A' && i <= 'Z') || (i >= 'a' && i <= 'z') || i == '_' || i == '.' || i == '$' || i == '#' || i == '?' || i == '@' || ((unsigned char)i >= 128); }
char symbol_dollar[] = "$", symbol_debug[] = "DEBUG";

#define FATAL_ERROR(s) return printerror(s), -1
char error_byte_overflow[] = "byte out of range";
char error_cannot_create_file[] = "cannot create file";
char error_cannot_open_file[] = "cannot open file";
char error_cannot_write_data[] = "cannot write data";
char error_char_overflow[] = "offset out of range";
char error_elif_without_if[] = "ELIF without IF";
char error_else_without_if[] = "ELSE without IF";
char error_endif_without_if[] = "ENDIF without IF";
char error_endm_without_macro[] = "ENDM without MACRO";
char error_forbidden_as80[] = "forbidden in AS80 mode";
char error_if_without_endif[] = "IF without ENDIF";
char error_improper_argument[] = "improper argument";
char error_invalid_expression[] = "invalid expression";
char error_invalid_string[] = "invalid string";
char error_invalid_symbol[] = "invalid symbol";
char error_macro_without_endm[] = "MACRO without ENDM";
char error_out_of_memory[] = "out of memory";
char error_stack_overflow[] = "stack overflow";
char error_symbol_already_exists[] = "symbol already exists";
char error_syntax_error[] = "syntax error";
char error_too_many_arguments[] = "too many arguments";
char error_undefined_opcode[] = "undefined opcode";
char error_undefined_symbol[] = "undefined symbol";
char error_word_overflow[] = "word out of range";
char string_greeting[] = "usage: n80 source [$] [target] [--args]\n";

// REL helpers
void write_rel_bits(unsigned int val, int n)
{
	if (pass == 1) return;
	for (int i = 0; i < n; i++)
	{
		if (rel_pos >= SIZEOF_REL_BUFFER) {
			fprintf(stderr, "REL buffer overflow!\n");
			exit(1);
		}
		if (val & (1 << (n - 1 - i)))
			rel_buffer[rel_pos] |= (1 << (7 - rel_bits));
		rel_bits++;
		if (rel_bits == 8)
		{
			rel_bits = 0;
			rel_pos++;
			if (rel_pos < SIZEOF_REL_BUFFER) rel_buffer[rel_pos] = 0;
		}
	}
}

void write_rel_byte(unsigned char b)
{
	write_rel_bits(0, 1); // 0 = absolute byte
	write_rel_bits(b, 8);
	if (current_seg == SEG_COMMON && current_common >= 0)
	{
		if (target + 1 > common_sizes[current_common])
			common_sizes[current_common] = target + 1;
	}
	if (target + 1 > seg_max[current_seg])
		seg_max[current_seg] = target + 1;
	seg_target[current_seg]++;
}

static void sdcc_record_rpn_reloc(int is_byte)
{
	if (!flag_sdcc || pass != 2)
		return;

	int flags = is_byte ? (0x01 | 0x08) : 0;
	int target_index = -1;
	int raw_value = 0;
	int is_external = 0;

	for (int i = 0; i < rpn_count; i++) {
		if (rpn_buffer[i].type == 0 && target_index < 0) {
			raw_value = rpn_buffer[i].val;
			if (eval_res_lbl >= 0 && eval_res_lbl < labels && label_sdcc_area[eval_res_lbl] >= 0)
				target_index = label_sdcc_area[eval_res_lbl];
			else
				target_index = sdcc_area_for_segment(rpn_buffer[i].seg);
		} else if (rpn_buffer[i].type == 1 && target_index < 0) {
			int lbl = -1;
			for (int j = 0; j < labels; j++) {
				if (!strcmp(&asciz[label[j]], rpn_buffer[i].sym)) {
					lbl = j;
					break;
				}
			}
			target_index = sdcc_external_index_for_label(lbl);
			is_external = 1;
		} else if (is_byte && rpn_buffer[i].type == 2 && rpn_buffer[i].val == OP_HIGH) {
			flags |= 0x80;
		}
	}

	if (target_index >= 0) {
		if (is_external)
			flags |= 0x02;
		sdcc_record_reloc(target, flags, target_index, raw_value);
	}
}

void write_rel_rpn(int is_byte)
{
	sdcc_record_rpn_reloc(is_byte);
	char payload[256];
	for (int i = 0; i < rpn_count; i++) {
		memset(payload, 0, sizeof(payload));
		if (rpn_buffer[i].type == 0) { // VAL
			payload[0] = EXT_ADDRESS;
			payload[1] = (char)rpn_buffer[i].seg;
			payload[2] = (char)(rpn_buffer[i].val & 0xFF);
			payload[3] = (char)((rpn_buffer[i].val >> 8) & 0xFF);
			write_rel_control(4, 0, 0, payload, 4);
		} else if (rpn_buffer[i].type == 1) { // EXT
			payload[0] = EXT_REF_EXTERNAL;
			int slen = strlen(rpn_buffer[i].sym);
			if (slen > 254) slen = 254;
			memcpy(payload + 1, rpn_buffer[i].sym, slen);
			write_rel_control(4, 0, 0, payload, slen + 1);
		} else if (rpn_buffer[i].type == 2) { // OP
			payload[0] = EXT_ARITHMETIC_OP;
			payload[1] = (char)rpn_buffer[i].val;
			write_rel_control(4, 0, 0, payload, 2);
		}
	}
	// Terminal Store OP
	memset(payload, 0, sizeof(payload));
	payload[0] = EXT_ARITHMETIC_OP;
	payload[1] = is_byte ? OP_STORE_AS_BYTE : OP_STORE_AS_WORD;
	write_rel_control(4, 0, 0, payload, 2);
	
	// Advance seg_target and max sizes
	int size = (is_byte ? 1 : 2);
	if (current_seg == SEG_COMMON && current_common >= 0) {
		if (target + size > common_sizes[current_common])
			common_sizes[current_common] = target + size;
	}
	if (target + size > seg_max[current_seg])
		seg_max[current_seg] = target + size;
	seg_target[current_seg] += size;
}

void write_rel_addr(unsigned int addr, int type)
{
	if (flag_v > 0) fprintf(stderr, "write_rel_addr: addr=%04X, type=%d, res_lbl=%d\n", addr, type, eval_res_lbl);
	if (flag_sdcc && pass == 2 && type != SEG_ASEG && type != SEG_EXTRN) {
		int target_area = -1;
		if (eval_res_lbl >= 0 && eval_res_lbl < labels && label_sdcc_area[eval_res_lbl] >= 0)
			target_area = label_sdcc_area[eval_res_lbl];
		else
			target_area = sdcc_area_for_segment(type);
		if (target_area >= 0)
			sdcc_record_reloc(target, 0, target_area, addr);
	} else if (flag_sdcc && pass == 2 && type == SEG_EXTRN) {
		int sym_idx = sdcc_external_index_for_label(eval_res_lbl);
		sdcc_record_reloc(target, 0x02, sym_idx, addr);
	}
	if (type == SEG_ASEG)
	{
		// Absolute value: NEXTWORD_REL emits the bytes via NEXTBYTE x2.
		// Emitting here would double the output (4 bytes instead of 2).
	}
	else if (type == SEG_EXTRN)
	{
		// Para externos, usamos add_external_usage para encadear a corrente.
		// add_external_usage agora lida com o offset (emite Tipo 9 se necessário).
		add_external_usage(eval_res_lbl, addr);
		seg_target[current_seg] += 2;
	}
	else
	{
		// 101 = CSEG (type 1), 110 = DSEG (type 2), 111 = COMMON (type 3)
		write_rel_bits(4 + type, 3);
		write_rel_bits(addr, 16);
		seg_target[current_seg] += 2;
	}
}

void write_rel_symbol_bytes(const char *s, int len)
{
	if (len >= 8 || (len >= 1 && (unsigned char)s[0] == 0xFF)) {
		int len_bytes = 1;
		if (len >= 256) len_bytes = 2;
		// Extended format: bits 100 type 010 (2) FFh len symbol...
		write_rel_bits(1 + len_bytes, 3); // Legacy length 2 or 3
		write_rel_bits(0xFF, 8);
		write_rel_bits(len & 0xFF, 8);
		if (len >= 256) {
			write_rel_bits((len >> 8) & 0xFF, 8);
			if (rel_bits > 0) {
				rel_bits = 0;
				rel_pos++;
			}
		}
	} else {
		write_rel_bits(len, 3);
	}
	for (int i = 0; i < len; i++)
		write_rel_bits(s[i], 8);
}

void write_rel_symbol(char *s)
{
	write_rel_symbol_bytes(s, s ? strlen(s) : 0);
}

int *label_flag;
int *label_last_addr;
int *label_last_seg;
#define LBL_PUBLIC 1
#define LBL_EXTRN 2

static int sdcc_external_index_for_label(int lbl)
{
	if (lbl < 0 || lbl >= labels)
		return 0;
	if (sdcc_symbol_index_by_label && sdcc_symbol_index_by_label[lbl] >= 0)
		return sdcc_symbol_index_by_label[lbl];
	int idx = 1;
	for (int i = 0; i < labels; i++) {
		if (!strcmp(&asciz[label[i]], symbol_debug))
			continue;
		if (label_flag[i] & LBL_EXTRN) {
			if (sdcc_symbol_index_by_label)
				sdcc_symbol_index_by_label[i] = idx;
			if (i == lbl)
				return idx;
			idx++;
		}
		if (label_flag[i] & LBL_PUBLIC)
			idx++;
	}
	return 0;
}

void add_external_usage(int lbl, int offset)
{
	int prev_addr = (offset == 0) ? label_last_addr[lbl] : 0;
	// Escreve o link para o uso anterior (ou 0000 se houver offset) como dois bytes absolutos (prefixo 0)
	unsigned char low = prev_addr & 0xFF;
	unsigned char high = (prev_addr >> 8) & 0xFF;

	write_rel_bits(0, 1); // byte absoluto
	write_rel_bits(low, 8);
	write_rel_bits(0, 1); // byte absoluto
	write_rel_bits(high, 8);

	if (offset != 0)
	{
		// Se houver offset, emitimos Tipo 9 seguido de Tipo 6 para ESTE endereço específico.
		// O offset é o 'addr' vindo do eval.
		write_rel_control(9, offset, SEG_ASEG, "", 0);
		write_rel_control(6, target, current_seg, &asciz[label[lbl]], 0);
	}
	else
	{
		// O endereço deste uso vira a nova 'cabeça' da corrente para este label
		label_last_addr[lbl] = target;
		label_last_seg[lbl] = current_seg;
	}
}

void write_rel_control(int type, int a_val, int a_seg, char *symbol, int sym_len)
{
	write_rel_bits(4, 3);	 // 100 = link item
	write_rel_bits(type, 4); // control type
	if (type == 15)
	{
		// End of file, no extra data
	}
	else if (type == 4)
	{
		/* Extension Link Item: symbol field is the payload */
		write_rel_symbol_bytes(symbol, sym_len);
	}
	else if (type >= 5 && type <= 7)
	{
		write_rel_bits(a_seg, 2);
		write_rel_bits(a_val & 0xFF, 8);
		write_rel_bits((a_val >> 8) & 0xFF, 8);
		write_rel_symbol(symbol);
	}
	else if (type <= 3)
	{
		write_rel_symbol(symbol);
	}
	else if (type >= 8 && type <= 14)
	{
		write_rel_bits(a_seg, 2);
		write_rel_bits(a_val & 0xFF, 8);
		write_rel_bits((a_val >> 8) & 0xFF, 8);
	}
}

static int sdcc_write_output(const char *source_name, const char *target_name)
{
	if (sdcc_area_count == 0)
		sdcc_select_area("_CODE", 0, 0);

	int public_count = 0;
	int external_count = 0;
	for (int i = 0; i < labels; i++) {
		if (!strcmp(&asciz[label[i]], symbol_debug))
			continue;
		if (label_flag[i] & LBL_PUBLIC)
			public_count++;
		if (label_flag[i] & LBL_EXTRN)
			external_count++;
	}

	FILE *f = stdout;
	if (strcmp(target_name, "-"))
		if (!(f = fopen(target_name, "w")))
			FATAL_ERROR(error_cannot_create_file);

	fprintf(f, "XL3\n");
	fprintf(f, "H %X areas %X global symbols\n", sdcc_area_count, public_count + external_count + 1);
	fprintf(f, "S .__.ABS. Def000000\n");

	int current_symbol_index = 1;
	for (int i = 0; i < labels; i++) {
		if ((label_flag[i] & LBL_EXTRN) && strcmp(&asciz[label[i]], symbol_debug)) {
			if (sdcc_symbol_index_by_label)
				sdcc_symbol_index_by_label[i] = current_symbol_index++;
			fprintf(f, "S %s Ref000000\n", &asciz[label[i]]);
		}
	}

	for (int ai = 0; ai < sdcc_area_count; ai++) {
		SdccArea *area = &sdcc_areas[ai];
		int flags = (area->is_ovr ? 4 : 0) | (area->is_abs ? 8 : 0);
		int addr = 0;
		int size = area->max_offset;
		fprintf(f, "A %s size %X flags %X addr %X\n", area->name, size, flags, addr);

		for (int i = 0; i < labels; i++) {
			if ((label_flag[i] & LBL_PUBLIC) && label_sdcc_area[i] == ai && strcmp(&asciz[label[i]], symbol_debug)) {
				if (sdcc_symbol_index_by_label)
					sdcc_symbol_index_by_label[i] = current_symbol_index++;
				fprintf(f, "S %s Def%06X\n", &asciz[label[i]], value[i] & 0xFFFFFF);
			}
		}

		int pos = 0;
		while (pos < area->max_offset) {
			while (pos < area->max_offset && !area->used[pos])
				pos++;
			if (pos >= area->max_offset)
				break;
			int start = pos;
			int represented_count = 0;
			while (pos < area->max_offset && area->used[pos]) {
				int represented = 1;
				for (int ri = 0; ri < area->reloc_count; ri++) {
					if (area->relocs[ri].offset == pos && (area->relocs[ri].flags & 0x01) && (area->relocs[ri].flags & 0x08)) {
						represented = 3;
						break;
					}
				}
				if (represented_count + represented > 13)
					break;
				pos++;
				represented_count += represented;
			}
			int end = pos;
			fprintf(f, "T %02X %02X 00", start & 0xFF, (start >> 8) & 0xFF);
			for (int j = start; j < end; j++) {
				SdccReloc *byte_reloc = NULL;
				for (int ri = 0; ri < area->reloc_count; ri++) {
					if (area->relocs[ri].offset == j && (area->relocs[ri].flags & 0x01) && (area->relocs[ri].flags & 0x08)) {
						byte_reloc = &area->relocs[ri];
						break;
					}
				}
				if (byte_reloc) {
					fprintf(f, " %02X %02X %02X",
						byte_reloc->raw_value & 0xFF,
						(byte_reloc->raw_value >> 8) & 0xFF,
						(byte_reloc->flags & 0x02) && byte_reloc->raw_value > 0x7FFF ? 0xFF : 0x00);
				} else {
					fprintf(f, " %02X", area->data[j]);
				}
			}
			fprintf(f, "\nR 00 00 %02X %02X", ai & 0xFF, (ai >> 8) & 0xFF);
			for (int ri = 0; ri < area->reloc_count; ri++) {
				SdccReloc *reloc = &area->relocs[ri];
				if (reloc->offset >= start && reloc->offset < end) {
					int t_offset = 3;
					for (int j = start; j < reloc->offset; j++) {
						int represented = 1;
						for (int rj = 0; rj < area->reloc_count; rj++) {
							if (area->relocs[rj].offset == j && (area->relocs[rj].flags & 0x01) && (area->relocs[rj].flags & 0x08)) {
								represented = 3;
								break;
							}
						}
						t_offset += represented;
					}
					fprintf(f, " %02X %02X %02X %02X",
						reloc->flags & 0xFF,
						t_offset & 0xFF,
						reloc->target_index & 0xFF,
						(reloc->target_index >> 8) & 0xFF);
				}
			}
			fprintf(f, "\n");
		}
	}

	if (f != stdout)
		fclose(f);
	if (flag_v >= 0)
		fprintf(stderr, "%s:%s (SDCC REL)\n", source_name, target_name);
	return 0;
}

unsigned char write_rel_byte_helper(unsigned char b)
{
	sdcc_record_byte(target > 0 ? target - 1 : target, b);
	write_rel_byte(b);
	if (phase_active)
		phase_target++;
	return b;
}

unsigned char write_output_byte_only(unsigned char b)
{
	sdcc_record_byte(target > 0 ? target - 1 : target, b);
	if (phase_active)
		phase_target++;
	return b;
}

static unsigned int decode_utf8(const char **p)
{
	unsigned char c = (unsigned char)**p;
	if (c < 0x80) { (*p)++; return c; }
	if ((c & 0xE0) == 0xC0) {
		unsigned int cp = (c & 0x1F) << 6;
		(*p)++;
		if (**p) { cp |= ((unsigned char)**p & 0x3F); (*p)++; }
		return cp;
	}
	if ((c & 0xF0) == 0xE0) {
		unsigned int cp = (c & 0x0F) << 12;
		(*p)++;
		if (**p) { cp |= (((unsigned char)**p & 0x3F) << 6); (*p)++; }
		if (**p) { cp |= ((unsigned char)**p & 0x3F); (*p)++; }
		return cp;
	}
	(*p)++;
	return c;
}

FILE *listing_f = NULL;
char listing_name[1024] = "";
int opt_listing_columns = 8;
unsigned char line_bytes[256];
int line_bytes_count = 0;
int line_pc = 0;
int line_seg = 0;

unsigned char record_and_write_rel_byte(unsigned char b) {
    if (pass == 2 && line_bytes_count < 256) {
        line_bytes[line_bytes_count++] = b;
    }
    return write_rel_byte_helper(b);
}

unsigned char record_and_write_output_byte_only(unsigned char b) {
    if (pass == 2 && line_bytes_count < 256) {
        line_bytes[line_bytes_count++] = b;
    }
    return write_output_byte_only(b);
}

#define NEXTBYTE(val) (output[target++] = record_and_write_rel_byte(val))
#define NEXTBYTE_OUTPUT(val) (output[target++] = record_and_write_output_byte_only(val))

static void append_byte_to_buffer(unsigned char **buf, int *len, int *cap, unsigned char b)
{
	if (*len >= *cap) {
		int new_cap = *cap ? *cap * 2 : 32;
		unsigned char *new_buf = realloc(*buf, new_cap);
		if (!new_buf) {
			fprintf(stderr, "Out of memory\n");
			exit(1);
		}
		*buf = new_buf;
		*cap = new_cap;
	}
	(*buf)[(*len)++] = b;
}

static void append_encoded_cp(unsigned char **buf, int *len, int *cap, unsigned int cp)
{
	if (current_encoding == ENC_UTF8) {
		if (cp < 0x80) append_byte_to_buffer(buf, len, cap, cp);
		else if (cp < 0x800) {
			append_byte_to_buffer(buf, len, cap, 0xC0 | (cp >> 6));
			append_byte_to_buffer(buf, len, cap, 0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			append_byte_to_buffer(buf, len, cap, 0xE0 | (cp >> 12));
			append_byte_to_buffer(buf, len, cap, 0x80 | ((cp >> 6) & 0x3F));
			append_byte_to_buffer(buf, len, cap, 0x80 | (cp & 0x3F));
		} else {
			append_byte_to_buffer(buf, len, cap, 0xF0 | (cp >> 18));
			append_byte_to_buffer(buf, len, cap, 0x80 | ((cp >> 12) & 0x3F));
			append_byte_to_buffer(buf, len, cap, 0x80 | ((cp >> 6) & 0x3F));
			append_byte_to_buffer(buf, len, cap, 0x80 | (cp & 0x3F));
		}
	} else if (current_encoding == ENC_ASCII) {
		append_byte_to_buffer(buf, len, cap, cp < 128 ? cp : '?');
	} else {
		append_byte_to_buffer(buf, len, cap, cp < 256 ? cp : '?');
	}
}
int main(int argc, char *argv[]);
int assemble(char *s, char *t);
int assemble_doubts(int q);

void printerror(char *s)
{
	int i;
	for (i = 0; i < inputs; ++i)
		fprintf(stderr, "%i:", input_source[i]);
	if (i) // any error but EOF
		fprintf(stderr, " %s\n", (char *)source);
	fprintf(stderr, "error: %s! (symbol: '%s', opcode: '%s', params: '%s')\n", s, (char *)split_symbol, (char *)split_opcode, (char *)split_parmtr);
}

// these functions return the entry index (success) or <0 (failure)
int get_asciz(char *s, int y[], int x)
{
	int i, w = 0, z;
	char s_copy[1024];
	strncpy(s_copy, s, 1023);
	s_copy[1023] = 0;
	char *p = strstr(s_copy, "##");
	if (p) *p = 0;
	s = s_copy;

	--x;
	while (w <= x)
	{
		if (!(z = strcasecmp(s, &asciz[y[i = (w + x) / 2]])))
			return i;
		if (z > 0)
			w = i + 1;
		else
			x = i - 1;
	}
	return ~w;
}
int put_asciz(char *s, int y[], int x, int xx)
{
	if (x >= xx)
		FATAL_ERROR(error_out_of_memory);
	char *t = &asciz[y[x] = ascizs];
	while ((*t++ = *s++))
	{
	}
	if ((ascizs = t - asciz) >= (ASCIZ_MAXIMUM - (int)sizeof(input0)))
		asciz = (char *)realloc(asciz, ASCIZ_MAXIMUM *= 2);
	return x;
}
#define get_label(s) get_asciz(s, label, labels) // search label names
int set_label(char *s, int n, int seg)
{
	n &= 0xFFFF;
	int k;
	if ((k = get_label(s)) >= 0)
	{
		value[k] = n;
		label_seg[k] = seg;
		return k;
	}
	return add_label(s, n, seg);
}
int add_label(char *s, int n, int seg)					 // create label
{
	n &= 0xFFFF;
	int i, j;
	char s_copy[256];
	strncpy(s_copy, s, 255);
	s_copy[255] = 0;
	char *p = strstr(s_copy, "##");
	if (p) *p = 0;
	s = s_copy;

	if ((i = ~get_label(s)) < 0)
	{
		// Símbolo já existe. Redefinir (modo permissivo para o Nextor)
		int k = get_label(s);
		value[k] = n;
		label_seg[k] = seg;
		return k;
	}
	if (labels >= LABEL_MAXIMUM) {
		fprintf(stderr, "add_label: resizing labels from %d\n", LABEL_MAXIMUM);
		if (!RESIZE_ARRAY(label, LABEL_MAXIMUM *= 2) || !RESIZE_ARRAY(value, LABEL_MAXIMUM) || !RESIZE_ARRAY(label_seg, LABEL_MAXIMUM) || !RESIZE_ARRAY(label_sdcc_area, LABEL_MAXIMUM) || !RESIZE_ARRAY(sdcc_symbol_index_by_label, LABEL_MAXIMUM) || !RESIZE_ARRAY(label_flag, LABEL_MAXIMUM) || !RESIZE_ARRAY(label_last_addr, LABEL_MAXIMUM) || !RESIZE_ARRAY(label_last_seg, LABEL_MAXIMUM))
			FATAL_ERROR(error_out_of_memory);
	}
	j = sizeof(int[labels - i]);
	memmove(&value[i + 1], &value[i], j);
	memmove(&label[i + 1], &label[i], j);
	memmove(&label_seg[i + 1], &label_seg[i], j);
	memmove(&label_sdcc_area[i + 1], &label_sdcc_area[i], j);
	memmove(&label_flag[i + 1], &label_flag[i], j);
	memmove(&label_last_addr[i + 1], &label_last_addr[i], j);
	memmove(&label_last_seg[i + 1], &label_last_seg[i], j);

	if (put_asciz(s, label, i, LABEL_MAXIMUM) < 0)
		return -1;
	value[i] = n;
	label_seg[i] = seg;
	label_sdcc_area[i] = -1;
	sdcc_symbol_index_by_label[i] = -1;
	label_flag[i] = 0;
	label_last_addr[i] = 0;
	label_last_seg[i] = SEG_ASEG;
	return ++labels, i;
}
// macros are stored line by line, but only their title tells them apart
#define add_cache(s) put_asciz(s, cache, caches++, CACHE_MAXIMUM)
int get_macro(char *s)
{
	int i;
	char s_copy[256];
	strncpy(s_copy, s, 255);
	s_copy[255] = 0;
	char *p = strstr(s_copy, "##");
	if (p) *p = 0;
	for (i = 0; i < macros; i++)
	{
		if (!strcasecmp(s_copy, &asciz[macro[i]]))
			return i;
	}
	return -1;
}
int add_macro(char *s) // create new macro
{
	// Avoid redefining native opcodes as macros to break recursion
	if (!strcasecmp(s, "printx") || !strcasecmp(s, ".printx")) {
		return 9999; // Dummy index
	}
	int i;
	if ((i = get_macro(s)) >= 0)
	{
		if (pass == 2) return i;
		FATAL_ERROR(error_symbol_already_exists);
	}
	if (macros >= CACHE_MAXIMUM)
		if (!RESIZE_ARRAY(cache, CACHE_MAXIMUM *= 2) || !RESIZE_ARRAY(macro, CACHE_MAXIMUM) || !RESIZE_ARRAY(macro_source, CACHE_MAXIMUM) || !RESIZE_ARRAY(rept_iterations, CACHE_MAXIMUM) || !RESIZE_ARRAY(macro_irp_list, CACHE_MAXIMUM))
			FATAL_ERROR(error_out_of_memory);
	
	i = macros++;
	int j;
	if ((j = add_cache(s)) < 0)
		return -1;
	macro[i] = cache[j];
	macro_source[i] = j;
	return i;
}
#define put_doubt(s) put_asciz(s, doubt, doubts++, DOUBT_MAXIMUM)
int add_doubt(char *s)
{
	int i;
	if ((i = put_doubt(s)) < 0)
		return -1;
	if (doubts >= DOUBT_MAXIMUM)
		if (!RESIZE_ARRAY(doubt, DOUBT_MAXIMUM *= 2) || !RESIZE_ARRAY(doubt_target, DOUBT_MAXIMUM) || !RESIZE_ARRAY(doubt_source, DOUBT_MAXIMUM))
			FATAL_ERROR(error_out_of_memory);
	doubt_target[i] = dollar;
	if (inputs > 1)
	{
		doubt_source[i] = ~chains;
		int n;
		for (n = 0; n < inputs - 1; ++n)
			chain[chains++] = input_source[n];
		chain[chains++] = ~input_source[n]; // final item
		if (chains >= CHAIN_MAXIMUM - INPUT_MAXIMUM)
			if (!RESIZE_ARRAY(chain, CHAIN_MAXIMUM *= 2))
				FATAL_ERROR(error_out_of_memory);
	}
	else
		doubt_source[i] = input_source[0];
	return i;
}

int split_param(char *s) // 0 OK, !0 ERROR
{
	if (params >= INPUT_MAXIMUM)
		FATAL_ERROR(error_stack_overflow);
	char c, i = 0, q = 0, angle = 0;
	char *t = &param0[PARAM_SIZE * params];
	while (*s && (unsigned char)*s <= 32) s++;
	if (*s)
	{
		param[params][(unsigned char)++i] = t - param0;
		while ((c = *s))
		{
			if (c == '!') {
				s++;
				if (*s) {
					*t++ = *s++;
				}
				continue;
			}
			if (q == 0 && angle == 0 && c == '<') { angle++; s++; continue; }
			if (q == 0 && angle > 0 && c == '>') { angle--; s++; continue; }
			if (q == 0 && angle == 0 && c == '%') {
				s++;
				if (!*s) break;
				eval_start_rpn();
				int val = eval(s);
				if (s == eval_cursor) s++;
				else s = eval_cursor;
				t += sprintf(t, "%d", val);
				continue;
			}
			if (q == 0 && (c == '"' || c == '\'')) q = c;
			else if (q == c) q = 0;

			if ((c == ',' || (unsigned char)c <= 32) && !q && !angle) {
				*t++ = 0;
				s++;
				while (*s && (unsigned char)*s <= 32) s++;
				param[params][(unsigned char)++i] = t - param0;
			} else {
				*t++ = c;
				s++;
			}
			if (t >= &param0[PARAM_SIZE * (params + 1) - 1])
				FATAL_ERROR(error_stack_overflow); // param too long
		}
		if (q) FATAL_ERROR(error_invalid_string);
		if (i >= 10) FATAL_ERROR(error_too_many_arguments);
	}
	param[params++][0] = i;
	*t = 0;
	return 0;
}
#define merge_param() --params

// these functions return 0 on failure, as fopen(), fgets() and fclose() do
#define INIT_INPUT                                   \
	if (inputs >= INPUT_MAXIMUM)                     \
		return printerror(error_stack_overflow), 0
#define EXIT_INPUT(f)           \
	input[inputs] = f;          \
	input_source[inputs] = 0;   \
	rept_remain[inputs] = 0;    \
	inputs++;                   \
	return 1

int open_input(char *s)
{
	INIT_INPUT;
	FILE *f;
	if (!(f = (strcmp(s, "-") ? fopen(s, "r") : stdin))) {
		if (strcmp(s, "-")) {
			char lower_s[1024];
			strncpy(lower_s, s, 1023);
			lower_s[1023] = '\0';
			for (int i = 0; lower_s[i]; i++) {
				lower_s[i] = tolower((unsigned char)lower_s[i]);
			}
			if (!(f = fopen(lower_s, "r")))
				return 0;
		} else {
			return 0;
		}
	}
	EXIT_INPUT(f);
}
static char irp_arg_buf[2048];
static char *extract_irp_arg(int level)
{
	int m_idx = macro_stack[level];
	char *list = &asciz[macro_irp_list[m_idx]];
	int pos = irp_pos[level];

	while (list[pos] && (unsigned char)list[pos] <= 32) pos++;
	if (list[pos] == 0) return NULL;

	char *m_name = &asciz[macro[m_idx]];
	if (strncmp(m_name, "??IRPC", 6) == 0 || strncmp(m_name, "??IRPS", 6) == 0)
	{
		irp_arg_buf[0] = list[pos];
		irp_arg_buf[1] = 0;
		irp_pos[level] = pos + 1;
		return irp_arg_buf;
	}

	// IRP: parse one argument
	int i = 0, q = 0, angle = 0;
	while (list[pos])
	{
		char c = list[pos];
		if (c == '!')
		{
			pos++;
			if (list[pos]) irp_arg_buf[i++] = list[pos++];
			continue;
		}
		if (q == 0 && angle == 0 && c == '<') { angle++; pos++; continue; }
		if (q == 0 && angle > 0 && c == '>') { angle--; pos++; continue; }
		if (q == 0 && (c == '"' || c == '\'')) q = c;
		else if (q == c) q = 0;

		if (c == ',' && !q && !angle)
		{
			pos++;
			break;
		}
		irp_arg_buf[i++] = c;
		pos++;
		if (i >= 2047) break;
	}
	irp_arg_buf[i] = 0;
	// Trim trailing spaces in IRP arg
	while (i > 0 && (unsigned char)irp_arg_buf[i - 1] <= 32) irp_arg_buf[--i] = 0;
	irp_pos[level] = pos;
	return irp_arg_buf;
}

int eval_status; // 0 OK, <0 ERROR, >0 DOUBT
char *eval_cursor;
unsigned int condition0;
char conditions, recording, skipping_macro;

int open_macro(int i)
{
	if (inputs > 100) fprintf(stderr, "DEBUG open_macro: name='%s', inputs=%d\n", &asciz[macro[i]], inputs);
	INIT_INPUT;
	local0[inputs] = locals++;
	macro0[inputs] = macro_source[i];
	macro_stack[inputs] = i;
	cond0[inputs] = conditions;
	EXIT_INPUT(0);
}

static void format_value(int val, char radix, int size, char *out) {
	if (radix == 'b' || radix == 'B') {
		int bits = 0;
		unsigned int temp = (unsigned int)val & 0xFFFF;
		for (int i = 15; i >= 0; i--) if ((temp >> i) & 1) { bits = i + 1; break; }
		if (bits < size) bits = size;
		if (bits == 0) bits = 1;
		for (int i = 0; i < bits; i++) {
			out[bits - 1 - i] = ((temp >> i) & 1) ? '1' : '0';
		}
		out[bits] = 0;
	} else {
		char fmt[16];
		if (radix == 'X' || radix == 'H') sprintf(fmt, "%%0%dX", size);
		else if (radix == 'x' || radix == 'h') sprintf(fmt, "%%0%dx", size);
		else sprintf(fmt, "%%0%dd", size);
		sprintf(out, fmt, (radix == 'd' || radix == 'D') ? val : (val & 0xFFFF));
	}
}

static void interpolate_string(const char *s, char *dest, size_t dest_size) {
	char *d = dest;
	char *d_end = dest + dest_size - 1;
	while (*s && d < d_end) {
		if (*s == '{') {
			const char *start = s;
			s++;
			char expr_buf[256];
			int ei = 0;
			int depth = 1;
			while (*s && depth > 0 && ei < 255) {
				if (*s == '{') depth++;
				else if (*s == '}') depth--;
				if (depth > 0) expr_buf[ei++] = *s++;
			}
			if (*s == '}') s++;
			expr_buf[ei] = 0;

			if (depth > 0) { // Unclosed brace
				size_t len = s - start;
				if (d + len < d_end) { memcpy(d, start, len); d += len; }
				continue;
			}

			char *colon = strchr(expr_buf, ':');
			char radix = 'd';
			int size = 0;
			if (colon) {
				*colon = 0;
				char *rs = colon + 1;
				if (*rs) {
					radix = *rs++;
					if (isdigit((unsigned char)*rs)) size = atoi(rs);
				}
			}

			int saved_status = eval_status;
			char *saved_cursor = eval_cursor;
			eval_start_rpn();
			int val = eval(expr_buf);
			
			if (eval_status != 0) {
				size_t len = s - start;
				if (d + len < d_end) { memcpy(d, start, len); d += len; }
			} else {
				char val_str[128];
				format_value(val, radix, size, val_str);
				size_t vlen = strlen(val_str);
				if (d + vlen < d_end) { strcpy(d, val_str); d += vlen; }
			}
			eval_status = saved_status;
			eval_cursor = saved_cursor;
		} else {
			*d++ = *s++;
		}
	}
	*d = 0;
}

int read_input(void)
{
	if (!inputs)
		return 0;
	FILE *f;
	int c;
	unsigned char *s, *t = input0;
	if (flag_v > 0) fprintf(stderr, "read_input: inputs=%d, pass=%d\n", inputs, pass);
	if ((f = input[inputs - 1]))
	{
		if (!fgets((char *)input0, sizeof(input0), f))
			return 0;
		// Remove UTF8 BOM
		s = (unsigned char *)input0;
		if (s[0] == 239 && s[1] == 187 && s[2] == 191)
			memmove(input0, input0 + 3, sizeof(input0) - 3);

		// Remove comments and newline, respecting strings
		s = (unsigned char *)input0;
		int q = 0;
		while (*s)
		{
			if (*s == '"' && q == 0)
			{
				q = '"';
			}
			else if (*s == '\'' && q == 0)
			{
				// No M80, aspas simples são usadas em AF' e constantes de char.
				// Só tratar como string se houver outra aspa adiante e NÃO for AF'.
				// Mas a forma mais segura é olhar se o caractere anterior é 'F' ou 'f'.
				if (s > (unsigned char *)input0 && (s[-1] | 32) == 'f')
				{
					// It's likely AF', do NOT start a string
				}
				else
				{
					q = '\'';
				}
			}
			else if (*s == q)
			{
				q = 0;
			}
			else if (*s == ';' && q == 0)
			{
				*s = 0;
				break;
			}
			else if (*s == '\n' || *s == '\r')
			{
				*s = 0;
				break;
			}
			s++;
		}
		++input_source[inputs - 1];
	}
	else
	{
		// Read next line; if "$$" (end marker) and REPT/IRP has remaining iterations,
		// reset to start of body for the next sequential iteration.
		while (!strcasecmp(symbol_dollar, (const char *)(s = (unsigned char *)&asciz[cache[++input_source[inputs - 1] + macro0[inputs - 1]]])))
		{
			flag_contm = 0; // Reset contm flag when we hit the end of iteration marker
			int saved_cond = cond0[inputs - 1];
			while (conditions > saved_cond) {
				conditions--;
				condition0 >>= 2;
			}
			if (rept_remain[inputs - 1])
			{
				rept_remain[inputs - 1]--;
				input_source[inputs - 1] = 0; // restart from body start
			}
			else
			{
				char *m_name = &asciz[macro[macro_stack[inputs - 1]]];
				if (strncmp(m_name, "??IRP", 5) == 0)
				{
					char *next_arg = extract_irp_arg(inputs - 1);
					if (next_arg)
					{
						merge_param();
						split_param(next_arg);
						input_source[inputs - 1] = 0; // restart from body start
					}
					else
						return 0;
				}
				else
					return 0;
			}
		}
		while ((c = *s++))
		{
			if (c == '!')
			{
				if (*s) *t++ = *s++;
			}
			else if (c == '%')
			{
				eval_start_rpn();
				int val = eval((char *)s);
				s = (unsigned char *)eval_cursor;
				t += sprintf((char *)t, "%d", val);
			}
			else if (c == '\\') // macro parameter?
			{
				if ((c = *s++) == '?')
					t += sprintf((char *)t, "??%04X", local0[inputs - 1]);
				else if (isnumber(c))
				{
					if ((c -= '0'))
					{
						if (c <= param[params - 1][0])
						{
							char *r = &param0[param[params - 1][c]];
							while ((*t++ = *r++))
							{
							}
							--t;
						}
					}
					else
						*t++ = '0' + param[params - 1][0];
				}
				else
					*t++ = '\\', *t++ = c;
			}
			else
				*t++ = c;
		}
		*t = 0;
	}
	strcpy((char *)source, (const char *)input0);
	return 1;
}
int close_input(void)
{
	if (!inputs)
		return 0;
	FILE *f;
	if ((f = input[--inputs]))
	{
		if (f != stdin)
			fclose(f);
	}
	else
	{
		int saved_cond = cond0[inputs];
		while (conditions > saved_cond) {
			conditions--;
			condition0 >>= 2;
		}
		rept_remain[inputs] = 0;
		merge_param();
	}
	input_source[inputs] = 0;
	return 1;
}

void split_input(void)
{
	unsigned char *s = input0;
	unsigned char *end = s;
	while (*end)
		end++;
	split_symbol = end;
	split_opcode = end;
	split_parmtr = end;

	unsigned char *first = s;
	while (*first && (unsigned char)*first <= 32)
		first++;
	if (*first == 0)
		return;

	unsigned char *p = first;
	while ((unsigned char)*p > 32 && *p != ':' && *p != '(')
		p++;

	if (*p == '(') {
		// Only split on '(' if it looks like an M80-style call of a known opcode/macro
		char saved = *p;
		*p = 0;
		int is_op = (get_opcode((char *)first) >= 0);
		int is_ma = (get_macro((char *)first) >= 0);
		*p = saved;
		if (!is_op && !is_ma) {
			// Not a known opcode, continue searching for space or colon
			while ((unsigned char)*p > 32 && *p != ':')
				p++;
		}
	}

	unsigned char *next = p;
	if (*next == ':')
		next++;
	if (*next == ':')
		next++;
	while (*next && (unsigned char)*next <= 32)
		next++;

	int is_label = 0;
	if ((unsigned char)s[0] > 32)
		is_label = 1;
	else if (*p == ':')
		is_label = 1;
	else if (*next && (strncasecmp((char *)next, "equ", 3) == 0 || strncasecmp((char *)next, "defl", 4) == 0 || strncasecmp((char *)next, "macro", 5) == 0))
		is_label = 1;

	saved_p = *p;
	if (saved_p == ':' && p[1] == ':') {
		saved_p = ';'; // Use a special marker for DOUBLE-COLON (PUBLIC)
		*p = 0;
		p++; // skip second colon
	} else {
		*p = 0;
	}
	int is_opcode = (get_opcode((char *)first) >= 0);
	int is_mac = (get_macro((char *)first) >= 0);
	if (is_label && (is_opcode || is_mac) && saved_p != ':')
	{
		// Only treat as label if followed by EQU/DEFL/MACRO
		if (!( (strncasecmp((char *)next, "equ", 3) == 0 && !iseither(next[3])) ||
		       (strncasecmp((char *)next, "defl", 4) == 0 && !iseither(next[4])) ||
		       (strncasecmp((char *)next, "macro", 5) == 0 && !iseither(next[5])) ))
			is_label = 0; // It's an opcode at column 1
	}

	if (is_label)
	{
		split_symbol = first;
		// *p is already 0
		split_opcode = next;
		s = split_opcode;
		while ((unsigned char)*s > 32 && *s != '(')
			s++;
		if (*s == '(')
		{
			// Check if known opcode/macro
			char saved = *s; *s = 0;
			int is_op = (get_opcode((char *)split_opcode) >= 0);
			int is_ma = (get_macro((char *)split_opcode) >= 0);
			*s = saved;
			if (is_ma && !is_op) {
				// Macro call: M80 uses MACRO(arg) syntax, strip the '('
				split_parmtr = s + 1;
				*s = 0;
			} else if (is_op) {
				// Built-in opcode: '(' is part of the operand (e.g. OUT(0AAH),A)
				size_t oplen = (size_t)(s - split_opcode);
				if (oplen > sizeof(split_opcode_buf) - 1) oplen = sizeof(split_opcode_buf) - 1;
				memcpy(split_opcode_buf, (char *)split_opcode, oplen);
				split_opcode_buf[oplen] = 0;
				split_opcode = split_opcode_buf;
				split_parmtr = s;
			} else {
				while ((unsigned char)*s > 32) s++;
				if (*s) {
					*s++ = 0;
					while (*s && (unsigned char)*s <= 32) s++;
					split_parmtr = s;
				}
			}
		}
		else if (*s)
		{
			*s++ = 0;
			while (*s && (unsigned char)*s <= 32)
				s++;
			split_parmtr = s;
		}
	}
	else
	{
		*p = saved_p; // Restore first token
		split_symbol = end;
		split_opcode = first;
		s = split_opcode;
		while ((unsigned char)*s > 32 && *s != '(')
			s++;
		if (*s == '(')
		{
			// Check if known opcode/macro
			char saved = *s; *s = 0;
			int is_op = (get_opcode((char *)split_opcode) >= 0);
			int is_ma = (get_macro((char *)split_opcode) >= 0);
			*s = saved;
			if (is_ma && !is_op) {
				// Macro call: M80 uses MACRO(arg) syntax, strip the '('
				split_parmtr = s + 1;
				*s = 0;
			} else if (is_op) {
				// Built-in opcode: '(' is part of the operand (e.g. OUT(0AAH),A)
				size_t oplen = (size_t)(s - split_opcode);
				if (oplen > sizeof(split_opcode_buf) - 1) oplen = sizeof(split_opcode_buf) - 1;
				memcpy(split_opcode_buf, (char *)split_opcode, oplen);
				split_opcode_buf[oplen] = 0;
				split_opcode = split_opcode_buf;
				split_parmtr = s;
			} else {
				while ((unsigned char)*s > 32) s++;
				if (*s) {
					*s++ = 0;
					while (*s && (unsigned char)*s <= 32) s++;
					split_parmtr = s;
				}
			}
		}
		else if (*s)
		{
			*s++ = 0;
			while (*s && (unsigned char)*s <= 32)
				s++;
			split_parmtr = s;
		}
	}

	unsigned char *trim = split_parmtr + strlen((char *)split_parmtr);
	while (trim > split_parmtr && (unsigned char)trim[-1] <= 32)
		*--trim = 0;
}
void merge_input(void)
{
	if (*split_parmtr)
		split_parmtr[-1] = 32;
	if (*split_opcode)
		split_opcode[-1] = 32;
}

// shunting-yard algorithm for expression evaluation
int eval_hex2i(char *s, char *t)
{
	if (s >= t)
		return eval_status = -1;
	int i = 0;
	char c;
	while (s < t)
	{
		if (i < 0)
			return eval_status = -1; // long!
		if ((c = *s) >= '0' && c <= '9')
			i = i * 16 + c - '0';
		else if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
			i = i * 16 + 9 + (c & 7);
		else
			return eval_status = -1; // bad!
		++s;
	}
	return i;
}
int eval_dec2i(char *s, char *t, int z)
{
	if (s >= t)
		return eval_status = -1;
	int i = 0;
	char c, k = '0' + z;
	while (s < t)
	{
		if (i < 0)
			return eval_status = -1; // long!
		if ((c = *s) >= '0' && c < k)
			i = i * z + c - '0';
		else
			return eval_status = -1; // bad!
		++s;
	}
	return i;
}
enum opertr_
{
	EVAL_NULL,	   // 0!
	EVAL_OP1_PLS,  // "+"
	EVAL_OP1_MNS,  // "-"
	EVAL_OP1_NOT,  // "!"
	EVAL_OP1_CPL,  // "~"
	EVAL_OP1_LOW,  // "LOW"
	EVAL_OP1_HIGH, // "HIGH"
	EVAL_UNARIES,  // CATEGORY
	EVAL_OP2_MUL,  // "*"
	EVAL_OP2_DIV,  // "/"
	EVAL_OP2_MOD,  // "%"
	EVAL_OP2_SHL,  // "<<"
	EVAL_OP2_SHR,  // ">>"
	EVAL_BINARY_H, // CATEGORY
	EVAL_OP2_AND,  // "&"
	EVAL_OP2_XOR,  // "^"
	EVAL_BINARY_M, // CATEGORY
	EVAL_OP2_ADD,  // "+"
	EVAL_OP2_SUB,  // "-"
	EVAL_OP2_ORR,  // "|"
	EVAL_BINARY_L, // CATEGORY
	EVAL_OP2_L_E,  // "<="
	EVAL_OP2_LSS,  // "<"
	EVAL_OP2_G_E,  // ">="
	EVAL_OP2_GRT,  // ">"
	EVAL_OP2_EQU,  // "=" / "=="
	EVAL_OP2_NEQ,  // "<>" / "!="
	EVAL_BINARIES, // CATEGORY
	EVAL_P_INIT,   // "("
	EVAL_P_EXIT,   // ")"
};
#define EVAL_MAXIMUM (3 << 4)
int eval_int[EVAL_MAXIMUM], eval_int_seg[EVAL_MAXIMUM], eval_int_lbl[EVAL_MAXIMUM], eval_ints, eval_ops;
char eval_op[EVAL_MAXIMUM], eval_pr[EVAL_MAXIMUM];
// Indexed by EVAL_* enum values (0-29). EVAL_P_INIT(28) and EVAL_P_EXIT(29) use priority 0.
char eval_priorities[] = {5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
// INLINE int eval_priority(int i) { return (i<EVAL_UNARIES)+(i<EVAL_BINARY_H)+(i<EVAL_BINARY_M)+(i<EVAL_BINARY_L)+(i<EVAL_BINARIES); }
void eval_start_rpn(void)
{
	rpn_count = 0;
	eval_has_extrn = 0;
}

void eval_push_int(int i, int seg)
{
	if (eval_ints >= EVAL_MAXIMUM)
		eval_status = -1; // FULL!
	else
	{
		if (flag_v > 0) fprintf(stderr, "  eval_push_int: %04X, seg=%d\n", i, seg);
		eval_int[eval_ints] = i;
		eval_int_seg[eval_ints] = seg;
		eval_int_lbl[eval_ints] = (seg == SEG_EXTRN) ? eval_res_lbl : -1;
		eval_ints++;

		/* Record for RPN (only if NOT in operate) */
		if (!eval_in_operate && rpn_count < RPN_MAX) {
			if (seg == SEG_EXTRN) {
				rpn_buffer[rpn_count].type = 1; // EXT
				rpn_buffer[rpn_count].val = 0;
				rpn_buffer[rpn_count].seg = SEG_EXTRN;
				snprintf(rpn_buffer[rpn_count].sym, 64, "%s", &asciz[label[eval_res_lbl]]);
				eval_has_extrn = 1;
			} else {
				rpn_buffer[rpn_count].type = 0; // VAL
				rpn_buffer[rpn_count].val = i;
				rpn_buffer[rpn_count].seg = seg;
				rpn_buffer[rpn_count].sym[0] = 0;
			}
			rpn_count++;
		}
	}
}
int eval_pop_int(int *seg)
{
	if (!eval_ints)
	{
		eval_status = -1;
		return 0;
	}
	eval_ints--;
	if (seg)
		*seg = eval_int_seg[eval_ints];
	if (flag_v > 0) fprintf(stderr, "  eval_pop_int: %04X, seg=%d\n", eval_int[eval_ints], eval_int_seg[eval_ints]);
	return eval_int[eval_ints];
}
void eval_push_op(int i)
{
	if (eval_ops >= EVAL_MAXIMUM)
		eval_status = -1; // FULL!
	else
	{
		eval_op[eval_ops] = (unsigned char)i;
		eval_pr[eval_ops] = eval_priorities[(unsigned char)eval_op[eval_ops]], ++eval_ops;
	}
}
int eval_pop_op(void)
{
	if (!eval_ops)
		return eval_status = -1; // EMPTY!
	return eval_op[--eval_ops];
}
int eval_op1(char o, int x, int x_seg, int *res_seg)
{
	*res_seg = x_seg;
	switch (o)
	{
	case EVAL_OP1_PLS:
		return +x;
	case EVAL_OP1_MNS:
		if (x_seg != SEG_ASEG)
			*res_seg = SEG_COMPLEX;
		return -x;
	case EVAL_OP1_NOT:
		if (x_seg != SEG_ASEG)
			*res_seg = SEG_COMPLEX;
		return !x;
	case EVAL_OP1_CPL:
		if (x_seg != SEG_ASEG)
			*res_seg = SEG_COMPLEX;
		return ~x;
	case EVAL_OP1_LOW:
		if (x_seg != SEG_ASEG)
			*res_seg = SEG_COMPLEX;
		return x & 0xFF;
	case EVAL_OP1_HIGH:
		if (x_seg != SEG_ASEG)
			*res_seg = SEG_COMPLEX;
		return (x >> 8) & 0xFF;
	}
	return eval_status = -1;
}
int eval_op2(char o, int x, int x_seg, int y, int y_seg, int *res_seg)
{
	*res_seg = SEG_ASEG;
	int res_val = 0;
	switch (o)
	{
	case EVAL_OP2_ADD:
		if (x_seg == SEG_COMPLEX || y_seg == SEG_COMPLEX || (x_seg != SEG_ASEG && y_seg != SEG_ASEG))
			*res_seg = SEG_COMPLEX;
		else if (x_seg != SEG_ASEG)
			*res_seg = x_seg;
		else if (y_seg != SEG_ASEG)
			*res_seg = y_seg;
		res_val = x + y;
		break;
	case EVAL_OP2_SUB:
		if (x_seg == SEG_COMPLEX || y_seg == SEG_COMPLEX)
			*res_seg = SEG_COMPLEX;
		else if (x_seg != SEG_ASEG && y_seg != SEG_ASEG)
		{
			if (x_seg == SEG_EXTRN || y_seg == SEG_EXTRN)
				*res_seg = SEG_COMPLEX;
			else if (x_seg != y_seg)
				*res_seg = SEG_COMPLEX;
			else
				*res_seg = SEG_ASEG;
		}
		else if (x_seg != SEG_ASEG)
			*res_seg = x_seg;
		else if (y_seg != SEG_ASEG)
			*res_seg = SEG_COMPLEX; // absolute - relocatable requires RPN
		res_val = x - y;
		break;
	default:
		if (x_seg != SEG_ASEG || y_seg != SEG_ASEG || x_seg == SEG_COMPLEX || y_seg == SEG_COMPLEX)
			*res_seg = SEG_COMPLEX;
		switch (o)
		{
		case EVAL_OP2_MUL:
			res_val = x * y; break;
		case EVAL_OP2_DIV:
			res_val = y ? x / y : (eval_status = eval_status ? eval_status : -1); break;
		case EVAL_OP2_MOD:
			res_val = y ? x % y : (eval_status = eval_status ? eval_status : -1); break;
		case EVAL_OP2_AND:
			res_val = x & y; break;
		case EVAL_OP2_XOR:
			res_val = x ^ y; break;
		case EVAL_OP2_ORR:
			res_val = x | y; break;
		case EVAL_OP2_SHL:
			res_val = y >= 0 ? x << (y & 15) : (eval_status = eval_status ? eval_status : -1); break;
		case EVAL_OP2_L_E:
			res_val = x <= y; break;
		case EVAL_OP2_LSS:
			res_val = (unsigned short)x < (unsigned short)y; break;
		case EVAL_OP2_SHR:
			res_val = y >= 0 ? (unsigned short)x >> (y & 15) : (eval_status = eval_status ? eval_status : -1); break;
		case EVAL_OP2_G_E:
			res_val = (unsigned short)x >= (unsigned short)y; break;
		case EVAL_OP2_GRT:
			res_val = (unsigned short)x > (unsigned short)y; break;
		case EVAL_OP2_EQU:
			res_val = (unsigned short)x == (unsigned short)y; break;
		case EVAL_OP2_NEQ:
			res_val = (unsigned short)x != (unsigned short)y; break;
		}
	}
	res_val &= 0xFFFF;
	if (flag_v > 0) fprintf(stderr, "eval_op2: op=%d, x=%04X(%d), y=%04X(%d) -> res=%04X(%d)\n", o, x, x_seg, y, y_seg, res_val, *res_seg);
	return res_val;
}
void eval_operate(void)
{
	char o = eval_pop_op();
	int s1 = SEG_ASEG, s2 = SEG_ASEG, res_s = SEG_ASEG;
	int i = eval_pop_int(&s1);
	int res_val;
	if (o < EVAL_UNARIES)
	{
		res_val = eval_op1(o, i, s1, &res_s);
		eval_in_operate = 1;
		eval_push_int(res_val, res_s);
		eval_in_operate = 0;

		/* Record for RPN */
		if (rpn_count < RPN_MAX) {
			rpn_buffer[rpn_count].type = 2; // OP
			rpn_buffer[rpn_count].seg = SEG_ASEG;
			rpn_buffer[rpn_count].sym[0] = 0;
			switch (o) {
				case EVAL_OP1_MNS: rpn_buffer[rpn_count].val = OP_UNARY_MINUS; break;
				case EVAL_OP1_NOT: rpn_buffer[rpn_count].val = OP_NOT; break;
				case EVAL_OP1_CPL: rpn_buffer[rpn_count].val = OP_NOT; break;
				case EVAL_OP1_LOW: rpn_buffer[rpn_count].val = 4; break;
				case EVAL_OP1_HIGH: rpn_buffer[rpn_count].val = 3; break;
				default: rpn_buffer[rpn_count].val = 0; break;
			}
			if (rpn_buffer[rpn_count].val != 0) rpn_count++;
		}
	}
	else
	{
		int lbl_y = eval_int_lbl[eval_ints]; // label of y (right) operand, just popped
		int j = eval_pop_int(&s2);
		int lbl_x = eval_int_lbl[eval_ints]; // label of x (left) operand, just popped
		res_val = eval_op2(o, j, s2, i, s1, &res_s);
		// Propagate the external label when result inherits an external operand
		if (res_s == SEG_EXTRN) {
			if (s2 == SEG_EXTRN && lbl_x >= 0) eval_res_lbl = lbl_x;
			else if (s1 == SEG_EXTRN && lbl_y >= 0) eval_res_lbl = lbl_y;
		}
		eval_in_operate = 1;
		eval_push_int(res_val, res_s);
		eval_in_operate = 0;

		/* Record for RPN */
		if (rpn_count < RPN_MAX) {
			rpn_buffer[rpn_count].type = 2; // OP
			rpn_buffer[rpn_count].seg = SEG_ASEG;
			rpn_buffer[rpn_count].sym[0] = 0;
			switch (o) {
				case EVAL_OP2_MUL: rpn_buffer[rpn_count].val = OP_MULTIPLY; break;
				case EVAL_OP2_DIV: rpn_buffer[rpn_count].val = OP_DIVIDE; break;
				case EVAL_OP2_MOD: rpn_buffer[rpn_count].val = OP_MOD; break;
				case EVAL_OP2_ADD: rpn_buffer[rpn_count].val = OP_PLUS; break;
				case EVAL_OP2_SUB: rpn_buffer[rpn_count].val = OP_MINUS; break;
				case EVAL_OP2_SHL: rpn_buffer[rpn_count].val = 17; break;
				case EVAL_OP2_SHR: rpn_buffer[rpn_count].val = 16; break;
				case EVAL_OP2_AND: rpn_buffer[rpn_count].val = 24; break;
				case EVAL_OP2_ORR: rpn_buffer[rpn_count].val = 25; break;
				case EVAL_OP2_XOR: rpn_buffer[rpn_count].val = 26; break;
				case EVAL_OP2_EQU: rpn_buffer[rpn_count].val = 18; break;
				case EVAL_OP2_NEQ: rpn_buffer[rpn_count].val = 19; break;
				case EVAL_OP2_LSS: rpn_buffer[rpn_count].val = 20; break;
				case EVAL_OP2_L_E: rpn_buffer[rpn_count].val = 21; break;
				case EVAL_OP2_GRT: rpn_buffer[rpn_count].val = 22; break;
				case EVAL_OP2_G_E: rpn_buffer[rpn_count].val = 23; break;
				default: rpn_buffer[rpn_count].val = 0; break;
			}
			if (rpn_buffer[rpn_count].val != 0) rpn_count++;
		}
	}
}
int eval_get_prefix(void)
{
	char c = *eval_cursor;
	if (c == '(') { eval_cursor++; return EVAL_P_INIT; }
	if (c == '+') { eval_cursor++; return EVAL_OP1_PLS; }
	if (c == '-') { eval_cursor++; return EVAL_OP1_MNS; }
	if (c == '~') { eval_cursor++; return EVAL_OP1_CPL; }
	if (c == '!') { eval_cursor++; return EVAL_OP1_NOT; }
	
	if (strncasecmp((char *)eval_cursor, "NOT", 3) == 0 && !iseither(eval_cursor[3]))
	{
		eval_cursor += 3;
		return EVAL_OP1_CPL;
	}
	if (strncasecmp((char *)eval_cursor, "LOW", 3) == 0 && !iseither(eval_cursor[3]))
	{
		eval_cursor += 3;
		return EVAL_OP1_LOW;
	}
	if (strncasecmp((char *)eval_cursor, "HIGH", 4) == 0 && !iseither(eval_cursor[4]))
	{
		eval_cursor += 4;
		return EVAL_OP1_HIGH;
	}
	return EVAL_NULL;
}
int eval_get_suffix(void)
{
	char c;
	switch (*eval_cursor++)
	{
	case ')':
		return EVAL_P_EXIT;
	case '+':
		return EVAL_OP2_ADD;
	case '-':
		return EVAL_OP2_SUB;
	case '*':
		return EVAL_OP2_MUL;
	case '/':
		return EVAL_OP2_DIV;
	case '%':
		return EVAL_OP2_MOD;
	case '&':
		return EVAL_OP2_AND;
	case '|':
		return EVAL_OP2_ORR;
	case '^':
		return EVAL_OP2_XOR;
	case '<':
		if ((c = *eval_cursor++) == '<')
			return EVAL_OP2_SHL;
		if (c == '=')
			return EVAL_OP2_L_E;
		if (c == '>')
			return EVAL_OP2_NEQ;
		return --eval_cursor, EVAL_OP2_LSS;
	case '>':
		if ((c = *eval_cursor++) == '>')
			return EVAL_OP2_SHR;
		if (c == '=')
			return EVAL_OP2_G_E;
		return --eval_cursor, EVAL_OP2_GRT;
	case '=':
		if (*eval_cursor == '=')
			++eval_cursor;
		return EVAL_OP2_EQU;
	case '!':
		if (*eval_cursor == '=')
			return ++eval_cursor, EVAL_OP2_NEQ;
	case 0:
	case ',': // end of expr.
		break;
	default: // unknown character!
		--eval_cursor;
		while (*eval_cursor && (unsigned char)*eval_cursor <= 32)
			eval_cursor++;
		if (strncasecmp((char *)eval_cursor, "AND", 3) == 0 && !iseither(eval_cursor[3]))
		{
			eval_cursor += 3;
			return EVAL_OP2_AND;
		}
		if (strncasecmp((char *)eval_cursor, "OR", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_ORR;
		}
		if (strncasecmp((char *)eval_cursor, "XOR", 3) == 0 && !iseither(eval_cursor[3]))
		{
			eval_cursor += 3;
			return EVAL_OP2_XOR;
		}
		if (strncasecmp((char *)eval_cursor, "MOD", 3) == 0 && !iseither(eval_cursor[3]))
		{
			eval_cursor += 3;
			return EVAL_OP2_MOD;
		}
		if (strncasecmp((char *)eval_cursor, "SHL", 3) == 0 && !iseither(eval_cursor[3]))
		{
			eval_cursor += 3;
			return EVAL_OP2_SHL;
		}
		if (strncasecmp((char *)eval_cursor, "SHR", 3) == 0 && !iseither(eval_cursor[3]))
		{
			eval_cursor += 3;
			return EVAL_OP2_SHR;
		}
		if (strncasecmp((char *)eval_cursor, "EQ", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_EQU;
		}
		if (strncasecmp((char *)eval_cursor, "NE", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_NEQ;
		}
		if (strncasecmp((char *)eval_cursor, "LT", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_LSS;
		}
		if (strncasecmp((char *)eval_cursor, "GT", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_GRT;
		}
		if (strncasecmp((char *)eval_cursor, "LE", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_L_E;
		}
		if (strncasecmp((char *)eval_cursor, "GE", 2) == 0 && !iseither(eval_cursor[2]))
		{
			eval_cursor += 2;
			return EVAL_OP2_G_E;
		}
		return --eval_cursor, EVAL_NULL;
	}
	return --eval_cursor, EVAL_NULL;
}
void eval_shunt(char c)
{
	if (c == EVAL_P_EXIT)
	{
		while (eval_ops && (eval_op[eval_ops - 1] != EVAL_P_INIT))
			eval_operate();
		if (eval_pop_op() != EVAL_P_INIT)
			eval_status = -1;
	}
	else
	{
		int i = eval_priorities[(unsigned char)c];
		if (c != EVAL_P_INIT)
			while (eval_ops && (i <= eval_pr[eval_ops - 1]))
				eval_operate();
		eval_push_op(c);
	}
}
unsigned int eval_escape(int i)
{
	if (!flag_string_escapes || i != '\\')
		return (unsigned char)i;
	switch (*++eval_cursor)
	{
	case 'a':
		return 7;
	case 'b':
		return 8;
	case 'e':
		return 27;
	case 'f':
		return 12;
	case 'n':
		return 10;
	case 'r':
		return 13;
	case 't':
		return 9;
	case 'v':
		return 11;
	case '0':
		return 0;
	case '\'':
		return '\'';
	case '"':
		return '"';
	case '\\':
		return i;
	case 'x':
	case 'u':
	{
		int n = (eval_cursor[0] == 'x' ? 2 : 4);
		unsigned int val = 0;
		int parsed_digits = 0;
		for (int k = 0; k < n; k++) {
			unsigned char h = (unsigned char)eval_cursor[1];
			if (isxdigit(h)) {
				val = (val << 4) | (h <= '9' ? h - '0' : (tolower(h) - 'a' + 10));
				eval_cursor++;
				parsed_digits++;
			} else break;
		}
		if (eval_cursor[0] == 'u' && parsed_digits < 4) {
			FATAL_ERROR(error_invalid_string); // \u requires exactly 4 digits in C# Regex.Unescape
		}
		if (val >= 0xD800 && val <= 0xDBFF) {
			// High surrogate. Check if next is \u and low surrogate.
			char *lookahead = eval_cursor + 1;
			if (lookahead[0] == '\\' && lookahead[1] == 'u') {
				unsigned int low_val = 0;
				int valid_low = 1;
				for (int k = 0; k < 4; k++) {
					unsigned char h = (unsigned char)lookahead[2+k];
					if (isxdigit(h)) {
						low_val = (low_val << 4) | (h <= '9' ? h - '0' : (tolower(h) - 'a' + 10));
					} else {
						valid_low = 0;
						break;
					}
				}
				if (valid_low && low_val >= 0xDC00 && low_val <= 0xDFFF) {
					// Combine
					val = 0x10000 + ((val - 0xD800) << 10) + (low_val - 0xDC00);
					eval_cursor += 6; // consume \uXXXX
				}
			}
		}
		return val;
	}
	}
	// Unknown escape sequence, treat backslash as literal
	return (unsigned char)i;
}
int eval(char *s)
{
	char *check_p = s;
	while (*check_p && (unsigned char)*check_p <= 32) check_p++;
	if (*check_p == '(') {
		char *check_end = check_p + 1;
		while (*check_end && (unsigned char)*check_end <= 32) check_end++;
		if (*check_end == ')') {
			check_end++;
			while (*check_end && (unsigned char)*check_end <= 32) check_end++;
			if (!*check_end || *check_end == ',') {
				eval_cursor = check_end;
				eval_res_seg = SEG_ASEG;
				eval_status = 0;
				return 0;
			}
		}
	}
	int i = 0; // perhaps this should be "long long"...
	char c, q = eval_status = eval_ints = eval_ops = 0;
	eval_cursor = s;
	while (eval_status >= 0)
	{
		while (*eval_cursor && (unsigned char)*eval_cursor <= 32)
			eval_cursor++;
		c = *eval_cursor;
		if (!c || c == ',')
			break;

		if (!q)
		{
			if (c == ')') {
				// Empty parens or operand missing before closing paren
				eval_push_int(0, SEG_ASEG);
				q = 1;
				continue;
			}
			if (c == '"' || c == '\'') // string or character constant
			{
				unsigned char delim = c;
				i = 0;
				while (eval_status >= 0 && (c = *++eval_cursor)) {
					if (c == delim) {
						if (eval_cursor[1] == delim) {
							eval_cursor++; // skip first, loop increments over second
						} else {
							break;
						}
					}
					i = (i << 8) + (eval_escape(c) & 255);
				}
				if (!c)
					eval_status = -1;
				else
				{
					++eval_cursor;
					eval_push_int(i, SEG_ASEG);
					q = 1;
				}
			}
			else if (!q && (i = eval_get_prefix())) // prefix
			{
				eval_shunt(i);
				continue;
			}
			else if (!q && (iseither(c) || (c >= '#' && c <= '%'))) // integer or symbol
			{
				s = eval_cursor;
				while ((c = *++eval_cursor) && iseither(c))
				{
				}
				c = *s;
				int seg = SEG_ASEG;
				if (c == '$' && !iseither(s[1]))
				{
					i = flag_dollar ? dollar : (phase_active ? phase_target : target);
					seg = flag_dollar ? dollar_seg : (phase_active ? phase_seg : current_seg);
				}
				else if (c == '#' || (c == '$' && isnumber(s[1])))
					i = eval_hex2i(&s[1], eval_cursor); // prefixed hexadecimal
				else if (c == '0' && (s[1] | 32) == 'x')
					i = eval_hex2i(&s[2], eval_cursor); // prefixed hexadecimal C-style
				else if (c == '%')
				{
					if (s[1] == '0' || s[1] == '1')
						i = eval_dec2i(&s[1], eval_cursor, 2); // prefixed binary
					else
					{
						eval_cursor = &s[1];
						continue;
					}
				}

				else if (c == '"' || c == '\'')
				{
					char delim = c;
					eval_cursor++;
					int val = (unsigned char)*eval_cursor;
					if (val == '\\') {
						val = eval_escape((unsigned char)*++eval_cursor);
					}
					eval_cursor++;
					if (*eval_cursor != delim)
						FATAL_ERROR(error_invalid_string);
					eval_cursor++;
					eval_push_int(val, SEG_ASEG);
					q = 1;
				}
				else if (isnumber(c))
				{
					int base = 10;
					char *end_p = eval_cursor;
					char last_c = eval_cursor[-1] | 32;
					if (last_c == 'h') base = 16;
					else if (last_c == 'd') base = 10;
					else if (last_c == 'o' || last_c == 'q') base = 8;
					else if (last_c == 'b') base = 2;
					
					if (base != 10) end_p--; // Exclude suffix
					
					if (base == 16) i = eval_hex2i(s, end_p);
					else i = eval_dec2i(s, end_p, base);
				}
				else
				{
					int is_hex_num = 0;
					if (eval_cursor - s > 1 && (eval_cursor[-1] | 32) == 'h') {
						is_hex_num = 1;
						for (char *p = s; p < eval_cursor - 1; p++) {
							char ch = *p | 32;
							if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
								is_hex_num = 0; break;
							}
						}
					}
					if (is_hex_num) {
						i = eval_hex2i(s, eval_cursor - 1);
					} else {
						char saved_c = *eval_cursor;
						*eval_cursor = 0;
						char scoped_name[1024];
						apply_module_scope(scoped_name, (char *)s);
						i = get_label(scoped_name);
						
						// .EXTROOT logic: if not found in module, try global and check if EXTRN
						if (i < 0 && flag_extroot && current_module[0] && s[0] != '.' && !strchr((char *)s, '.')) {
							int global_i = get_label((char *)s);
							if (global_i >= 0 && (label_flag[global_i] & LBL_EXTRN)) {
								i = global_i;
							}
						}

						int sym_has_hash = (strstr((char *)s, "##") != NULL);
						*eval_cursor = saved_c;
						if (i < 0) {
							if (sym_has_hash) {
								/* MACRO-80 ## suffix: auto-declare as external reference */
								char sym_auto[1024];
								snprintf(sym_auto, sizeof(sym_auto), "%s", (char *)s);
								char *hp = strstr(sym_auto, "##");
								if (hp) *hp = 0;
								i = add_label(sym_auto, 0, SEG_EXTRN);
								label_flag[i] |= LBL_EXTRN;
								seg = SEG_EXTRN;
								eval_res_lbl = i;
								i = 0;
							} else if (pass == 2 && opt_unknown_symbols_external) {
								/* Auto-declare as external reference if --unknown-symbols-external is set */
								i = add_label(scoped_name, 0, SEG_EXTRN);
								label_flag[i] |= LBL_EXTRN;
								seg = SEG_EXTRN;
								eval_res_lbl = i;
								i = 0;
							} else {
								if (flag_v > 0) fprintf(stderr, "eval: symbol '%s' (scoped: '%s') NOT FOUND\n", (char *)s, scoped_name);
								eval_status |= i = 1;
							}
						}
 else {
							seg = label_seg[i];
							eval_res_lbl = i;
							if (label_flag[i] & LBL_EXTRN)
								seg = SEG_EXTRN;
							if (flag_v > 0) fprintf(stderr, "eval: symbol '%s' found as '%s', value=%04X, seg=%d, flag=%d\n", (char *)s, scoped_name, value[i], label_seg[i], label_flag[i]);
							i = value[i];
						}
					}
				}
				eval_push_int(i, seg);
				q = 1;
			}
			else
				eval_status = -1;
		}
		else
		{
			if ((c = eval_get_suffix()))
			{
				eval_shunt(c);
				if (c != EVAL_P_EXIT)
					q = 0;
			}
			else
				break;
		}
	}
	if (eval_status >= 0)
	{
		if (strcmp(s, "()") == 0) {
			eval_push_int(0, SEG_ASEG);
		} else {
			while (eval_ops)
				eval_operate();
		}
		i = eval_pop_int(&eval_res_seg);
		if (eval_ints)
		{
			if (flag_v > 0 || strcmp(s, "()") == 0) fprintf(stderr, "DEBUG eval FAILED: s='%s', eval_ints=%d, eval_status=%d\n", s, eval_ints, eval_status);
			eval_status = -1;
		}
	}
	return i;
}
void write_rel_switch_seg()
{
	write_rel_control(11, target, current_seg, "", 0);
}

static void sdcc_select_for_segment(int new_seg, int new_common)
{
	if (new_seg == SEG_CSEG)
		sdcc_select_area("_CODE", 0, 0);
	else if (new_seg == SEG_DSEG)
		sdcc_select_area("_DATA", 0, 0);
	else if (new_seg == SEG_ASEG)
		sdcc_select_area("_ABS", 1, 1);
	else if (new_seg == SEG_COMMON && new_common >= 0)
		sdcc_select_area(common_names[new_common], 0, 1);
}

static void switch_seg(int new_seg, int new_common) {
	if (current_seg == SEG_COMMON && current_common >= 0) common_targets[current_common] = target;
	else seg_target[current_seg] = target;
	
	current_seg = new_seg;
	current_common = new_common;
	
	if (current_seg == SEG_COMMON && current_common >= 0) target = common_targets[current_common];
	else target = seg_target[current_seg];
	
	sdcc_select_for_segment(new_seg, new_common);
	write_rel_switch_seg();
}

// tables and functions of opcodes and parameters

#define GET_SEARCH(x)                                            \
	{                                                            \
		int i, j = 0, k = sizeof(x) / sizeof(x[0]) - 1, l;       \
		do                                                       \
		{                                                        \
			if (!(l = strcasecmp(s, x[i = ((j + k) / 2)].name))) \
				return i;                                        \
			if (l > 0)                                           \
				j = i + 1;                                       \
			else                                                 \
				k = i - 1;                                       \
		} while (j <= k);                                        \
		return -1;                                               \
	}
 // >=0 OK, <0 ERROR

enum opcode_
{
	PSEUDO_NULL = 0,
	PSEUDO_IGNORE,
	PSEUDO_PHASE,
	PSEUDO_DEPHASE,
	PSEUDO_EXTROOT,
	PSEUDO_XEXTROOT,
	PSEUDO_ALIGN,
	PSEUDO_DEFB,
	PSEUDO_DEFC,
	PSEUDO_DEFD,
	PSEUDO_DEFS,
	PSEUDO_DEFW,
	PSEUDO_DEFZ,
	PSEUDO_ELIF,
	PSEUDO_ELSE,
	PSEUDO_END,
	PSEUDO_ENDIF,
	PSEUDO_ENDM,
	PSEUDO_EXITM,
	PSEUDO_CONTM,
	PSEUDO_EQU,
	PSEUDO_DEFL,
	PSEUDO_PUBLIC,
	PSEUDO_EXTRN,
	PSEUDO_NAME,
	PSEUDO_PRINTX,
	PSEUDO_PRINT,
	PSEUDO_WARN,
	PSEUDO_ERROR,
	PSEUDO_FATAL,
	PSEUDO_LOCAL,
	PSEUDO_REPT,
	PSEUDO_IRP,
	PSEUDO_IRPC,
	PSEUDO_IRPS,
	PSEUDO_ASEG,
	PSEUDO_CSEG,
	PSEUDO_DSEG,
	PSEUDO_COMMON,
	PSEUDO_IF,
	PSEUDO_IF1,
	PSEUDO_IF2,
	PSEUDO_IFABS,
	PSEUDO_IFREL,
	PSEUDO_IFCPU,
	PSEUDO_IFNCPU,
	PSEUDO_IFB,
	PSEUDO_IFDEF,
	PSEUDO_IFNDEF,
	PSEUDO_IFIDN,
	PSEUDO_IFIDNI,
	PSEUDO_IFDIF,
	PSEUDO_IFDIFI,
	PSEUDO_IFNB,
	PSEUDO_INCBIN,
	PSEUDO_INCLUDE,
	PSEUDO_MACRO,
	PSEUDO_ORG,
	PSEUDO_MODULE,
	PSEUDO_ENDMOD,
	PSEUDO_STRENC,
	PSEUDO_STRESC,
	PSEUDO_RELAB,
	PSEUDO_XRELAB,
	PSEUDO_CPU,
	PSEUDO_8080,
	PSEUDO_Z80,
	PSEUDO_PRINT1,
	PSEUDO_PRINT2,
	PSEUDO_AREA,

	OPCODE_COPY1 = 200,
	OPCODE_COPY2,
	OPCODE_ADC,
	OPCODE_ADD,
	OPCODE_BIT,
	OPCODE_CALL,
	OPCODE_DJNZ,
	OPCODE_EX,
	OPCODE_IM,
	OPCODE_IN,
	OPCODE_INC,
	OPCODE_JP,
	OPCODE_JR,
	OPCODE_LD,
	OPCODE_LDA,
	OPCODE_LDCTL,
	OPCODE_LDW,
	OPCODE_ADDW,
	OPCODE_SUBW,
	OPCODE_CPW,
	OPCODE_MULT,
	OPCODE_DIV,
	OPCODE_MULTW,
	OPCODE_MULTUW,
	OPCODE_DIVW,
	OPCODE_DIVUW,
	OPCODE_EXTS,
	OPCODE_EPUF,
	OPCODE_EPUI,
	OPCODE_EPUM,
	OPCODE_MEPU,
	OPCODE_TSET,
	OPCODE_TSTI,
	OPCODE_PCACHE,
	OPCODE_MULUB,
	OPCODE_MULUW,
	OPCODE_NOP,
	OPCODE_OUT,
	OPCODE_POP,
	OPCODE_RET,
	OPCODE_RLC,
	OPCODE_RST,
	OPCODE_SUB,
	OPCODE_INW,
	OPCODE_OUTW,
	OPCODE_SC,
	OPCODE_LDUD,
	OPCODE_LDUP,
	OPCODE_JAF,
	OPCODE_8080_JMPW,
	OPCODE_8080_MOV,
	OPCODE_8080_MVI,
	OPCODE_8080_INR,
	OPCODE_8080_ANA,
	OPCODE_8080_ALU_I,
	OPCODE_8080_DAD,
	OPCODE_8080_LXI,
	OPCODE_8080_INXDCX,
	OPCODE_8080_LDSTAX,
	OPCODE_8080_CPI,

	TYPE_MACRO = 1000
};
t_opcode opcode[] = {
	// MUST BE IN ALPHABETICAL ORDER!
	{".8080", 0, PSEUDO_8080},
	{".align", 0, PSEUDO_ALIGN},
	{".area", 0, PSEUDO_AREA},
	{".cpu", 0, PSEUDO_CPU},
	{".dephase", 0, PSEUDO_DEPHASE},
	{".error", 0, PSEUDO_ERROR},
	{".extroot", 0, PSEUDO_EXTROOT},
	{".fatal", 0, PSEUDO_FATAL},
	{".lall", 0, PSEUDO_IGNORE},
	{".list", 0, PSEUDO_IGNORE},
	{".name", 0, PSEUDO_NAME},
	{".nolist", 0, PSEUDO_IGNORE},
	{".phase", 0, PSEUDO_PHASE},
	{".print", 0, PSEUDO_PRINT},
	{".print1", 0, PSEUDO_PRINT1},
	{".print2", 0, PSEUDO_PRINT2},
	{".printx", 0, PSEUDO_PRINTX},
	{".radix", 0, PSEUDO_IGNORE},
	{".relab", 0, PSEUDO_RELAB},
	{".sall", 0, PSEUDO_IGNORE},
	{".strenc", 0, PSEUDO_STRENC},
	{".stresc", 0, PSEUDO_STRESC},
	{".title", 0, PSEUDO_NAME},
	{".warn", 0, PSEUDO_WARN},
	{".xall", 0, PSEUDO_IGNORE},
	{".xextroot", 0, PSEUDO_XEXTROOT},
	{".xlist", 0, PSEUDO_IGNORE},
	{".xrelab", 0, PSEUDO_XRELAB},
	{".z80", 0, PSEUDO_Z80},
	{"=", 0, PSEUDO_EQU},
	{"aci", 0xCE, OPCODE_8080_ALU_I},
	{"adc", +0, OPCODE_ADC},
	{"add", 0, OPCODE_ADD},
	{"addw", 0, OPCODE_ADDW},
	{"adi", 0xC6, OPCODE_8080_ALU_I},
	{"align", 0, PSEUDO_ALIGN},
	{"and", +4, OPCODE_SUB},
	{"area", 0, PSEUDO_AREA},
	{"aseg", 0, PSEUDO_ASEG},
	{"bit", +0x40, OPCODE_BIT},
	{"brk", 0xEDFF, OPCODE_COPY2}, // RASM breakpoint
	{"call", 0, OPCODE_CALL},
	{"ccf", 0x3F, OPCODE_COPY1},
	{"common", 0, PSEUDO_COMMON},
	{"contm", 0, PSEUDO_CONTM},
	{"cp", +7, OPCODE_SUB},
	{"cpd", 0xEDA9, OPCODE_COPY2},
	{"cpdr", 0xEDB9, OPCODE_COPY2},
	{"cpi", 0xEDA1, OPCODE_8080_CPI},
	{"cpir", 0xEDB1, OPCODE_COPY2},
	{"cpl", 0x2F, OPCODE_COPY1},
	{"cpw", 0, OPCODE_CPW},
	{"cseg", 0, PSEUDO_CSEG},
	{"daa", 0x27, OPCODE_COPY1},
	{"db", 0, PSEUDO_DEFB},
	{"dc", 0, PSEUDO_DEFC},
	{"dd", 0, PSEUDO_DEFD},
	{"dec", +1, OPCODE_INC},
	{"decw", +11, OPCODE_INC}, // oo=+11 means 16-bit DECW
	{"defb", 0, PSEUDO_DEFB},
	{"defc", 0, PSEUDO_DEFC},
	{"defd", 0, PSEUDO_DEFD},
	{"defl", 0, PSEUDO_DEFL},
	{"defm", 0, PSEUDO_DEFB},
	{"dephase", 0, PSEUDO_DEPHASE},
	{"defs", 0, PSEUDO_DEFS},
	{"defw", 0, PSEUDO_DEFW},
	{"defz", 0, PSEUDO_DEFZ},
	{"di", 0xF3, OPCODE_COPY1},
	{"div", 0, OPCODE_DIV},
	{"divu", 8, OPCODE_DIV},
	{"divuw", 0, OPCODE_DIVUW},
	{"divw", 0, OPCODE_DIVW},
	{"djnz", 0, OPCODE_DJNZ},
	{"ds", 0, PSEUDO_DEFS},
	{"dseg", 0, PSEUDO_DSEG},
	{"dw", 0, PSEUDO_DEFW},
	{"dz", 0, PSEUDO_DEFZ},
	{"ei", 0xFB, OPCODE_COPY1},
	{"elif", 0, PSEUDO_ELIF},
	{"else", 0, PSEUDO_ELSE},
	{"elseif", 0, PSEUDO_ELIF},
	{"end", 0, PSEUDO_END},
	{"endif", 0, PSEUDO_ENDIF},
	{"endm", 0, PSEUDO_ENDM},
	{"endmod", 0, PSEUDO_ENDMOD},
	{"equ", 0, PSEUDO_EQU},
	{"ex", 0, OPCODE_EX},
	{"exa", 0x08, OPCODE_COPY1}, // RASM synonym
	{"exitm", 0, PSEUDO_EXITM},
	{"external", 0, PSEUDO_EXTRN},
	{"extrn", 0, PSEUDO_EXTRN},
	{"exx", 0xD9, OPCODE_COPY1},
	{"epuf", 0xED97, OPCODE_COPY2},
	{"epui", 0xED9F, OPCODE_COPY2},
	{"epum", 0, OPCODE_EPUM},
	{"exts", 0, OPCODE_EXTS},
	{"halt", 0x76, OPCODE_COPY1},
	{"hlt", 0x76, OPCODE_COPY1},
	{"if", 0, PSEUDO_IF},
	{"if1", 0, PSEUDO_IF1},
	{"if2", 0, PSEUDO_IF2},
	{"ifabs", 0, PSEUDO_IFABS},
	{"ifb", 0, PSEUDO_IFB},
	{"ifcpu", 0, PSEUDO_IFCPU},
	{"ifdif", 0, PSEUDO_IFDIF},
	{"ifdifi", 0, PSEUDO_IFDIFI},
	{"ifdef", 0, PSEUDO_IFDEF},
	{"ifidn", 0, PSEUDO_IFIDN},
	{"ifidni", 0, PSEUDO_IFIDNI},
	{"ifnb", 0, PSEUDO_IFNB},
	{"ifncpu", 0, PSEUDO_IFNCPU},
	{"ifndef", 0, PSEUDO_IFNDEF},
	{"ifrel", 0, PSEUDO_IFREL},
	{"im", 0, OPCODE_IM},
	{"in", 0, OPCODE_IN},
	{"ind", 0xEDAA, OPCODE_COPY2},
	{"indr", 0xEDBA, OPCODE_COPY2},
	{"indrw", 0xED9A, OPCODE_COPY2},
	{"indw", 0xED8A, OPCODE_COPY2},
	{"ini", 0xEDA2, OPCODE_COPY2},
	{"inir", 0xEDB2, OPCODE_COPY2},
	{"inirw", 0xED92, OPCODE_COPY2},
	{"iniw", 0xED82, OPCODE_COPY2},
	{"inw", 0, OPCODE_INW},
	{"inc", +0, OPCODE_INC},
	{"incw", +10, OPCODE_INC}, // oo=+10 means 16-bit INCW
	{"incbin", 0, PSEUDO_INCBIN},
	{"include", 0, PSEUDO_INCLUDE},
	{"irp", 0, PSEUDO_IRP},
	{"irpc", 0, PSEUDO_IRPC},
	{"irps", 0, PSEUDO_IRPS},
	{"jaf", 0x28, OPCODE_JAF},
	{"jar", 0x20, OPCODE_JAF},
	{"jp", 0, OPCODE_JP},
	{"jr", 0, OPCODE_JR},
	{"ld", 0, OPCODE_LD},
	{"lda", 0, OPCODE_LDA},
	{"ldctl", 0, OPCODE_LDCTL},
	{"ldud", 0, OPCODE_LDUD},
	{"ldup", 0, OPCODE_LDUP},
	{"ldd", 0xEDA8, OPCODE_COPY2},
	{"lddr", 0xEDB8, OPCODE_COPY2},
	{"ldi", 0xEDA0, OPCODE_COPY2},
	{"ldir", 0xEDB0, OPCODE_COPY2},
	{"ldw", 0, OPCODE_LDW},
	{"list", 0, PSEUDO_IGNORE},
	{"local", 0, PSEUDO_LOCAL},
	{"macro", 0, PSEUDO_MACRO},
	{"mepu", 0, OPCODE_MEPU},
	{"module", 0, PSEUDO_MODULE},
	{"mult", 0, OPCODE_MULT},
	{"multu", 8, OPCODE_MULT},
	{"multuw", 0, OPCODE_MULTUW},
	{"multw", 0, OPCODE_MULTW},
	{"mulub", 0, OPCODE_MULUB},
	{"muluw", 0, OPCODE_MULUW},
	{"name", 0, PSEUDO_NAME},
	{"neg", 0xED44, OPCODE_COPY2},
	{"nolist", 0, PSEUDO_IGNORE},
	{"nop", 0x00, OPCODE_NOP},
	{"or", +6, OPCODE_SUB},
	{"org", 0, PSEUDO_ORG},
	{"otdr", 0xEDBB, OPCODE_COPY2},
	{"otdrw", 0xED9B, OPCODE_COPY2},
	{"otir", 0xEDB3, OPCODE_COPY2},
	{"otirw", 0xED93, OPCODE_COPY2},
	{"out", 0, OPCODE_OUT},
	{"outd", 0xEDAB, OPCODE_COPY2},
	{"outdw", 0xED8B, OPCODE_COPY2},
	{"outi", 0xEDA3, OPCODE_COPY2},
	{"outiw", 0xED83, OPCODE_COPY2},
	{"outw", 0, OPCODE_OUTW},
	{"page", 0, PSEUDO_IGNORE},
	{"pcache", 0xED65, OPCODE_COPY2},
	{"pop", +0xC1, OPCODE_POP},
	{"print", 0, PSEUDO_PRINT},
	{"public", 0, PSEUDO_PUBLIC},
	{"push", +0xC5, OPCODE_POP},
	{"relab", 0, PSEUDO_RELAB},
	{"rept", 0, PSEUDO_REPT},
	{"res", +0x80, OPCODE_BIT},
	{"ret", 0, OPCODE_RET},
	{"reti", 0xED4D, OPCODE_COPY2},
	{"retil", 0xED55, OPCODE_COPY2},
	{"retn", 0xED45, OPCODE_COPY2},
	{"rl", +0x10, OPCODE_RLC},
	{"rla", 0x17, OPCODE_COPY1},
	{"rlc", +0x00, OPCODE_RLC},
	{"rlca", 0x07, OPCODE_COPY1},
	{"rld", 0xED6F, OPCODE_COPY2},
	{"rr", +0x18, OPCODE_RLC},
	{"rra", 0x1F, OPCODE_COPY1},
	{"rrc", +0x08, OPCODE_RLC},
	{"rrca", 0x0F, OPCODE_COPY1},
	{"rrd", 0xED67, OPCODE_COPY2},
	{"rst", 0, OPCODE_RST},
	{"sc", 0, OPCODE_SC},
	{"sbc", +1, OPCODE_ADC},
	{"scf", 0x37, OPCODE_COPY1},
	{"set", +0xC0, OPCODE_BIT},
	{"shl", +0x20, OPCODE_RLC}, // synonym
	{"shr", +0x38, OPCODE_RLC}, // synonym
	{"sl1", +0x30, OPCODE_RLC}, // RASM synonym
	{"sla", +0x20, OPCODE_RLC},
	{"sll", +0x30, OPCODE_RLC},
	{"sra", +0x28, OPCODE_RLC},
	{"srl", +0x38, OPCODE_RLC},
	{"sub", +2, OPCODE_SUB},
	{"subw", 0, OPCODE_SUBW},
	{"subttl", 0, PSEUDO_IGNORE},
	{"title", 0, PSEUDO_NAME},
	{"tsti", 0, OPCODE_TSTI},
	{"tset", 0, OPCODE_TSET},
	{"xrelab", 0, PSEUDO_XRELAB},
	{"xor", +5, OPCODE_SUB},
	// Intel 8080 mnemonics
	{"ana",  0xA0, OPCODE_8080_ANA},
	{"ani",  0xE6, OPCODE_8080_ALU_I},
	{"cc",   0xDC, OPCODE_8080_JMPW},
	{"cm",   0xFC, OPCODE_8080_JMPW},
	{"cma",  0x2F, OPCODE_COPY1},
	{"cmc",  0x3F, OPCODE_COPY1},
	{"cmp",  0xB8, OPCODE_8080_ANA},
	{"cnc",  0xD4, OPCODE_8080_JMPW},
	{"cnz",  0xC4, OPCODE_8080_JMPW},
	{"cpe",  0xEC, OPCODE_8080_JMPW},
	{"cpo",  0xE4, OPCODE_8080_JMPW},
	{"cz",   0xCC, OPCODE_8080_JMPW},
	{"dad",  0,    OPCODE_8080_DAD},
	{"dcr",  1,    OPCODE_8080_INR},
	{"dcx",  0x0B, OPCODE_8080_INXDCX},
	{"inr",  0,    OPCODE_8080_INR},
	{"inx",  0x03, OPCODE_8080_INXDCX},
	{"jc",   0xDA, OPCODE_8080_JMPW},
	{"jm",   0xFA, OPCODE_8080_JMPW},
	{"jmp",  0xC3, OPCODE_8080_JMPW},
	{"jnc",  0xD2, OPCODE_8080_JMPW},
	{"jnz",  0xC2, OPCODE_8080_JMPW},
	{"jpe",  0xEA, OPCODE_8080_JMPW},
	{"jpo",  0xE2, OPCODE_8080_JMPW},
	{"jz",   0xCA, OPCODE_8080_JMPW},
	{"ldax", 0x0A, OPCODE_8080_LDSTAX},
	{"lhld", 0x2A, OPCODE_8080_JMPW},
	{"lxi",  0,    OPCODE_8080_LXI},
	{"mov",  0,    OPCODE_8080_MOV},
	{"mvi",  0,    OPCODE_8080_MVI},
	{"ora",  0xB0, OPCODE_8080_ANA},
	{"ori",  0xF6, OPCODE_8080_ALU_I},
	{"pchl", 0xE9, OPCODE_COPY1},
	{"ral",  0x17, OPCODE_COPY1},
	{"rar",  0x1F, OPCODE_COPY1},
	{"rc",   0xD8, OPCODE_COPY1},
	{"rm",   0xF8, OPCODE_COPY1},
	{"rnc",  0xD0, OPCODE_COPY1},
	{"rnz",  0xC0, OPCODE_COPY1},
	{"rp",   0xF0, OPCODE_COPY1},
	{"rpe",  0xE8, OPCODE_COPY1},
	{"rpo",  0xE0, OPCODE_COPY1},
	{"rz",   0xC8, OPCODE_COPY1},
	{"sbi",  0xDE, OPCODE_8080_ALU_I},
	{"shld", 0x22, OPCODE_8080_JMPW},
	{"sphl", 0xF9, OPCODE_COPY1},
	{"sta",  0x32, OPCODE_8080_JMPW},
	{"stax", 0x02, OPCODE_8080_LDSTAX},
	{"stc",  0x37, OPCODE_COPY1},
	{"sui",  0xD6, OPCODE_8080_ALU_I},
	{"xchg", 0xEB, OPCODE_COPY1},
	{"xra",  0xA8, OPCODE_8080_ANA},
	{"xri",  0xEE, OPCODE_8080_ALU_I},
	{"xthl", 0xE3, OPCODE_COPY1},
};
int get_opcode(char *s)
{
	int i, n = sizeof(opcode) / sizeof(opcode[0]);
	for (i = 0; i < n; i++)
	{
		if (!strcasecmp(s, opcode[i].name))
			return i;
	}
	return -1;
}

enum parmtr_
{
	PARMTR_NULL, // 0!
	PARMTR_INTEGER = 128,
	PARMTR_POINTER = 64,
	PARMTR_VAL = 128,
	PARMTR_PTR = 64,
	PARMTR_P_BC = 3,
	PARMTR_P_C,
	PARMTR_P_DE,
	PARMTR_P_HL,
	PARMTR_P_IX,
	PARMTR_P_IY,
	PARMTR_P_SP,
	PARMTR_P_PC,
	PARMTR_P_HL_IX,
	PARMTR_P_HL_IY,
	PARMTR_P_IX_IY,
	PARMTR_DEHL,
	PARMTR_BCDE,
	PARMTR_A,
	PARMTR_AF,
	PARMTR_AF2,
	PARMTR_B,
	PARMTR_BC,
	PARMTR_C,
	PARMTR_D,
	PARMTR_DE,
	PARMTR_E,
	PARMTR_H,
	PARMTR_HL,
	PARMTR_I,
	PARMTR_IX,
	PARMTR_IY,
	PARMTR_L,
	PARMTR_NC,
	PARMTR_NS,
	PARMTR_NV,
	PARMTR_NZ,
	PARMTR_R,
	PARMTR_S,
	PARMTR_SP,
	PARMTR_USP,
	PARMTR_V,
	PARMTR_XH,
	PARMTR_XL,
	PARMTR_YH,
	PARMTR_YL,
	PARMTR_Z,
};
t_parmtr parmtr[] = {
	// MUST BE IN ALPHABETICAL ORDER!
	{"(bc)", PARMTR_P_BC},
	{"(c)", PARMTR_P_C},
	{"(de)", PARMTR_P_DE},
	{"(hl)", PARMTR_P_HL},
	{"(ix)", PARMTR_P_IX},
	{"(iy)", PARMTR_P_IY},
	{"(sp)", PARMTR_P_SP},
	{"[bc]", PARMTR_P_BC},
	{"[c]", PARMTR_P_C},
	{"[de]", PARMTR_P_DE},
	{"[hl]", PARMTR_P_HL},
	{"[ix]", PARMTR_P_IX},
	{"[iy]", PARMTR_P_IY},
	{"[sp]", PARMTR_P_SP},
	{"a", PARMTR_A},
	{"af", PARMTR_AF},
	{"af'", PARMTR_AF2},
	{"b", PARMTR_B},
	{"bc", PARMTR_BC},
	{"bcde", PARMTR_BCDE},
	{"c", PARMTR_C},
	{"d", PARMTR_D},
	{"de", PARMTR_DE},
	{"dehl", PARMTR_DEHL},
	{"e", PARMTR_E},
	{"h", PARMTR_H},
	{"hix", PARMTR_XH},
	{"hiy", PARMTR_YH},
	{"hl", PARMTR_HL},
	{"hx", PARMTR_XH},
	{"hy", PARMTR_YH},
	{"i", PARMTR_I},

	{"ix", PARMTR_IX},
	{"ixh", PARMTR_XH},
	{"ixl", PARMTR_XL},
	{"iy", PARMTR_IY},
	{"iyh", PARMTR_YH},
	{"iyl", PARMTR_YL},
	{"l", PARMTR_L},
	{"lix", PARMTR_XL},
	{"liy", PARMTR_YL},
	{"lx", PARMTR_XL},
	{"ly", PARMTR_YL},
	{"m", PARMTR_S},
	{"nc", PARMTR_NC},
	{"ns", PARMTR_NS},
	{"nv", PARMTR_NV},
	{"nz", PARMTR_NZ},
	{"p", PARMTR_NS},
	{"pe", PARMTR_V},
	{"po", PARMTR_NV},
	{"psw", PARMTR_AF},
	{"r", PARMTR_R},
	{"s", PARMTR_S},
	{"sp", PARMTR_SP},
	{"usp", PARMTR_USP},
	{"v", PARMTR_V},
	{"xh", PARMTR_XH},
	{"xl", PARMTR_XL},
	{"yh", PARMTR_YH},
	{"yl", PARMTR_YL},
	{"z", PARMTR_Z},
};
int get_parmtr(char *s) GET_SEARCH(parmtr)

int eval_parmtr_seg;
int eval_parmtr(char *s, int *e)
{
	*e = eval_status = eval_parmtr_seg = 0;
	eval_start_rpn();
	if (!s) return PARMTR_NULL;
	while (*s && (unsigned char)*s <= 32) s++; // skip leading whitespace
	if (!*s) {
		eval_cursor = s;
		return PARMTR_NULL;
	}
	int i;
	if ((i = get_parmtr(s)) >= 0)
		return parmtr[i].type; // parmtr

	char q = 0, *t = s, z;
	while (*t)
		++t;

	if ((z = *--t) == ')' && *s == '(') // catch special case (EXPRESSION) OP (EXPRESSION)
	{
		char *r = s;
		q = 1;
		while (q && ++r < t)
			switch (*r)
			{
			case '(':
				++q;
				break;
			case ')':
				--q;
				break;
			}
		q = (q == 1); // only q==1 ensures that this is a pointer
	}
	if (q |= (z == ']' && *s == '[')) // POINTER, PARMTR_P_IX or PARMTR_P_IY?
	{
		char x, o;
		// Z280 Double indexing: (HL+IX), (HL+IY), (IX+IY)
		if (s[1] && s[2] && s[3] && s[4] && s[5] && s[6] == ')') {
			char r1a = s[1] | 32, r1b = s[2] | 32;
			char op = s[3];
			char r2a = s[4] | 32, r2b = s[5] | 32;
			if (op == '+') {
				if (r1a == 'h' && r1b == 'l') {
					if (r2a == 'i' && r2b == 'x') { *t = z; eval_cursor = &s[7]; return PARMTR_P_HL_IX; }
					if (r2a == 'i' && r2b == 'y') { *t = z; eval_cursor = &s[7]; return PARMTR_P_HL_IY; }
				} else if (r1a == 'i' && r1b == 'x' && r2a == 'i' && r2b == 'y') {
					*t = z; eval_cursor = &s[7]; return PARMTR_P_IX_IY;
				}
			}
		}

		if ((s[1] | 32) == 'i' && ((x = s[2] | 32) == 'x' || x == 'y') && ((o = s[3]) == '+' || o == '-'))
		{
			char y = *t;
			*t = 0;
			*e = flag_dollar ? (eval(&s[4]) * (o == '-' ? -1 : 1)) : eval(&s[3]);
			eval_parmtr_seg = eval_res_seg;
			*t = y;
			return eval_status < 0 ? -1 : ((x & 1) ? PARMTR_P_IY : PARMTR_P_IX);
		}
		if ((s[1] | 32) == 'h' && (s[2] | 32) == 'l' && ((o = s[3]) == '+' || o == '-'))
		{
			char y = *t;
			*t = 0;
			*e = flag_dollar ? (eval(&s[4]) * (o == '-' ? -1 : 1)) : eval(&s[3]);
			eval_parmtr_seg = eval_res_seg;
			*t = y;
			return eval_status < 0 ? -1 : 512 + 2;
		}
		if ((s[1] | 32) == 's' && (s[2] | 32) == 'p' && ((o = s[3]) == '+' || o == '-'))
		{
			char y = *t;
			*t = 0;
			*e = flag_dollar ? (eval(&s[4]) * (o == '-' ? -1 : 1)) : eval(&s[3]);
			eval_parmtr_seg = eval_res_seg;
			*t = y;
			return eval_status < 0 ? -1 : PARMTR_P_SP;
		}
		if ((s[1] | 32) == 'p' && (s[2] | 32) == 'c' && ((o = s[3]) == '+' || o == '-'))
		{
			char y = *t;
			*t = 0;
			*e = flag_dollar ? (eval(&s[4]) * (o == '-' ? -1 : 1)) : eval(&s[3]);
			eval_parmtr_seg = eval_res_seg;
			*t = y;
			return eval_status < 0 ? -1 : PARMTR_P_PC;
		}
		*t = 0;
		++s;
	}
	*e = eval(s);
	eval_parmtr_seg = eval_res_seg;
	if (flag_v > 0) fprintf(stderr, "eval_parmtr: expr='%s' -> val=%04X, seg=%d\n", s, *e, eval_parmtr_seg);
	*t = z;
	return eval_status < 0 ? -1 : (q ? PARMTR_POINTER : PARMTR_INTEGER);
}

int get_parmtr_addhl(int i)
{
	switch (i)
	{
	case PARMTR_BC:
		return 0x00;
	case PARMTR_DE:
		return 0x10;
	case PARMTR_HL:
		return 0x20;
	case PARMTR_SP:
		return 0x30;
	}
	return -1;
}
int get_parmtr_bit(int i)
{
	switch (i)
	{
	case PARMTR_B:
		return 0;
	case PARMTR_C:
		return 1;
	case PARMTR_D:
		return 2;
	case PARMTR_E:
		return 3;
	case PARMTR_H:
		return 4;
	case PARMTR_L:
		return 5;
	case PARMTR_P_HL:
		return 6;
	case PARMTR_A:
		return 7;
	case PARMTR_P_IX:
		return 16 + 0 + 6;
	case PARMTR_P_IY:
		return 16 + 8 + 6;
	}
	return -1;
}
int get_parmtr_call(int i)
{
	switch (i)
	{
	case PARMTR_NZ:
		return 0xC4;
	case PARMTR_Z:
		return 0xCC;
	case PARMTR_NC:
		return 0xD4;
	case PARMTR_C:
		return 0xDC;
	case PARMTR_NV:
		return 0xE4;
	case PARMTR_V:
		return 0xEC;
	case PARMTR_NS:
		return 0xF4;
	case PARMTR_S:
		return 0xFC;
	case PARMTR_INTEGER:
		return 0xCD;
	}
	return -1;
}
int get_parmtr_in(int i)
{
	switch (i)
	{
	case PARMTR_B:
		return 0;
	case PARMTR_C:
		return 1;
	case PARMTR_D:
		return 2;
	case PARMTR_E:
		return 3;
	case PARMTR_H:
		return 4;
	case PARMTR_L:
		return 5;
	case PARMTR_NULL:
		return 6;
	case PARMTR_A:
		return 7;
	}
	return -1;
}
int get_parmtr_inc(int i)
{
	switch (i)
	{
	case PARMTR_B:
		return 0;
	case PARMTR_C:
		return 1;
	case PARMTR_D:
		return 2;
	case PARMTR_E:
		return 3;
	case PARMTR_H:
		return 4;
	case PARMTR_L:
		return 5;
	case PARMTR_P_HL:
		return 6;
	case PARMTR_A:
		return 7;
	case PARMTR_P_PC:
		return 512 + 1;
	case PARMTR_POINTER:
	case PARMTR_INTEGER:
		return PARMTR_PTR;
	case PARMTR_XH:
		return 16 + 0 + 4;
	case PARMTR_XL:
		return 16 + 0 + 5;
	case PARMTR_P_IX:
		return 16 + 0 + 6;
	case PARMTR_YH:
		return 16 + 8 + 4;
	case PARMTR_YL:
		return 16 + 8 + 5;
	case PARMTR_P_IY:
		return 16 + 8 + 6;
	case PARMTR_BC:
		return 32 + 00 + 0 + 0;
	case PARMTR_DE:
		return 32 + 00 + 0 + 1;
	case PARMTR_HL:
		return 32 + 00 + 0 + 2;
	case PARMTR_SP:
		return 32 + 00 + 0 + 3;
	case PARMTR_IX:
		return 32 + 16 + 0 + 2;
	case PARMTR_IY:
		return 32 + 16 + 8 + 2;
	}
	return -1;
}
int get_parmtr_jp(int i)
{
	switch (i)
	{
	case PARMTR_NZ:
		return 0xC2;
	case PARMTR_Z:
		return 0xCA;
	case PARMTR_NC:
		return 0xD2;
	case PARMTR_C:
		return 0xDA;
	case PARMTR_NV:
		return 0xE2;
	case PARMTR_V:
		return 0xEA;
	case PARMTR_NS:
		return 0xF2;
	case PARMTR_S:
		return 0xFA;
	case PARMTR_HL:
	case PARMTR_P_HL:
		return 0xE9;
	case PARMTR_IX:
	case PARMTR_P_IX:
		return 0xDDE9;
	case PARMTR_IY:
	case PARMTR_P_IY:
		return 0xFDE9;
	case PARMTR_INTEGER:
		return 0xC3;
	}
	return -1;
}
int get_parmtr_jr(int i)
{
	switch (i)
	{
	case PARMTR_NZ:
		return 0x20;
	case PARMTR_Z:
		return 0x28;
	case PARMTR_NC:
		return 0x30;
	case PARMTR_C:
		return 0x38;
	case PARMTR_INTEGER:
		return 0x18;
	}
	return -1;
}
int get_parmtr_ld(int i)
{
	switch (i)
	{
	case PARMTR_INTEGER:
		return PARMTR_VAL;
	case PARMTR_POINTER:
		return PARMTR_PTR;
	case PARMTR_P_SP:
		return 512 + 0;
	case PARMTR_P_PC:
		return 512 + 1;
	case PARMTR_B:
		return 0;
	case PARMTR_C:
		return 1;
	case PARMTR_D:
		return 2;
	case PARMTR_E:
		return 3;
	case PARMTR_H:
		return 4;
	case PARMTR_L:
		return 5;
	case PARMTR_P_HL:
		return 6;
	case PARMTR_A:
		return 7;
	case PARMTR_XH:
		return 16 + 0 + 4;
	case PARMTR_XL:
		return 16 + 0 + 5;
	case PARMTR_P_IX:
		return 16 + 0 + 6;
	case PARMTR_YH:
		return 16 + 8 + 4;
	case PARMTR_YL:
		return 16 + 8 + 5;
	case PARMTR_P_IY:
		return 16 + 8 + 6;
	case PARMTR_BC:
		return 32 + 00 + 0 + 0;
	case PARMTR_DE:
		return 32 + 00 + 0 + 1;
	case PARMTR_HL:
		return 32 + 00 + 0 + 2;
	case PARMTR_SP:
		return 32 + 00 + 0 + 3;
	case PARMTR_IX:
		return 32 + 16 + 0 + 2;
	case PARMTR_IY:
		return 32 + 16 + 8 + 2;
	case PARMTR_P_BC:
		return 256 + 0;
	case PARMTR_P_DE:
		return 256 + 1;
	case PARMTR_I:
		return 256 + 2;
	case PARMTR_R:
		return 256 + 3;
	}
	return -1;
}
int get_parmtr_pop(int i)
{
	switch (i)
	{
	case PARMTR_BC:
	case PARMTR_B:
		return 0x00;
	case PARMTR_DE:
	case PARMTR_D:
		return 0x10;
	case PARMTR_HL:
	case PARMTR_H:
		return 0x20;
	case PARMTR_AF:
		return 0x30;
	case PARMTR_IX:
		return 0xDD20;
	case PARMTR_IY:
		return 0xFD20;
	}
	return -1;
}
int get_parmtr_ret(int i)
{
	switch (i)
	{
	case PARMTR_NZ:
		return 0xC0;
	case PARMTR_Z:
		return 0xC8;
	case PARMTR_NC:
		return 0xD0;
	case PARMTR_C:
		return 0xD8;
	case PARMTR_NV:
		return 0xE0;
	case PARMTR_V:
		return 0xE8;
	case PARMTR_NS:
		return 0xF0;
	case PARMTR_S:
		return 0xF8;
	case PARMTR_NULL:
		return 0xC9;
	}
	return -1;
}
int get_parmtr_rlc(int i)
{
	switch (i)
	{
	case PARMTR_B:
		return 0;
	case PARMTR_C:
		return 1;
	case PARMTR_D:
		return 2;
	case PARMTR_E:
		return 3;
	case PARMTR_H:
		return 4;
	case PARMTR_L:
		return 5;
	case PARMTR_P_HL:
		return 6;
	case PARMTR_A:
		return 7;
	case PARMTR_P_IX:
		return 16 + 0 + 6;
	case PARMTR_P_IY:
		return 16 + 8 + 6;
		// SJASM shortcuts
	case PARMTR_BC:
		return 8;
	case PARMTR_DE:
		return 10;
	case PARMTR_HL:
		return 12;
	}
	return -1;
}
int get_parmtr_sub(int i)
{
	switch (i)
	{
	case PARMTR_B:
		return 0;
	case PARMTR_C:
		return 1;
	case PARMTR_D:
		return 2;
	case PARMTR_E:
		return 3;
	case PARMTR_H:
		return 4;
	case PARMTR_L:
		return 5;
	case PARMTR_P_HL:
		return 6;
	case PARMTR_A:
		return 7;
	case PARMTR_XH:
		return 16 + 0 + 4;
	case PARMTR_XL:
		return 16 + 0 + 5;
	case PARMTR_P_IX:
		return 16 + 0 + 6;
	case PARMTR_YH:
		return 16 + 8 + 4;
	case PARMTR_YL:
		return 16 + 8 + 5;
	case PARMTR_P_IY:
		return 16 + 8 + 6;
	case PARMTR_INTEGER:
		return PARMTR_VAL;
	}
	return -1;
}

#define CHECK_OVERFLOW(x, a, z, e)                         \
	do                                                     \
	{                                                      \
		if (pass == 2 && !eval_status && (x < a || x > z)) \
			FATAL_ERROR(e);                                \
	} while (0)
#define CHECK_BAD_CHAR(x) CHECK_OVERFLOW(x, -128, +127, error_char_overflow)
void check_bad_byte(int x)
{
	if (pass != 2 || eval_status) return;
	// Accept 0 to 255, and also -128 to -1 (expressed as 16-bit negative)
	if ((x >= -128 && x <= 255) || (x >= 0xFF80 && x <= 0xFFFF)) {
		return;
	}
	fprintf(stderr, "DEBUG check_bad_byte FAILED: x=%d(0x%X), status=%d\n", x, x, eval_status);
	printerror(error_byte_overflow);
}
#define CHECK_BAD_BYTE(x) check_bad_byte(x)
void check_bad_word(int x)
{
	if (pass == 2 && !eval_status && (x < -32768 || x > 65535)) {
		fprintf(stderr, "DEBUG check_bad_word FAILED: x=%d(0x%X), status=%d\n", x, x, eval_status);
		printerror(error_word_overflow);
	}
}
#define CHECK_BAD_WORD(x) check_bad_word(x)

int assemble_filler(int i, int j) // 0 OK, !0 ERROR
{
	if (i < 0) {
		FATAL_ERROR(error_improper_argument);
	}
	if ((target + i) > SIZEOF_OUTPUT)
		FATAL_ERROR(error_out_of_memory);
	while (i--)
		NEXTBYTE(j);
	return 0;
}

#define NEXTWORD_REL(val, seg)                  \
	do                                          \
	{                                           \
		if ((seg) == SEG_COMPLEX)               \
		{                                       \
			write_rel_rpn(0);                   \
			NEXTBYTE_OUTPUT((val)&0xFF);        \
			NEXTBYTE_OUTPUT(((val) >> 8) & 0xFF); \
		}                                       \
		else if ((seg) != SEG_ASEG)             \
		{                                       \
			write_rel_addr((unsigned int)(val), seg); \
			NEXTBYTE_OUTPUT((val)&0xFF);        \
			NEXTBYTE_OUTPUT(((val) >> 8) & 0xFF); \
		}                                       \
		else                                    \
		{                                       \
			NEXTBYTE((val)&0xFF);               \
			NEXTBYTE(((val) >> 8) & 0xFF);      \
		}                                       \
	} while (0)

#define NEXTBYTE_REL(val, seg)                  \
	do                                          \
	{                                           \
		if ((seg) == SEG_COMPLEX || (seg) != SEG_ASEG) \
		{                                       \
			write_rel_rpn(1);                   \
			NEXTBYTE_OUTPUT((val)&0xFF);        \
		}                                       \
		else                                    \
			NEXTBYTE(val);                      \
	} while (0)

#define FETCH_PARMTR(a, aa, a_seg)           \
	do                                       \
	{                                        \
		t = s;                               \
		q = 0;                               \
		while ((c = *t) && (c != ',' || q))  \
		{                                    \
			if (c == '"' || c == '\'')       \
				q = !q;                      \
			++t;                             \
		};                                   \
		*t = 0;                              \
		if ((a = eval_parmtr(s, &aa)) < 0)   \
			FATAL_ERROR(error_syntax_error); \
		a_seg = eval_parmtr_seg;             \
		if ((*t = c))                        \
			++t;                             \
		s = t;                               \
	} while (0)
#define FATAL_PARMTR FATAL_ERROR(error_improper_argument)
#define GET_PARMTR_F(a, f) \
	a = f(a)
#define INDEX_PREFIX(a) \
	if ((a) & 16)       \
	NEXTBYTE(((a) & 8) * 4 + 0xDD)

// 8080 register encoding: B=0,C=1,D=2,E=3,H=4,L=5,M/(HL)=6,A=7
static int get_8080_reg(int p)
{
	switch (p) {
	case PARMTR_B:    return 0;
	case PARMTR_C:    return 1;
	case PARMTR_D:    return 2;
	case PARMTR_E:    return 3;
	case PARMTR_H:    return 4;
	case PARMTR_L:    return 5;
	case PARMTR_S:    return 6; // 'm' maps to PARMTR_S; means M=(HL) in 8080
	case PARMTR_P_HL: return 6; // also accept (HL)
	case PARMTR_A:    return 7;
	default:          return -1;
	}
}

// 8080 register pair encoding: B/BC=0, D/DE=1, H/HL=2, SP=3
static int get_8080_rp(int p)
{
	switch (p) {
	case PARMTR_B:  case PARMTR_BC: return 0;
	case PARMTR_D:  case PARMTR_DE: return 1;
	case PARMTR_H:  case PARMTR_HL: return 2;
	case PARMTR_SP:                 return 3;
	default:                        return -1;
	}
}

int assemble_pseudo_db(int o)
{
	if (!*(eval_cursor = (char *)split_parmtr))
		FATAL_ERROR(error_syntax_error);
	int global_eval_status = 0;
	int i;
	do
	{
		while (*eval_cursor && (unsigned char)*eval_cursor <= 32) eval_cursor++;
		int is_arith_string = 0;
		if (*eval_cursor == '"' || *eval_cursor == '\'') {
			char *p_arith = eval_cursor + 1;
			char a_delim = *eval_cursor;
			while (*p_arith) {
				if (*p_arith == a_delim) {
					if (p_arith[1] == a_delim) {
						p_arith += 2;
						continue;
					} else {
						break;
					}
				}
				p_arith++;
			}
			if (*p_arith == a_delim) {
				p_arith++;
				while (*p_arith && (unsigned char)*p_arith <= 32) p_arith++;
				if (*p_arith != ',' && *p_arith != 0) {
					is_arith_string = 1; // It's "char"+expr, let eval handle it
				}
			}
		}

		if (!is_arith_string && (*eval_cursor == '"' || *eval_cursor == '\'') && (o == PSEUDO_DEFB || o == PSEUDO_DEFC || o == PSEUDO_DEFZ))
		{
			unsigned char delim = *eval_cursor;
			unsigned char *str_buf = NULL;
			int str_len = 0;
			int str_cap = 0;
			eval_cursor++;
			while (eval_status >= 0 && (i = (unsigned char)*eval_cursor))
			{
				if (i == delim) {
					if (eval_cursor[1] == delim) {
						// Double delimiter: "" -> literal "
						append_encoded_cp(&str_buf, &str_len, &str_cap, delim);
						eval_cursor += 2;
						continue;
					} else {
						// End of string
						break;
					}
				}
				if (flag_string_escapes && i == '\\') {
					append_encoded_cp(&str_buf, &str_len, &str_cap, eval_escape(i));
					eval_cursor++;
				} else {
					const char *p_utf8 = eval_cursor;
					append_encoded_cp(&str_buf, &str_len, &str_cap, decode_utf8(&p_utf8));
					eval_cursor = (char *)p_utf8;
				}
			}
			if (eval_status < 0 || *eval_cursor != delim) {
				free(str_buf);
				FATAL_ERROR(error_invalid_string);
			}
			if (o == PSEUDO_DEFC) {
				if (str_len == 0) {
					free(str_buf);
					FATAL_ERROR(error_invalid_string);
				}
				str_buf[str_len - 1] |= 0x80;
			}
			for (int j = 0; j < str_len; j++)
				NEXTBYTE(str_buf[j]);
			if (o == PSEUDO_DEFZ)
				NEXTBYTE(0); // a DEFZ string is NULL-terminated
			free(str_buf);
			eval_cursor++; // skip final delim
		}
		else
		{
			eval_start_rpn();
			i = eval(eval_cursor);
			if (eval_status < 0)
				FATAL_ERROR(error_invalid_expression);
			if (eval_status > 0)
				global_eval_status = eval_status;

			if (eval_res_seg == SEG_COMPLEX)
			{
				write_rel_rpn(o != PSEUDO_DEFW);
				if (o == PSEUDO_DEFW)
				{
					NEXTBYTE_OUTPUT(i & 0xFF);
					NEXTBYTE_OUTPUT((i >> 8) & 0xFF);
				}
				else
				{
					NEXTBYTE_OUTPUT(i & 0xFF);
				}
			}
			else if (o == PSEUDO_DEFW)
			{
				NEXTWORD_REL(i, eval_res_seg);
				if (eval_res_seg == SEG_ASEG) CHECK_BAD_WORD(i);
			}
			else if (eval_res_seg != SEG_ASEG)
			{
				write_rel_rpn(1);
				NEXTBYTE_OUTPUT(i & 0xFF);
			}
			else
			{
				NEXTBYTE(i & 0xFF);
				if (o != PSEUDO_DEFB && o != PSEUDO_DEFC && o != PSEUDO_DEFZ)
					CHECK_BAD_BYTE(i);
			}
		}
		while (*eval_cursor && (unsigned char)*eval_cursor <= 32)
			eval_cursor++;
	} while (*eval_cursor++ == ',');
	eval_status = global_eval_status;
	return 0;
}

int assemble_opcode(int o, int oo) // 0 OK, !0 ERROR
{
	int x, xx, y, yy, z;
	int xx_seg, yy_seg;
	char *s = (char *)split_parmtr, *t, c, q;
	FETCH_PARMTR(x, xx, xx_seg);
	z = eval_status; // 1st doubt
	switch (o)
	{
	case OPCODE_ADC:
		if (x == PARMTR_HL)
		{
			FETCH_PARMTR(y, yy, yy_seg);
			GET_PARMTR_F(y, get_parmtr_addhl);
			NEXTBYTE(0xED);
			NEXTBYTE(y - oo * 8 + 0x4A);
		}
		else
		{
			if (x == PARMTR_A && *s)
				FETCH_PARMTR(x, xx, xx_seg);
			GET_PARMTR_F(x, get_parmtr_sub);
			if (x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY)
			{
				NEXTBYTE(0xDD);
				int offset = (x == PARMTR_P_HL_IX) ? 0 : (x == PARMTR_P_HL_IY ? 1 : 2);
				NEXTBYTE(0x89 + offset);
			}
			else if (x == 512 + 0 || x == 512 + 1 || ((x & 16) && (current_cpu == CPU_Z280 && (xx_seg != SEG_ASEG || xx < -128 || xx > 127))))
			{
				if (x == 512 + 0) NEXTBYTE(0xDD); else NEXTBYTE(0xFD);
				if (x == 512 + 0 || x == 512 + 1) {
					NEXTBYTE(0x88); NEXTWORD_REL(xx, xx_seg);
				} else {
					NEXTBYTE(0x88 + (x & 8 ? 2 : 1)); NEXTWORD_REL(xx, xx_seg);
				}
				if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
			}
			else if (x < 32)
			{
				INDEX_PREFIX(x);
				NEXTBYTE((x & 7) + oo * 16 + 0x88);
				if ((x & 23) == 22)
				{
					NEXTBYTE(xx);
					if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
				}
			}
			else if (x == PARMTR_VAL)
			{
				NEXTBYTE(oo * 16 + 0xCE);
				NEXTBYTE_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_BYTE(xx);
			}
		}
		break;
	case OPCODE_ADD:
		if (x == PARMTR_HL || x == PARMTR_IX || x == PARMTR_IY)
		{
			FETCH_PARMTR(y, yy, yy_seg);
			if (x != PARMTR_HL)
			{
				NEXTBYTE(x == PARMTR_IX ? 0xDD : 0xFD);
				y = y == PARMTR_HL ? -1 : (y == x ? PARMTR_HL : y); // *"ADD IX/IY,HL", "ADD IX/IY,IY/IX"...
			}
			GET_PARMTR_F(y, get_parmtr_addhl);
			NEXTBYTE(y + 0x09);
		}
		else
		{
			if (x == PARMTR_A && *s)
				FETCH_PARMTR(x, xx, xx_seg);
			GET_PARMTR_F(x, get_parmtr_sub);
			if (x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY)
			{
				NEXTBYTE(0xDD);
				int offset = (x == PARMTR_P_HL_IX) ? 0 : (x == PARMTR_P_HL_IY ? 1 : 2);
				NEXTBYTE(0x81 + offset);
			}
			else if (x == 512 + 0 || x == 512 + 1 || ((x & 16) && (current_cpu == CPU_Z280 && (xx_seg != SEG_ASEG || xx < -128 || xx > 127))))
			{
				if (x == 512 + 0) NEXTBYTE(0xDD); else NEXTBYTE(0xFD);
				if (x == 512 + 0 || x == 512 + 1) {
					NEXTBYTE(0x80); NEXTWORD_REL(xx, xx_seg);
				} else {
					NEXTBYTE(0x80 + (x & 8 ? 2 : 1)); NEXTWORD_REL(xx, xx_seg);
				}
				if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
			}
			else if (x < 32)
			{
				INDEX_PREFIX(x);
				NEXTBYTE((x & 7) + 0x80);
				if ((x & 23) == 22)
				{
					NEXTBYTE(xx);
					if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
				}
			}
			else if (x == PARMTR_VAL)
			{
				NEXTBYTE(0xC6);
				NEXTBYTE_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_BYTE(xx);
			}
		}
		break;
	case OPCODE_BIT:
		if (x != PARMTR_INTEGER || xx < 0 || xx > 7)
			FATAL_PARMTR;
		FETCH_PARMTR(y, yy, yy_seg);
		GET_PARMTR_F(y, get_parmtr_bit);
		if (y < 16)
		{
			NEXTBYTE(0xCB);
			NEXTBYTE((y & 7) + oo + xx * 8);
		}
		else
		{
			NEXTBYTE((y & 8) * 4 + 0xDD);
			NEXTBYTE(0xCB);
			NEXTBYTE(yy);
			NEXTBYTE((y & 7) + oo + xx * 8);
			CHECK_BAD_CHAR(yy);
		}
		break;
	case OPCODE_CALL:
		GET_PARMTR_F(x, get_parmtr_call);
		if ((x & 0xFF) != 0xCD)
		{
			// Conditional CALL: first param was the condition, address is next
			FETCH_PARMTR(y, xx, xx_seg);
			if (y != PARMTR_INTEGER && y != PARMTR_NULL)
				FATAL_PARMTR;
		}
		// Unconditional CALL: xx already holds the address from initial FETCH_PARMTR
		NEXTBYTE(x);
		NEXTWORD_REL(xx, xx_seg);
		if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		break;
	case OPCODE_DJNZ:
		if (x != PARMTR_INTEGER)
			FATAL_PARMTR;
		NEXTBYTE(0x10);
		xx = xx - (phase_active ? phase_target : target) - 1;
		NEXTBYTE(xx);
		CHECK_BAD_CHAR(xx);
		break;
	case OPCODE_JAF:
		// Z280: JAF/JAR label  →  DD 28/20 n  (3-byte relative jump)
		// n = dest - (PC + 3); after emitting DD+oo, target = PC+2, so n = dest - target - 1
		if (x != PARMTR_INTEGER) FATAL_PARMTR;
		NEXTBYTE(0xDD);
		NEXTBYTE(oo); // 0x28 for JAF, 0x20 for JAR
		xx = xx - (phase_active ? phase_target : target) - 1;
		NEXTBYTE(xx);
		CHECK_BAD_CHAR(xx);
		break;
	case OPCODE_EX:
		FETCH_PARMTR(y, yy, yy_seg);
		if (x == PARMTR_P_SP)
			x = y, y = PARMTR_P_SP;
		if (x == PARMTR_AF && (y == PARMTR_AF2 || y == PARMTR_AF))
			NEXTBYTE(0x08);
		else if ((x == PARMTR_HL && y == PARMTR_DE) || (y == PARMTR_HL && x == PARMTR_DE))
			NEXTBYTE(0xEB);
		else if (y == PARMTR_P_SP)
		{
			if (x == PARMTR_IX)
				NEXTBYTE(0xDD);
			else if (x == PARMTR_IY)
				NEXTBYTE(0xFD);
			else if (x != PARMTR_HL)
				FATAL_PARMTR;
			NEXTBYTE(0xE3);
		}
		else
			FATAL_PARMTR;
		break;
	case OPCODE_IM:
		if (x != PARMTR_INTEGER || xx < 0 || xx > 2)
			FATAL_PARMTR;
		if (xx)
			++xx;
		NEXTBYTE(0xED);
		NEXTBYTE(xx * 8 + 0x46);
		break;
	case OPCODE_IN:
		FETCH_PARMTR(y, yy, yy_seg);
		if (x == PARMTR_P_C && !y)
			y = x, x = 0;
		if (x == PARMTR_A && y == PARMTR_PTR)
		{
			NEXTBYTE(0xDB);
			NEXTBYTE(yy);
			CHECK_BAD_BYTE(yy);
		}
		else if (y == PARMTR_P_C) // PARMTR_P_BC?
		{
			GET_PARMTR_F(x, get_parmtr_in);
			NEXTBYTE(0xED);
			NEXTBYTE(x * 8 + 0x40);
		}
		else
			FATAL_PARMTR;
		break;
	case OPCODE_INC:
		GET_PARMTR_F(x, get_parmtr_inc);
		if (oo == 10 || oo == 11) { // INCW or DECW
			int op_base = (oo == 10) ? 0x03 : 0x0B;
			if (x & 32) { // 16-bit registers: BC, DE, HL, SP, IX, IY
				INDEX_PREFIX(x);
				NEXTBYTE((x & 3) * 16 + op_base);
			} else if (x == PARMTR_P_HL) { // (HL)
				NEXTBYTE(0xDD); NEXTBYTE(op_base);
			} else if (x == PARMTR_PTR) { // (nn)
				NEXTBYTE(0xDD); NEXTBYTE(0x10 + op_base);
				NEXTWORD_REL(xx, xx_seg);
			} else if (x == 512 + 1) { // (PC+dd)
				NEXTBYTE(0xDD); NEXTBYTE(0x30 + op_base);
				NEXTWORD_REL(xx, xx_seg);
			} else if (x == 16 + 0 + 6) { // (IX+dd)
				NEXTBYTE(0xFD); NEXTBYTE(op_base);
				NEXTWORD_REL(xx, xx_seg);
			} else if (x == 16 + 8 + 6) { // (IY+dd)
				NEXTBYTE(0xFD); NEXTBYTE(0x10 + op_base);
				NEXTWORD_REL(xx, xx_seg);
			} else {
				FATAL_PARMTR;
			}
		} else {
			// Standard INC (8 or 16 bit)
			INDEX_PREFIX(x);
			NEXTBYTE((x & 32) ? (x & 3) * 16 + oo * 8 + 0x03 : (x & 7) * 8 + oo + 0x04);
			if ((x & 23) == 22)
			{
				NEXTBYTE(xx);
				if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
			}
		}
		break;

	case OPCODE_JP:
		{
			int jp_addr_fetched = 0;
			GET_PARMTR_F(x, get_parmtr_jp);
			// In 8080 mode, JP nn = "Jump if Positive" (0xF2); addr already in xx
			if (current_cpu == CPU_8080 && x == 0xC3) {
				x = 0xF2;
				jp_addr_fetched = 1;
			}
			if ((x & 0xFF) == 0xE9)
			{
				if (x & 0xFF00)
					NEXTBYTE(x >> 8);
				NEXTBYTE(x);
				if (xx) // JP (IX+!0)
					FATAL_PARMTR;
			}
			else
			{
				if (!jp_addr_fetched && x != 0xC3)
				{
					FETCH_PARMTR(y, xx, xx_seg);
					if (y != PARMTR_INTEGER && y != PARMTR_NULL)
						FATAL_PARMTR;
				}
				NEXTBYTE(x);
				NEXTWORD_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
			}
		}
		break;
	case OPCODE_JR:
		GET_PARMTR_F(x, get_parmtr_jr);
		if (x != 0x18)
		{
			FETCH_PARMTR(y, xx, xx_seg);
			if (y != PARMTR_INTEGER && y != PARMTR_NULL)
				FATAL_PARMTR;
		}
		NEXTBYTE(x);
		{
			int pc = (phase_active ? phase_target : target);
			int offset = (int)((short)((xx & 0xFFFF) - ((pc + 1) & 0xFFFF)));
			if (strstr((char *)source, "jr") && strstr((char *)source, "wr_secondary")) {
				fprintf(stderr, "DEBUG JR: dest=0x%04X, pc=0x%04X, offset=%d, pass=%d\n", xx & 0xFFFF, pc & 0xFFFF, offset, pass);
			}
			xx = offset;
		}
		NEXTBYTE(xx);
		// if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
		break;
	case OPCODE_LD:
		GET_PARMTR_F(x, get_parmtr_ld);
		if (x == PARMTR_VAL)
			FATAL_PARMTR;
		FETCH_PARMTR(y, yy, yy_seg);
		GET_PARMTR_F(y, get_parmtr_ld);
		if (x & 256) // LD (BC)/(DE)/I/R,A
		{
			if (y != 0x07)
				FATAL_PARMTR;
			if (x & 2)
			{
				NEXTBYTE(0xED);
				NEXTBYTE((x & 1) * 8 + 0x47);
			}
			else
				NEXTBYTE(0x02 + (x & 1) * 16);
		}
		else if (y & 256) // LD A,(BC)/(DE)/I/R
		{
			if (x != 0x07)
				FATAL_PARMTR;
			if (y & 2)
			{
				NEXTBYTE(0xED);
				NEXTBYTE((y & 1) * 8 + 0x57);
			}
			else
				NEXTBYTE(0x0A + (y & 1) * 16);
		}
		else if (x & 32) // LD REG16,...
		{
			INDEX_PREFIX(x | y);
			if (x == 35 && (y & 35) == 34) // LD SP,HL/IX/IY
				NEXTBYTE(0xF9);
			else if (y == PARMTR_VAL) // ...NN
			{
				NEXTBYTE((x & 3) * 16 + 0x01);
				NEXTWORD_REL(yy, yy_seg);
				CHECK_BAD_WORD(yy);
			}
			else if (y == PARMTR_PTR) // ...(NN)
			{
				if ((x & 3) == 2)
					NEXTBYTE(0x2A);
				else
				{
					NEXTBYTE(0xED);
					NEXTBYTE((x & 3) * 16 + 0x4B);
				}
				NEXTWORD_REL(yy, yy_seg);
				CHECK_BAD_WORD(yy);
			}
			else if (x < 35 && (y & 23) == 22) // SJASM shortcut: LD BC,(IX+0) = LD C,(IX+0) + LD B,(IX+1)
			{
				NEXTBYTE((x & 3) * 16 + 0X4E);
				NEXTBYTE(yy);
				INDEX_PREFIX(y);
				NEXTBYTE((x & 3) * 16 + 0X46);
				NEXTBYTE(++yy);
				CHECK_BAD_CHAR(yy);
			}
			else if (y & 32 && ((x < 35 || x & 16) && (y < 35 || y & 16))) // shortcut: LD REG16,REG16 = LD REG8H,REG8H + LD REG8L,REG8L
			{
				if (((x & y & 3) == 2) && ((x ^ y) & (16 | 8))) // *"LD HL,IX", "LD IX,IY"...
					FATAL_PARMTR;
				NEXTBYTE(c = (0x40 + (x & 3) * 16 + (y & 3) * 2));
				INDEX_PREFIX(x | y);
				NEXTBYTE(c + 9);
			}
			else
				FATAL_PARMTR;
		}
		else if (y & 32) // LD ...,REG16
		{
			INDEX_PREFIX(x | y);
			if (x == PARMTR_PTR) // (NN)...
			{
				if ((y & 3) == 2)
					NEXTBYTE(0x22);
				else
				{
					NEXTBYTE(0xED);
					NEXTBYTE((y & 3) * 16 + 0x43);
				}
				NEXTWORD_REL(xx, xx_seg);
				CHECK_BAD_WORD(xx);
			}
			else if (y < 35 && (x & 23) == 22) // SJASM shortcut: LD (IX+0),BC = LD (IX+0),C + LD (IX+1),B
			{
				NEXTBYTE((y & 3) * 2 + 0X71);
				NEXTBYTE(xx);
				INDEX_PREFIX(x);
				NEXTBYTE((y & 3) * 2 + 0X70);
				NEXTBYTE(++xx);
				CHECK_BAD_CHAR(xx);
			}
			else
				FATAL_PARMTR;
		}
		else
		{
			if (((x & 7) == 6) && ((y & 7) == 6))
				FATAL_PARMTR; // *"LD (HL),(HL)"...
			
			// Handle indexed addressing
			int is_indexed = ((x | y) & 16);
			int long_offset_target = (current_cpu == CPU_Z280 && is_indexed && (x & 16) && (xx_seg != SEG_ASEG || xx < -128 || xx > 127));
			int long_offset_source = (current_cpu == CPU_Z280 && is_indexed && (y & 16) && (yy_seg != SEG_ASEG || yy < -128 || yy > 127));

			if (is_indexed && !long_offset_target && !long_offset_source)
			{
				NEXTBYTE(((x | y) & 8) * 4 + 0xDD);
				if (((x & y) & 16) && ((x ^ y) & 8))
					FATAL_PARMTR; // *"LD XL,YL"...
			}
			
			if (x < 32) // LD REG8,...
			{
				if (y < 32) // ...REG8
				{
					if (((x ^ y) & 16) && ((x & 6) == 4) && ((y & 6) == 4))
						FATAL_PARMTR; // *"LD XL,L"...
					
					if (long_offset_target || long_offset_source) {
						// Z280 Word Offset for REG8, (IX/IY+dd)
						if ((x & 7) == 6) { // LD (IX/IY+dd), reg8
							NEXTBYTE(0xFD);
							NEXTBYTE((x & 8 ? 0x19 : 0x09) + (y & 7)); // Mapping for LD (base+dd), reg8
							NEXTWORD_REL(xx, xx_seg);
						} else if ((y & 7) == 6) { // LD reg8, (IX/IY+dd)
							NEXTBYTE(0xFD);
							NEXTBYTE((y & 8 ? 0x7A : 0x79) + (x & 7));
							NEXTWORD_REL(yy, yy_seg);
						} else FATAL_PARMTR;
					} else {
						// Standard Z80 or short Z280
						NEXTBYTE((x & 7) * 8 + (y & 7) + 0x40);
						if ((x & 7) == 6)
						{
							if (y & 16)
								FATAL_PARMTR;
							if (x & 16)
							{
								NEXTBYTE(xx);
								if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
							}
						}
						else if ((y & 7) == 6)
						{
							if (x & 16)
								FATAL_PARMTR;
							if (y & 16)
							{
								NEXTBYTE(yy);
								if (yy_seg == SEG_ASEG) CHECK_BAD_CHAR(yy);
							}
						}
					}
				}
				else if (y == PARMTR_VAL) // ...N
				{
					NEXTBYTE((x & 7) * 8 + 0x06);
					if ((x & 23) == 22)
					{
						NEXTBYTE(xx);
						if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
					}
					NEXTBYTE_REL(yy, yy_seg);
					if (yy_seg != SEG_COMPLEX) CHECK_BAD_BYTE(yy);
				}
				else if (x == 7 && (y == PARMTR_P_HL_IX || y == PARMTR_P_HL_IY || y == PARMTR_P_IX_IY))
				{
					NEXTBYTE(0xDD);
					NEXTBYTE(0x7B + (y - PARMTR_P_HL_IX));
				}
				else if ((x & 7) != 6 && (y == PARMTR_PTR || y == 512 + 0 || y == 512 + 1)) // LD reg8,(NN) or (SP+d) or (PC+d)
				{
					if (y == 512 + 0) { // (SP+d)
						NEXTBYTE(0xDD); NEXTBYTE(0x70 + (x & 7));
						NEXTWORD_REL(yy, yy_seg);
						if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
					} else if (y == 512 + 1) { // (PC+d)
						NEXTBYTE(0xFD); NEXTBYTE(0x70 + (x & 7));
						NEXTWORD_REL(yy, yy_seg);
						if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
					} else { // (NN)
						if (x == 7) { // LD A,(NN)
							NEXTBYTE(0x3A);
							NEXTWORD_REL(yy, yy_seg);
							if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
						} else if (current_cpu == CPU_Z280) {
							NEXTBYTE(0xDD); NEXTBYTE(0x87 + (x & 7));
							NEXTWORD_REL(yy, yy_seg);
						} else FATAL_PARMTR;
					}
				}
				else
					FATAL_PARMTR;
			}
			else if ((x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY) && y == 7)
			{
				NEXTBYTE(0xDD);
				NEXTBYTE(0x3B + (x - PARMTR_P_HL_IX));
			}
			else if ((x == PARMTR_PTR || x == 512 + 0 || x == 512 + 1) && (y & 7) != 6) // LD (NN) or (SP+d) or (PC+d), reg8
			{
				if (x == 512 + 0) { // (SP+d)
					NEXTBYTE(0xDD); NEXTBYTE(0x00 + (y & 7)); // Opcode from Nestor80 logic
					NEXTWORD_REL(xx, xx_seg);
					if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
				} else if (x == 512 + 1) { // (PC+d)
					NEXTBYTE(0xFD); NEXTBYTE(0x00 + (y & 7));
					NEXTWORD_REL(xx, xx_seg);
					if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
				} else { // (NN)
					if (y == 7) { // LD (NN),A
						NEXTBYTE(0x32);
						NEXTWORD_REL(xx, xx_seg);
						if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
					} else if (current_cpu == CPU_Z280) {
						NEXTBYTE(0xDD); NEXTBYTE(0x07 + (y & 7));
						NEXTWORD_REL(xx, xx_seg);
					} else FATAL_PARMTR;
				}
			}
			else if ((x == 16 + 0 + 6 || x == 16 + 8 + 6) && (y & 7) != 6) // LD (IX/IY+d), reg8
			{
				// Z280 Word Offset for (IX/IY+dd), REG8
				int long_offset = (current_cpu == CPU_Z280 && (xx_seg != SEG_ASEG || xx < -128 || xx > 127));
				if (long_offset) {
					NEXTBYTE(0xFD);
					NEXTBYTE((x == 16 + 0 + 6 ? 0x09 : 0x19) + (y & 7));
					NEXTWORD_REL(xx, xx_seg);
				} else {
					// Standard Z80
					NEXTBYTE((x == 16 + 0 + 6) ? 0xDD : 0xFD);
					NEXTBYTE(0x70 + (y & 7));
					NEXTBYTE(xx);
				}
			}
			else
				FATAL_PARMTR;
		}
		break;
	case OPCODE_MULUB: // R800 only!
		if (flag_dollar)
			FATAL_ERROR(error_forbidden_as80);
		if (x == PARMTR_A && *s)
			FETCH_PARMTR(x, xx, xx_seg); // accept both "MULUB r8" and "MULUB A,r8"
		GET_PARMTR_F(x, get_parmtr_in);
		NEXTBYTE(0XED);
		NEXTBYTE(x * 8 + 0xC1);
		break;
	case OPCODE_MULUW: // R800 only!
		if (flag_dollar)
			FATAL_ERROR(error_forbidden_as80);
		if (x != PARMTR_HL || !*s)
			FATAL_PARMTR; // reject "MULUW r16" instead of "MULUW HL,r16"!
		FETCH_PARMTR(x, xx, xx_seg);
		GET_PARMTR_F(x, get_parmtr_addhl);
		NEXTBYTE(0XED);
		NEXTBYTE(x + 0xC3);
		break;
	case OPCODE_NOP:
		if (!x)
			xx = 1;
		else if (x != PARMTR_INTEGER || eval_status)
			FATAL_PARMTR;
		if (assemble_filler(xx, 0))
			return -1;
		break;
	case OPCODE_OUT:
		FETCH_PARMTR(y, yy, yy_seg);
		if ((x == PARMTR_PTR || x == PARMTR_POINTER || x == 1024) && y == PARMTR_A)
		{
			NEXTBYTE(0xD3);
			NEXTBYTE(xx);
			CHECK_BAD_BYTE(xx);
		}
		else if (x == PARMTR_PTR && y == PARMTR_A)
		{
			NEXTBYTE(0xD3);
			NEXTBYTE(xx);
			CHECK_BAD_BYTE(xx);
		}
		else if (x == PARMTR_P_C)
		{
			GET_PARMTR_F(y, get_parmtr_in);
			NEXTBYTE(0xED);
			NEXTBYTE(y * 8 + 0x41);
		}
		else
			FATAL_PARMTR;
		break;
	case OPCODE_POP:
		// shortcut: PUSH HL,DE... = PUSH HL + PUSH DE...; POP HL,DE... = ...POP DE + POP HL
		y = 0;
		while (x)
		{
			asciz[ASCIZ_MAXIMUM - ++y] = x;
			FETCH_PARMTR(x, xx, xx_seg);
		}
		if (oo & 4)
			xx = 1, yy = +1; // PUSH
		else
			xx = y, yy = -1; // POP!
		while (y--)
		{
			x = asciz[ASCIZ_MAXIMUM - xx];
			GET_PARMTR_F(x, get_parmtr_pop);
			if (x & 0xFF00)
				NEXTBYTE(x >> 8);
			NEXTBYTE(oo + x);
			xx += yy;
		}
		break;
	case OPCODE_RET:
		GET_PARMTR_F(x, get_parmtr_ret);
		NEXTBYTE(x);
		break;
	case OPCODE_RLC:
		// In 8080 mode, RLC=RLCA(07), RRC=RRCA(0F) — no operand
		if (current_cpu == CPU_8080 && (oo == 0 || oo == 0x08)) {
			if (x != PARMTR_NULL) FATAL_PARMTR;
			NEXTBYTE(0x07 + (oo & 8));
			break;
		}
		GET_PARMTR_F(x, get_parmtr_rlc);
		if (x < 8)
		{
			NEXTBYTE(0xCB);
			NEXTBYTE((x & 7) + oo);
		}
		else if (x < 16) // SJASM shortcut: SLA HL = SLA L + RL H; SRA HL = SRA H + RR L
		{
			if (oo < 16) // reject RLC and RRC!
				FATAL_PARMTR;
			NEXTBYTE(0xCB);
			if (oo & 8) // right: hi + lo
				NEXTBYTE((x++ & 7) + oo);
			else // left: lo + hi
				NEXTBYTE((x & 7) + oo + 1);
			NEXTBYTE(0xCB);
			NEXTBYTE((x & 7) + (oo & 8) + 16); // the second opcode is always RL or RR
		}
		else
		{
			NEXTBYTE((x & 8) * 4 + 0xDD);
			NEXTBYTE(0xCB);
			NEXTBYTE(xx);
			NEXTBYTE((x & 7) + oo);
			CHECK_BAD_BYTE(xx);
		}
		break;
	case OPCODE_RST:
		if (x != PARMTR_INTEGER || xx < 0 || xx > 63 || (xx % 8 && xx > 8))
			FATAL_PARMTR;
		if (xx < 8)
			xx *= 8;
		NEXTBYTE(xx + 0xC7);
		break;
	case OPCODE_SUB:
		// In 8080 mode, CP = CALL if Positive (0xF4 + 16-bit address)
		if (current_cpu == CPU_8080 && oo == 7) {
			if (x != PARMTR_INTEGER) FATAL_PARMTR;
			NEXTBYTE(0xF4);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
			break;
		}
		if (x == PARMTR_HL) // SJASM shortcut: SUB HL,BC = CP A + SBC HL,BC
		{
			NEXTBYTE(0xBF);
			FETCH_PARMTR(y, yy, yy_seg);
			GET_PARMTR_F(y, get_parmtr_addhl);
			NEXTBYTE(0xED);
			NEXTBYTE(y + 0x42);
		}
		else
		{
			if (x == PARMTR_A && *s)
				FETCH_PARMTR(x, xx, xx_seg);
			GET_PARMTR_F(x, get_parmtr_sub);
			if (x == PARMTR_VAL)
			{
				NEXTBYTE(oo * 8 + 0xC6);
				NEXTBYTE_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_BYTE(xx);
			}
			else
			{
				if (x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY)
				{
					NEXTBYTE(0xDD);
					int base_op = 0;
					if (oo == 0x90) base_op = 0x91;      // SUB
					else if (oo == 0x98) base_op = 0x99; // SBC
					else if (oo == 0xA8) base_op = 0xA9; // XOR
					else if (oo == 0x80) base_op = 0x81; // ADD
					else if (oo == 0x88) base_op = 0x89; // ADC
					else if (oo == 0xA0) base_op = 0xA1; // AND
					else if (oo == 0xB0) base_op = 0xB1; // OR
					else if (oo == 0xB8) base_op = 0xB9; // CP
					
					int offset = 0;
					if (x == PARMTR_P_HL_IX) offset = 0;
					else if (x == PARMTR_P_HL_IY) offset = 1;
					else if (x == PARMTR_P_IX_IY) offset = 2;
					
					NEXTBYTE(base_op + offset);
				}
				else if (x == 512 + 0 || x == 512 + 1 || ((x & 16) && (current_cpu == CPU_Z280 && (xx_seg != SEG_ASEG || xx < -128 || xx > 127))))
				{
					// Z280 Arithmetic: (SP+d), (PC+d) or (IX/IY+dd)
					if (x == 512 + 0) NEXTBYTE(0xDD); // SP
					else NEXTBYTE(0xFD); // PC or IX/IY word offset
					
					int base_op = (oo & 0xF8); // Base arithmetic opcode
					
					if (x == 512 + 0 || x == 512 + 1) { // SP+d or PC+d
						NEXTBYTE(base_op);
						NEXTWORD_REL(xx, xx_seg);
						if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
					} else { // IX/IY word offset
						NEXTBYTE(base_op + (x & 8 ? 2 : 1));
						NEXTWORD_REL(xx, xx_seg);
						if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
					}
				}
				else
				{
					// Standard Z80
					INDEX_PREFIX(x);
					NEXTBYTE(oo * 8 + (x & 7) + 0x80);
					if ((x & 23) == 22)
					{
						NEXTBYTE(xx);
						if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
					}
				}
			}
		}
		break;
	case OPCODE_ADDW:
	case OPCODE_SUBW:
		// Z280: ADDW HL,src16 or SUBW HL,src16
		if (x == PARMTR_HL) {
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_BC || y == PARMTR_DE || y == PARMTR_HL || y == PARMTR_SP) {
				NEXTBYTE(0xED);
				int reg_code = 0;
				if (y == PARMTR_BC) reg_code = 0;
				else if (y == PARMTR_DE) reg_code = 1;
				else if (y == PARMTR_HL) reg_code = 2;
				else if (y == PARMTR_SP) reg_code = 3;
				NEXTBYTE((o == OPCODE_ADDW ? 0xC6 : 0xCE) + reg_code * 0x10);
			} else if (y == PARMTR_IX || y == PARMTR_IY) {
				NEXTBYTE(y == PARMTR_IX ? 0xDD : 0xFD);
				NEXTBYTE(0xED);
				NEXTBYTE(o == OPCODE_ADDW ? 0xE6 : 0xEE);
			} else if (y == PARMTR_P_HL) {
				NEXTBYTE(0xDD);
				NEXTBYTE(0xED);
				NEXTBYTE(o == OPCODE_ADDW ? 0xC6 : 0xCE);
			} else if (y == PARMTR_P_IX || y == PARMTR_P_IY || y == PARMTR_P_PC) {
				NEXTBYTE(y == PARMTR_P_PC ? 0xDD : 0xFD);
				NEXTBYTE(0xED);
				if (y == PARMTR_P_IX) NEXTBYTE(o == OPCODE_ADDW ? 0xC6 : 0xCE);
				else if (y == PARMTR_P_IY) NEXTBYTE(o == OPCODE_ADDW ? 0xD6 : 0xDE);
				else if (y == PARMTR_P_PC) NEXTBYTE(o == OPCODE_ADDW ? 0xF6 : 0xFE);
				NEXTWORD_REL(yy, yy_seg);
			} else if (y == PARMTR_PTR) {
				NEXTBYTE(0xDD);
				NEXTBYTE(0xED);
				NEXTBYTE(o == OPCODE_ADDW ? 0xD6 : 0xDE);
				NEXTWORD_REL(yy, yy_seg);
			} else if (y == PARMTR_VAL) {
				NEXTBYTE(0xFD);
				NEXTBYTE(0xED);
				NEXTBYTE(o == OPCODE_ADDW ? 0xF6 : 0xFE);
				NEXTWORD_REL(yy, yy_seg);
			} else {
				FATAL_PARMTR;
			}
		} else FATAL_PARMTR;
		break;
	case OPCODE_CPW:
		// Z280: CPW HL,src16
		if (x == PARMTR_HL) {
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_BC || y == PARMTR_DE || y == PARMTR_HL || y == PARMTR_SP) {
				int reg_idx = (y == PARMTR_BC) ? 0 : (y == PARMTR_DE) ? 1 : (y == PARMTR_HL) ? 2 : 3;
				NEXTBYTE(0xED);
				NEXTBYTE(0xC7 + reg_idx * 0x10);
			} else if (y == PARMTR_IX || y == PARMTR_IY) {
				NEXTBYTE(y == PARMTR_IX ? 0xDD : 0xFD);
				NEXTBYTE(0xED);
				NEXTBYTE(0xE7);
			} else if (y == PARMTR_P_HL) {
				NEXTBYTE(0xDD);
				NEXTBYTE(0xED);
				NEXTBYTE(0xC7);
			} else if (y == PARMTR_P_IX || y == PARMTR_P_IY || y == PARMTR_P_PC) {
				NEXTBYTE(y == PARMTR_P_PC ? 0xDD : 0xFD);
				NEXTBYTE(0xED);
				if (y == PARMTR_P_IX) NEXTBYTE(0xC7);
				else if (y == PARMTR_P_IY) NEXTBYTE(0xD7);
				else NEXTBYTE(0xF7);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_P_HL_IX || y == PARMTR_P_HL_IY || y == PARMTR_P_IX_IY) {
				NEXTBYTE(0xDD);
				NEXTBYTE(0xED);
				NEXTBYTE(y == PARMTR_P_HL_IX ? 0xC8 : (y == PARMTR_P_HL_IY ? 0xD0 : 0xD8));
			} else if (y == 512 + 2) {
				NEXTBYTE(0xFD);
				NEXTBYTE(0xED);
				NEXTBYTE(0xDF);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_PTR) {
				NEXTBYTE(0xDD);
				NEXTBYTE(0xED);
				NEXTBYTE(0xD7);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_VAL) {
				NEXTBYTE(0xFD);
				NEXTBYTE(0xED);
				NEXTBYTE(0xF7);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	case OPCODE_LDW:
	{
		FETCH_PARMTR(y, yy, yy_seg);
		int r1 = -1, r2 = -1;
		if (x == PARMTR_BC) r1 = 0;
		else if (x == PARMTR_DE) r1 = 1;
		else if (x == PARMTR_HL) r1 = 2;
		else if (x == PARMTR_SP) r1 = 3;
		else if (x == PARMTR_IX) r1 = 4; // IX
		else if (x == PARMTR_IY) r1 = 5; // IY
		if (y == PARMTR_BC) r2 = 0;
		else if (y == PARMTR_DE) r2 = 1;
		else if (y == PARMTR_HL) r2 = 2;
		else if (y == PARMTR_SP) r2 = 3;
		else if (y == PARMTR_IX) r2 = 4; // IX
		else if (y == PARMTR_IY) r2 = 5; // IY
		// LDW SP, HL/IX/IY
		if (r1 == 3 && r2 >= 2) {
			if (r2 == 4) NEXTBYTE(0xDD); else if (r2 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xF9);
		}
		// LDW reg16, nn
		else if (r1 >= 0 && y == PARMTR_VAL) {
			if (r1 == 4) NEXTBYTE(0xDD); else if (r1 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0x01 + (r1 & 3) * 0x10);
			NEXTWORD_REL(yy, yy_seg);
		}
		// LDW reg16, (nn)
		else if (r1 >= 0 && y == PARMTR_PTR) {
			if (r1 == 2) { NEXTBYTE(0x2A); NEXTWORD_REL(yy, yy_seg); }
			else if (r1 == 4) { NEXTBYTE(0xDD); NEXTBYTE(0x2A); NEXTWORD_REL(yy, yy_seg); }
			else if (r1 == 5) { NEXTBYTE(0xFD); NEXTBYTE(0x2A); NEXTWORD_REL(yy, yy_seg); }
			else { NEXTBYTE(0xED); NEXTBYTE(0x4B + (r1 & 3) * 0x10); NEXTWORD_REL(yy, yy_seg); }
		}
		// LDW (nn), reg16
		else if (x == PARMTR_PTR && r2 >= 0) {
			if (r2 == 2) { NEXTBYTE(0x22); NEXTWORD_REL(xx, xx_seg); }
			else if (r2 == 4) { NEXTBYTE(0xDD); NEXTBYTE(0x22); NEXTWORD_REL(xx, xx_seg); }
			else if (r2 == 5) { NEXTBYTE(0xFD); NEXTBYTE(0x22); NEXTWORD_REL(xx, xx_seg); }
			else { NEXTBYTE(0xED); NEXTBYTE(0x43 + (r2 & 3) * 0x10); NEXTWORD_REL(xx, xx_seg); }
		}
		// LDW (nn), nn
		else if (x == PARMTR_PTR && y == PARMTR_VAL) {
			NEXTBYTE(0xDD); NEXTBYTE(0x11); NEXTWORD_REL(xx, xx_seg); NEXTWORD_REL(yy, yy_seg);
		}
		// LDW reg16, (HL)
		else if (r1 >= 0 && y == PARMTR_P_HL) {
			if (r1 == 4) NEXTBYTE(0xDD); else if (r1 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(0x06 + (r1 & 3) * 0x10);
		}
		// LDW (HL), reg16
		else if (x == PARMTR_P_HL && r2 >= 0) {
			if (r2 == 4) NEXTBYTE(0xDD); else if (r2 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(0x0E + (r2 & 3) * 0x10);
		}
		// LDW (HL), nn
		else if (x == PARMTR_P_HL && y == PARMTR_VAL) {
			NEXTBYTE(0xDD); NEXTBYTE(0x01); NEXTWORD_REL(yy, yy_seg);
		}
		// LDW reg16, (SP+nn) / (PC+nn)
		else if (r1 >= 0 && (y == PARMTR_P_SP || y == PARMTR_P_PC)) {
			if (r1 == 4) NEXTBYTE(0xDD); else if (r1 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(y == PARMTR_P_SP ? 0x04 : 0x24);
			NEXTWORD_REL(yy, yy_seg);
		}
		// LDW (SP+nn) / (PC+nn), reg16
		else if ((x == PARMTR_P_SP || x == PARMTR_P_PC) && r2 >= 0) {
			if (r2 == 4) NEXTBYTE(0xDD); else if (r2 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(x == PARMTR_P_SP ? 0x05 : 0x25);
			NEXTWORD_REL(xx, xx_seg);
		}
		// LDW (PC+nn), nn
		else if (x == PARMTR_P_PC && y == PARMTR_VAL) {
			NEXTBYTE(0xDD); NEXTBYTE(0x31); NEXTWORD_REL(xx, xx_seg); NEXTWORD_REL(yy, yy_seg);
		}
		// LDW reg16, (HL+IX/IY) or (IX+IY)
		else if (r1 >= 0 && (y == PARMTR_P_HL_IX || y == PARMTR_P_HL_IY || y == PARMTR_P_IX_IY)) {
			if (r1 == 4) NEXTBYTE(0xDD); else if (r1 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(y == PARMTR_P_HL_IX ? 0x0C : (y == PARMTR_P_HL_IY ? 0x14 : 0x1C));
		}
		// LDW (HL+IX/IY) or (IX+IY), reg16
		else if ((x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY) && r2 >= 0) {
			if (r2 == 4) NEXTBYTE(0xDD); else if (r2 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(x == PARMTR_P_HL_IX ? 0x0D : (x == PARMTR_P_HL_IY ? 0x15 : 0x1D));
		}
		// LDW reg16, (IX+d/nn) or (IY+d/nn)
		else if (r1 >= 0 && (y == PARMTR_P_IX || y == PARMTR_P_IY)) {
			int is_long = (yy_seg != SEG_ASEG || yy < -128 || yy > 127);
			if (r1 == 2 || r1 == 4 || r1 == 5) {
				if (is_long) {
					if (r1 == 4) NEXTBYTE(0xDD); else if (r1 == 5) NEXTBYTE(0xFD);
					NEXTBYTE(0xED); NEXTBYTE(y == PARMTR_P_IX ? 0x2C : 0x34); NEXTWORD_REL(yy, yy_seg);
				} else {
					NEXTBYTE(y == PARMTR_P_IX ? 0xDD : 0xFD);
					NEXTBYTE(0xED); NEXTBYTE(0x06 + (r1 & 3) * 0x10);
					NEXTBYTE_REL(yy, yy_seg);
				}
			} else {
				// BC, DE, SP only have 8-bit offset forms
				NEXTBYTE(y == PARMTR_P_IX ? 0xDD : 0xFD);
				NEXTBYTE(0xED); NEXTBYTE(0x06 + (r1 & 3) * 0x10);
				NEXTBYTE_REL(yy, yy_seg);
				if (is_long) CHECK_BAD_CHAR(yy);
			}
		}
		// LDW (IX+d/nn) or (IY+d/nn), reg16
		else if ((x == PARMTR_P_IX || x == PARMTR_P_IY) && r2 >= 0) {
			int is_long = (xx_seg != SEG_ASEG || xx < -128 || xx > 127);
			if (r2 == 2 || r2 == 4 || r2 == 5) {
				if (is_long) {
					if (r2 == 4) NEXTBYTE(0xDD); else if (r2 == 5) NEXTBYTE(0xFD);
					NEXTBYTE(0xED); NEXTBYTE(x == PARMTR_P_IX ? 0x2D : 0x35); NEXTWORD_REL(xx, xx_seg);
				} else {
					NEXTBYTE(x == PARMTR_P_IX ? 0xDD : 0xFD);
					NEXTBYTE(0xED); NEXTBYTE(0x0E + (r2 & 3) * 0x10);
					NEXTBYTE_REL(xx, xx_seg);
				}
			} else {
				// BC, DE, SP only have 8-bit offset forms
				NEXTBYTE(x == PARMTR_P_IX ? 0xDD : 0xFD);
				NEXTBYTE(0xED); NEXTBYTE(0x0E + (r2 & 3) * 0x10);
				NEXTBYTE_REL(xx, xx_seg);
				if (is_long) CHECK_BAD_CHAR(xx);
			}
		}
		// LDW reg16, (HL+nn)
		else if (r1 >= 0 && y == 512+2) {
			if (r1 == 4) NEXTBYTE(0xDD); else if (r1 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(0x3C); NEXTWORD_REL(yy, yy_seg);
		}
		// LDW (HL+nn), reg16
		else if (x == 512+2 && r2 >= 0) {
			if (r2 == 4) NEXTBYTE(0xDD); else if (r2 == 5) NEXTBYTE(0xFD);
			NEXTBYTE(0xED); NEXTBYTE(0x3D); NEXTWORD_REL(xx, xx_seg);
		}
		else FATAL_PARMTR;
		break;
	}
	case OPCODE_MULT:
	case OPCODE_DIV:
	{
		int is_div = (o == OPCODE_DIV);
		int is_unsigned = (oo == 8); // MULTU / DIVU
		if ((!is_div && x == PARMTR_A) || (is_div && x == PARMTR_HL)) {
			FETCH_PARMTR(y, yy, yy_seg);
			int base_op = is_div ? (is_unsigned ? 0xC5 : 0xC4) : (is_unsigned ? 0xC1 : 0xC0);
			int reg_code = get_parmtr_ld(y);
			if (reg_code >= 0 && reg_code < 8 && reg_code != 6) { // B, C, D, E, H, L, A
				NEXTBYTE(0xED); NEXTBYTE(base_op + reg_code * 8);
			} else if (y == PARMTR_XH) { // IXH
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + 4 * 8);
			} else if (y == PARMTR_XL) { // IXL
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + 5 * 8);
			} else if (y == PARMTR_YH) { // IYH
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 4 * 8);
			} else if (y == PARMTR_YL) { // IYL
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 5 * 8);
			} else if (y == PARMTR_P_HL) { // (HL)
				NEXTBYTE(0xED); NEXTBYTE(base_op + 0x30);
			} else if (y == PARMTR_P_HL_IX || y == PARMTR_P_HL_IY || y == PARMTR_P_IX_IY) {
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + (y == PARMTR_P_HL_IX ? 0x08 : (y == PARMTR_P_HL_IY ? 0x10 : 0x18)));
			} else if (y == PARMTR_P_IX || y == PARMTR_P_IY) {
				int is_long = (yy_seg != SEG_ASEG || yy < -128 || yy > 127);
				if (is_long) {
					NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + (y == PARMTR_P_IX ? 0x08 : 0x10));
					NEXTWORD_REL(yy, yy_seg);
				} else {
					NEXTBYTE(y == PARMTR_P_IX ? 0xDD : 0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x30);
					NEXTBYTE_REL(yy, yy_seg);
				}
			} else if (y == PARMTR_P_SP || y == PARMTR_P_PC) {
				NEXTBYTE(y == PARMTR_P_SP ? 0xDD : 0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op);
				NEXTWORD_REL(yy, yy_seg);
			} else if (y == 512+2) { // (HL+nn)
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x18);
				NEXTWORD_REL(yy, yy_seg);
			} else if (y == PARMTR_PTR) { // (nn)
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x38);
				NEXTWORD_REL(yy, yy_seg);
			} else if (y == PARMTR_VAL) { // nn
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x38);
				NEXTBYTE_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_CHAR(yy);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	}
	case OPCODE_MULTW:
	case OPCODE_MULTUW:
	case OPCODE_DIVW:
	case OPCODE_DIVUW:
	{
		// MULTW/MULTUW: Z280 16x16→32 multiply, first operand HL, result in DEHL
		// DIVW/DIVUW:   Z280 32/16 divide, first operand DEHL, quotient→HL, remainder→DE
		int is_div = (o == OPCODE_DIVW || o == OPCODE_DIVUW);
		int is_unsigned = (o == OPCODE_MULTUW || o == OPCODE_DIVUW);
		int base_op = is_div ? (is_unsigned ? 0xCB : 0xCA) : (is_unsigned ? 0xC3 : 0xC2);
		if ((!is_div && x == PARMTR_HL) || (is_div && x == PARMTR_DEHL)) {
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_BC || y == PARMTR_DE || y == PARMTR_HL || y == PARMTR_SP) {
				int reg_idx = (y == PARMTR_BC) ? 0 : (y == PARMTR_DE) ? 1 : (y == PARMTR_HL) ? 2 : 3;
				NEXTBYTE(0xED);
				NEXTBYTE(base_op + reg_idx * 0x10);
			} else if (y == PARMTR_P_HL) {
				// (HL): DD ED base
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op);
			} else if (y == PARMTR_IX) {
				// bare IX register: DD ED (base+0x20)
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x20);
			} else if (y == PARMTR_IY) {
				// bare IY register: FD ED (base+0x20)
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x20);
			} else if (y == PARMTR_P_IX) {
				// (IX+nn): FD ED base nn nn
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_P_IY) {
				// (IY+nn): FD ED (base+0x10) nn nn
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x10);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_P_PC) {
				// (PC+nn): DD ED (base+0x30) nn nn
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x30);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_PTR) {
				// (nn): DD ED (base+0x10) nn nn
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x10);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_VAL) {
				// nn immediate: FD ED (base+0x30) nn nn
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(base_op + 0x30);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	}
	case OPCODE_EXTS:
		// Z280: EXTS A (ED 64) or EXTS HL (ED 6C)
		if (x == PARMTR_A) {
			NEXTBYTE(0xED);
			NEXTBYTE(0x64);
		} else if (x == PARMTR_HL) {
			NEXTBYTE(0xED);
			NEXTBYTE(0x6C);
		} else FATAL_PARMTR;
		break;
	case OPCODE_EPUM:
		// Z280: Extended cache maintenance.
		if (x == PARMTR_P_HL) {
			NEXTBYTE(0xED);
			NEXTBYTE(0xA6);
		} else if (x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY) {
			NEXTBYTE(0xED);
			NEXTBYTE(x == PARMTR_P_HL_IX ? 0x8C : (x == PARMTR_P_HL_IY ? 0x94 : 0x9C));
		} else if (x == 512 + 2 || x == PARMTR_P_IX || x == PARMTR_P_IY || x == PARMTR_P_PC || x == PARMTR_P_SP) {
			NEXTBYTE(0xED);
			if (x == 512 + 2) NEXTBYTE(0xBC);
			else if (x == PARMTR_P_IX) NEXTBYTE(0xAC);
			else if (x == PARMTR_P_IY) NEXTBYTE(0xB4);
			else if (x == PARMTR_P_PC) NEXTBYTE(0xA4);
			else NEXTBYTE(0x84);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		} else if (x == PARMTR_PTR) {
			NEXTBYTE(0xED);
			NEXTBYTE(0xA7);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		} else FATAL_PARMTR;
		break;
	case OPCODE_MEPU:
		// Z280: External cache maintenance.
		if (x == PARMTR_P_HL) {
			NEXTBYTE(0xED);
			NEXTBYTE(0xAE);
		} else if (x == PARMTR_P_HL_IX || x == PARMTR_P_HL_IY || x == PARMTR_P_IX_IY) {
			NEXTBYTE(0xED);
			NEXTBYTE(x == PARMTR_P_HL_IX ? 0x8D : (x == PARMTR_P_HL_IY ? 0x95 : 0x9D));
		} else if (x == 512 + 2 || x == PARMTR_P_IX || x == PARMTR_P_IY || x == PARMTR_P_PC || x == PARMTR_P_SP) {
			NEXTBYTE(0xED);
			if (x == 512 + 2) NEXTBYTE(0xBD);
			else if (x == PARMTR_P_IX) NEXTBYTE(0xAD);
			else if (x == PARMTR_P_IY) NEXTBYTE(0xB5);
			else if (x == PARMTR_P_PC) NEXTBYTE(0xA5);
			else NEXTBYTE(0x85);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		} else if (x == PARMTR_PTR) {
			NEXTBYTE(0xED);
			NEXTBYTE(0xAF);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		} else FATAL_PARMTR;
		break;
	case OPCODE_TSET:
		// Z280: TSET reg8, (HL), (IX+d), or (IY+d)
		{
			int reg_code = get_parmtr_ld(x);
			if (reg_code >= 0 && reg_code < 8) {
				NEXTBYTE(0xCB);
				NEXTBYTE(0x30 + reg_code);
			} else if (reg_code == 16 + 0 + 6 || reg_code == 16 + 8 + 6) {
				NEXTBYTE((reg_code & 8) ? 0xFD : 0xDD);
				NEXTBYTE(0xCB);
				NEXTBYTE_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
				NEXTBYTE(0x36);
			} else FATAL_PARMTR;
		}
		break;
	case OPCODE_TSTI:
		// Z280: TSTI (C)
		if (x == PARMTR_P_C) {
			NEXTBYTE(0xED);
			NEXTBYTE(0x70);
		} else FATAL_PARMTR;
		break;
	case OPCODE_INW:
		// Z280: INW HL, (C)
		if (x == PARMTR_HL) {
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_P_C) {
				NEXTBYTE(0xED); NEXTBYTE(0xB7);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	case OPCODE_OUTW:
		// Z280: OUTW (C), HL
		if (x == PARMTR_P_C) {
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_HL) {
				NEXTBYTE(0xED); NEXTBYTE(0xBF);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	case OPCODE_LDA:
		// 8080: LDA nn = LD A,(nn)
		if (current_cpu == CPU_8080) {
			if (x != PARMTR_INTEGER) FATAL_PARMTR;
			NEXTBYTE(0x3A);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
			break;
		}
		// Z280: LDA HL/IX/IY, address expression.
		if (x == PARMTR_HL || x == PARMTR_IX || x == PARMTR_IY) {
			FETCH_PARMTR(y, yy, yy_seg);
			int prefix = x == PARMTR_IX ? 0xDD : (x == PARMTR_IY ? 0xFD : 0);
			if (y == PARMTR_POINTER) {
				if (prefix) NEXTBYTE(prefix);
				NEXTBYTE(0x21);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else if (y == PARMTR_P_HL_IX || y == PARMTR_P_HL_IY || y == PARMTR_P_IX_IY) {
				if (prefix) NEXTBYTE(prefix);
				NEXTBYTE(0xED);
				NEXTBYTE(y == PARMTR_P_HL_IX ? 0x0A : (y == PARMTR_P_HL_IY ? 0x12 : 0x1A));
			} else if (y == PARMTR_P_SP || y == PARMTR_P_PC || y == PARMTR_P_IX || y == PARMTR_P_IY || y == PARMTR_P_HL || y == 512 + 2) {
				if (prefix) NEXTBYTE(prefix);
				NEXTBYTE(0xED);
				if (y == PARMTR_P_SP) NEXTBYTE(0x02);
				else if (y == PARMTR_P_PC) NEXTBYTE(0x22);
				else if (y == PARMTR_P_IX) NEXTBYTE(0x2A);
				else if (y == PARMTR_P_IY) NEXTBYTE(0x32);
				else NEXTBYTE(0x3A);
				NEXTWORD_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	case OPCODE_LDCTL:
		// Z280: LDCTL (C),rr / rr,(C) / rr,USP / USP,rr, where rr = HL/IX/IY.
		{
			FETCH_PARMTR(y, yy, yy_seg);
			int prefix = 0;
			int opcode = -1;
			if (x == PARMTR_P_C && (y == PARMTR_HL || y == PARMTR_IX || y == PARMTR_IY)) {
				prefix = y == PARMTR_IX ? 0xDD : (y == PARMTR_IY ? 0xFD : 0);
				opcode = 0x6E;
			} else if ((x == PARMTR_HL || x == PARMTR_IX || x == PARMTR_IY) && y == PARMTR_P_C) {
				prefix = x == PARMTR_IX ? 0xDD : (x == PARMTR_IY ? 0xFD : 0);
				opcode = 0x66;
			} else if ((x == PARMTR_HL || x == PARMTR_IX || x == PARMTR_IY) && y == PARMTR_USP) {
				prefix = x == PARMTR_IX ? 0xDD : (x == PARMTR_IY ? 0xFD : 0);
				opcode = 0x87;
			} else if (x == PARMTR_USP && (y == PARMTR_HL || y == PARMTR_IX || y == PARMTR_IY)) {
				prefix = y == PARMTR_IX ? 0xDD : (y == PARMTR_IY ? 0xFD : 0);
				opcode = 0x8F;
			}
			if (opcode < 0) FATAL_PARMTR;
			if (prefix) NEXTBYTE(prefix);
			NEXTBYTE(0xED);
			NEXTBYTE(opcode);
		}
		break;
	case OPCODE_SC:
		// Z280: SC nn  (system call with 16-bit number)
		// ED 71 nn nn
		if (x == PARMTR_VAL) {
			NEXTBYTE(0xED); NEXTBYTE(0x71);
			NEXTWORD_REL(xx, xx_seg);
			if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		} else FATAL_PARMTR;
		break;
	case OPCODE_LDUD:
	case OPCODE_LDUP:
	{
		// Z280 privileged load/store via user address space
		// LDUD: load_op=86 (A←mem), store_op=8E (mem←A)
		// LDUP: load_op=96 (A←mem), store_op=9E (mem←A)
		int load_op  = (o == OPCODE_LDUD) ? 0x86 : 0x96;
		int store_op = (o == OPCODE_LDUD) ? 0x8E : 0x9E;
		if (x == PARMTR_A) {
			// LDUD A,(HL) → ED load_op
			// LDUD A,(IX+n) → DD ED load_op n
			// LDUD A,(IY+n) → FD ED load_op n
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_P_HL) {
				NEXTBYTE(0xED); NEXTBYTE(load_op);
			} else if (y == PARMTR_P_IX) {
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(load_op);
				NEXTBYTE_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_CHAR(yy);
			} else if (y == PARMTR_P_IY) {
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(load_op);
				NEXTBYTE_REL(yy, yy_seg);
				if (yy_seg == SEG_ASEG) CHECK_BAD_CHAR(yy);
			} else FATAL_PARMTR;
		} else if (x == PARMTR_P_HL) {
			// LDUD (HL),A → ED store_op
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_A) {
				NEXTBYTE(0xED); NEXTBYTE(store_op);
			} else FATAL_PARMTR;
		} else if (x == PARMTR_P_IX) {
			// LDUD (IX+n),A → DD ED store_op n
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_A) {
				NEXTBYTE(0xDD); NEXTBYTE(0xED); NEXTBYTE(store_op);
				NEXTBYTE_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
			} else FATAL_PARMTR;
		} else if (x == PARMTR_P_IY) {
			// LDUD (IY+n),A → FD ED store_op n
			FETCH_PARMTR(y, yy, yy_seg);
			if (y == PARMTR_A) {
				NEXTBYTE(0xFD); NEXTBYTE(0xED); NEXTBYTE(store_op);
				NEXTBYTE_REL(xx, xx_seg);
				if (xx_seg == SEG_ASEG) CHECK_BAD_CHAR(xx);
			} else FATAL_PARMTR;
		} else FATAL_PARMTR;
		break;
	}
	// Intel 8080 opcode handlers
	case OPCODE_8080_JMPW:
		// emit fixed opcode byte (oo) + 16-bit address
		if (x != PARMTR_INTEGER && x != PARMTR_NULL) FATAL_PARMTR;
		NEXTBYTE(oo);
		NEXTWORD_REL(xx, xx_seg);
		if (xx_seg == SEG_ASEG) CHECK_BAD_WORD(xx);
		break;
	case OPCODE_8080_MOV:
		{
			int r1 = get_8080_reg(x);
			FETCH_PARMTR(y, yy, yy_seg);
			int r2 = get_8080_reg(y);
			if (r1 < 0 || r2 < 0) FATAL_PARMTR;
			if (r1 == 6 && r2 == 6) {
				// MOV M,M is technically HLT (0x76)
				NEXTBYTE(0x76);
			} else {
				NEXTBYTE(0x40 + (r1 << 3) + r2);
			}
		}
		break;
	case OPCODE_8080_MVI:
		{
			int r = get_8080_reg(x);
			if (r < 0) FATAL_PARMTR;
			FETCH_PARMTR(y, yy, yy_seg);
			if (y != PARMTR_INTEGER) FATAL_PARMTR;
			NEXTBYTE(0x06 + (r << 3));
			NEXTBYTE(yy);
			if (yy_seg == SEG_ASEG) CHECK_BAD_BYTE(yy);
		}
		break;
	case OPCODE_8080_INR:
		// INR r: emit 0x04+(r<<3); DCR r: oo=1, emit 0x05+(r<<3)
		{
			int r = get_8080_reg(x);
			if (r < 0) FATAL_PARMTR;
			NEXTBYTE(0x04 + oo + (r << 3));
		}
		break;
	case OPCODE_8080_ANA:
		// ANA/ORA/XRA/CMP r: emit oo+r
		{
			int r = get_8080_reg(x);
			if (r < 0) FATAL_PARMTR;
			NEXTBYTE(oo + r);
		}
		break;
	case OPCODE_8080_ALU_I:
		// ANI/ORI/XRI/SUI/SBI/ADI/ACI n: emit oo + n
		if (current_cpu != CPU_8080) { printerror("8080 instruction used in non-8080 mode"); return -1; }
		if (x != PARMTR_INTEGER) FATAL_PARMTR;
		NEXTBYTE(oo);
		NEXTBYTE(xx);
		if (xx_seg == SEG_ASEG) CHECK_BAD_BYTE(xx);
		break;
	case OPCODE_8080_DAD:
		// DAD rp: emit 0x09 + (rp<<4)
		{
			int rp = get_8080_rp(x);
			if (rp < 0) FATAL_PARMTR;
			NEXTBYTE(0x09 + (rp << 4));
		}
		break;
	case OPCODE_8080_LXI:
		// LXI rp,nn: emit (0x01+(rp<<4)) + 16-bit immediate
		{
			int rp = get_8080_rp(x);
			if (rp < 0) FATAL_PARMTR;
			FETCH_PARMTR(y, yy, yy_seg);
			if (y != PARMTR_INTEGER) FATAL_PARMTR;
			NEXTBYTE(0x01 + (rp << 4));
			NEXTWORD_REL(yy, yy_seg);
			if (yy_seg == SEG_ASEG) CHECK_BAD_WORD(yy);
		}
		break;
	case OPCODE_8080_INXDCX:
		// INX/DCX rp: emit oo + (rp<<4)
		{
			int rp = get_8080_rp(x);
			if (rp < 0) FATAL_PARMTR;
			NEXTBYTE(oo + (rp << 4));
		}
		break;
	case OPCODE_8080_LDSTAX:
		// LDAX rp (oo=0x0A) / STAX rp (oo=0x02): emit oo + (rp<<4)
		// Only BC (rp=0) and DE (rp=1) are valid
		{
			int rp = get_8080_rp(x);
			if (rp != 0 && rp != 1) FATAL_PARMTR;
			NEXTBYTE(oo + (rp << 4));
		}
		break;
	case OPCODE_8080_CPI:
		// CPI: in 8080 mode = compare immediate (0xFE n); in Z80 mode = block CPI (0xEDA1)
		if (current_cpu == CPU_8080) {
			if (x != PARMTR_INTEGER) FATAL_PARMTR;
			NEXTBYTE(0xFE);
			NEXTBYTE(xx);
			if (xx_seg == SEG_ASEG) CHECK_BAD_BYTE(xx);
		} else {
			if (x != PARMTR_NULL) FATAL_PARMTR;
			NEXTBYTE(0xED);
			NEXTBYTE(0xA1);
		}
		break;
	default:
		FATAL_ERROR(error_syntax_error);
		break;
	}
	if (*s)
		FATAL_ERROR(error_too_many_arguments);
	eval_status |= z; // merge doubts
	return 0;
}

int assemble_copypath(void) // 0 OK, !0 ERROR
{
	int c;
	eval_cursor = (char *)split_parmtr;
	if (*eval_cursor == '"')
	{
		while ((c = *++eval_cursor) && c != '"')
			if (c == (PATHCHAR ^ '\\' ^ '/'))
				*eval_cursor = PATHCHAR;
		if (c != '"')
			return -1;
		*eval_cursor++ = 0;
		strcpy((char *)newpath, (const char *)folder);
		strcat((char *)newpath, (const char *)++split_parmtr);
	}
	else
	{
		while ((c = *eval_cursor) && (unsigned char)c > 32 && c != ',')
		{
			if (c == (PATHCHAR ^ '\\' ^ '/'))
				*eval_cursor = (unsigned char)PATHCHAR;
			eval_cursor++;
		}
		c = *eval_cursor;
		*eval_cursor = 0;
		strcpy((char *)newpath, (const char *)folder);
		strcat((char *)newpath, (const char *)split_parmtr);
		*eval_cursor = (char)c;
		split_parmtr = (unsigned char *)eval_cursor;
	}
	return 0;
}

int assemble_pseudo_db(int o);
int assemble_input(void) // 0 OK, !0 ERROR
{
	split_input();
	int i, j, k, o, oo;
	i = get_opcode((char *)split_opcode);
	oo = i >= 0 ? opcode[i].code : 0;
	o = i >= 0 ? opcode[i].type : -1;
	
	if (o < 0) {
		oo = get_macro((char *)split_opcode);
		if (oo >= 0) o = TYPE_MACRO;
	}

	if (recording)
	{
		if (o == PSEUDO_LOCAL)
		{
			// Record local labels to be substituted by \? during recording
			char *s_loc = (char *)split_parmtr;
			while (*s_loc) {
				while (*s_loc && (unsigned char)*s_loc <= 32) s_loc++;
				char *start = s_loc;
				while (*s_loc && (unsigned char)*s_loc > 32 && *s_loc != ',') s_loc++;
				char saved = *s_loc;
				*s_loc = 0;
				if (*start) {
					// Store local symbol name in current macro level
					if (locals < LOCAL_MAXIMUM) {
						int loc_idx = put_asciz(start, local_name, locals, LOCAL_MAXIMUM);
						if (loc_idx >= 0) {
							local_macro[loc_idx] = macro_recording_idx;
							locals++;
						}
					}
				}
				*s_loc = saved;
				if (*s_loc == ',') s_loc++;
			}
			return 0; // Skip writing LOCAL line to cache
		}

		if (o != PSEUDO_ENDM)
		{
			if (o == PSEUDO_MACRO || o == PSEUDO_REPT || o == PSEUDO_IRP || o == PSEUDO_IRPC || o == PSEUDO_IRPS)
			{
				skipping_macro++;
			}
			if (pass == 1)
			{
				// use the original source line before split_input corrupted input0
				char c, *s = (char *)source, *t = &asciz[ascizs];
				while ((c = *s))
				{
					if (c == '&')
					{
						s++;
						char *d = s;
						while (iseither(*d))
							++d;
						char saved_d = *d;
						*d = 0;
						for (i = 1; i <= param[params - 1][0]; ++i)
							if (!strcasecmp(s, &param0[param[params - 1][i]]))
								break;
						if (i <= param[params - 1][0])
						{
							*t++ = '\\';
							*t++ = '0' + i;
							s = d;
							*d = saved_d; // Restore character to check it
							if (*s == '&') s++; 
						}
						else
						{
							*t++ = '&';
							*d = saved_d;
						}
					}
					else if (!isletter(c))
						*t++ = c, ++s;
					else
					{
						char *d = s;
						while (iseither(*d))
							++d;
						char saved_d = *d;
						*d = 0;
						// Check if it's a parameter (\1, \2, ...)
						for (i = 1; i <= param[params - 1][0]; ++i)
							if (!strcasecmp(s, &param0[param[params - 1][i]]))
								break;
						if (i <= param[params - 1][0])
						{
							*t++ = '\\';
							*t++ = '0' + i;
							s = d;
							*d = saved_d;
							if (*s == '&') s++; 
						}
						else
						{
							// Check if it's a local symbol (\?)
							for (i = 0; i < locals; i++) {
								if (local_macro[i] == macro_recording_idx && !strcasecmp(s, &asciz[local_name[i]]))
									break;
							}
							if (i < locals) {
								*t++ = '\\';
								*t++ = '?';
								*d = saved_d;
								s = d;
							} else {
								*d = saved_d;
								while (s < d)
									*t++ = *s++;
							}
						}
					}
				}
				*t = 0;
				if (add_cache(&asciz[ascizs]) < 0)
					return -1;
			}
			return 0;
		}
		else
		{
			// end macro recording
			if (*split_parmtr)
				FATAL_ERROR(error_too_many_arguments);
			if (*(char *)split_symbol)
				FATAL_ERROR(error_invalid_symbol);
			if (pass == 1)
			{
				if (add_cache(symbol_dollar) < 0)
					return -1;
			}

			int iter = rept_iterations[macro_recording_idx]; // always use stored count (updated in both passes)
			char *m_name = &asciz[macro[macro_recording_idx]];
			int is_rept = (strncmp(m_name, "??REPT", 6) == 0);
			int is_irp = (strncmp(m_name, "??IRP", 5) == 0); // handles ??IRP, ??IRPC, ??IRPS

			// REPT/IRP: split_param called on definition → merge_param always needed to pop the dummy/empty frame.
			// MACRO: split_param called only in pass 1 → merge_param only in pass 1.
			if (is_rept || is_irp || pass == 1)
				merge_param();

			if (skipping_macro)
			{
				skipping_macro--;
				return 0;
			}
			recording = 0;

			// Expand immediately if needed
			if (iter > 0 && is_rept)
			{
				if (!open_macro(macro_recording_idx) || split_param(""))
					return -1;
				rept_remain[inputs - 1] = iter - 1; // remaining after this first iteration
			}
			else if (is_irp)
			{
				irp_pos[inputs] = 0; // Reset position for level about to be pushed
				macro_stack[inputs] = macro_recording_idx; 
				char *first_arg = extract_irp_arg(inputs);
				if (first_arg)
				{
					int next_irp_pos = irp_pos[inputs];
					if (!open_macro(macro_recording_idx) || split_param(first_arg))
						return -1;
					irp_pos[inputs - 1] = next_irp_pos;
				}
			}
			macro_recording_idx = -1;
			return 0;
		}
	}
	else
	{
		if (o == PSEUDO_ENDM)
		{
			if (skipping_macro)
			{
				skipping_macro--;
				return 0;
			}
			FATAL_ERROR(error_endm_without_macro);
		}
		
		if (condition0 != 0)
		{
			if (o == PSEUDO_MACRO || o == PSEUDO_REPT || o == PSEUDO_IRP || o == PSEUDO_IRPC || o == PSEUDO_IRPS)
			{
				skipping_macro++;
				return 0;
			}

		}

		// IF, ELSE, ENDIF — always processed regardless of condition0, so nested IFs inside
		// skipped blocks still push/pop the conditions stack correctly.
		if (o == PSEUDO_IF || o == PSEUDO_ELIF || o == PSEUDO_IFIDN || o == PSEUDO_IFIDNI || o == PSEUDO_IFDIF || o == PSEUDO_IFDIFI || o == PSEUDO_IFNB || o == PSEUDO_IFB || o == PSEUDO_IFDEF || o == PSEUDO_IFNDEF || o == PSEUDO_IF1 || o == PSEUDO_IF2 || o == PSEUDO_IFABS || o == PSEUDO_IFREL || o == PSEUDO_IFCPU || o == PSEUDO_IFNCPU)
		{
			if (o == PSEUDO_ELIF)
			{
				if (flag_dollar)
					FATAL_ERROR(error_forbidden_as80);
				if (!conditions)
					FATAL_ERROR(error_elif_without_if);
				j = condition0 & 1 ? condition0 & 2 : 2; // SKIP status!
				--conditions;
				condition0 >>= 2;
			}
			else
				j = 0;

			if (condition0)
				j = 2;
			else if (!j)
			{
				if (o == PSEUDO_IF || o == PSEUDO_ELIF)
				{
					j = !eval((char *)split_parmtr);
					if (eval_status < 0)
						FATAL_ERROR(error_invalid_expression);
					if (eval_status > 0 && pass == 1)
					{
						j = 1;			 // Assume false
						eval_status = 0; // Clear doubt for now
					}
					else if (eval_status > 0)
						FATAL_ERROR(error_undefined_symbol);
				}
				else if (o == PSEUDO_IF1)
				{
					j = (pass != 1);
				}
				else if (o == PSEUDO_IF2)
				{
					j = (pass != 2);
				}
				else if (o == PSEUDO_IFABS)
				{
					j = (flag_z != 0); // flag_z=0 means ABS mode, so j=0 (true) if flag_z==0
				}
				else if (o == PSEUDO_IFREL)
				{
					j = (flag_z == 0); // flag_z!=0 means REL mode, so j=0 (true) if flag_z!=0
				}
				else if (o == PSEUDO_IFCPU || o == PSEUDO_IFNCPU)
				{
					char cpu_name[256];
					char *p_cpu = (char *)split_parmtr;
					while (*p_cpu && (unsigned char)*p_cpu <= 32) p_cpu++;
					int ci = 0;
					while (*p_cpu && (unsigned char)*p_cpu > 32) cpu_name[ci++] = *p_cpu++;
					cpu_name[ci] = 0;

					int match = 0;
					if (strcasecmp(cpu_name, "Z80") == 0 && current_cpu == CPU_Z80) match = 1;
					else if (strcasecmp(cpu_name, "R800") == 0 && current_cpu == CPU_R800) match = 1;
					else if (strcasecmp(cpu_name, "Z280") == 0 && current_cpu == CPU_Z280) match = 1;
					else if (strcasecmp(cpu_name, "8080") == 0 && current_cpu == CPU_8080) match = 1;

					if (o == PSEUDO_IFCPU) j = !match;
					else j = match;
				}
				else if (o == PSEUDO_IFDEF || o == PSEUDO_IFNDEF)
				{
					j = (get_label((char *)split_parmtr) < 0);
					if (o == PSEUDO_IFNDEF) j = !j;
				}
				else if (o == PSEUDO_IFIDN || o == PSEUDO_IFIDNI || o == PSEUDO_IFDIF || o == PSEUDO_IFDIFI)
				{
					char *s1 = strchr((char *)split_parmtr, "<"[0]);
					char *e1 = strchr((char *)split_parmtr, ">"[0]);
					char *s2 = (e1) ? strchr(e1, "<"[0]) : NULL;
					char *e2 = (s2) ? strchr(s2, ">"[0]) : NULL;
					if (s1 && e1 && s2 && e2)
					{
						*e1 = 0;
						*e2 = 0;
						int res;
						if (o == PSEUDO_IFIDNI || o == PSEUDO_IFDIFI)
							res = strcasecmp(s1 + 1, s2 + 1);
						else
							res = strcmp(s1 + 1, s2 + 1);
						
						if (o == PSEUDO_IFIDN || o == PSEUDO_IFIDNI)
							j = (res != 0); // skip if NOT equal
						else
							j = (res == 0); // skip if equal
					}
					else j = 1;
				}
				else if (o == PSEUDO_IFNB || o == PSEUDO_IFB)
				{
					char *s = strchr((char *)split_parmtr, '<');
					char *e = strrchr((char *)split_parmtr, '>');
					if (s && e) {
						char *p_nb = s + 1;
						while (p_nb < e && (unsigned char)*p_nb <= 32) p_nb++;
						j = (p_nb == e); // j=1 if blank
					} else {
						char *p_nb = (char *)split_parmtr;
						while (*p_nb && (unsigned char)*p_nb <= 32) p_nb++;
						j = (*p_nb == 0); // j=1 if blank
					}
					// IFB: true if blank. IFNB: true if NOT blank.
					// j is currently "is blank".
					if (o == PSEUDO_IFB)
						j = !j; // Skip if NOT blank
					else
						j = j; // Skip if blank
					// (Log removed for efficiency)
				}
			}

			if (*(char *)split_symbol)
				FATAL_ERROR(error_invalid_symbol);
			if (conditions >= 15)
				FATAL_ERROR(error_stack_overflow);
			++conditions;
			condition0 = (condition0 << 2) + j;
		}
		else if (o == PSEUDO_ELSE)
		{
			if (*split_parmtr)
				FATAL_ERROR(error_too_many_arguments);
			if (*(char *)split_symbol)
				FATAL_ERROR(error_invalid_symbol);
			if (!conditions)
				FATAL_ERROR(error_else_without_if);
			condition0 ^= 1;
		}
		else if (o == PSEUDO_ENDIF)
		{
			if (*split_parmtr)
				FATAL_ERROR(error_too_many_arguments);
			if (*(char *)split_symbol)
				FATAL_ERROR(error_invalid_symbol);
			if (!conditions)
				FATAL_ERROR(error_endif_without_if);
			--conditions;
			condition0 >>= 2;
		}
		else if (!condition0)
		{
			if (*(char *)split_symbol)
			{
				char scoped_name[1024];
				apply_module_scope(scoped_name, (char *)split_symbol);

				if (!isletter(*(char *)split_symbol) || get_parmtr((char *)split_symbol) >= 0)
					FATAL_ERROR(error_invalid_symbol);
				// <label> EQU <expression>
				if (o == PSEUDO_EQU || o == PSEUDO_DEFL)
				{
					if (flag_relab && split_symbol[0] != '.') {
						strncpy(last_global_label, (char *)split_symbol, 255);
						last_global_label[255] = 0;
					}
					i = eval((char *)split_parmtr);
					if (eval_status < 0)
						FATAL_ERROR(error_invalid_expression);
					if (!eval_status) {
						if (o == PSEUDO_EQU) {
							k = add_label(scoped_name, i, SEG_ASEG);
							if (k < 0) return -1;
							if (saved_p == ';') label_flag[k] |= LBL_PUBLIC;
						} else if (o == PSEUDO_DEFL) {
							k = set_label(scoped_name, i, SEG_ASEG);
							if (k < 0) return -1;
							if (saved_p == ';') label_flag[k] |= LBL_PUBLIC;
						}
					}
					return 0;
				}
				// <label> MACRO [params]
				else if (o == PSEUDO_MACRO || o == PSEUDO_REPT)
				{
					if (recording)
						FATAL_ERROR(error_macro_without_endm);
					char rept_name[32];
					char *mname = scoped_name;
					if (o == PSEUDO_REPT)
					{
						sprintf(rept_name, "??REPT%04X", rept_count++);
						mname = rept_name;
					}
					
					int m_idx = add_macro(mname);
					if (m_idx < 0)
						return -1;
					macro_recording_idx = m_idx;
					if (o == PSEUDO_REPT)
					{
						int iter = eval((char *)split_parmtr);
						if (pass == 1) rept_iterations[m_idx] = iter;
						if (eval_status < 0)
							FATAL_ERROR(error_invalid_expression);
						if (pass == 1 && split_param("")) // push empty frame to balance close_input→merge_param
							return -1;
					}
					else
					{
						if (pass == 1) {
							rept_iterations[m_idx] = 1;
							if ((o = split_param((char *)split_parmtr)))
								return -1;
						}
					}
					recording = 1;
					return 0;
				}
				else if (o == PSEUDO_LOCAL)
				{
					return 0;
				}
				// <label>[:]
				else
				{
					if (flag_relab && split_symbol[0] != '.') {
						strncpy(last_global_label, (char *)split_symbol, 255);
						last_global_label[255] = 0;
					}
					k = add_label(scoped_name, phase_active ? phase_target : target, phase_active ? phase_seg : current_seg);
					if (k < 0) return -1;
					label_sdcc_area[k] = current_sdcc_area;
					if (saved_p == ';') label_flag[k] |= LBL_PUBLIC;
				}
			}
			if (!*split_opcode)
				return 0;
			switch (o)
			{
			// INCLUDE, END, macros
			case PSEUDO_INCLUDE:
				if (assemble_copypath())
					FATAL_ERROR(error_invalid_string);
				if (*eval_cursor)
					FATAL_ERROR(error_too_many_arguments);
				if (open_input((char *)newpath))
					break;
				if (!*incpath || (strcpy((char *)newpath, (const char *)folder), strcat((char *)newpath, (const char *)incpath), strcat((char *)newpath, (const char *)split_parmtr), open_input((char *)newpath)))
					break;
				return printerror(error_cannot_open_file), -1;
			case PSEUDO_IRP:
			case PSEUDO_IRPC:
			case PSEUDO_IRPS:
			{
				if (recording)
					FATAL_ERROR(error_macro_without_endm);
				char m_name[32];
				sprintf(m_name, "??%s%04X", (o == PSEUDO_IRP ? "IRP" : (o == PSEUDO_IRPC ? "IRPC" : "IRPS")), rept_count++);
				int m_idx = add_macro(m_name);
				if (m_idx < 0)
					return -1;
				macro_recording_idx = m_idx;

				// Parse dummy parameter
				char dummy[256];
				char *s = (char *)split_parmtr;
				while (*s && (unsigned char)*s <= 32) s++;
				int d_i = 0;
				while (*s && (unsigned char)*s > 32 && *s != ',') dummy[d_i++] = *s++;
				dummy[d_i] = 0;
				if (*s == ',') s++;
				while (*s && (unsigned char)*s <= 32) s++;

				// Set up dummy parameter in parameter frame
				if (split_param(dummy))
					return -1;

				// Store list/string
				if (o == PSEUDO_IRPC && *s == '<') {
					s++;
					char *end = s + strlen(s) - 1;
					while (end > s && (unsigned char)*end <= 32) end--;
					if (*end == '>') *end = 0;
				} else if (o == PSEUDO_IRP && *s == '<') {
					s++;
					char *end = s + strlen(s) - 1;
					while (end > s && (unsigned char)*end <= 32) end--;
					if (*end == '>') *end = 0;
				} else if (o == PSEUDO_IRPS) {
					if (*s == '"' || *s == '\'') {
						unsigned char delim = *s++;
						char *t_irp = s;
						eval_cursor = s;
						while (*eval_cursor && *eval_cursor != delim) {
							if (flag_string_escapes && *eval_cursor == '\\') {
								*t_irp++ = (char)eval_escape('\\');
								eval_cursor++;
							} else {
								*t_irp++ = *eval_cursor++;
							}
						}
						*t_irp = 0;
					}
				}
				
				macro_irp_list[m_idx] = ascizs;
				put_asciz(s, &macro_irp_list[m_idx], 0, 1);

				recording = 1;
				break;
			}
			case PSEUDO_ASEG:
				switch_seg(SEG_ASEG, -1);
				break;
			case PSEUDO_CSEG:
				switch_seg(SEG_CSEG, -1);
				break;
			case PSEUDO_DSEG:
				switch_seg(SEG_DSEG, -1);
				break;
			case PSEUDO_COMMON:
			{
				int new_common = -1;
				char *s_com = (char *)split_parmtr;
				while (*s_com && (unsigned char)*s_com <= 32)
					s_com++;
				if (*s_com == '/')
				{
					s_com++;
					char *start = s_com;
					while (*s_com && *s_com != '/')
						s_com++;
					char saved = *s_com;
					*s_com = 0;
					int found = -1;
					for (int k = 0; k < num_commons; k++)
					{
						if (!strcasecmp(common_names[k], start))
						{
							found = k;
							break;
						}
					}
					if (found < 0)
					{
						found = num_commons++;
						strncpy(common_names[found], start, 15);
						common_names[found][15] = 0;
						common_sizes[found] = 0;
						common_targets[found] = 0;
					}
					new_common = found;
					if (saved == '/')
						s_com++;
					*s_com = saved;
				}
				switch_seg(SEG_COMMON, new_common);
				if (current_common >= 0)
					write_rel_control(1, 0, SEG_ASEG, common_names[current_common], 0);
				break;
			}
			case PSEUDO_LOCAL:
				break; // LOCAL labels are resolved during macro recording, not at expansion time
			case PSEUDO_REPT:
			{
				if (recording)
					FATAL_ERROR(error_macro_without_endm);
				char rept_name[32];
				sprintf(rept_name, "??REPT%04X", rept_count++);
				int m_idx = add_macro(rept_name);
				if (m_idx < 0)
					return -1;
				macro_recording_idx = m_idx;
				int rept_iter = eval((char *)split_parmtr);
				rept_iterations[m_idx] = rept_iter;
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				if (split_param(""))
					return -1;
				recording = 1;
				break;
			}
			case PSEUDO_MODULE:
			{
				char *s_mod = (char *)split_parmtr;
				while (*s_mod && (unsigned char)*s_mod <= 32) s_mod++;
				char *start = s_mod;
				while (*s_mod && (unsigned char)*s_mod > 32 && *s_mod != ',') s_mod++;
				char saved = *s_mod;
				*s_mod = 0;
				if (*start) {
					strncpy(current_module, start, 255);
					current_module[255] = 0;
				}
				*s_mod = saved;
				last_global_label[0] = 0;
				break;
			}
			case PSEUDO_ENDMOD:
				current_module[0] = 0;
				last_global_label[0] = 0;
				break;
			case PSEUDO_STRENC:
			{
				char enc_name[256];
				char *p_enc = (char *)split_parmtr;
				while (*p_enc && (unsigned char)*p_enc <= 32) p_enc++;
				int ei = 0;
				while (*p_enc && (unsigned char)*p_enc > 32 && ei < 255) enc_name[ei++] = *p_enc++;
				enc_name[ei] = 0;

				if (strcasecmp(enc_name, "ASCII") == 0 || strcasecmp(enc_name, "US-ASCII") == 0) current_encoding = ENC_ASCII;
				else if (strcasecmp(enc_name, "UTF-8") == 0 || strcasecmp(enc_name, "UTF8") == 0) current_encoding = ENC_UTF8;
				else if (strcasecmp(enc_name, "LATIN1") == 0 || strcasecmp(enc_name, "ISO-8859-1") == 0) current_encoding = ENC_LATIN1;
				else if (strcasecmp(enc_name, "CP1252") == 0 || strcmp(enc_name, "1252") == 0) current_encoding = ENC_CP1252;
				else if (strcasecmp(enc_name, "CP850") == 0 || strcmp(enc_name, "850") == 0) current_encoding = ENC_CP850;
				else if (strcasecmp(enc_name, "CP437") == 0 || strcmp(enc_name, "437") == 0) current_encoding = ENC_CP437;
				else if (strcasecmp(enc_name, "SJIS") == 0 || strcasecmp(enc_name, "SHIFT-JIS") == 0 || strcmp(enc_name, "932") == 0) current_encoding = ENC_SJIS;
				else if (strcasecmp(enc_name, "default") == 0 || strcasecmp(enc_name, "def") == 0) current_encoding = default_encoding;
				break;
			}
			case PSEUDO_STRESC:
			{
				if (strcasecmp((char *)split_parmtr, "ON") == 0) flag_string_escapes = 1;
				else if (strcasecmp((char *)split_parmtr, "OFF") == 0) flag_string_escapes = 0;
				break;
			}
			case PSEUDO_RELAB:
				flag_relab = 1;
				last_global_label[0] = 0;
				break;
			case PSEUDO_XRELAB:
				flag_relab = 0;
				last_global_label[0] = 0;
				break;
			case PSEUDO_EXTROOT:
				flag_extroot = 1;
				break;
			case PSEUDO_XEXTROOT:
				flag_extroot = 0;
				break;
			case PSEUDO_CPU:
			{
				if (strcasecmp((char *)split_parmtr, "Z80") == 0) current_cpu = CPU_Z80;
				else if (strcasecmp((char *)split_parmtr, "R800") == 0) current_cpu = CPU_R800;
				else if (strcasecmp((char *)split_parmtr, "Z280") == 0) current_cpu = CPU_Z280;
				else if (strcasecmp((char *)split_parmtr, "8080") == 0) current_cpu = CPU_8080;
				break;
			}
			case PSEUDO_8080:
				current_cpu = CPU_8080;
				break;
			case PSEUDO_Z80:
				current_cpu = CPU_Z80;
				break;
			case PSEUDO_PRINT1:
				if (pass == 1) printf("%s\n", (char *)split_parmtr);
				break;
			case PSEUDO_PRINT2:
				if (pass == 2) printf("%s\n", (char *)split_parmtr);
				break;
			case PSEUDO_EXITM:
				close_input();
				return 0;
			case PSEUDO_CONTM:
				flag_contm = 1;
				return 0;
			case PSEUDO_AREA:
			{
				char area_name[256];
				char *p_area = (char *)split_parmtr;
				while (*p_area && (unsigned char)*p_area <= 32) p_area++;
				int ai = 0;
				while (*p_area && (unsigned char)*p_area > 32 && *p_area != '(') area_name[ai++] = *p_area++;
				area_name[ai] = 0;

				int is_abs = 0;
				int is_ovr = 0;
				if (strchr(p_area, '(')) {
					if (strstr(p_area, "ABS") || strstr(p_area, "abs")) is_abs = 1;
					if (strstr(p_area, "OVR") || strstr(p_area, "ovr")) is_ovr = 1;
				}

				if (is_abs || strcasecmp(area_name, "_ABS") == 0) {
					switch_seg(SEG_ASEG, -1);
					sdcc_select_area(area_name[0] ? area_name : "_ABS", 1, is_ovr || strcasecmp(area_name, "_ABS") == 0);
					if (flag_sdcc && current_sdcc_area >= 0) target = sdcc_areas[current_sdcc_area].max_offset;
				} else if (strcasecmp(area_name, "_CODE") == 0) {
					switch_seg(SEG_CSEG, -1);
					sdcc_select_area("_CODE", 0, is_ovr);
					if (flag_sdcc && current_sdcc_area >= 0) target = sdcc_areas[current_sdcc_area].max_offset;
				} else if (strcasecmp(area_name, "_DATA") == 0) {
					switch_seg(SEG_DSEG, -1);
					sdcc_select_area("_DATA", 0, is_ovr);
					if (flag_sdcc && current_sdcc_area >= 0) target = sdcc_areas[current_sdcc_area].max_offset;
				} else {
					// Área arbitrária: mapear para um COMMON nomeado
					int found = -1;
					for (int k = 0; k < num_commons; k++) {
						if (!strcasecmp(common_names[k], area_name)) {
							found = k; break;
						}
					}
					if (found < 0 && num_commons < 256) {
						found = num_commons++;
						strncpy(common_names[found], area_name, 15);
						common_names[found][15] = 0;
						common_sizes[found] = 0;
						common_targets[found] = 0;
					}
					switch_seg(SEG_COMMON, found);
					sdcc_select_area(area_name, 0, is_ovr);
					if (flag_sdcc && current_sdcc_area >= 0) target = sdcc_areas[current_sdcc_area].max_offset;
					if (current_common >= 0)
						write_rel_control(1, 0, SEG_ASEG, common_names[current_common], 0);
				}
				break;
			}
			case PSEUDO_NAME:

			{
				char name[16];
				strncpy(name, (char *)split_parmtr, 15);
				name[15] = 0;
				// Remover aspas e parênteses se houver: name('PROG') -> PROG
				char *p1 = strchr(name, '\'');
				if (p1)
				{
					char *p2 = strchr(p1 + 1, '\'');
					if (p2)
					{
						*p2 = 0;
						write_rel_control(2, 0, 0, p1 + 1, 0);
						break;
					}
				}
				write_rel_control(2, 0, 0, name, 0);
			}
			break;
			case PSEUDO_PUBLIC:
			{
				char *s_pub = (char *)split_parmtr;
				while (*s_pub)
				{
					while (*s_pub && (unsigned char)*s_pub <= 32)
						s_pub++;
					char *start = s_pub;
					while (*s_pub && (unsigned char)*s_pub > 32 && *s_pub != ',')
						s_pub++;
					char saved = *s_pub;
					*s_pub = 0;
					if (*start)
					{
						char scoped_name[1024];
						apply_module_scope(scoped_name, start);
						int lbl = get_label(scoped_name);
						if (lbl < 0)
							lbl = add_label(scoped_name, 0, SEG_ASEG);
						if (lbl >= 0)
							label_flag[lbl] |= LBL_PUBLIC;
					}
					*s_pub = saved;
					if (*s_pub == ',')
						s_pub++;
				}
			}
			break;
			case PSEUDO_EXTRN:
			{
				char *s_ext = (char *)split_parmtr;
				while (*s_ext)
				{
					while (*s_ext && (unsigned char)*s_ext <= 32)
						s_ext++;
					char *start = s_ext;
					while (*s_ext && (unsigned char)*s_ext > 32 && *s_ext != ',')
						s_ext++;
					char saved = *s_ext;
					*s_ext = 0;
					if (*start)
					{
						/* EXTRN symbols are usually global, but docs say "All symbols defined inside a module... will be considered relative" */
						/* However, EXTRN is a reference to something else. */
						/* Linkstor80 doesn't care about module prefixes for externals unless we want them to be module-local? */
						/* MACRO-80 EXTRN is global. */
						/* Let's follow apply_module_scope logic which excludes ## and existing . */
						char scoped_name[1024];
						apply_module_scope(scoped_name, start);
						int lbl = get_label(scoped_name);
						if (lbl < 0)
							lbl = add_label(scoped_name, 0, SEG_EXTRN);
						if (lbl >= 0)
						{
							label_flag[lbl] |= LBL_EXTRN;
							label_seg[lbl] = SEG_EXTRN;
						}
					}
					*s_ext = saved;
					if (*s_ext == ',')
						s_ext++;
				}
			}
			break;
			case PSEUDO_PRINTX:
			case PSEUDO_PRINT:
			case PSEUDO_WARN:
			{
				char *s_pr = (char *)split_parmtr;
				while (*s_pr && (unsigned char)*s_pr <= 32) s_pr++;
				char buf[2048];
				if (o == PSEUDO_WARN) {
					interpolate_string(s_pr, buf, sizeof(buf));
					fprintf(stderr, "WARN: %s\n", buf);
				} else if (o == PSEUDO_PRINT) {
					interpolate_string(s_pr, buf, sizeof(buf));
					fprintf(stderr, "%s\n", buf);
				} else if (*s_pr) {
					// .PRINTX: first char is delimiter; print from it up to and
					// including the closing delimiter (or end of line if absent)
					char delim = *s_pr;
					char *end_pr = strchr(s_pr + 1, delim);
					if (end_pr) *(end_pr + 1) = 0;
					interpolate_string(s_pr, buf, sizeof(buf));
					fprintf(stderr, "%s\n", buf);
				}
				break;
			}
			case PSEUDO_ERROR:
			case PSEUDO_FATAL:
			{
				char buf[2048];
				interpolate_string((char *)split_parmtr, buf, sizeof(buf));
				fprintf(stderr, "%s: %s\n", (o == PSEUDO_FATAL ? "FATAL" : "ERROR"), buf);
				return -1;
			}
			case PSEUDO_END:
				if (flag_v >= 0) fprintf(stderr, "Handling PSEUDO_END, pass %d\n", pass);
				if (*split_parmtr)
				{
					entry_point = eval((char *)split_parmtr);
					entry_seg = eval_res_seg;
					if (eval_status > 0)
						FATAL_ERROR(error_undefined_symbol);
					if (eval_status < 0)
						FATAL_ERROR(error_invalid_expression);
					if (*eval_cursor)
						FATAL_ERROR(error_too_many_arguments);
				}
				close_input();
				break;
			case TYPE_MACRO: // macro name
				if (!open_macro(oo) || split_param((char *)split_parmtr))
					return -1;
				break;
			// data and code output
			case PSEUDO_ALIGN:
				i = eval((char *)split_parmtr);
				if (eval_status > 0)
					FATAL_ERROR(error_undefined_symbol);
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				k = (*eval_cursor == ',') ? eval(++eval_cursor) : 0;
				if (eval_status > 0)
					FATAL_ERROR(error_undefined_symbol);
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				if (*eval_cursor)
					FATAL_ERROR(error_too_many_arguments);
				if (i && (j = target % i))
					if (assemble_filler(i - j, k))
						return -1;
				break;
			case PSEUDO_DEFB:
			case PSEUDO_DEFC:
			case PSEUDO_DEFD:
			case PSEUDO_DEFW:
			case PSEUDO_DEFZ:
				return assemble_pseudo_db(o);
			case PSEUDO_EQU:
			case PSEUDO_DEFL:
			{
				if (!*(char *)split_symbol)
					FATAL_ERROR(error_invalid_symbol);
				i = eval((char *)split_parmtr);
				if (eval_status > 0)
					FATAL_ERROR(error_undefined_symbol);
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				if (*eval_cursor)
					FATAL_ERROR(error_too_many_arguments);
				char scoped_name[1024];
				apply_module_scope(scoped_name, (char *)split_symbol);
				set_label(scoped_name, i, eval_res_seg);
				break;
			}
			case PSEUDO_PHASE:
				phase_target = eval((char *)split_parmtr);
				phase_seg = eval_res_seg;
				phase_active = 1;
				break;
			case PSEUDO_DEPHASE:
				phase_active = 0;
				break;
			case PSEUDO_DEFS:
			{
				i = eval((char *)split_parmtr);
				j = (*eval_cursor == ',') ? eval(++eval_cursor) : 0;
				while (*eval_cursor && (unsigned char)*eval_cursor <= 32)
					eval_cursor++;
				if (*eval_cursor)
					FATAL_ERROR(error_too_many_arguments);
				if (eval_status > 0 && pass == 2)
					FATAL_ERROR(error_undefined_symbol);
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				CHECK_BAD_BYTE(j);
				if (assemble_filler(i, j))
					return -1;
				break;
			}
			case PSEUDO_INCBIN:
				if (flag_dollar)
					FATAL_ERROR(error_forbidden_as80);
				if (assemble_copypath())
					FATAL_ERROR(error_invalid_string);
				i = (*eval_cursor == ',') ? eval(++eval_cursor) : 0;
				j = (!eval_status && *eval_cursor == ',') ? eval(++eval_cursor) : (1 << 30);
				if (*eval_cursor)
					FATAL_ERROR(error_too_many_arguments);
				if (eval_status > 0)
					FATAL_ERROR(error_undefined_symbol);
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				{
					FILE *f = fopen((const char *)newpath, "rb");
					if (!f)
						return printerror(error_cannot_open_file), -1;
					fseek(f, i, i < 0 ? SEEK_END : SEEK_SET);
					if (j > SIZEOF_OUTPUT - target)
						j = 1 + SIZEOF_OUTPUT - target;
					target += fread(&output[target], 1, j, f);
					fclose(f);
				}
				break;
			case PSEUDO_ORG:
				i = eval((char *)split_parmtr);
				if (eval_status > 0)
					FATAL_ERROR(error_undefined_symbol);
				if (eval_status < 0)
					FATAL_ERROR(error_invalid_expression);
				if (*eval_cursor)
					FATAL_ERROR(error_too_many_arguments);
				
				target = i;
				if (current_seg == SEG_COMMON && current_common >= 0)
					common_targets[current_common] = target;
				else
					seg_target[current_seg] = target;
				if (target > seg_max[current_seg])
					seg_max[current_seg] = target;
				write_rel_switch_seg();
				break;
			// case OPCODE_COPY4:
			// NEXTBYTE=oo>>24;
			// case OPCODE_COPY3:
			// NEXTBYTE=oo>>16;
			case OPCODE_COPY2:
				NEXTBYTE(oo >> 8);
				/* fall through */
			case OPCODE_COPY1:
				NEXTBYTE(oo);
				/* fall through */
			case PSEUDO_NULL:
				if (*split_parmtr)
					FATAL_ERROR(error_too_many_arguments);
				break;
			case PSEUDO_IGNORE:
				break;
			default:
				if (o < 0) {
					if (opt_allow_bare_expressions && (*split_opcode || *split_parmtr)) {
						/* Treat as bare expressions (DB style) */
						char bare_line[4096];
						if (*split_opcode && *split_parmtr)
							snprintf(bare_line, sizeof(bare_line), "%s,%s", (char *)split_opcode, (char *)split_parmtr);
						else if (*split_opcode)
							snprintf(bare_line, sizeof(bare_line), "%s", (char *)split_opcode);
						else
							snprintf(bare_line, sizeof(bare_line), "%s", (char *)split_parmtr);
						
						char *saved_parmtr = (char *)split_parmtr;
						split_parmtr = (unsigned char *)bare_line;
						int res = assemble_pseudo_db(PSEUDO_DEFB);
						split_parmtr = (unsigned char *)saved_parmtr;
						if (res) return -1;
						break;
					}
					FATAL_ERROR(error_undefined_opcode);
				}
				if (assemble_opcode(o, oo))
					return -1;
			}
			if (target > SIZEOF_OUTPUT)
				FATAL_ERROR(error_out_of_memory);
			if (target > remote)
				remote = target;
		}
	}
	return 0;
}

int assemble_doubts(int q) // Q=0: solve labels; Q!=0: solve parameters; 0 OK, !0 ERROR
{
	unsigned char *s;
	int i, j, k;
	do
	{
		j = k = 0;
		for (i = 0; i < doubts; ++i)
			if (*(s = (unsigned char *)&asciz[doubt[i]]) && (q || ((unsigned char)*s > 32)))
			{
				++j;
				strcpy((char *)input0, (const char *)s);
				strcpy((char *)source, (const char *)input0);
				dollar = target = doubt_target[i];
				int l;
				inputs = 0;
				if ((l = doubt_source[i]) < 0)
				{
					int n = ~l;
					while ((l = chain[n++]) >= 0)
						input_source[inputs++] = l;
					l = ~l; // final item
				}
				input_source[inputs++] = l;
				if (assemble_input())
				{
				}
				else if (eval_status)
				{
					if (q)
						printerror(error_undefined_symbol);
				}
				else
					*s = 0, ++k;
			}
	} while (k && !q);
	return q && (k != j);
}

int assemble(char *s, char *t) // assemble source `s` to target `t`; 0 OK, !0 ERROR
{
	int i, j;
	inputs = caches = macros = labels = ascizs = params = locals = doubts = chains = dollar = target = origin = remote = recording = conditions = condition0 = 0;
	num_commons = 0;
	sdcc_area_count = 0;
	current_sdcc_area = -1;
	phase_active = phase_target = 0;
	rept_count = 0;
	macro_local_count = 0;
	macro_recording_idx = -1;
	eval_status = 0;
	memset(common_names, 0, sizeof(common_names));
	memset(common_sizes, 0, sizeof(common_sizes));
	memset(common_targets, 0, sizeof(common_targets));
	add_label(symbol_debug, 0, SEG_ASEG);

	for (pass = 1; pass <= 2; pass++) {
		inputs = params = locals = doubts = chains = dollar = target = origin = remote = recording = conditions = condition0 = 0;
		rept_count = 0;
		memset(rept_remain, 0, sizeof(rept_remain));
		rel_bits = rel_pos = 0;
		if (pass == 2) {
			static unsigned char header[] = {0x85, 0xD3, 0x13, 0x92, 0xD4, 0xD5, 0x13, 0xD4, 0xA5, 0x00, 0x00, 0x13, 0x8F, 0xFF, 0xF0, 0x9E};
			memcpy(rel_buffer, header, 16);
			rel_pos = 16;
		}
		phase_active = phase_target = 0;
		entry_point = 0;
		entry_seg = SEG_ASEG;
		current_common = -1;
		current_sdcc_area = -1;
		memset(common_sizes, 0, sizeof(common_sizes));
		memset(common_targets, 0, sizeof(common_targets));
		memset(seg_target, 0, sizeof(seg_target));
		memset(seg_max, 0, sizeof(seg_max));
		memset(label_last_addr, 0, LABEL_MAXIMUM * sizeof(int));
		memset(label_last_seg, 0, LABEL_MAXIMUM * sizeof(int));
		current_seg = SEG_CSEG; // M80 default: CSEG (not ASEG)
		current_encoding = default_encoding;
		if (pass == 2 && flag_sdcc)
			sdcc_select_area("_CODE", 0, 0);

		if (pass == 2 && listing_name[0]) {
			listing_f = fopen(listing_name, "w");
			if (!listing_f) {
				fprintf(stderr, "Warning: Could not open listing file %s\n", listing_name);
			}
		}

		write_rel_switch_seg();

		if (!open_input(s))
			return printerror(error_cannot_open_file), -1;
		do
		{
			while (read_input())
			{
				line_pc = phase_active ? phase_target : target;
				line_seg = phase_active ? phase_seg : current_seg;
				line_bytes_count = 0;
				char current_line[4096];
				strncpy(current_line, (char *)input0, sizeof(current_line) - 1);
				current_line[sizeof(current_line) - 1] = 0;
				// Remove trailing newline for listing
				char *nl = strpbrk(current_line, "\r\n");
				if (nl) *nl = 0;

				if (assemble_input())
					return -1;
				
				if (pass == 2 && listing_f) {
					fprintf(listing_f, "%04X%c ", line_pc, (line_seg == SEG_ASEG ? ' ' : (line_seg == SEG_CSEG ? '\'' : (line_seg == SEG_DSEG ? '"' : '!'))));
					int k;
					for (k = 0; k < opt_listing_columns; k++) {
						if (k < line_bytes_count) {
							fprintf(listing_f, "%02X ", line_bytes[k]);
						} else {
							fprintf(listing_f, "   ");
						}
					}
					fprintf(listing_f, " %s\n", current_line);
					while (k < line_bytes_count) {
						fprintf(listing_f, "      ");
						int start = k;
						for (; k < start + opt_listing_columns; k++) {
							if (k < line_bytes_count) {
								fprintf(listing_f, "%02X ", line_bytes[k]);
							} else {
								fprintf(listing_f, "   ");
							}
						}
						fprintf(listing_f, "\n");
					}
				}

				if (flag_contm) {
					// Skipping until end of current iteration
					while (flag_contm && read_input()) {
						// flag_contm is reset inside read_input when it hits symbol_dollar
						// for a REPT/IRP that has more iterations.
					}
					// If flag_contm is still set here, it means we exited the block.
					flag_contm = 0;
					continue;
				}

				if (eval_status > 0) {
					if (pass == 2)
						return printerror(error_undefined_symbol), -1;
					else
						eval_status = 0; // Ignore in pass 1
				}
				dollar = phase_active ? phase_target : target;
				dollar_seg = phase_active ? phase_seg : current_seg;
			}
		} while (close_input());

		if (recording)
			FATAL_ERROR(error_macro_without_endm);
		if (conditions)
			FATAL_ERROR(error_if_without_endif);
	}

	if (flag_sdcc)
		return sdcc_write_output(s, t);

	// Finalize .REL bitstream
	// 1. Escrever tamanhos de segmentos
	if (seg_target[SEG_DSEG] > 0)
		write_rel_control(10, seg_target[SEG_DSEG], SEG_ASEG, "", 0);
	if (seg_target[SEG_CSEG] > 0)
		write_rel_control(13, seg_target[SEG_CSEG], SEG_ASEG, "", 0);

	for (i = 0; i < num_commons; i++)
	{
		write_rel_control(5, common_sizes[i], SEG_ASEG, common_names[i], 0);
	}

	// 2. Escrever Símbolos Públicos e Cabeças de Cadeia de Externos
	for (i = 0; i < labels; i++)
	{
		char *name = &asciz[label[i]];
		if (!strcmp(name, symbol_debug))
			continue;

		if (label_flag[i] & LBL_PUBLIC)
		{
			write_rel_control(7, value[i], label_seg[i], name, 0);
		}
		if ((label_flag[i] & LBL_EXTRN) && label_last_addr[i] != 0)
		{
			write_rel_control(6, label_last_addr[i], label_last_seg[i], name, 0);
		}
	}

	// 3. Fim de programa e arquivo
	write_rel_control(14, entry_point, entry_seg, "", 0); // End Program
	if (rel_bits > 0) {
		rel_bits = 0;
		rel_pos++;
	}
	write_rel_control(15, 0, SEG_ASEG, "", 0); // End File

	if (rel_bits > 0)
		rel_pos++;

	FILE *f = stdout;
	if (strcmp(t, "-"))
		if (!(f = fopen(t, "wb")))
			FATAL_ERROR(error_cannot_create_file);
	i = fwrite(rel_buffer, 1, j = rel_pos, f);
	if (f != stdout)
		fclose(f);
	if (i != j)
		FATAL_ERROR(error_cannot_write_data);
	if (flag_v >= 0)
		fprintf(stderr, "%s:%s (%i bytes)\n", s, t, j);
	if (listing_f) {
		fclose(listing_f);
		listing_f = NULL;
	}
	return 0;
}

typedef struct {
	char **argv;
	int argc;
	int capacity;
} t_arg_list;

static void arg_list_init(t_arg_list *l) {
	l->capacity = 256;
	l->argc = 0;
	l->argv = malloc(l->capacity * sizeof(char *));
}

static void arg_list_add(t_arg_list *l, char *arg) {
	if (l->argc >= l->capacity - 1) {
		l->capacity *= 2;
		l->argv = realloc(l->argv, l->capacity * sizeof(char *));
	}
	l->argv[l->argc++] = arg;
}

static void arg_list_parse_string(t_arg_list *l, char *s) {
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
		arg_list_add(l, token_start);
		
		if (next_char == '\0') break;
		p = src + 1;
	}
}

static void arg_list_parse_file(t_arg_list *l, const char *fname) {
	FILE *f = fopen(fname, "r");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(sz + 1);
	if (fread(buf, 1, sz, f) == (size_t)sz) {
		buf[sz] = 0;
		arg_list_parse_string(l, buf);
	}
	fclose(f);
}

static int arg_list_has(int argc, char **argv, const char *pattern) {
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], pattern) == 0) return 1;
	return 0;
}

void reset_n80_config(void)
{
	flag_v = 0;
	flag_string_escapes = 1;
	flag_relab = 0;
	flag_extroot = 0;
	flag_z = 1;
	flag_sdcc = 0;
	opt_show_banner = 1;
	opt_file_case_lower = 0;
	opt_output_file_explicit = 0;
	default_encoding = ENC_ASCII;
	incpath[0] = 0;
}

void set_encoding_common(const char *enc_name, Encoding *target_enc) {
	if (strcasecmp(enc_name, "ASCII") == 0 || strcasecmp(enc_name, "US-ASCII") == 0) *target_enc = ENC_ASCII;
	else if (strcasecmp(enc_name, "UTF-8") == 0 || strcasecmp(enc_name, "UTF8") == 0) *target_enc = ENC_UTF8;
	else if (strcasecmp(enc_name, "LATIN1") == 0 || strcasecmp(enc_name, "ISO-8859-1") == 0) *target_enc = ENC_LATIN1;
	else if (strcasecmp(enc_name, "CP1252") == 0 || strcmp(enc_name, "1252") == 0) *target_enc = ENC_CP1252;
	else if (strcasecmp(enc_name, "CP850") == 0 || strcmp(enc_name, "850") == 0) *target_enc = ENC_CP850;
	else if (strcasecmp(enc_name, "CP437") == 0 || strcmp(enc_name, "437") == 0) *target_enc = ENC_CP437;
	else if (strcasecmp(enc_name, "SJIS") == 0 || strcasecmp(enc_name, "SHIFT-JIS") == 0 || strcmp(enc_name, "932") == 0) *target_enc = ENC_SJIS;
}

int main(int argc, char *argv[])
{
	int i, j, k;
	char *r, *s = 0, *t = 0;

	t_arg_list al;
	arg_list_init(&al);
	arg_list_add(&al, argv[0]);

	int use_env = !arg_list_has(argc, argv, "--no-env-args");
	int use_def_file = !arg_list_has(argc, argv, "--no-default-file-args") && !arg_list_has(argc, argv, "--no-def-file-args");

	if (use_env) {
		const char *env = getenv("N80_ARGS");
		if (env) {
			char *ecopy = strdup(env);
			arg_list_parse_string(&al, ecopy);
		}
	}

	if (use_def_file) {
		arg_list_parse_file(&al, ".N80");
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--arguments-file") == 0 || strcmp(argv[i], "--args-file") == 0) {
			if (i + 1 < argc) {
				arg_list_parse_file(&al, argv[++i]);
			}
		} else {
			arg_list_add(&al, argv[i]);
		}
	}

	int final_argc = al.argc;
	char **final_argv = al.argv;

	if (!(asciz = (char *)malloc(ASCIZ_MAXIMUM)) || !CREATE_ARRAY(label, LABEL_MAXIMUM) || !CREATE_ARRAY(value, LABEL_MAXIMUM) || !CREATE_ARRAY(label_seg, LABEL_MAXIMUM) || !CREATE_ARRAY(label_sdcc_area, LABEL_MAXIMUM) || !CREATE_ARRAY(sdcc_symbol_index_by_label, LABEL_MAXIMUM) || !CREATE_ARRAY(label_flag, LABEL_MAXIMUM) || !CREATE_ARRAY(label_last_addr, LABEL_MAXIMUM) || !CREATE_ARRAY(label_last_seg, LABEL_MAXIMUM) || !CREATE_ARRAY(cache, CACHE_MAXIMUM) || !CREATE_ARRAY(macro, CACHE_MAXIMUM) || !CREATE_ARRAY(macro_source, CACHE_MAXIMUM) || !CREATE_ARRAY(rept_iterations, CACHE_MAXIMUM) || !CREATE_ARRAY(macro_irp_list, CACHE_MAXIMUM) || !CREATE_ARRAY(doubt, DOUBT_MAXIMUM) || !CREATE_ARRAY(doubt_target, DOUBT_MAXIMUM) || !CREATE_ARRAY(doubt_source, DOUBT_MAXIMUM) || !CREATE_ARRAY(chain, CHAIN_MAXIMUM))
		return printerror(error_out_of_memory), 1; // assuming OS and runtime release memory after exit :-I
	*(int *)incpath = 0;
	for (i = 1; i < final_argc; ++i)
	{
		r = final_argv[i];
		if (*r == '-')
		{
			if (r[1] == '-') // Long option
			{
				if (strcmp(r, "--reset-config") == 0)
				{
					reset_n80_config();
				}
				else if (strcmp(r, "--no-env-args") == 0 || strcmp(r, "--no-default-file-args") == 0 || strcmp(r, "--no-def-file-args") == 0)
				{
					/* already handled in pre-scan */
				}
				else if (strcmp(r, "--no-string-escapes") == 0)
				{
					flag_string_escapes = 0;
				}
				else if (strcmp(r, "--string-encoding") == 0 && i + 1 < final_argc)
				{
					set_encoding_common(final_argv[++i], &default_encoding);
				}
				else if (strcmp(r, "--list-encodings") == 0)
				{
					printf("Supported encodings: ASCII, UTF-8, LATIN1, CP1252, CP850, CP437, SJIS\n");
					return 0;
				}
				else if (strcmp(r, "--no-show-banner") == 0)
				{
					opt_show_banner = 0;
				}
				else if ((strcmp(r, "--include-directory") == 0 || strcmp(r, "--include-dir") == 0) && i + 1 < final_argc)
				{
					char *u = (char *)incpath;
					char *path_val = final_argv[++i];
					while ((*u++ = *path_val++))
						;
					u--; // point to null terminator
					if (u > (char *)incpath && u[-1] != PATHCHAR)
					{
						*u++ = PATHCHAR;
						*u = 0;
					}
				}
				else if (strcmp(r, "--verbosity") == 0 && i + 1 < final_argc)
				{
					flag_v = atoi(final_argv[++i]);
				}
				else if (strcmp(r, "--build-type") == 0 && i + 1 < final_argc)
				{
					char *bt = final_argv[++i];
					if (strcasecmp(bt, "abs") == 0) flag_z = 0;
					else if (strcasecmp(bt, "rel") == 0) flag_z = 1;
					else if (strcasecmp(bt, "sdcc") == 0) {
						flag_z = 1;
						flag_sdcc = 1;
					}
				}
				else if (strcmp(r, "--output-file-case") == 0 && i + 1 < final_argc)
				{
					if (strcasecmp(final_argv[++i], "lower") == 0) opt_file_case_lower = 1;
				}
				else if (strcmp(r, "--unknown-symbols-external") == 0 || strcmp(r, "-use") == 0)
				{
					opt_unknown_symbols_external = 1;
				}
				else if (strcmp(r, "--allow-bare-expressions") == 0 || strcmp(r, "-abe") == 0)
				{
					opt_allow_bare_expressions = 1;
				}
				else if (strcmp(r, "--listing-file") == 0 && i + 1 < final_argc)
				{
					strncpy(listing_name, final_argv[++i], sizeof(listing_name) - 1);
				}
				else if (strcmp(r, "--listing-columns") == 0 && i + 1 < final_argc)
				{
					opt_listing_columns = atoi(final_argv[++i]);
				}
				else if (strcmp(r, "--output-file-extension") == 0 && i + 1 < final_argc)
				{
					i++; // silently ignore
				}
				else if (strcmp(r, "--define-symbols") == 0 && i + 1 < final_argc)
				{
					/* comma-separated list of symbols: SYM or SYM=VALUE */
					char *defs = strdup(final_argv[++i]);
					char *sym_tok = strtok(defs, ",");
					while (sym_tok) {
						char *u;
						int def_val = 1;
						if ((u = strchr(sym_tok, '='))) {
							*u = 0;
							def_val = atoi(u + 1);
						}
						if ((k = get_label(sym_tok)) >= 0) {
							value[k] = def_val;
							label_seg[k] = SEG_ASEG;
						} else {
							add_label(sym_tok, def_val, SEG_ASEG);
						}
						sym_tok = strtok(NULL, ",");
					}
					free(defs);
				}
				continue;
			}
			++r;
			while (r && (j = *r++))
				switch (j | 32)
				{
				case '$':
					flag_dollar = 1;
					break;
				case 'v':
					flag_v = 1;
					break;
				case 'q':
					flag_v = -1;
					break;
				case 'z':
					flag_z = 0;
					break;
				case 'd':
					j = 1; // -Dlabel[=1]
					if (!*r)
						r = symbol_debug; // -D[DEBUG=1]
					{
						char *u;
						if ((u = strchr(r, '=')))
						{
							*u = 0; // -Dlabel=expr
							j = eval(++u);
							if (eval_status)
								i = final_argc; // HELP!
						}
					}
					if ((k = get_label(r)) >= 0)
					{
						value[k] = j; // allow redefining
						label_seg[k] = SEG_ASEG;
					}
					else if (add_label(r, j, SEG_ASEG) < 0)
						i = final_argc; // HELP!
					r = 0;
					break;
				case 'o':
					if (i + 1 < final_argc)
					{
						t = final_argv[++i], r = 0;
						opt_output_file_explicit = 1;
					}
					else
						i = final_argc; // HELP!
					break;
				case 'i':
					if (*r)
					{
						char *u = (char *)incpath;
						while ((*u++ = *r++))
							;
						u--; // point to null terminator
						if (u > (char *)incpath && u[-1] != PATHCHAR)
						{
							*u++ = PATHCHAR;
							*u = 0;
						}
						r = 0;
					}
					else
						i = final_argc; // HELP!
					break;
				default:
					i = final_argc; // HELP!
				}
		}
		else if (*r == '$')
		{
			continue;
		}
		else if (!s)
			s = r;
		else if (!t || !*t)
			t = r;
		else
			i = final_argc; // HELP!
	}
	if (i > final_argc || !s || strcmp(s, "-h") == 0 || strcmp(s, "--help") == 0)
	{
		printf("n80 - Z80/8080/R800/Z280 Assembler (Nestor80 compatible)\n\n");
		printf("Usage:\n");
		printf("  n80 <source-file> [output-file] [options...]\n\n");
		printf("Assembler Options:\n");
		printf("  -o <file>                Name of the output file\n");
		printf("  --build-type <type>      Output format: abs (absolute), rel (relocatable, default), sdcc\n");
		printf("  -z                       Absolute output (alias for --build-type abs)\n");
		printf("  -d<label>[=<val>]        Define a symbol (default value is 1)\n");
		printf("  --define-symbols <list>  Comma-separated list of symbols (SYM or SYM=VAL)\n");
		printf("  -i <dir>                 Include directory (alias for --include-dir)\n");
		printf("  --include-dir <dir>      Add directory to the include search path\n\n");
		printf("Listing Options:\n");
		printf("  --listing-file <file>    Generate a listing file\n");
		printf("  --listing-columns <n>    Number of hex bytes per line in listing (default: 8)\n\n");
		printf("Source Control Options:\n");
		printf("  --encoding <enc>         Source file encoding (ASCII, UTF-8, etc.)\n");
		printf("  --list-encodings         List all supported text encodings\n");
		printf("  --no-string-escapes      Disable backslash escape sequences in strings\n\n");
		printf("Global Options:\n");
		printf("  --output-file-case lower Lowercase the output filename\n");
		printf("  --no-show-banner         Suppress the startup greeting\n");
		printf("  -v, --verbosity <n>      Set log level (0=quiet, 1=normal)\n");
		printf("  -q                       Quiet mode (alias for --verbosity 0)\n");
		return 0;
	}
	
	if (!t || strcmp(t, "$") == 0)
	{
		static char target_buf[256];
		strncpy(target_buf, s, 250);
		char *dot = strrchr(target_buf, '.');
		if (dot) *dot = 0;
		strcat(target_buf, ".rel");
		t = target_buf;
	}

	static char final_output_filename[1024];
	strncpy(final_output_filename, t, sizeof(final_output_filename)-1);
	final_output_filename[sizeof(final_output_filename)-1] = '\0';

	if (opt_file_case_lower && !opt_output_file_explicit)
	{
		char *p = final_output_filename;
		while (*p) { *p = tolower((unsigned char)*p); p++; }
	}
	t = final_output_filename;

	if (opt_show_banner)
		fprintf(stderr, "%s", string_greeting);

	strcpy((char *)folder, s);
	if ((r = strrchr((char *)folder, PATHCHAR))) // the source includes a path?
		r[1] = 0;
	else // no path in source, use current path
		folder[0] = 0;
	
	rel_buffer = (unsigned char *)calloc(1, SIZEOF_REL_BUFFER);
	rel_buffer_copy = rel_buffer;
	rel_bits = 0;
	rel_pos = 0;

	j = assemble(s, t);
	if (flag_v > 0)
	{
		// puts("Labels:");
		for (i = 0; i < labels; ++i)
			printf("%-24s= %10i ; #%08X\n", &asciz[label[i]], value[i], value[i]);
		// puts("Macros:");
		for (i = 0; i < macros; ++i)
		{
			printf("%s MACRO\n", &asciz[cache[k = macro_source[i]]]);
			while (strcmp(symbol_dollar, r = &asciz[cache[++k]]))
				puts(r);
			puts(" ENDM");
		}
		printf("-- %i labels, %i macros, %i doubts, %i buffer\n", labels, macros, doubts, ascizs);
	}
	return j != 0; // 0 OK, !0 ERROR
}
