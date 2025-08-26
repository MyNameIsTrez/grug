//// GRUG BACKEND LINUX

// TODO: Remove every `tmp_` prefix in this file,
// once this file has been moved to its own dedicated repository.

#include "grug.h"

#include "grug_backend.h"

// TODO: Remove unused includes
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

////// COMPILING

#define backend_error(...) {\
	if (snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__) < 0) {\
		abort();\
	}\
    grug_error_impl(__LINE__);\
	longjmp(backend_error_jmp_buffer, 1);\
}

#define backend_assert(condition, ...) {\
	if (!(condition)) {\
		backend_error(__VA_ARGS__);\
	}\
}

#ifdef CRASH_ON_UNREACHABLE
#define backend_unreachable() {\
	assert(false && "This line of code is supposed to be unreachable. Please report this bug to the grug backend developers!");\
}
#else
#define backend_unreachable() {\
	backend_error("This line of code in grug.c:%d is supposed to be unreachable. Please report this bug to the grug backend developers!", __LINE__);\
}
#endif

#define GAME_FN_PREFIX "game_fn_"

#define MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS 420420
#define MAX_SYMBOLS 420420
#define MAX_CODES 420420
#define MAX_RESOURCE_STRINGS_CHARACTERS 420420
#define MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS 420420
#define MAX_DATA_STRING_CODES 420420
#define MAX_GAME_FN_CALLS 420420
#define MAX_USED_EXTERN_GLOBAL_VARIABLES 420420
#define MAX_HELPER_FN_CALLS 420420
#define MAX_USED_GAME_FNS 420
#define MAX_HELPER_FN_OFFSETS 420420
#define MAX_DATA_STRINGS 420420
#define MAX_RESOURCES 420420
#define MAX_LOOP_DEPTH 420
#define MAX_BREAK_STATEMENTS_PER_LOOP 420
#define MAX_ENTITY_DEPENDENCIES 420420

#define NEXT_INSTRUCTION_OFFSET sizeof(u32)

#define GLOBAL_VARIABLES_POINTER_SIZE sizeof(void *)

#define BFD_HASH_BUCKET_SIZE 4051 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l345

// 0xDEADBEEF in little-endian
#define PLACEHOLDER_8 0xDE
#define PLACEHOLDER_16 0xADDE
#define PLACEHOLDER_32 0xEFBEADDE
#define PLACEHOLDER_64 0xEFBEADDEEFBEADDE

// We use a limit of 64 KiB, since native JNI methods can use up to 80 KiB
// without a risk of a JVM crash:
// See https://pangin.pro/posts/stack-overflow-handling
#define GRUG_STACK_LIMIT 0x10000

// Start of code enums

#define XOR_EAX_BY_N 0x35 // xor eax, n

#define CMP_EAX_WITH_N 0x3d // cmp eax, n

#define PUSH_RAX 0x50 // push rax
#define PUSH_RBP 0x55 // push rbp

#define POP_RAX 0x58 // pop rax
#define POP_RCX 0x59 // pop rcx
#define POP_RDX 0x5a // pop rdx
#define POP_RBP 0x5d // pop rbp
#define POP_RSI 0x5e // pop rsi
#define POP_RDI 0x5f // pop rdi

#define PUSH_32_BITS 0x68 // push n

#define JE_8_BIT_OFFSET 0x74 // je $+n
#define JNE_8_BIT_OFFSET 0x75 // jne $+n
#define JG_8_BIT_OFFSET 0x7f // jg $+n

#define MOV_DEREF_RAX_TO_AL 0x8a // mov al, [rax]

#define NOP_8_BITS 0x90 // nop

#define CDQ_SIGN_EXTEND_EAX_BEFORE_DIVISION 0x99 // cdq

#define MOV_TO_EAX 0xb8 // mov eax, n
#define MOV_TO_EDI 0xbf // mov edi, n

#define RET 0xc3 // ret

#define MOV_8_BIT_TO_DEREF_RAX 0xc6 // mov [rax], byte n

#define CALL 0xe8 // call a function

#define JMP_32_BIT_OFFSET 0xe9 // jmp $+n

#define JNO_8_BIT_OFFSET 0x71 // jno $+n

#define JMP_REL 0x25ff // Not quite jmp [$+n]
#define PUSH_REL 0x35ff // Not quite push qword [$+n]

#define MOV_DEREF_RAX_TO_EAX_8_BIT_OFFSET 0x408b // mov eax, rax[n]
#define MOV_DEREF_RBP_TO_EAX_8_BIT_OFFSET 0x458b // mov eax, rbp[n]
#define MOV_DEREF_RBP_TO_EAX_32_BIT_OFFSET 0x858b // mov eax, rbp[n]

#define MOV_AL_TO_DEREF_RBP_8_BIT_OFFSET 0x4588 // mov rbp[n], al
#define MOV_EAX_TO_DEREF_RBP_8_BIT_OFFSET 0x4589 // mov rbp[n], eax
#define MOV_ECX_TO_DEREF_RBP_8_BIT_OFFSET 0x4d89 // mov rbp[n], ecx
#define MOV_EDX_TO_DEREF_RBP_8_BIT_OFFSET 0x5589 // mov rbp[n], edx

#define POP_R8 0x5841 // pop r8
#define POP_R9 0x5941 // pop r9
#define POP_R11 0x5b41 // pop r11

#define MOV_ESI_TO_DEREF_RBP_8_BIT_OFFSET 0x7589 // mov rbp[n], esi
#define MOV_DEREF_RAX_TO_EAX_32_BIT_OFFSET 0x808b // mov eax, rax[n]
#define JE_32_BIT_OFFSET 0x840f // je strict $+n
#define MOV_AL_TO_DEREF_RBP_32_BIT_OFFSET 0x8588 // mov rbp[n], al
#define MOV_EAX_TO_DEREF_RBP_32_BIT_OFFSET 0x8589 // mov rbp[n], eax
#define MOV_ECX_TO_DEREF_RBP_32_BIT_OFFSET 0x8d89 // mov rbp[n], ecx
#define MOV_EDX_TO_DEREF_RBP_32_BIT_OFFSET 0x9589 // mov rbp[n], edx
#define MOV_ESI_TO_DEREF_RBP_32_BIT_OFFSET 0xb589 // mov rbp[n], esi
#define XOR_CLEAR_EAX 0xc031 // xor eax, eax

#define TEST_AL_IS_ZERO 0xc084 // test al, al
#define TEST_EAX_IS_ZERO 0xc085 // test eax, eax

#define NEGATE_EAX 0xd8f7 // neg eax

#define MOV_GLOBAL_VARIABLE_TO_RAX 0x58b48 // mov rax, [rel foo wrt ..got]

#define LEA_STRINGS_TO_RAX 0x58d48 // lea rax, strings[rel n]

#define MOV_R11_TO_DEREF_RAX 0x18894c // mov [rax], r11
#define MOV_DEREF_R11_TO_R11B 0x1b8a45 // mov r11b, [r11]
#define MOV_GLOBAL_VARIABLE_TO_R11 0x1d8b4c // mov r11, [rel foo wrt ..got]
#define LEA_STRINGS_TO_R11 0x1d8d4c // lea r11, strings[rel n]
#define CMP_RSP_WITH_RAX 0xc43948 // cmp rsp, rax
#define MOV_RSP_TO_DEREF_RAX 0x208948 // mov [rax], rsp

#define SUB_DEREF_RAX_32_BITS 0x288148 // sub qword [rax], n

#define MOV_RSI_TO_DEREF_RDI 0x378948 // mov rdi[0x0], rsi

#define NOP_32_BITS 0x401f0f // There isn't a nasm equivalent

#define MOV_DEREF_RAX_TO_RAX_8_BIT_OFFSET 0x408b48 // mov rax, rax[n]

#define MOVZX_BYTE_DEREF_RAX_TO_EAX_8_BIT_OFFSET 0x40b60f // movzx eax, byte rax[n]

#define MOV_AL_TO_DEREF_R11_8_BIT_OFFSET 0x438841 // mov r11[n], al
#define MOV_EAX_TO_DEREF_R11_8_BIT_OFFSET 0x438941 // mov r11[n], eax
#define MOV_R8D_TO_DEREF_RBP_8_BIT_OFFSET 0x458944 // mov rbp[n], r8d
#define MOV_RAX_TO_DEREF_RBP_8_BIT_OFFSET 0x458948 // mov rbp[n], rax
#define MOV_RAX_TO_DEREF_R11_8_BIT_OFFSET 0x438949 // mov r11[n], rax
#define MOV_R8_TO_DEREF_RBP_8_BIT_OFFSET 0x45894c // mov rbp[n], r8

#define MOV_DEREF_RBP_TO_RAX_8_BIT_OFFSET 0x458b48 // mov rax, rbp[n]

#define MOVZX_BYTE_DEREF_RBP_TO_EAX_8_BIT_OFFSET 0x45b60f // movzx eax, byte rbp[n]

#define MOV_R9D_TO_DEREF_RBP_8_BIT_OFFSET 0x4d8944 // mov rbp[n], r9d
#define MOV_RCX_TO_DEREF_RBP_8_BIT_OFFSET 0x4d8948 // mov rbp[n], rcx
#define MOV_R9_TO_DEREF_RBP_8_BIT_OFFSET 0x4d894c // mov rbp[n], r9
#define MOV_RDX_TO_DEREF_RBP_8_BIT_OFFSET 0x558948 // mov rbp[n], rdx

#define MOV_DEREF_RBP_TO_R11_8_BIT_OFFSET 0x5d8b4c // mov r11, rbp[n]

#define MOV_RSI_TO_DEREF_RBP_8_BIT_OFFSET 0x758948 // mov rbp[n], rsi

#define MOV_RDI_TO_DEREF_RBP_8_BIT_OFFSET 0x7d8948 // mov rbp[n], rdi
#define MOVZX_BYTE_DEREF_RAX_TO_EAX_32_BIT_OFFSET 0x80b60f // movzx eax, byte rax[n]
#define MOV_DEREF_RAX_TO_RAX_32_BIT_OFFSET 0x808b48 // mov rax, rax[n]
#define MOV_AL_TO_DEREF_R11_32_BIT_OFFSET 0x838841 // mov r11[n], al
#define MOV_EAX_TO_DEREF_R11_32_BIT_OFFSET 0x838941 // mov r11[n], eax
#define MOV_RAX_TO_DEREF_R11_32_BIT_OFFSET 0x838949 // mov r11[n], rax
#define MOV_R8D_TO_DEREF_RBP_32_BIT_OFFSET 0x858944 // mov rbp[n], r8d
#define MOV_RAX_TO_DEREF_RBP_32_BIT_OFFSET 0x858948 // mov rbp[n], rax
#define MOV_R8_TO_DEREF_RBP_32_BIT_OFFSET 0x85894c // mov rbp[n], r8
#define MOV_DEREF_RBP_TO_RAX_32_BIT_OFFSET 0x858b48 // mov rax, rbp[n]
#define MOVZX_BYTE_DEREF_RBP_TO_EAX_32_BIT_OFFSET 0x85b60f // movzx eax, byte rbp[n]
#define MOV_R9D_TO_DEREF_RBP_32_BIT_OFFSET 0x8d8944 // mov rbp[n], r9d
#define MOV_RCX_TO_DEREF_RBP_32_BIT_OFFSET 0x8d8948 // mov rbp[n], rcx
#define MOV_R9_TO_DEREF_RBP_32_BIT_OFFSET 0x8d894c // mov rbp[n], r9
#define MOV_RDX_TO_DEREF_RBP_32_BIT_OFFSET 0x958948 // mov rbp[n], rdx
#define MOV_RSI_TO_DEREF_RBP_32_BIT_OFFSET 0xb58948 // mov rbp[n], rsi

#define SETB_AL 0xc0920f // setb al (set if below)
#define SETAE_AL 0xc0930f // setae al (set if above or equal)
#define SETE_AL 0xc0940f // sete al
#define SETNE_AL 0xc0950f // setne al
#define SETBE_AL 0xc0960f // setbe al (set if below or equal)
#define SETA_AL 0xc0970f // seta al (set if above)
#define SETGT_AL 0xc09f0f // setg al
#define SETGE_AL 0xc09d0f // setge al
#define SETLT_AL 0xc09c0f // setl al
#define SETLE_AL 0xc09e0f // setle al

// See this for an explanation of "ordered" vs. "unordered":
// https://stackoverflow.com/a/8627368/13279557
#define ORDERED_CMP_XMM0_WITH_XMM1 0xc12f0f // comiss xmm0, xmm1

#define ADD_RSP_32_BITS 0xc48148 // add rsp, n
#define ADD_RSP_8_BITS 0xc48348 // add rsp, n
#define MOV_RAX_TO_RDI 0xc78948 // mov rdi, rax
#define MOV_RDX_TO_RAX 0xd08948 // mov rax, rdx
#define ADD_R11D_TO_EAX 0xd80144 // add eax, r11d
#define SUB_R11D_FROM_EAX 0xd82944 // sub eax, r11d
#define CMP_EAX_WITH_R11D 0xd83944 // cmp eax, r11d
#define CMP_RAX_WITH_R11 0xd8394c // cmp rax, r11
#define TEST_R11B_IS_ZERO 0xdb8445 // test r11b, r11b
#define TEST_R11_IS_ZERO 0xdb854d // test r11, r11
#define MOV_R11_TO_RSI 0xde894c // mov rsi, r11

#define MOV_RSP_TO_RBP 0xe58948 // mov rbp, rsp

#define IMUL_EAX_BY_R11D 0xebf741 // imul r11d

#define SUB_RSP_8_BITS 0xec8348 // sub rsp, n
#define SUB_RSP_32_BITS 0xec8148 // sub rsp, n

#define MOV_RBP_TO_RSP 0xec8948 // mov rsp, rbp

#define CMP_R11D_WITH_N 0xfb8141 // mov r11d, n

#define DIV_RAX_BY_R11D 0xfbf741 // idiv r11d

#define MOV_XMM0_TO_DEREF_RBP_8_BIT_OFFSET 0x45110ff3 // movss rbp[n], xmm0
#define MOV_XMM1_TO_DEREF_RBP_8_BIT_OFFSET 0x4d110ff3 // movss rbp[n], xmm1
#define MOV_XMM2_TO_DEREF_RBP_8_BIT_OFFSET 0x55110ff3 // movss rbp[n], xmm2
#define MOV_XMM3_TO_DEREF_RBP_8_BIT_OFFSET 0x5d110ff3 // movss rbp[n], xmm3
#define MOV_XMM4_TO_DEREF_RBP_8_BIT_OFFSET 0x65110ff3 // movss rbp[n], xmm4
#define MOV_XMM5_TO_DEREF_RBP_8_BIT_OFFSET 0x6d110ff3 // movss rbp[n], xmm5
#define MOV_XMM6_TO_DEREF_RBP_8_BIT_OFFSET 0x75110ff3 // movss rbp[n], xmm6
#define MOV_XMM7_TO_DEREF_RBP_8_BIT_OFFSET 0x7d110ff3 // movss rbp[n], xmm7

#define MOV_XMM0_TO_DEREF_RBP_32_BIT_OFFSET 0x85110ff3 // movss rbp[n], xmm0
#define MOV_XMM1_TO_DEREF_RBP_32_BIT_OFFSET 0x8d110ff3 // movss rbp[n], xmm1
#define MOV_XMM2_TO_DEREF_RBP_32_BIT_OFFSET 0x95110ff3 // movss rbp[n], xmm2
#define MOV_XMM3_TO_DEREF_RBP_32_BIT_OFFSET 0x9d110ff3 // movss rbp[n], xmm3
#define MOV_XMM4_TO_DEREF_RBP_32_BIT_OFFSET 0xa5110ff3 // movss rbp[n], xmm4
#define MOV_XMM5_TO_DEREF_RBP_32_BIT_OFFSET 0xad110ff3 // movss rbp[n], xmm5
#define MOV_XMM6_TO_DEREF_RBP_32_BIT_OFFSET 0xb5110ff3 // movss rbp[n], xmm6
#define MOV_XMM7_TO_DEREF_RBP_32_BIT_OFFSET 0xbd110ff3 // movss rbp[n], xmm7

#define MOV_EAX_TO_XMM0 0xc06e0f66 // movd xmm0, eax
#define MOV_XMM0_TO_EAX 0xc07e0f66 // movd eax, xmm0

#define ADD_XMM1_TO_XMM0 0xc1580ff3 // addss xmm0, xmm1
#define MUL_XMM0_WITH_XMM1 0xc1590ff3 // mulss xmm0, xmm1
#define SUB_XMM1_FROM_XMM0 0xc15c0ff3 // subss xmm0, xmm1
#define DIV_XMM0_BY_XMM1 0xc15e0ff3 // divss xmm0, xmm1

#define MOV_EAX_TO_XMM1 0xc86e0f66 // movd xmm1, eax
#define MOV_EAX_TO_XMM2 0xd06e0f66 // movd xmm2, eax
#define MOV_EAX_TO_XMM3 0xd86e0f66 // movd xmm3, eax
#define MOV_EAX_TO_XMM4 0xe06e0f66 // movd xmm4, eax
#define MOV_EAX_TO_XMM5 0xe86e0f66 // movd xmm5, eax
#define MOV_EAX_TO_XMM6 0xf06e0f66 // movd xmm6, eax
#define MOV_EAX_TO_XMM7 0xf86e0f66 // movd xmm7, eax

#define MOV_R11D_TO_XMM1 0xcb6e0f4166 // movd xmm1, r11d

// End of code enums

struct data_string_code {
	const char *string;
	size_t code_offset;
};

static size_t text_offsets[MAX_SYMBOLS];

static u8 codes[MAX_CODES];
static size_t codes_size;

static char resource_strings[MAX_RESOURCE_STRINGS_CHARACTERS];
static size_t resource_strings_size;

static char entity_dependency_strings[MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS];
static size_t entity_dependency_strings_size;

static struct data_string_code data_string_codes[MAX_DATA_STRING_CODES];
static size_t data_string_codes_size;

struct offset {
	const char *name;
	size_t offset;
};
static struct offset extern_fn_calls[MAX_GAME_FN_CALLS];
static size_t extern_fn_calls_size;
static struct offset helper_fn_calls[MAX_HELPER_FN_CALLS];
static size_t helper_fn_calls_size;

struct used_extern_global_variable {
	const char *variable_name;
	size_t codes_offset;
};
static struct used_extern_global_variable used_extern_global_variables[MAX_USED_EXTERN_GLOBAL_VARIABLES];
static size_t used_extern_global_variables_size;

static const char *used_extern_fns[MAX_USED_GAME_FNS];
static size_t extern_fns_size;
static u32 buckets_used_extern_fns[BFD_HASH_BUCKET_SIZE];
static u32 chains_used_extern_fns[MAX_USED_GAME_FNS];

static char used_extern_fn_symbols[MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS];
static size_t used_extern_fn_symbols_size;

static struct offset helper_fn_offsets[MAX_HELPER_FN_OFFSETS];
static size_t helper_fn_offsets_size;
static u32 buckets_helper_fn_offsets[MAX_HELPER_FN_OFFSETS];
static u32 chains_helper_fn_offsets[MAX_HELPER_FN_OFFSETS];

static size_t pushed;

static size_t start_of_loop_jump_offsets[MAX_LOOP_DEPTH];
struct loop_break_statements {
	size_t break_statements[MAX_BREAK_STATEMENTS_PER_LOOP];
	size_t break_statements_size;
};
static struct loop_break_statements loop_break_statements_stack[MAX_LOOP_DEPTH];
static size_t loop_depth;

static u32 resources[MAX_RESOURCES];
static size_t resources_size;

static u32 entity_dependencies[MAX_ENTITY_DEPENDENCIES];
static size_t entity_dependencies_size;

static bool compiling_fast_mode;

static bool compiled_init_globals_fn;

static bool is_runtime_error_handler_used;

static char helper_fn_mode_names[MAX_HELPER_FN_MODE_NAMES_CHARACTERS];
static size_t helper_fn_mode_names_size;

static const char *current_grug_path;
static const char *current_fn_name;

static size_t stack_frame_bytes;
static size_t max_stack_frame_bytes;

static size_t tmp_type_sizes[] = {
	[type_bool] = sizeof(bool),
	[type_i32] = sizeof(i32),
	[type_f32] = sizeof(float),
	[type_string] = sizeof(const char *),
	[type_id] = sizeof(u64),
	[type_resource] = sizeof(const char *),
	[type_entity] = sizeof(const char *),
};

static struct variable tmp_variables[MAX_VARIABLES_PER_FUNCTION];
static size_t tmp_variables_size;
static u32 tmp_buckets_variables[MAX_VARIABLES_PER_FUNCTION];
static u32 tmp_chains_variables[MAX_VARIABLES_PER_FUNCTION];

static u32 entity_types[MAX_ENTITY_DEPENDENCIES];
static size_t entity_types_size;

static const char *data_strings[MAX_DATA_STRINGS];
static size_t data_strings_size;

static u32 buckets_data_strings[MAX_DATA_STRINGS];
static u32 chains_data_strings[MAX_DATA_STRINGS];

static jmp_buf backend_error_jmp_buffer;

static struct grug_ast ast;

static void reset_compiling(void) {
	codes_size = 0;
	resource_strings_size = 0;
	entity_dependency_strings_size = 0;
	data_string_codes_size = 0;
	extern_fn_calls_size = 0;
	helper_fn_calls_size = 0;
	used_extern_global_variables_size = 0;
	extern_fns_size = 0;
	used_extern_fn_symbols_size = 0;
	helper_fn_offsets_size = 0;
	loop_depth = 0;
	resources_size = 0;
	entity_dependencies_size = 0;
	compiling_fast_mode = false;
	compiled_init_globals_fn = false;
	is_runtime_error_handler_used = false;
	helper_fn_mode_names_size = 0;
	entity_types_size = 0;
	data_strings_size = 0;
	memset(buckets_data_strings, 0xff, sizeof(buckets_data_strings));
}

static const char *get_helper_fn_mode_name(const char *name, bool safe) {
	size_t length = strlen(name);

	backend_assert(helper_fn_mode_names_size + length + (sizeof("_safe") - 1) < MAX_HELPER_FN_MODE_NAMES_CHARACTERS, "There are more than %d characters in the helper_fn_mode_names array, exceeding MAX_HELPER_FN_MODE_NAMES_CHARACTERS", MAX_HELPER_FN_MODE_NAMES_CHARACTERS);

	const char *mode_name = helper_fn_mode_names + helper_fn_mode_names_size;

	memcpy(helper_fn_mode_names + helper_fn_mode_names_size, name, length);
	helper_fn_mode_names_size += length;

	memcpy(helper_fn_mode_names + helper_fn_mode_names_size, safe ? "_safe" : "_fast", 6);
	helper_fn_mode_names_size += 6;

	return mode_name;
}

static const char *get_fast_helper_fn_name(const char *name) {
	return get_helper_fn_mode_name(name, false);
}

static const char *get_safe_helper_fn_name(const char *name) {
	return get_helper_fn_mode_name(name, true);
}

static size_t get_helper_fn_offset(const char *name) {
	assert(helper_fn_offsets_size > 0);

	u32 i = buckets_helper_fn_offsets[elf_hash(name) % helper_fn_offsets_size];

	while (true) {
		assert(i != UINT32_MAX && "get_helper_fn_offset() is supposed to never fail");

		if (streq(name, helper_fn_offsets[i].name)) {
			break;
		}

		i = chains_helper_fn_offsets[i];
	}

	return helper_fn_offsets[i].offset;
}

static void hash_helper_fn_offsets(void) {
	memset(buckets_helper_fn_offsets, 0xff, helper_fn_offsets_size * sizeof(u32));

	for (size_t i = 0; i < helper_fn_offsets_size; i++) {
		const char *name = helper_fn_offsets[i].name;

		u32 bucket_index = elf_hash(name) % helper_fn_offsets_size;

		chains_helper_fn_offsets[i] = buckets_helper_fn_offsets[bucket_index];

		buckets_helper_fn_offsets[bucket_index] = i;
	}
}

static void push_helper_fn_offset(const char *fn_name, size_t offset) {
	backend_assert(helper_fn_offsets_size < MAX_HELPER_FN_OFFSETS, "There are more than %d helper functions, exceeding MAX_HELPER_FN_OFFSETS", MAX_HELPER_FN_OFFSETS);

	helper_fn_offsets[helper_fn_offsets_size++] = (struct offset){
		.name = fn_name,
		.offset = offset,
	};
}

// This is solely here to put the symbols in the same weird order as ld does
// From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l508
static unsigned long bfd_hash(const char *string) {
	const unsigned char *s;
	unsigned long hash;
	unsigned int len;
	unsigned int c;

	hash = 0;
	s = (const unsigned char *) string;
	while ((c = *s++) != '\0') {
		hash += c + (c << 17);
		hash ^= hash >> 2;
	}
	len = (s - (const unsigned char *) string) - 1;
	hash += len + (len << 17);
	hash ^= hash >> 2;
	return hash;
}

static bool has_used_extern_fn(const char *name) {
	u32 i = buckets_used_extern_fns[bfd_hash(name) % BFD_HASH_BUCKET_SIZE];

	while (true) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (streq(name, used_extern_fns[i])) {
			break;
		}

		i = chains_used_extern_fns[i];
	}

	return true;
}

static void hash_used_extern_fns(void) {
	memset(buckets_used_extern_fns, 0xff, sizeof(buckets_used_extern_fns));

	for (size_t i = 0; i < extern_fn_calls_size; i++) {
		const char *name = extern_fn_calls[i].name;

		if (has_used_extern_fn(name)) {
			continue;
		}

		used_extern_fns[extern_fns_size] = name;

		u32 bucket_index = bfd_hash(name) % BFD_HASH_BUCKET_SIZE;

		chains_used_extern_fns[extern_fns_size] = buckets_used_extern_fns[bucket_index];

		buckets_used_extern_fns[bucket_index] = extern_fns_size++;
	}
}

static void push_helper_fn_call(const char *fn_name, size_t codes_offset) {
	backend_assert(helper_fn_calls_size < MAX_HELPER_FN_CALLS, "There are more than %d helper function calls, exceeding MAX_HELPER_FN_CALLS", MAX_HELPER_FN_CALLS);

	helper_fn_calls[helper_fn_calls_size++] = (struct offset){
		.name = fn_name,
		.offset = codes_offset,
	};
}

static const char *push_used_extern_fn_symbol(const char *name, bool is_game_fn) {
	size_t length = strlen(name);
	size_t fn_prefix_length = is_game_fn ? sizeof(GAME_FN_PREFIX) - 1 : 0;

	backend_assert(used_extern_fn_symbols_size + fn_prefix_length + length < MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS, "There are more than %d characters in the used_extern_fn_symbols array, exceeding MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS", MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS);

	char *symbol = used_extern_fn_symbols + used_extern_fn_symbols_size;

	if (is_game_fn) {
		memcpy(symbol, GAME_FN_PREFIX, fn_prefix_length);
		used_extern_fn_symbols_size += fn_prefix_length;
	}

	for (size_t i = 0; i < length; i++) {
		used_extern_fn_symbols[used_extern_fn_symbols_size++] = name[i];
	}
	used_extern_fn_symbols[used_extern_fn_symbols_size++] = '\0';

	return symbol;
}

static void push_extern_fn_call(const char *fn_name, size_t codes_offset, bool is_game_fn) {
	backend_assert(extern_fn_calls_size < MAX_GAME_FN_CALLS, "There are more than %d game function calls, exceeding MAX_GAME_FN_CALLS", MAX_GAME_FN_CALLS);

	extern_fn_calls[extern_fn_calls_size++] = (struct offset){
		.name = push_used_extern_fn_symbol(fn_name, is_game_fn),
		.offset = codes_offset,
	};
}

static void push_game_fn_call(const char *fn_name, size_t codes_offset) {
	push_extern_fn_call(fn_name, codes_offset, true);
}

static void push_system_fn_call(const char *fn_name, size_t codes_offset) {
	push_extern_fn_call(fn_name, codes_offset, false);
}

static void push_data_string_code(const char *string, size_t code_offset) {
	backend_assert(data_string_codes_size < MAX_DATA_STRING_CODES, "There are more than %d data string code bytes, exceeding MAX_DATA_STRING_CODES", MAX_DATA_STRING_CODES);

	data_string_codes[data_string_codes_size++] = (struct data_string_code){
		.string = string,
		.code_offset = code_offset,
	};
}

static void compile_byte(u8 byte) {
	backend_assert(codes_size < MAX_CODES, "There are more than %d code bytes, exceeding MAX_CODES", MAX_CODES);

	codes[codes_size++] = byte;
}

static void compile_padded(u64 n, size_t byte_count) {
	while (byte_count-- > 0) {
		compile_byte(n & 0xff); // Little-endian
		n >>= 8;
	}
}

static void compile_16(u16 n) {
	compile_padded(n, sizeof(u16));
}

static void compile_32(u32 n) {
	compile_padded(n, sizeof(u32));
}

static void compile_unpadded(u64 n) {
	while (n > 0) {
		compile_byte(n & 0xff); // Little-endian
		n >>= 8;
	}
}

static void overwrite_jmp_address_8(size_t jump_address, size_t size) {
	assert(size > jump_address);
	u8 n = size - (jump_address + 1);
	codes[jump_address] = n;
}

static void overwrite_jmp_address_32(size_t jump_address, size_t size) {
	assert(size > jump_address);
	size_t byte_count = 4;
	for (u32 n = size - (jump_address + byte_count); byte_count > 0; n >>= 8, byte_count--) {
		codes[jump_address++] = n & 0xff; // Little-endian
	}
}

static void stack_pop_r11(void) {
	compile_unpadded(POP_R11);
	stack_frame_bytes -= sizeof(u64);

	assert(pushed > 0);
	pushed--;
}

static void stack_push_rax(void) {
	compile_byte(PUSH_RAX);
	stack_frame_bytes += sizeof(u64);

	pushed++;
}

static struct variable *tmp_get_local_variable(const char *name) {
	if (tmp_variables_size == 0) {
		return NULL;
	}

	u32 i = tmp_buckets_variables[elf_hash(name) % MAX_VARIABLES_PER_FUNCTION];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		// When a scope block is exited, the local variables in it aren't reachable anymore.
		// It is possible for a new local variable with the same name to be added after the block,
		// which is why we still keep looping.
		if (streq(name, tmp_variables[i].name) && tmp_variables[i].offset != SIZE_MAX) {
			break;
		}

		i = tmp_chains_variables[i];
	}

	return tmp_variables + i;
}

static void tmp_add_local_variable(const char *name, enum type type, const char *type_name) {
	// TODO: Print the exact grug file path, function and line number
	backend_assert(tmp_variables_size < MAX_VARIABLES_PER_FUNCTION, "There are more than %d variables in a function, exceeding MAX_VARIABLES_PER_FUNCTION", MAX_VARIABLES_PER_FUNCTION);

	backend_assert(!tmp_get_local_variable(name), "The local variable '%s' shadows an earlier local variable with the same name, so change the name of one of them", name);
	backend_assert(!get_global_variable(name), "The local variable '%s' shadows an earlier global variable with the same name, so change the name of one of them", name);

	stack_frame_bytes += tmp_type_sizes[type];

	tmp_variables[tmp_variables_size] = (struct variable){
		.name = name,
		.type = type,
		.type_name = type_name,

		// This field is used to track the stack location of a local variable.
		.offset = stack_frame_bytes,
	};

	u32 bucket_index = elf_hash(name) % MAX_VARIABLES_PER_FUNCTION;

	tmp_chains_variables[tmp_variables_size] = tmp_buckets_variables[bucket_index];

	tmp_buckets_variables[bucket_index] = tmp_variables_size++;
}

static void move_arguments(struct argument *fn_arguments, size_t argument_count) {
	size_t integer_argument_index = 0;
	size_t float_argument_index = 0;

	// Every function starts with `push rbp`, `mov rbp, rsp`,
	// so because calling a function always pushes the return address (8 bytes),
	// and the `push rbp` also pushes 8 bytes, the spilled args start at `rbp-0x10`
	size_t spill_offset = 0x10;

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		struct argument arg = fn_arguments[argument_index];

		size_t offset = tmp_get_local_variable(arg.name)->offset;

		// We skip EDI/RDI, since that is reserved by the secret global variables pointer
		switch (arg.type) {
			case type_void:
			case type_resource:
			case type_entity:
				backend_unreachable();
			case type_bool:
			case type_i32:
				if (integer_argument_index < 5) {
					if (offset <= 0x80) {
						compile_unpadded((u32[]){
							MOV_ESI_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_EDX_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_ECX_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_R8D_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_R9D_TO_DEREF_RBP_8_BIT_OFFSET,
						}[integer_argument_index++]);
						compile_byte(-offset);
					} else {
						// Reached by tests/ok/spill_args_to_helper_fn_32_bit_i32

						compile_unpadded((u32[]){
							MOV_ESI_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_EDX_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_ECX_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_R8D_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_R9D_TO_DEREF_RBP_32_BIT_OFFSET,
						}[integer_argument_index++]);
						compile_32(-offset);
					}
				} else {
					// Reached by tests/ok/spill_args_to_helper_fn

					compile_unpadded(MOV_DEREF_RBP_TO_EAX_32_BIT_OFFSET);
					compile_32(spill_offset);
					spill_offset += sizeof(u64);

					compile_unpadded(MOV_EAX_TO_DEREF_RBP_32_BIT_OFFSET);
					compile_32(-offset);
				}
				break;
			case type_f32:
				if (float_argument_index < 8) {
					if (offset <= 0x80) {
						compile_unpadded((u32[]){
							MOV_XMM0_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM1_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM2_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM3_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM4_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM5_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM6_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_XMM7_TO_DEREF_RBP_8_BIT_OFFSET,
						}[float_argument_index++]);
						compile_byte(-offset);
					} else {
						// Reached by tests/ok/spill_args_to_helper_fn_32_bit_f32

						compile_unpadded((u32[]){
							MOV_XMM0_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM1_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM2_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM3_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM4_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM5_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM6_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_XMM7_TO_DEREF_RBP_32_BIT_OFFSET,
						}[float_argument_index++]);
						compile_32(-offset);
					}
				} else {
					// Reached by tests/ok/spill_args_to_helper_fn

					compile_unpadded(MOV_DEREF_RBP_TO_EAX_32_BIT_OFFSET);
					compile_32(spill_offset);
					spill_offset += sizeof(u64);

					compile_unpadded(MOV_EAX_TO_DEREF_RBP_32_BIT_OFFSET);
					compile_32(-offset);
				}
				break;
			case type_string:
			case type_id:
				if (integer_argument_index < 5) {
					if (offset <= 0x80) {
						compile_unpadded((u32[]){
							MOV_RSI_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_RDX_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_RCX_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_R8_TO_DEREF_RBP_8_BIT_OFFSET,
							MOV_R9_TO_DEREF_RBP_8_BIT_OFFSET,
						}[integer_argument_index++]);
						compile_byte(-offset);
					} else {
						// Reached by tests/ok/spill_args_to_helper_fn_32_bit_string

						compile_unpadded((u32[]){
							MOV_RSI_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_RDX_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_RCX_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_R8_TO_DEREF_RBP_32_BIT_OFFSET,
							MOV_R9_TO_DEREF_RBP_32_BIT_OFFSET,
						}[integer_argument_index++]);
						compile_32(-offset);
					}
				} else {
					// Reached by tests/ok/spill_args_to_helper_fn

					compile_unpadded(MOV_DEREF_RBP_TO_RAX_32_BIT_OFFSET);
					compile_32(spill_offset);
					spill_offset += sizeof(u64);

					compile_unpadded(MOV_RAX_TO_DEREF_RBP_32_BIT_OFFSET);
					compile_32(-offset);
				}
				break;
		}
	}
}

static void push_break_statement_jump_address_offset(size_t offset) {
	backend_assert(loop_depth > 0, "There is a break statement that isn't inside of a while loop");

	struct loop_break_statements *loop_break_statements = &loop_break_statements_stack[loop_depth - 1];

	backend_assert(loop_break_statements->break_statements_size < MAX_BREAK_STATEMENTS_PER_LOOP, "There are more than %d break statements in one of the while loops, exceeding MAX_BREAK_STATEMENTS_PER_LOOP", MAX_BREAK_STATEMENTS_PER_LOOP);

	loop_break_statements->break_statements[loop_break_statements->break_statements_size++] = offset;
}

static void compile_expr(struct expr expr);

static void compile_statements(struct statement *statements_offset, size_t statement_count);

static void compile_function_epilogue(void) {
	compile_unpadded(MOV_RBP_TO_RSP);
	compile_byte(POP_RBP);
	compile_byte(RET);
}

static void push_used_extern_global_variable(const char *variable_name, size_t codes_offset) {
	backend_assert(used_extern_global_variables_size < MAX_USED_EXTERN_GLOBAL_VARIABLES, "There are more than %d usages of game global variables, exceeding MAX_USED_EXTERN_GLOBAL_VARIABLES", MAX_USED_EXTERN_GLOBAL_VARIABLES);

	used_extern_global_variables[used_extern_global_variables_size++] = (struct used_extern_global_variable){
		.variable_name = variable_name,
		.codes_offset = codes_offset,
	};
}

static void compile_runtime_error(enum grug_runtime_error_type type) {
	// mov rax, [rel grug_has_runtime_error_happened wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_RAX);
	push_used_extern_global_variable("grug_has_runtime_error_happened", codes_size);
	compile_32(PLACEHOLDER_32);

	// mov [rax], byte 1:
	compile_16(MOV_8_BIT_TO_DEREF_RAX);
	compile_byte(1);

	// mov edi, type:
	compile_unpadded(MOV_TO_EDI);
	compile_32(type);

	// call grug_call_runtime_error_handler wrt ..plt:
	compile_byte(CALL);
	push_system_fn_call("grug_call_runtime_error_handler", codes_size);
	compile_unpadded(PLACEHOLDER_32);

	compile_function_epilogue();
}

static void compile_return_if_runtime_error(void) {
	// mov r11, [rel grug_has_runtime_error_happened wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_R11);
	push_used_extern_global_variable("grug_has_runtime_error_happened", codes_size);
	compile_unpadded(PLACEHOLDER_32);

	// mov r11b, [r11]:
	compile_unpadded(MOV_DEREF_R11_TO_R11B);

	// test r11b, r11b:
	compile_unpadded(TEST_R11B_IS_ZERO);

	// je %%skip:
	compile_byte(JE_8_BIT_OFFSET);
	size_t skip_offset = codes_size;
	compile_byte(PLACEHOLDER_8);

	compile_function_epilogue();

	// %%skip:
	overwrite_jmp_address_8(skip_offset, codes_size);
}

static void compile_check_game_fn_error(void) {
	// mov r11, [rel grug_has_runtime_error_happened wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_R11);
	push_used_extern_global_variable("grug_has_runtime_error_happened", codes_size);
	compile_unpadded(PLACEHOLDER_32);

	// mov r11b, [r11]:
	compile_unpadded(MOV_DEREF_R11_TO_R11B);

	// test r11b, r11b:
	compile_unpadded(TEST_R11B_IS_ZERO);

	// je %%skip:
	compile_byte(JE_8_BIT_OFFSET);
	size_t skip_offset = codes_size;
	compile_byte(PLACEHOLDER_8);

	// mov edi, GRUG_ON_FN_GAME_FN_ERROR:
	compile_byte(MOV_TO_EDI);
	compile_32(GRUG_ON_FN_GAME_FN_ERROR);

	// call grug_call_runtime_error_handler wrt ..plt:
	compile_byte(CALL);
	push_system_fn_call("grug_call_runtime_error_handler", codes_size);
	compile_unpadded(PLACEHOLDER_32);

	compile_function_epilogue();

	// %%skip:
	overwrite_jmp_address_8(skip_offset, codes_size);
}

static void compile_check_overflow(void) {
	compile_byte(JNO_8_BIT_OFFSET);
	size_t skip_offset = codes_size;
	compile_byte(PLACEHOLDER_8);

	compile_runtime_error(GRUG_ON_FN_OVERFLOW);

	overwrite_jmp_address_8(skip_offset, codes_size);
}

static void compile_check_division_overflow(void) {
	compile_byte(CMP_EAX_WITH_N);
	compile_32(INT32_MIN);

	compile_byte(JNE_8_BIT_OFFSET);
	size_t skip_offset_1 = codes_size;
	compile_byte(PLACEHOLDER_8);

	compile_unpadded(CMP_R11D_WITH_N);
	compile_32(-1);

	compile_byte(JNE_8_BIT_OFFSET);
	size_t skip_offset_2 = codes_size;
	compile_byte(PLACEHOLDER_8);

	compile_runtime_error(GRUG_ON_FN_OVERFLOW);

	overwrite_jmp_address_8(skip_offset_1, codes_size);
	overwrite_jmp_address_8(skip_offset_2, codes_size);
}

static void compile_check_division_by_0(void) {
	compile_unpadded(TEST_R11_IS_ZERO);

	compile_byte(JNE_8_BIT_OFFSET);
	size_t skip_offset = codes_size;
	compile_byte(PLACEHOLDER_8);

	compile_runtime_error(GRUG_ON_FN_DIVISION_BY_ZERO);

	overwrite_jmp_address_8(skip_offset, codes_size);
}

static void compile_check_time_limit_exceeded(void) {
	// call grug_is_time_limit_exceeded wrt ..plt:
	compile_byte(CALL);
	push_system_fn_call("grug_is_time_limit_exceeded", codes_size);
	compile_unpadded(PLACEHOLDER_32);

	// test al, al:
	compile_unpadded(TEST_AL_IS_ZERO);

	// je %%skip:
	compile_byte(JE_8_BIT_OFFSET);
	size_t skip_offset = codes_size;
	compile_byte(PLACEHOLDER_8);

	// runtime_error GRUG_ON_FN_TIME_LIMIT_EXCEEDED
	compile_runtime_error(GRUG_ON_FN_TIME_LIMIT_EXCEEDED);

	// %%skip:
	overwrite_jmp_address_8(skip_offset, codes_size);
}

static void compile_continue_statement(void) {
	backend_assert(loop_depth > 0, "There is a continue statement that isn't inside of a while loop");
	if (!compiling_fast_mode) {
		compile_check_time_limit_exceeded();
	}
	compile_unpadded(JMP_32_BIT_OFFSET);
	size_t start_of_loop_jump_offset = start_of_loop_jump_offsets[loop_depth - 1];
	compile_32(start_of_loop_jump_offset - (codes_size + NEXT_INSTRUCTION_OFFSET));
}

static void compile_clear_has_runtime_error_happened(void) {
	// mov rax, [rel grug_has_runtime_error_happened wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_RAX);
	push_used_extern_global_variable("grug_has_runtime_error_happened", codes_size);
	compile_32(PLACEHOLDER_32);

	// mov [rax], byte 0:
	compile_16(MOV_8_BIT_TO_DEREF_RAX);
	compile_byte(0);
}

static void push_data_string(const char *string) {
	backend_assert(data_strings_size < MAX_DATA_STRINGS, "There are more than %d data strings, exceeding MAX_DATA_STRINGS", MAX_DATA_STRINGS);

	data_strings[data_strings_size++] = string;
}

static u32 get_data_string_index(const char *string) {
	if (data_strings_size == 0) {
		return UINT32_MAX;
	}

	u32 i = buckets_data_strings[elf_hash(string) % MAX_DATA_STRINGS];

	while (true) {
		if (i == UINT32_MAX) {
			return UINT32_MAX;
		}

		if (streq(string, data_strings[i])) {
			break;
		}

		i = chains_data_strings[i];
	}

	return i;
}

static void add_data_string(const char *string) {
	if (get_data_string_index(string) == UINT32_MAX) {
		u32 bucket_index = elf_hash(string) % MAX_DATA_STRINGS;

		chains_data_strings[data_strings_size] = buckets_data_strings[bucket_index];

		buckets_data_strings[bucket_index] = data_strings_size;

		push_data_string(string);
	}
}

static void push_entity_type(const char *entity_type) {
	add_data_string(entity_type);

	backend_assert(entity_types_size < MAX_ENTITY_DEPENDENCIES, "There are more than %d entity types, exceeding MAX_ENTITY_DEPENDENCIES", MAX_ENTITY_DEPENDENCIES);

	entity_types[entity_types_size++] = get_data_string_index(entity_type);
}

static void compile_save_fn_name_and_path(const char *grug_path, const char *fn_name) {
	// mov rax, [rel grug_fn_path wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_RAX);
	push_used_extern_global_variable("grug_fn_path", codes_size);
	compile_32(PLACEHOLDER_32);

	// lea r11, strings[rel n]:
	add_data_string(grug_path);
	compile_unpadded(LEA_STRINGS_TO_R11);
	push_data_string_code(grug_path, codes_size);
	compile_unpadded(PLACEHOLDER_32);

	// mov [rax], r11:
	compile_unpadded(MOV_R11_TO_DEREF_RAX);

	// mov rax, [rel grug_fn_name wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_RAX);
	push_used_extern_global_variable("grug_fn_name", codes_size);
	compile_32(PLACEHOLDER_32);

	// lea r11, strings[rel n]:
	add_data_string(fn_name);
	compile_unpadded(LEA_STRINGS_TO_R11);
	push_data_string_code(fn_name, codes_size);
	compile_unpadded(PLACEHOLDER_32);

	// mov [rax], r11:
	compile_unpadded(MOV_R11_TO_DEREF_RAX);
}

static void compile_while_statement(struct while_statement while_statement) {
	size_t start_of_loop_jump_offset = codes_size;

	backend_assert(loop_depth < MAX_LOOP_DEPTH, "There are more than %d while loops nested inside each other, exceeding MAX_LOOP_DEPTH", MAX_LOOP_DEPTH);
	start_of_loop_jump_offsets[loop_depth] = start_of_loop_jump_offset;
	loop_break_statements_stack[loop_depth].break_statements_size = 0;
	loop_depth++;

	compile_expr(while_statement.condition);
	compile_unpadded(TEST_AL_IS_ZERO);
	compile_unpadded(JE_32_BIT_OFFSET);
	size_t end_jump_offset = codes_size;
	compile_unpadded(PLACEHOLDER_32);

	compile_statements(while_statement.body_statements, while_statement.body_statement_count);

	if (!compiling_fast_mode) {
		compile_check_time_limit_exceeded();
	}

	compile_unpadded(JMP_32_BIT_OFFSET);
	compile_32(start_of_loop_jump_offset - (codes_size + NEXT_INSTRUCTION_OFFSET));

	overwrite_jmp_address_32(end_jump_offset, codes_size);

	struct loop_break_statements *loop_break_statements = &loop_break_statements_stack[loop_depth - 1];

	for (size_t i = 0; i < loop_break_statements->break_statements_size; i++) {
		size_t break_statement_codes_offset = loop_break_statements->break_statements[i];

		overwrite_jmp_address_32(break_statement_codes_offset, codes_size);
	}

	loop_depth--;
}

static void compile_if_statement(struct if_statement if_statement) {
	compile_expr(if_statement.condition);
	compile_unpadded(TEST_AL_IS_ZERO);
	compile_unpadded(JE_32_BIT_OFFSET);
	size_t else_or_end_jump_offset = codes_size;
	compile_unpadded(PLACEHOLDER_32);
	compile_statements(if_statement.if_body_statements, if_statement.if_body_statement_count);

	if (if_statement.else_body_statement_count > 0) {
		compile_unpadded(JMP_32_BIT_OFFSET);
		size_t skip_else_jump_offset = codes_size;
		compile_unpadded(PLACEHOLDER_32);

		overwrite_jmp_address_32(else_or_end_jump_offset, codes_size);

		compile_statements(if_statement.else_body_statements, if_statement.else_body_statement_count);

		overwrite_jmp_address_32(skip_else_jump_offset, codes_size);
	} else {
		overwrite_jmp_address_32(else_or_end_jump_offset, codes_size);
	}
}

static void compile_check_stack_overflow(void) {
	// call grug_get_max_rsp wrt ..plt:
	compile_byte(CALL);
	push_system_fn_call("grug_get_max_rsp", codes_size);
	compile_unpadded(PLACEHOLDER_32);

	// cmp rsp, rax:
	compile_unpadded(CMP_RSP_WITH_RAX);

	// jg $+0xn:
	compile_byte(JG_8_BIT_OFFSET);
	size_t skip_offset = codes_size;
	compile_byte(PLACEHOLDER_8);

	compile_runtime_error(GRUG_ON_FN_STACK_OVERFLOW);

	overwrite_jmp_address_8(skip_offset, codes_size);
}

static void compile_call_expr(struct call_expr call_expr) {
	const char *fn_name = call_expr.fn_name;

	struct helper_fn *helper_fn = get_helper_fn(fn_name);
	bool calls_helper_fn = helper_fn != NULL;

	// `integer` here refers to the classification type:
	// "integer types and pointers which use the general purpose registers"
	// See https://stackoverflow.com/a/57861992/13279557
	size_t integer_argument_count = 0;
	if (calls_helper_fn) {
		integer_argument_count++;
	}

	size_t float_argument_count = 0;

	for (size_t i = 0; i < call_expr.argument_count; i++) {
		struct expr argument = call_expr.arguments[i];

		if (argument.result_type == type_f32) {
			float_argument_count++;
		} else {
			integer_argument_count++;
		}
	}

	size_t pushes = 0;
	if (float_argument_count > 8) {
		pushes += float_argument_count - 8;
	}
	if (integer_argument_count > 6) {
		pushes += integer_argument_count - 6;
	}

	// The reason that we increment `pushed` by `pushes` here,
	// instead of just doing it after the below `stack_push_rax()` calls,
	// is because we need to know *right now* whether SUB_RSP_8_BITS needs to be emitted.
	pushed += pushes;

	// Ensures the call will be 16-byte aligned, even when there are local variables.
	// We add `pushes` instead of `argument_count`,
	// because the arguments that don't spill onto the stack will get popped
	// into their registers (rdi, rsi, etc.) before the CALL instruction.
	bool requires_padding = pushed % 2 == 1;
	if (requires_padding) {
		compile_unpadded(SUB_RSP_8_BITS);
		compile_byte(sizeof(u64));
		stack_frame_bytes += sizeof(u64);
	}

	// We need to restore the balance,
	// as the below `stack_push_rax()` calls also increment `pushed`.
	pushed -= pushes;

	// These are 1-based indices that ensure
	// we don't push the args twice that end up on the stack
	// See tests/ok/spill_args_to_game_fn/input.s in the grug-tests repository,
	// as it calls motherload(1, 2, 3, 4, 5, 6, 7, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, me, 9.0)
	size_t float_pos = call_expr.argument_count;
	size_t integer_pos = call_expr.argument_count;

	// Pushing the args that spill onto the stack
	for (size_t i = call_expr.argument_count; i > 0; i--) {
		struct expr argument = call_expr.arguments[i - 1];

		if (argument.result_type == type_f32) {
			if (float_argument_count > 8) {
				float_argument_count--;
				float_pos = i - 1;
				compile_expr(argument);
				stack_push_rax();
			}
		} else if (integer_argument_count > 6) {
			integer_argument_count--;
			integer_pos = i - 1;
			compile_expr(argument);
			stack_push_rax();
		}
	}
	assert(integer_argument_count <= 6);
	assert(float_argument_count <= 8);

	// Pushing the args that *don't* spill onto the stack
	for (size_t i = call_expr.argument_count; i > 0; i--) {
		struct expr argument = call_expr.arguments[i - 1];

		if (argument.result_type == type_f32) {
			if (i <= float_pos) {
				compile_expr(argument);
				stack_push_rax();
			}
		} else if (i <= integer_pos) {
			compile_expr(argument);
			stack_push_rax();
		}
	}

	if (calls_helper_fn) {
		// Push the secret global variables pointer argument
		compile_unpadded(MOV_DEREF_RBP_TO_RAX_8_BIT_OFFSET);
		compile_byte(-(u8)GLOBAL_VARIABLES_POINTER_SIZE);
		stack_push_rax();
	}

	size_t popped_argument_count = integer_argument_count + float_argument_count;

	// The reason we need to decrement `pushed` and `stack_frame_bytes` here manually,
	// rather than having pop_rax(), pop_rdi(), etc. do it for us,
	// is because we use the lookup tables movs[] and pops[] below here
	assert(pushed >= popped_argument_count);
	pushed -= popped_argument_count;

	// u64 is the size of the RAX register that gets pushed for every argument
	assert(stack_frame_bytes >= popped_argument_count * sizeof(u64));
	stack_frame_bytes -= popped_argument_count * sizeof(u64);

	size_t popped_floats_count = 0;
	size_t popped_integers_count = 0;

	if (calls_helper_fn) {
		// Pop the secret global variables pointer argument
		compile_byte(POP_RDI);
		popped_integers_count++;
	}

	for (size_t i = 0; i < call_expr.argument_count; i++) {
		struct expr argument = call_expr.arguments[i];

		if (argument.result_type == type_f32) {
			if (popped_floats_count < float_argument_count) {
				compile_byte(POP_RAX);

				static u32 movs[] = {
					MOV_EAX_TO_XMM0,
					MOV_EAX_TO_XMM1,
					MOV_EAX_TO_XMM2,
					MOV_EAX_TO_XMM3,
					MOV_EAX_TO_XMM4,
					MOV_EAX_TO_XMM5,
					MOV_EAX_TO_XMM6,
					MOV_EAX_TO_XMM7,
				};

				compile_unpadded(movs[popped_floats_count++]);
			}
		} else if (popped_integers_count < integer_argument_count) {
			static u16 pops[] = {
				POP_RDI,
				POP_RSI,
				POP_RDX,
				POP_RCX,
				POP_R8,
				POP_R9,
			};

			compile_unpadded(pops[popped_integers_count++]);
		}
	}

	compile_byte(CALL);

	struct grug_game_function *game_fn = get_grug_game_fn(fn_name);

	// Push every entity type into an array, so the linker can embed them in the shared library
	if (!compiling_fast_mode) {
		struct argument *params = game_fn ? game_fn->arguments : helper_fn->arguments;
		for (size_t i = 0; i < call_expr.argument_count; i++) {
			struct argument param = params[i];
			if (param.type == type_entity) {
				push_entity_type(param.entity_type);
			}
		}
	}

	bool calls_game_fn = game_fn != NULL;
	assert(calls_helper_fn || calls_game_fn);

	bool returns_float = false;
	if (calls_game_fn) {
		push_game_fn_call(fn_name, codes_size);
		returns_float = game_fn->return_type == type_f32;
	} else if (helper_fn) {
		push_helper_fn_call(get_helper_fn_mode_name(fn_name, !compiling_fast_mode), codes_size);
		returns_float = helper_fn->return_type == type_f32;
	} else {
		backend_unreachable();
	}
	compile_unpadded(PLACEHOLDER_32);

	// Ensures the top of the stack is where it was before the alignment,
	// which is important during nested expressions, since they expect
	// the top of the stack to hold their intermediate values
	size_t offset = (pushes + requires_padding) * sizeof(u64);
	if (offset > 0) {
		if (offset < 0x80) {
			compile_unpadded(ADD_RSP_8_BITS);
			compile_byte(offset);
		} else {
			// Reached by tests/ok/spill_args_to_helper_fn_32_bit_i32

			compile_unpadded(ADD_RSP_32_BITS);
			compile_32(offset);
		}

		stack_frame_bytes += offset;
	}

	assert(pushed >= pushes);
	pushed -= pushes;

	if (returns_float) {
		compile_unpadded(MOV_XMM0_TO_EAX);
	}

	if (!compiling_fast_mode) {
		if (calls_game_fn) {
			compile_check_game_fn_error();
		} else {
			compile_return_if_runtime_error();
		}
	}
}

static void compile_logical_expr(struct binary_expr logical_expr) {
	switch (logical_expr.operator) {
		case AND_TOKEN: {
			compile_expr(*logical_expr.left_expr);
			compile_unpadded(TEST_AL_IS_ZERO);
			compile_unpadded(JE_32_BIT_OFFSET);
			size_t end_jump_offset = codes_size;
			compile_unpadded(PLACEHOLDER_32);
			compile_expr(*logical_expr.right_expr);
			compile_unpadded(TEST_AL_IS_ZERO);
			compile_unpadded(MOV_TO_EAX);
			compile_32(0);
			compile_unpadded(SETNE_AL);
			overwrite_jmp_address_32(end_jump_offset, codes_size);
			break;
		}
		case OR_TOKEN: {
			compile_expr(*logical_expr.left_expr);
			compile_unpadded(TEST_AL_IS_ZERO);
			compile_byte(JE_8_BIT_OFFSET);
			compile_byte(10);
			compile_byte(MOV_TO_EAX);
			compile_32(1);
			compile_unpadded(JMP_32_BIT_OFFSET);
			size_t end_jump_offset = codes_size;
			compile_unpadded(PLACEHOLDER_32);
			compile_expr(*logical_expr.right_expr);
			compile_unpadded(TEST_AL_IS_ZERO);
			compile_unpadded(MOV_TO_EAX);
			compile_32(0);
			compile_unpadded(SETNE_AL);
			overwrite_jmp_address_32(end_jump_offset, codes_size);
			break;
		}
		default:
			backend_unreachable();
	}
}

static void compile_binary_expr(struct expr expr) {
	assert(expr.type == BINARY_EXPR);
	struct binary_expr binary_expr = expr.binary;

	compile_expr(*binary_expr.right_expr);
	stack_push_rax();
	compile_expr(*binary_expr.left_expr);
	stack_pop_r11();

	switch (binary_expr.operator) {
		case PLUS_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(ADD_R11D_TO_EAX);

				if (!compiling_fast_mode) {
					compile_check_overflow();
				}
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(ADD_XMM1_TO_XMM0);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case MINUS_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(SUB_R11D_FROM_EAX);

				if (!compiling_fast_mode) {
					compile_check_overflow();
				}
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(SUB_XMM1_FROM_XMM0);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case MULTIPLICATION_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(IMUL_EAX_BY_R11D);

				if (!compiling_fast_mode) {
					compile_check_overflow();
				}
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(MUL_XMM0_WITH_XMM1);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case DIVISION_TOKEN:
			if (expr.result_type == type_i32) {
				if (!compiling_fast_mode) {
					compile_check_division_by_0();
					compile_check_division_overflow();
				}

				compile_byte(CDQ_SIGN_EXTEND_EAX_BEFORE_DIVISION);
				compile_unpadded(DIV_RAX_BY_R11D);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(DIV_XMM0_BY_XMM1);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case REMAINDER_TOKEN:
			if (!compiling_fast_mode) {
				compile_check_division_by_0();
				compile_check_division_overflow();
			}

			compile_byte(CDQ_SIGN_EXTEND_EAX_BEFORE_DIVISION);
			compile_unpadded(DIV_RAX_BY_R11D);
			compile_unpadded(MOV_RDX_TO_RAX);
			break;
		case EQUALS_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_EAX_WITH_R11D);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETE_AL);
			} else if (binary_expr.left_expr->result_type == type_f32) {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETE_AL);
			} else if (binary_expr.left_expr->result_type == type_id) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETE_AL);
			} else {
				compile_unpadded(MOV_R11_TO_RSI);
				compile_unpadded(MOV_RAX_TO_RDI);
				compile_byte(CALL);
				push_system_fn_call("strcmp", codes_size);
				compile_unpadded(PLACEHOLDER_32);
				compile_unpadded(TEST_EAX_IS_ZERO);
				compile_unpadded(SETE_AL);
			}
			break;
		case NOT_EQUALS_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_EAX_WITH_R11D);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETNE_AL);
			} else if (binary_expr.left_expr->result_type == type_f32) {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETNE_AL);
			} else if (binary_expr.left_expr->result_type == type_id) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETNE_AL);
			} else {
				compile_unpadded(MOV_R11_TO_RSI);
				compile_unpadded(MOV_RAX_TO_RDI);
				compile_byte(CALL);
				push_system_fn_call("strcmp", codes_size);
				compile_unpadded(PLACEHOLDER_32);
				compile_unpadded(TEST_EAX_IS_ZERO);
				compile_unpadded(SETNE_AL);
			}
			break;
		case GREATER_OR_EQUAL_TOKEN:
			if (binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_EAX_WITH_R11D);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETGE_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETAE_AL);
			}
			break;
		case GREATER_TOKEN:
			if (binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_EAX_WITH_R11D);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETGT_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETA_AL);
			}
			break;
		case LESS_OR_EQUAL_TOKEN:
			if (binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_EAX_WITH_R11D);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETLE_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETBE_AL);
			}
			break;
		case LESS_TOKEN:
			if (binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_EAX_WITH_R11D);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETLT_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETB_AL);
			}
			break;
		default:
			backend_unreachable();
	}
}

static void compile_unary_expr(struct unary_expr unary_expr) {
	switch (unary_expr.operator) {
		case MINUS_TOKEN:
			compile_expr(*unary_expr.expr);
			if (unary_expr.expr->result_type == type_i32) {
				compile_unpadded(NEGATE_EAX);

				if (!compiling_fast_mode) {
					compile_check_overflow();
				}
			} else {
				compile_byte(XOR_EAX_BY_N);
				compile_32(0x80000000);
			}
			break;
		case NOT_TOKEN:
			compile_expr(*unary_expr.expr);
			compile_unpadded(TEST_AL_IS_ZERO);
			compile_unpadded(MOV_TO_EAX);
			compile_32(0);
			compile_unpadded(SETE_AL);
			break;
		default:
			backend_unreachable();
	}
}

static void push_entity_dependency(u32 string_index) {
	backend_assert(entity_dependencies_size < MAX_ENTITY_DEPENDENCIES, "There are more than %d entity dependencies, exceeding MAX_ENTITY_DEPENDENCIES", MAX_ENTITY_DEPENDENCIES);

	entity_dependencies[entity_dependencies_size++] = string_index;
}

static void push_resource(u32 string_index) {
	backend_assert(resources_size < MAX_RESOURCES, "There are more than %d resources, exceeding MAX_RESOURCES", MAX_RESOURCES);

	resources[resources_size++] = string_index;
}

static const char *push_entity_dependency_string(const char *string) {
	static char entity[MAX_ENTITY_DEPENDENCY_NAME_LENGTH];

	if (strchr(string, ':')) {
		backend_assert(strlen(string) + 1 <= sizeof(entity), "There are more than %d characters in the entity string '%s', exceeding MAX_ENTITY_DEPENDENCY_NAME_LENGTH", MAX_ENTITY_DEPENDENCY_NAME_LENGTH, string);

		memcpy(entity, string, strlen(string) + 1);
	} else {
		snprintf(entity, sizeof(entity), "%s:%s", ast.mod, string);
	}

	size_t length = strlen(entity);

	backend_assert(entity_dependency_strings_size + length < MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS, "There are more than %d characters in the entity_dependency_strings array, exceeding MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS", MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS);

	const char *entity_str = entity_dependency_strings + entity_dependency_strings_size;

	for (size_t i = 0; i < length; i++) {
		entity_dependency_strings[entity_dependency_strings_size++] = entity[i];
	}
	entity_dependency_strings[entity_dependency_strings_size++] = '\0';

	return entity_str;
}

static const char *push_resource_string(const char *string) {
	static char resource[STUPID_MAX_PATH];
	backend_assert(snprintf(resource, sizeof(resource), "%s/%s/%s", ast.mods_root_dir_path, ast.mod, string) >= 0, "Filling the variable 'resource' failed");

	size_t length = strlen(resource);

	backend_assert(resource_strings_size + length < MAX_RESOURCE_STRINGS_CHARACTERS, "There are more than %d characters in the resource_strings array, exceeding MAX_RESOURCE_STRINGS_CHARACTERS", MAX_RESOURCE_STRINGS_CHARACTERS);

	const char *resource_str = resource_strings + resource_strings_size;

	for (size_t i = 0; i < length; i++) {
		resource_strings[resource_strings_size++] = resource[i];
	}
	resource_strings[resource_strings_size++] = '\0';

	return resource_str;
}

static void compile_expr(struct expr expr) {
	switch (expr.type) {
		case TRUE_EXPR:
			compile_byte(MOV_TO_EAX);
			compile_32(1);
			break;
		case FALSE_EXPR:
			compile_unpadded(XOR_CLEAR_EAX);
			break;
		case STRING_EXPR: {
			const char *string = expr.literal.string;

			add_data_string(string);

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case RESOURCE_EXPR: {
			const char *string = expr.literal.string;

			string = push_resource_string(string);

			bool had_string = get_data_string_index(string) != UINT32_MAX;

			add_data_string(string);

			if (!had_string) {
				push_resource(get_data_string_index(string));
			}

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case ENTITY_EXPR: {
			const char *string = expr.literal.string;

			string = push_entity_dependency_string(string);

			// This check prevents the output entities array from containing duplicate entities
			if (!compiling_fast_mode) {
				add_data_string(string);

				// We can't do the same thing we do with RESOURCE_EXPR,
				// where we only call `push_entity_dependency()` when `!had_string`,
				// because the same entity dependency strings
				// can have different "entity_type" values in mod_api.json:
				// Game fn 1 might have entity type "car", while game fn 2 has the empty string "".
				push_entity_dependency(get_data_string_index(string));
			}

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case IDENTIFIER_EXPR: {
			struct variable *var = tmp_get_local_variable(expr.literal.string);
			if (var) {
				switch (var->type) {
					case type_void:
					case type_resource:
					case type_entity:
						backend_unreachable();
					case type_bool:
						if (var->offset <= 0x80) {
							compile_unpadded(MOVZX_BYTE_DEREF_RBP_TO_EAX_8_BIT_OFFSET);
						} else {
							compile_unpadded(MOVZX_BYTE_DEREF_RBP_TO_EAX_32_BIT_OFFSET);
						}
						break;
					case type_i32:
					case type_f32:
						if (var->offset <= 0x80) {
							compile_unpadded(MOV_DEREF_RBP_TO_EAX_8_BIT_OFFSET);
						} else {
							compile_unpadded(MOV_DEREF_RBP_TO_EAX_32_BIT_OFFSET);
						}
						break;
					case type_string:
					case type_id:
						if (var->offset <= 0x80) {
							compile_unpadded(MOV_DEREF_RBP_TO_RAX_8_BIT_OFFSET);
						} else {
							compile_unpadded(MOV_DEREF_RBP_TO_RAX_32_BIT_OFFSET);
						}
						break;
				}

				if (var->offset <= 0x80) {
					compile_byte(-var->offset);
				} else {
					compile_32(-var->offset);
				}
				return;
			}

			compile_unpadded(MOV_DEREF_RBP_TO_RAX_8_BIT_OFFSET);
			compile_byte(-(u8)GLOBAL_VARIABLES_POINTER_SIZE);

			var = get_global_variable(expr.literal.string);
			switch (var->type) {
				case type_void:
				case type_resource:
				case type_entity:
					backend_unreachable();
				case type_bool:
					if (var->offset < 0x80) {
						compile_unpadded(MOVZX_BYTE_DEREF_RAX_TO_EAX_8_BIT_OFFSET);
					} else {
						compile_unpadded(MOVZX_BYTE_DEREF_RAX_TO_EAX_32_BIT_OFFSET);
					}
					break;
				case type_i32:
				case type_f32:
					if (var->offset < 0x80) {
						compile_unpadded(MOV_DEREF_RAX_TO_EAX_8_BIT_OFFSET);
					} else {
						compile_unpadded(MOV_DEREF_RAX_TO_EAX_32_BIT_OFFSET);
					}
					break;
				case type_string:
				case type_id:
					if (var->offset < 0x80) {
						compile_unpadded(MOV_DEREF_RAX_TO_RAX_8_BIT_OFFSET);
					} else {
						compile_unpadded(MOV_DEREF_RAX_TO_RAX_32_BIT_OFFSET);
					}
					break;
			}

			if (var->offset < 0x80) {
				compile_byte(var->offset);
			} else {
				compile_32(var->offset);
			}
			break;
		}
		case I32_EXPR: {
			i32 n = expr.literal.i32;
			if (n == 0) {
				compile_unpadded(XOR_CLEAR_EAX);
			} else if (n == 1) {
				compile_byte(MOV_TO_EAX);
				compile_32(1);
			} else {
				compile_unpadded(MOV_TO_EAX);
				compile_32(n);
			}
			break;
		}
		case F32_EXPR:
			compile_unpadded(MOV_TO_EAX);
			unsigned const char *bytes = (unsigned const char *)&expr.literal.f32.value;
			for (size_t i = 0; i < sizeof(float); i++) {
				compile_byte(*bytes); // Little-endian
				bytes++;
			}
			break;
		case UNARY_EXPR:
			compile_unary_expr(expr.unary);
			break;
		case BINARY_EXPR:
			compile_binary_expr(expr);
			break;
		case LOGICAL_EXPR:
			compile_logical_expr(expr.binary);
			break;
		case CALL_EXPR:
			compile_call_expr(expr.call);
			break;
		case PARENTHESIZED_EXPR:
			compile_expr(*expr.parenthesized);
			break;
	}
}

static void compile_global_variable_statement(const char *name) {
	compile_unpadded(MOV_DEREF_RBP_TO_R11_8_BIT_OFFSET);
	compile_byte(-(u8)GLOBAL_VARIABLES_POINTER_SIZE);

	struct variable *var = get_global_variable(name);
	switch (var->type) {
		case type_void:
		case type_resource:
		case type_entity:
			backend_unreachable();
		case type_bool:
			if (var->offset < 0x80) {
				compile_unpadded(MOV_AL_TO_DEREF_R11_8_BIT_OFFSET);
			} else {
				compile_unpadded(MOV_AL_TO_DEREF_R11_32_BIT_OFFSET);
			}
			break;
		case type_i32:
		case type_f32:
			if (var->offset < 0x80) {
				compile_unpadded(MOV_EAX_TO_DEREF_R11_8_BIT_OFFSET);
			} else {
				compile_unpadded(MOV_EAX_TO_DEREF_R11_32_BIT_OFFSET);
			}
			break;
		case type_id:
			// See tests/err/global_id_cant_be_reassigned
			backend_assert(!compiled_init_globals_fn, "Global id variables can't be reassigned");
			__attribute__((fallthrough));
		case type_string:
			if (var->offset < 0x80) {
				compile_unpadded(MOV_RAX_TO_DEREF_R11_8_BIT_OFFSET);
			} else {
				compile_unpadded(MOV_RAX_TO_DEREF_R11_32_BIT_OFFSET);
			}
			break;
	}

	if (var->offset < 0x80) {
		compile_byte(var->offset);
	} else {
		compile_32(var->offset);
	}
}

static void compile_variable_statement(struct variable_statement variable_statement) {
	compile_expr(*variable_statement.assignment_expr);

	// The "TYPE PROPAGATION" section already checked for any possible errors.
	if (variable_statement.has_type) {
		tmp_add_local_variable(variable_statement.name, variable_statement.type, variable_statement.type_name);
	}

	struct variable *var = tmp_get_local_variable(variable_statement.name);
	if (var) {
		switch (var->type) {
			case type_void:
			case type_resource:
			case type_entity:
				backend_unreachable();
			case type_bool:
				if (var->offset <= 0x80) {
					compile_unpadded(MOV_AL_TO_DEREF_RBP_8_BIT_OFFSET);
				} else {
					compile_unpadded(MOV_AL_TO_DEREF_RBP_32_BIT_OFFSET);
				}
				break;
			case type_i32:
			case type_f32:
				if (var->offset <= 0x80) {
					compile_unpadded(MOV_EAX_TO_DEREF_RBP_8_BIT_OFFSET);
				} else {
					compile_unpadded(MOV_EAX_TO_DEREF_RBP_32_BIT_OFFSET);
				}
				break;
			case type_string:
			case type_id:
				if (var->offset <= 0x80) {
					compile_unpadded(MOV_RAX_TO_DEREF_RBP_8_BIT_OFFSET);
				} else {
					compile_unpadded(MOV_RAX_TO_DEREF_RBP_32_BIT_OFFSET);
				}
				break;
		}

		if (var->offset <= 0x80) {
			compile_byte(-var->offset);
		} else {
			compile_32(-var->offset);
		}
		return;
	}

	compile_global_variable_statement(variable_statement.name);
}

static void tmp_mark_local_variables_unreachable(struct statement *body_statements, size_t statement_count) {
	// Mark all local variables in this exited scope block as being unreachable.
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		if (statement.type == VARIABLE_STATEMENT && statement.variable_statement.has_type) {
			struct variable *var = tmp_get_local_variable(statement.variable_statement.name);
			assert(var);

			var->offset = SIZE_MAX;

			// Even though we have already calculated the final stack frame size in advance
			// before we started compiling the function's body, we are still calling add_local_variable()
			// during the compilation of the function body. And that fn uses stack_frame_bytes.
			assert(stack_frame_bytes >= tmp_type_sizes[var->type]);
			stack_frame_bytes -= tmp_type_sizes[var->type];
		}
	}
}

static void compile_statements(struct statement *body_statements, size_t statement_count) {
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				compile_variable_statement(statement.variable_statement);
				break;
			case CALL_STATEMENT:
				compile_call_expr(statement.call_statement.expr->call);
				break;
			case IF_STATEMENT:
				compile_if_statement(statement.if_statement);
				break;
			case RETURN_STATEMENT:
				if (statement.return_statement.has_value) {
					compile_expr(*statement.return_statement.value);
				}
				compile_function_epilogue();
				break;
			case WHILE_STATEMENT:
				compile_while_statement(statement.while_statement);
				break;
			case BREAK_STATEMENT:
				compile_unpadded(JMP_32_BIT_OFFSET);
				push_break_statement_jump_address_offset(codes_size);
				compile_unpadded(PLACEHOLDER_32);
				break;
			case CONTINUE_STATEMENT:
				compile_continue_statement();
				break;
			case EMPTY_LINE_STATEMENT:
			case COMMENT_STATEMENT:
				break;
		}
	}

	tmp_mark_local_variables_unreachable(body_statements, statement_count);
}

static void calc_max_local_variable_stack_usage(struct statement *body_statements, size_t statement_count) {
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				if (statement.variable_statement.has_type) {
					stack_frame_bytes += tmp_type_sizes[statement.variable_statement.type];

					if (stack_frame_bytes > max_stack_frame_bytes) {
						max_stack_frame_bytes = stack_frame_bytes;
					}
				}
				break;
			case IF_STATEMENT:
				calc_max_local_variable_stack_usage(statement.if_statement.if_body_statements, statement.if_statement.if_body_statement_count);

				if (statement.if_statement.else_body_statement_count > 0) {
					calc_max_local_variable_stack_usage(statement.if_statement.else_body_statements, statement.if_statement.else_body_statement_count);
				}

				break;
			case WHILE_STATEMENT:
				calc_max_local_variable_stack_usage(statement.while_statement.body_statements, statement.while_statement.body_statement_count);
				break;
			case CALL_STATEMENT:
			case RETURN_STATEMENT:
			case BREAK_STATEMENT:
			case CONTINUE_STATEMENT:
			case EMPTY_LINE_STATEMENT:
			case COMMENT_STATEMENT:
				break;
		}
	}

	// All local variables in this exited scope block are now unreachable.
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		if (statement.type == VARIABLE_STATEMENT && statement.variable_statement.has_type) {
			assert(stack_frame_bytes >= tmp_type_sizes[statement.variable_statement.type]);
			stack_frame_bytes -= tmp_type_sizes[statement.variable_statement.type];
		}
	}
}

static size_t compile_safe_je(void) {
	// mov rax, [rel grug_on_fns_in_safe_mode wrt ..got]:
	compile_unpadded(MOV_GLOBAL_VARIABLE_TO_RAX);
	push_used_extern_global_variable("grug_on_fns_in_safe_mode", codes_size);
	compile_32(PLACEHOLDER_32);

	// mov al, [rax]:
	compile_padded(MOV_DEREF_RAX_TO_AL, 2);

	// test al, al:
	compile_unpadded(TEST_AL_IS_ZERO);

	// je strict $+0xn:
	compile_unpadded(JE_32_BIT_OFFSET);
	size_t skip_safe_code_offset = codes_size;
	compile_unpadded(PLACEHOLDER_32);

	return skip_safe_code_offset;
}

static void compile_move_globals_ptr(void) {
	// We need to move the secret global variables pointer to this function's stack frame,
	// because the RDI register will get clobbered when this function calls another function:
	// https://stackoverflow.com/a/55387707/13279557
	compile_unpadded(MOV_RDI_TO_DEREF_RBP_8_BIT_OFFSET);
	compile_byte(-(u8)GLOBAL_VARIABLES_POINTER_SIZE);
}

// From https://stackoverflow.com/a/9194117/13279557
static size_t round_to_power_of_2(size_t n, size_t multiple) {
	// Assert that `multiple` is a power of 2
	assert(multiple && ((multiple & (multiple - 1)) == 0));

	return (n + multiple - 1) & -multiple;
}

static void compile_function_prologue(void) {
	compile_byte(PUSH_RBP);

	// Deliberately leaving this out, as we also don't include the 8 byte starting offset
	// that the calling convention guarantees on entering a function (from pushing the return address).
	// max_stack_frame_bytes += sizeof(u64);

	compile_unpadded(MOV_RSP_TO_RBP);

	// The System V ABI requires 16-byte stack alignment for function calls: https://stackoverflow.com/q/49391001/13279557
	max_stack_frame_bytes = round_to_power_of_2(max_stack_frame_bytes, 0x10);

	if (max_stack_frame_bytes < 0x80) {
		compile_unpadded(SUB_RSP_8_BITS);
		compile_byte(max_stack_frame_bytes);
	} else {
		compile_unpadded(SUB_RSP_32_BITS);
		compile_32(max_stack_frame_bytes);
	}
}

static void tmp_add_argument_variables(struct argument *fn_arguments, size_t argument_count) {
	tmp_variables_size = 0;
	memset(tmp_buckets_variables, 0xff, sizeof(tmp_buckets_variables));

	stack_frame_bytes = sizeof(void *); // Size of the global variables pointer.
	max_stack_frame_bytes = stack_frame_bytes;

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		struct argument arg = fn_arguments[argument_index];
		tmp_add_local_variable(arg.name, arg.type, arg.type_name);

		if (arg.type == type_entity) {
			push_entity_type(arg.entity_type);
		}

		max_stack_frame_bytes += tmp_type_sizes[arg.type];
	}
}

static void compile_on_fn_impl(const char *fn_name, struct argument *fn_arguments, size_t argument_count, struct statement *body_statements, size_t body_statement_count, const char *grug_path, bool on_fn_calls_helper_fn, bool on_fn_contains_while_loop) {
	tmp_add_argument_variables(fn_arguments, argument_count);

	calc_max_local_variable_stack_usage(body_statements, body_statement_count);

	compile_function_prologue();

	compile_move_globals_ptr();

	move_arguments(fn_arguments, argument_count);

	size_t skip_safe_code_offset = compile_safe_je();

	compile_save_fn_name_and_path(grug_path, fn_name);

	if (on_fn_calls_helper_fn) {
		// call grug_get_max_rsp_addr wrt ..plt:
		compile_byte(CALL);
		push_system_fn_call("grug_get_max_rsp_addr", codes_size);
		compile_unpadded(PLACEHOLDER_32);

		// mov [rax], rsp:
		compile_unpadded(MOV_RSP_TO_DEREF_RAX);

		// sub qword [rax], GRUG_STACK_LIMIT:
		compile_unpadded(SUB_DEREF_RAX_32_BITS);
		compile_32(GRUG_STACK_LIMIT);
	}

	if (on_fn_calls_helper_fn || on_fn_contains_while_loop) {
		// call grug_set_time_limit wrt ..plt:
		compile_byte(CALL);
		push_system_fn_call("grug_set_time_limit", codes_size);
		compile_unpadded(PLACEHOLDER_32);
	}

	compile_clear_has_runtime_error_happened();

	current_grug_path = grug_path;
	current_fn_name = fn_name;

	compile_statements(body_statements, body_statement_count);
	assert(pushed == 0);

	compile_function_epilogue();

	overwrite_jmp_address_32(skip_safe_code_offset, codes_size);

	compiling_fast_mode = true;
	compile_statements(body_statements, body_statement_count);
	assert(pushed == 0);
	compiling_fast_mode = false;

	compile_function_epilogue();
}

static void compile_on_fn(struct on_fn fn, const char *grug_path) {
	compile_on_fn_impl(fn.fn_name, fn.arguments, fn.argument_count, fn.body_statements, fn.body_statement_count, grug_path, fn.calls_helper_fn, fn.contains_while_loop);
}

static void compile_helper_fn_impl(struct argument *fn_arguments, size_t argument_count, struct statement *body_statements, size_t body_statement_count) {
	tmp_add_argument_variables(fn_arguments, argument_count);

	calc_max_local_variable_stack_usage(body_statements, body_statement_count);

	compile_function_prologue();

	compile_move_globals_ptr();

	move_arguments(fn_arguments, argument_count);

	if (!compiling_fast_mode) {
		compile_check_stack_overflow();
		compile_check_time_limit_exceeded();
	}

	compile_statements(body_statements, body_statement_count);
	assert(pushed == 0);

	compile_function_epilogue();
}

static void compile_helper_fn(struct helper_fn fn) {
	compile_helper_fn_impl(fn.arguments, fn.argument_count, fn.body_statements, fn.body_statement_count);
}

static void compile_init_globals_fn(const char *grug_path) {
	// The "me" global variable is always present
	// If there are no other global variables, take a shortcut
	if (ast.global_variables_size == 1) {
		// The entity ID passed in the rsi register is always the first global
		compile_unpadded(MOV_RSI_TO_DEREF_RDI);

		compile_byte(RET);
		compiled_init_globals_fn = true;
		return;
	}

	stack_frame_bytes = GLOBAL_VARIABLES_POINTER_SIZE;
	max_stack_frame_bytes = stack_frame_bytes;

	compile_function_prologue();

	compile_move_globals_ptr();

	// The entity ID passed in the rsi register is always the first global
	compile_unpadded(MOV_RSI_TO_DEREF_RDI);

	size_t skip_safe_code_offset = compile_safe_je();

	compile_save_fn_name_and_path(grug_path, "init_globals");

	compile_clear_has_runtime_error_happened();

	current_grug_path = grug_path;
	current_fn_name = "init_globals";

	for (size_t i = 0; i < ast.global_variable_statements_size; i++) {
		struct global_variable_statement global = ast.global_variable_statements[i];

		compile_expr(global.assignment_expr);

		compile_global_variable_statement(global.name);
	}
	assert(pushed == 0);

	compile_function_epilogue();

	overwrite_jmp_address_32(skip_safe_code_offset, codes_size);

	compiling_fast_mode = true;
	for (size_t i = 0; i < ast.global_variable_statements_size; i++) {
		struct global_variable_statement global = ast.global_variable_statements[i];

		compile_expr(global.assignment_expr);

		compile_global_variable_statement(global.name);
	}
	assert(pushed == 0);
	compiling_fast_mode = false;

	compile_function_epilogue();

	compiled_init_globals_fn = true;
}

static void compile(const char *grug_path) {
	reset_compiling();

	size_t text_offset_index = 0;
	size_t text_offset = 0;

	compile_init_globals_fn(grug_path);
	text_offsets[text_offset_index++] = text_offset;
	text_offset = codes_size;

	for (size_t on_fn_index = 0; on_fn_index < ast.on_fns_size; on_fn_index++) {
		struct on_fn fn = ast.on_fns[on_fn_index];

		compile_on_fn(fn, grug_path);

		text_offsets[text_offset_index++] = text_offset;
		text_offset = codes_size;
	}

	for (size_t helper_fn_index = 0; helper_fn_index < ast.helper_fns_size; helper_fn_index++) {
		struct helper_fn fn = ast.helper_fns[helper_fn_index];

		push_helper_fn_offset(get_safe_helper_fn_name(fn.fn_name), codes_size);

		compile_helper_fn(fn);

		text_offsets[text_offset_index++] = text_offset;
		text_offset = codes_size;

		// The same, but for fast mode:

		push_helper_fn_offset(get_fast_helper_fn_name(fn.fn_name), codes_size);

		compiling_fast_mode = true;
		compile_helper_fn(fn);
		compiling_fast_mode = false;

		text_offsets[text_offset_index++] = text_offset;
		text_offset = codes_size;
	}

	hash_used_extern_fns();
	hash_helper_fn_offsets();
}

////// LINKING

#define MAX_BYTES 420420
#define MAX_GAME_FN_OFFSETS 420420
#define MAX_GLOBAL_VARIABLE_OFFSETS 420420
#define MAX_HASH_BUCKETS 32771 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elflink.c;h=6db6a9c0b4702c66d73edba87294e2a59ffafcf5;hb=refs/heads/master#l6560

// The first three addresses pushed by push_got_plt() are special:
// A recent update of the "ld" linker causes the first three .got.plt addresses to always be placed
// 0x18 bytes before the start of a new page, so at 0x2fe8/0x3fe8, etc.
// The grug tester compares the grug output against ld, so that's why we mimic ld here
#define GOT_PLT_INTRO_SIZE 0x18

#define RELA_ENTRY_SIZE 24
#define SYMTAB_ENTRY_SIZE 24
#define PLT_ENTRY_SIZE 24

#ifdef LOGGING
#define grug_log_section(section_name) {\
	grug_log("%s: 0x%lx\n", section_name, bytes_size);\
}
#else
#define grug_log_section(section_name)
#endif

static size_t shindex_hash;
static size_t shindex_dynsym;
static size_t shindex_dynstr;
static size_t shindex_rela_dyn;
static size_t shindex_rela_plt;
static size_t shindex_plt;
static size_t shindex_text;
static size_t shindex_eh_frame;
static size_t shindex_dynamic;
static size_t shindex_got;
static size_t shindex_got_plt;
static size_t shindex_data;
static size_t shindex_symtab;
static size_t shindex_strtab;
static size_t shindex_shstrtab;

static const char *symbols[MAX_SYMBOLS];
static size_t symbols_size;

static size_t on_fns_symbol_offset;

static size_t data_symbols_size;
static size_t extern_data_symbols_size;

static size_t symbol_name_dynstr_offsets[MAX_SYMBOLS];
static size_t symbol_name_strtab_offsets[MAX_SYMBOLS];

static u32 buckets_on_fns[MAX_ON_FNS];
static u32 chains_on_fns[MAX_ON_FNS];

static const char *shuffled_symbols[MAX_SYMBOLS];
static size_t shuffled_symbols_size;

static size_t shuffled_symbol_index_to_symbol_index[MAX_SYMBOLS];
static size_t symbol_index_to_shuffled_symbol_index[MAX_SYMBOLS];

static size_t first_extern_data_symbol_index;
static size_t first_used_extern_fn_symbol_index;

static size_t data_offsets[MAX_SYMBOLS];
static size_t data_string_offsets[MAX_SYMBOLS];

static u8 bytes[MAX_BYTES];
static size_t bytes_size;

static size_t symtab_index_first_global;

static size_t pltgot_value_offset;

static size_t text_size;
static size_t data_size;
static size_t hash_offset;
static size_t hash_size;
static size_t dynsym_offset;
static size_t dynsym_placeholders_offset;
static size_t dynsym_size;
static size_t dynstr_offset;
static size_t dynstr_size;
static size_t rela_dyn_offset;
static size_t rela_dyn_size;
static size_t rela_plt_offset;
static size_t rela_plt_size;
static size_t plt_offset;
static size_t plt_size;
static size_t text_offset;
static size_t eh_frame_offset;
static size_t dynamic_offset;
static size_t dynamic_size;
static size_t got_offset;
static size_t got_size;
static size_t got_plt_offset;
static size_t got_plt_size;
static size_t data_offset;
static size_t segment_0_size;
static size_t symtab_offset;
static size_t symtab_size;
static size_t strtab_offset;
static size_t strtab_size;
static size_t shstrtab_offset;
static size_t shstrtab_size;
static size_t section_headers_offset;

static size_t hash_shstrtab_offset;
static size_t dynsym_shstrtab_offset;
static size_t dynstr_shstrtab_offset;
static size_t rela_dyn_shstrtab_offset;
static size_t rela_plt_shstrtab_offset;
static size_t plt_shstrtab_offset;
static size_t text_shstrtab_offset;
static size_t eh_frame_shstrtab_offset;
static size_t dynamic_shstrtab_offset;
static size_t got_shstrtab_offset;
static size_t got_plt_shstrtab_offset;
static size_t data_shstrtab_offset;
static size_t symtab_shstrtab_offset;
static size_t strtab_shstrtab_offset;
static size_t shstrtab_shstrtab_offset;

static struct offset game_fn_offsets[MAX_GAME_FN_OFFSETS];
static size_t game_fn_offsets_size;
static u32 buckets_game_fn_offsets[MAX_GAME_FN_OFFSETS];
static u32 chains_game_fn_offsets[MAX_GAME_FN_OFFSETS];

static struct offset global_variable_offsets[MAX_GLOBAL_VARIABLE_OFFSETS];
static size_t global_variable_offsets_size;
static u32 buckets_global_variable_offsets[MAX_GLOBAL_VARIABLE_OFFSETS];
static u32 chains_global_variable_offsets[MAX_GLOBAL_VARIABLE_OFFSETS];

static size_t resources_offset;
static size_t entities_offset;
static size_t entity_types_offset;

static void reset_generate_shared_object(void) {
	symbols_size = 0;
	data_symbols_size = 0;
	extern_data_symbols_size = 0;
	shuffled_symbols_size = 0;
	bytes_size = 0;
	game_fn_offsets_size = 0;
	global_variable_offsets_size = 0;
}

static void overwrite(u64 n, size_t bytes_offset, size_t overwrite_count) {
	for (size_t i = 0; i < overwrite_count; i++) {
		bytes[bytes_offset++] = n & 0xff; // Little-endian
		n >>= 8;
	}
}

static void overwrite_16(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, sizeof(u16));
}

static void overwrite_32(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, sizeof(u32));
}

static void overwrite_64(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, sizeof(u64));
}

static struct on_fn *get_on_fn(const char *name) {
	if (ast.on_fns_size == 0) {
		return NULL;
	}

	u32 i = buckets_on_fns[elf_hash(name) % ast.on_fns_size];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, ast.on_fns[i].fn_name)) {
			break;
		}

		i = chains_on_fns[i];
	}

	return ast.on_fns + i;
}

static void hash_on_fns(void) {
	memset(buckets_on_fns, 0xff, ast.on_fns_size * sizeof(u32));

	for (size_t i = 0; i < ast.on_fns_size; i++) {
		const char *name = ast.on_fns[i].fn_name;

		backend_assert(!get_on_fn(name), "The function '%s' was defined several times in the same file", name);

		u32 bucket_index = elf_hash(name) % ast.on_fns_size;

		chains_on_fns[i] = buckets_on_fns[bucket_index];

		buckets_on_fns[bucket_index] = i;
	}
}

static void patch_plt(void) {
	size_t overwritten_address = plt_offset;

	size_t address_size = sizeof(u32);

	overwritten_address += sizeof(u16);
	overwrite_32(got_plt_offset - overwritten_address - address_size + 0x8, overwritten_address);

	overwritten_address += address_size + sizeof(u16);
	overwrite_32(got_plt_offset - overwritten_address - address_size + 0x10, overwritten_address);

	size_t got_plt_fn_address = got_plt_offset + GOT_PLT_INTRO_SIZE;

	overwritten_address += 2 * sizeof(u32) + sizeof(u16);

	for (size_t i = 0; i < BFD_HASH_BUCKET_SIZE; i++) {
		u32 chain_index = buckets_used_extern_fns[i];
		if (chain_index == UINT32_MAX) {
			continue;
		}

		while (true) {
			overwrite_32(got_plt_fn_address - overwritten_address - NEXT_INSTRUCTION_OFFSET, overwritten_address);

			got_plt_fn_address += sizeof(u64);

			overwritten_address += sizeof(u32) + sizeof(u8) + sizeof(u32) + sizeof(u8) + sizeof(u32) + sizeof(u16);

			chain_index = chains_used_extern_fns[chain_index];
			if (chain_index == UINT32_MAX) {
				break;
			}
		}
	}
}

static void patch_rela_plt(void) {
	size_t value_offset = got_plt_offset + GOT_PLT_INTRO_SIZE;

	size_t address_offset = rela_plt_offset;

	for (size_t shuffled_symbol_index = 0; shuffled_symbol_index < symbols_size; shuffled_symbol_index++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[shuffled_symbol_index];

		if (symbol_index < first_used_extern_fn_symbol_index || symbol_index >= first_used_extern_fn_symbol_index + extern_fns_size) {
			continue;
		}

		overwrite_64(value_offset, address_offset);
		value_offset += sizeof(u64);
		size_t entry_size = 3;
		address_offset += entry_size * sizeof(u64);
	}
}

static void patch_rela_dyn(void) {
	size_t globals_size_data_size = sizeof(u64);
	size_t on_fn_data_offset = globals_size_data_size;

	size_t excess = on_fn_data_offset % sizeof(u64); // Alignment
	if (excess > 0) {
		on_fn_data_offset += sizeof(u64) - excess;
	}

	size_t bytes_offset = rela_dyn_offset;
	for (size_t i = 0; i < ast.grug_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(ast.grug_entity->on_functions[i].name);
		if (on_fn) {
			size_t on_fn_index = on_fn - ast.on_fns;

			overwrite_64(got_plt_offset + got_plt_size + on_fn_data_offset, bytes_offset);
			bytes_offset += 2 * sizeof(u64);

			size_t fns_before_on_fns = 1; // Just init_globals()
			overwrite_64(text_offset + text_offsets[on_fn_index + fns_before_on_fns], bytes_offset);
			bytes_offset += sizeof(u64);
		}
		on_fn_data_offset += sizeof(size_t);
	}

	for (size_t i = 0; i < resources_size; i++) {
		overwrite_64(resources_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(data_offset + data_string_offsets[resources[i]], bytes_offset);
		bytes_offset += sizeof(u64);
	}

	for (size_t i = 0; i < entity_dependencies_size; i++) {
		overwrite_64(entities_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(data_offset + data_string_offsets[entity_dependencies[i]], bytes_offset);
		bytes_offset += sizeof(u64);
	}

	for (size_t i = 0; i < entity_dependencies_size; i++) {
		overwrite_64(entity_types_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(data_offset + data_string_offsets[entity_types[i]], bytes_offset);
		bytes_offset += sizeof(u64);
	}

	for (size_t i = 0; i < extern_data_symbols_size; i++) {
		overwrite_64(got_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(0, bytes_offset);
		bytes_offset += sizeof(u64);
	}
}

static u32 get_symbol_offset(size_t symbol_index) {
	bool is_data = symbol_index < data_symbols_size;
	if (is_data) {
		return data_offset + data_offsets[symbol_index];
	}

	bool is_extern_data = symbol_index < first_extern_data_symbol_index + extern_data_symbols_size;
	if (is_extern_data) {
		return 0;
	}

	bool is_extern = symbol_index < first_used_extern_fn_symbol_index + extern_fns_size;
	if (is_extern) {
		return 0;
	}

	return text_offset + text_offsets[symbol_index - data_symbols_size - extern_data_symbols_size - extern_fns_size];
}

static u16 get_symbol_shndx(size_t symbol_index) {
	bool is_data = symbol_index < data_symbols_size;
	if (is_data) {
		return shindex_data;
	}

	bool is_extern_data = symbol_index < first_extern_data_symbol_index + extern_data_symbols_size;
	if (is_extern_data) {
		return SHN_UNDEF;
	}

	bool is_extern = symbol_index < first_used_extern_fn_symbol_index + extern_fns_size;
	if (is_extern) {
		return SHN_UNDEF;
	}

	return shindex_text;
}

static void patch_dynsym(void) {
	// The symbols are pushed in shuffled_symbols order
	size_t bytes_offset = dynsym_placeholders_offset;
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		overwrite_32(symbol_name_dynstr_offsets[symbol_index], bytes_offset);
		bytes_offset += sizeof(u32);
		overwrite_16(ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), bytes_offset);
		bytes_offset += sizeof(u16);
		overwrite_16(get_symbol_shndx(symbol_index), bytes_offset);
		bytes_offset += sizeof(u16);
		overwrite_32(get_symbol_offset(symbol_index), bytes_offset);
		bytes_offset += sizeof(u32);

		bytes_offset += SYMTAB_ENTRY_SIZE - sizeof(u32) - sizeof(u16) - sizeof(u16) - sizeof(u32);
	}
}

static size_t get_game_fn_offset(const char *name) {
	assert(game_fn_offsets_size > 0);

	u32 i = buckets_game_fn_offsets[elf_hash(name) % game_fn_offsets_size];

	while (true) {
		assert(i != UINT32_MAX && "get_game_fn_offset() is supposed to never fail");

		if (streq(name, game_fn_offsets[i].name)) {
			break;
		}

		i = chains_game_fn_offsets[i];
	}

	return game_fn_offsets[i].offset;
}

static void hash_game_fn_offsets(void) {
	memset(buckets_game_fn_offsets, 0xff, game_fn_offsets_size * sizeof(u32));

	for (size_t i = 0; i < game_fn_offsets_size; i++) {
		const char *name = game_fn_offsets[i].name;

		u32 bucket_index = elf_hash(name) % game_fn_offsets_size;

		chains_game_fn_offsets[i] = buckets_game_fn_offsets[bucket_index];

		buckets_game_fn_offsets[bucket_index] = i;
	}
}

static void push_game_fn_offset(const char *fn_name, size_t offset) {
	backend_assert(game_fn_offsets_size < MAX_GAME_FN_OFFSETS, "There are more than %d game functions, exceeding MAX_GAME_FN_OFFSETS", MAX_GAME_FN_OFFSETS);

	game_fn_offsets[game_fn_offsets_size++] = (struct offset){
		.name = fn_name,
		.offset = offset,
	};
}

static bool has_got(void) {
	return ast.global_variables_size > 1 || ast.on_fns_size > 0;
}

// Used for both .plt and .rela.plt
static bool has_plt(void) {
	return extern_fn_calls_size > 0;
}

static bool has_rela_dyn(void) {
	return ast.global_variables_size > 1 || ast.on_fns_size > 0 || resources_size > 0 || entity_dependencies_size > 0;
}

static void patch_dynamic(void) {
	if (has_plt()) {
		overwrite_64(got_plt_offset, pltgot_value_offset);
	}
}

static size_t get_global_variable_offset(const char *name) {
	// push_got() guarantees we always have 4
	assert(global_variable_offsets_size > 0);

	u32 i = buckets_global_variable_offsets[elf_hash(name) % global_variable_offsets_size];

	while (true) {
		assert(i != UINT32_MAX && "get_global_variable_offset() is supposed to never fail");

		if (streq(name, global_variable_offsets[i].name)) {
			break;
		}

		i = chains_global_variable_offsets[i];
	}

	return global_variable_offsets[i].offset;
}

static void hash_global_variable_offsets(void) {
	memset(buckets_global_variable_offsets, 0xff, global_variable_offsets_size * sizeof(u32));

	for (size_t i = 0; i < global_variable_offsets_size; i++) {
		const char *name = global_variable_offsets[i].name;

		u32 bucket_index = elf_hash(name) % global_variable_offsets_size;

		chains_global_variable_offsets[i] = buckets_global_variable_offsets[bucket_index];

		buckets_global_variable_offsets[bucket_index] = i;
	}
}

static void push_global_variable_offset(const char *name, size_t offset) {
	backend_assert(global_variable_offsets_size < MAX_GLOBAL_VARIABLE_OFFSETS, "There are more than %d game functions, exceeding MAX_GLOBAL_VARIABLE_OFFSETS", MAX_GLOBAL_VARIABLE_OFFSETS);

	global_variable_offsets[global_variable_offsets_size++] = (struct offset){
		.name = name,
		.offset = offset,
	};
}

static void patch_global_variables(void) {
	for (size_t i = 0; i < used_extern_global_variables_size; i++) {
		struct used_extern_global_variable global = used_extern_global_variables[i];
		size_t offset = text_offset + global.codes_offset;
		size_t address_after_global_instruction = offset + NEXT_INSTRUCTION_OFFSET;
		size_t variable_offset = get_global_variable_offset(global.variable_name);
		size_t global_variable_got_offset = got_offset + variable_offset;
		size_t value = global_variable_got_offset - address_after_global_instruction;

		overwrite_32(value, offset);
	}
}

static void patch_strings(void) {
	for (size_t i = 0; i < data_string_codes_size; i++) {
		struct data_string_code dsc = data_string_codes[i];
		const char *string = dsc.string;
		size_t code_offset = dsc.code_offset;

		size_t string_index = get_data_string_index(string);
		assert(string_index != UINT32_MAX);

		size_t string_address = data_offset + data_string_offsets[string_index];

		size_t next_instruction_address = text_offset + code_offset + NEXT_INSTRUCTION_OFFSET;

		// RIP-relative address of data string
		size_t string_offset = string_address - next_instruction_address;

		overwrite_32(string_offset, text_offset + code_offset);
	}
}

static void patch_helper_fn_calls(void) {
	for (size_t i = 0; i < helper_fn_calls_size; i++) {
		struct offset fn_call = helper_fn_calls[i];
		size_t offset = text_offset + fn_call.offset;
		size_t address_after_call_instruction = offset + NEXT_INSTRUCTION_OFFSET;
		size_t helper_fn_text_offset = text_offset + get_helper_fn_offset(fn_call.name);
		overwrite_32(helper_fn_text_offset - address_after_call_instruction, offset);
	}
}

static void patch_extern_fn_calls(void) {
	for (size_t i = 0; i < extern_fn_calls_size; i++) {
		struct offset fn_call = extern_fn_calls[i];
		size_t offset = text_offset + fn_call.offset;
		size_t address_after_call_instruction = offset + NEXT_INSTRUCTION_OFFSET;
		size_t game_fn_plt_offset = plt_offset + get_game_fn_offset(fn_call.name);
		overwrite_32(game_fn_plt_offset - address_after_call_instruction, offset);
	}
}

static void patch_text(void) {
	patch_extern_fn_calls();
	patch_helper_fn_calls();
	patch_strings();
	patch_global_variables();
}

static void patch_program_headers(void) {
	// .hash, .dynsym, .dynstr, .rela.dyn, .rela.plt segment
	overwrite_64(segment_0_size, 0x60); // file_size
	overwrite_64(segment_0_size, 0x68); // mem_size

	// .plt, .text segment
	overwrite_64(plt_offset, 0x80); // offset
	overwrite_64(plt_offset, 0x88); // virtual_address
	overwrite_64(plt_offset, 0x90); // physical_address
	size_t size = text_size;
	if (has_plt()) {
		size += plt_size;
	}
	overwrite_64(size, 0x98); // file_size
	overwrite_64(size, 0xa0); // mem_size

	// .eh_frame segment
	overwrite_64(eh_frame_offset, 0xb8); // offset
	overwrite_64(eh_frame_offset, 0xc0); // virtual_address
	overwrite_64(eh_frame_offset, 0xc8); // physical_address

	// .dynamic, .got, .got.plt, .data segment
	overwrite_64(dynamic_offset, 0xf0); // offset
	overwrite_64(dynamic_offset, 0xf8); // virtual_address
	overwrite_64(dynamic_offset, 0x100); // physical_address
	size = dynamic_size + data_size;
	if (has_got()) {
		size += got_size + got_plt_size;
	}
	overwrite_64(size, 0x108); // file_size
	overwrite_64(size, 0x110); // mem_size

	// .dynamic segment
	overwrite_64(dynamic_offset, 0x128); // offset
	overwrite_64(dynamic_offset, 0x130); // virtual_address
	overwrite_64(dynamic_offset, 0x138); // physical_address
	overwrite_64(dynamic_size, 0x140); // file_size
	overwrite_64(dynamic_size, 0x148); // mem_size

	// empty segment for GNU_STACK

	// .dynamic, .got segment
	overwrite_64(dynamic_offset, 0x198); // offset
	overwrite_64(dynamic_offset, 0x1a0); // virtual_address
	overwrite_64(dynamic_offset, 0x1a8); // physical_address
	size_t segment_5_size = dynamic_size;
	if (has_got()) {
		segment_5_size += got_size;

#ifndef OLD_LD
		segment_5_size += GOT_PLT_INTRO_SIZE;
#endif
	}
	overwrite_64(segment_5_size, 0x1b0); // file_size
	overwrite_64(segment_5_size, 0x1b8); // mem_size
}

static void patch_bytes(void) {
	// ELF section header table offset
	overwrite_64(section_headers_offset, 0x28);

	patch_program_headers();

	patch_dynsym();
	if (has_rela_dyn()) {
		patch_rela_dyn();
	}
	if (has_plt()) {
		patch_rela_plt();
		patch_plt();
	}
	patch_text();
	patch_dynamic();
}

static void push_byte(u8 byte) {
	backend_assert(bytes_size < MAX_BYTES, "There are more than %d bytes, exceeding MAX_BYTES", MAX_BYTES);

	bytes[bytes_size++] = byte;
}

static void push_zeros(size_t count) {
	for (size_t i = 0; i < count; i++) {
		push_byte(0);
	}
}

static void push_nasm_alignment(size_t alignment) {
	size_t excess = bytes_size % alignment;
	if (excess > 0) {
		for (size_t i = 0; i < alignment - excess; i++) {
			// nasm aligns using the NOP instruction:
			// https://stackoverflow.com/a/18414187/13279557
			push_byte(NOP_8_BITS);
		}
	}
}

static void push_alignment(size_t alignment) {
	size_t excess = bytes_size % alignment;
	if (excess > 0) {
		push_zeros(alignment - excess);
	}
}

static void push_string_bytes(const char *str) {
	while (*str) {
		push_byte(*str);
		str++;
	}
	push_byte('\0');
}

static void push_shstrtab(void) {
	grug_log_section(".shstrtab");

	shstrtab_offset = bytes_size;

	size_t offset = 0;

	push_byte(0);
	offset += 1;

	symtab_shstrtab_offset = offset;
	push_string_bytes(".symtab");
	offset += sizeof(".symtab");

	strtab_shstrtab_offset = offset;
	push_string_bytes(".strtab");
	offset += sizeof(".strtab");

	shstrtab_shstrtab_offset = offset;
	push_string_bytes(".shstrtab");
	offset += sizeof(".shstrtab");

	hash_shstrtab_offset = offset;
	push_string_bytes(".hash");
	offset += sizeof(".hash");

	dynsym_shstrtab_offset = offset;
	push_string_bytes(".dynsym");
	offset += sizeof(".dynsym");

	dynstr_shstrtab_offset = offset;
	push_string_bytes(".dynstr");
	offset += sizeof(".dynstr");

	if (has_rela_dyn()) {
		rela_dyn_shstrtab_offset = offset;
		push_string_bytes(".rela.dyn");
		offset += sizeof(".rela.dyn");
	}

	if (has_plt()) {
		rela_plt_shstrtab_offset = offset;
		push_string_bytes(".rela.plt");
		offset += sizeof(".rela") - 1;

		plt_shstrtab_offset = offset;
		offset += sizeof(".plt");
	}

	text_shstrtab_offset = offset;
	push_string_bytes(".text");
	offset += sizeof(".text");

	eh_frame_shstrtab_offset = offset;
	push_string_bytes(".eh_frame");
	offset += sizeof(".eh_frame");

	dynamic_shstrtab_offset = offset;
	push_string_bytes(".dynamic");
	offset += sizeof(".dynamic");

	if (has_got()) {
		got_shstrtab_offset = offset;
		push_string_bytes(".got");
		offset += sizeof(".got");

		got_plt_shstrtab_offset = offset;
		push_string_bytes(".got.plt");
		offset += sizeof(".got.plt");
	}

	data_shstrtab_offset = offset;
	push_string_bytes(".data");
	// offset += sizeof(".data");

	shstrtab_size = bytes_size - shstrtab_offset;

	push_alignment(8);
}

static void push_strtab(void) {
	grug_log_section(".strtab");

	strtab_offset = bytes_size;

	push_byte(0);
	push_string_bytes("_DYNAMIC");
	if (has_got()) {
		push_string_bytes("_GLOBAL_OFFSET_TABLE_");
	}

	for (size_t i = 0; i < symbols_size; i++) {
		push_string_bytes(shuffled_symbols[i]);
	}

	strtab_size = bytes_size - strtab_offset;
}

static void push_number(u64 n, size_t byte_count) {
	for (; byte_count-- > 0; n >>= 8) {
		push_byte(n & 0xff); // Little-endian
	}
}

static void push_16(u16 n) {
	push_number(n, sizeof(u16));
}

static void push_32(u32 n) {
	push_number(n, sizeof(u32));
}

static void push_64(u64 n) {
	push_number(n, sizeof(u64));
}

// See https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-79797/index.html
// See https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblj/index.html#chapter6-tbl-21
static void push_symbol_entry(u32 name, u16 info, u16 shndx, u32 offset) {
	push_32(name); // Indexed into .strtab for .symtab, because .symtab its "link" points to it; .dynstr for .dynstr
	push_16(info);
	push_16(shndx);
	push_32(offset); // In executable and shared object files, st_value holds a virtual address

	push_zeros(SYMTAB_ENTRY_SIZE - sizeof(u32) - sizeof(u16) - sizeof(u16) - sizeof(u32));
}

static void push_symtab(void) {
	grug_log_section(".symtab");

	symtab_offset = bytes_size;

	size_t pushed_symbol_entries = 0;

	// Null entry
	push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), SHN_UNDEF, 0);
	pushed_symbol_entries++;

	// The `1 +` skips the 0 byte that .strtab always starts with
	size_t name_offset = 1;

	// "_DYNAMIC" entry
	push_symbol_entry(name_offset, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), shindex_dynamic, dynamic_offset);
	pushed_symbol_entries++;
	name_offset += sizeof("_DYNAMIC");

	if (has_got()) {
		// "_GLOBAL_OFFSET_TABLE_" entry
		push_symbol_entry(name_offset, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), shindex_got_plt, got_plt_offset);
		pushed_symbol_entries++;
		name_offset += sizeof("_GLOBAL_OFFSET_TABLE_");
	}

	symtab_index_first_global = pushed_symbol_entries;

	// The symbols are pushed in shuffled_symbols order
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		push_symbol_entry(name_offset + symbol_name_strtab_offsets[symbol_index], ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), get_symbol_shndx(symbol_index), get_symbol_offset(symbol_index));
	}

	symtab_size = bytes_size - symtab_offset;
}

static void push_data(void) {
	grug_log_section(".data");

	data_offset = bytes_size;

	// "globals_size" symbol
	push_64(ast.globals_bytes);

	// "on_fns" function addresses
	size_t previous_on_fn_index = 0;
	for (size_t i = 0; i < ast.grug_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(ast.grug_entity->on_functions[i].name);
		if (on_fn) {
			size_t on_fn_index = on_fn - ast.on_fns;
			backend_assert(previous_on_fn_index <= on_fn_index, "The function '%s' needs to be moved before/after a different on_ function, according to the entity '%s' in mod_api.json", on_fn->fn_name, ast.grug_entity->name);
			previous_on_fn_index = on_fn_index;

			size_t fns_before_on_fns = 1; // Just init_globals()
			push_64(text_offset + text_offsets[on_fn_index + fns_before_on_fns]);
		} else {
			push_64(0x0);
		}
	}

	// data strings
	for (size_t i = 0; i < data_strings_size; i++) {
		push_string_bytes(data_strings[i]);
	}

	// "resources_size" symbol
	push_nasm_alignment(8);
	push_64(resources_size);

	// "resources" symbol
	resources_offset = bytes_size;
	for (size_t i = 0; i < resources_size; i++) {
		push_64(data_offset + data_string_offsets[resources[i]]);
	}

	// "entities_size" symbol
	push_64(entity_dependencies_size);

	// "entities" symbol
	entities_offset = bytes_size;
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_64(data_offset + data_string_offsets[entity_dependencies[i]]);
	}

	// "entity_types" symbol
	entity_types_offset = bytes_size;
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_64(data_offset + data_string_offsets[entity_types[i]]);
	}

	push_alignment(8);
}

static void push_got_plt(void) {
	grug_log_section(".got.plt");

	got_plt_offset = bytes_size;

	push_64(dynamic_offset);
	push_zeros(8); // TODO: What is this for? I presume it's patched by the dynamic linker at runtime?
	push_zeros(8); // TODO: What is this for? I presume it's patched by the dynamic linker at runtime?

	// 0x6 is the offset every .plt entry has to their push instruction
	size_t entry_size = 0x10;
	size_t offset = plt_offset + entry_size + 0x6;

	for (size_t i = 0; i < extern_fns_size; i++) {
		push_64(offset); // text section address of push <i> instruction
		offset += entry_size;
	}

	got_plt_size = bytes_size - got_plt_offset;
}

// The .got section is for extern globals
static void push_got(void) {
	grug_log_section(".got");

	got_offset = bytes_size;

	size_t offset = 0;

	push_global_variable_offset("grug_on_fns_in_safe_mode", offset);
	offset += sizeof(u64);
	push_zeros(sizeof(u64));

	push_global_variable_offset("grug_has_runtime_error_happened", offset);
	offset += sizeof(u64);
	push_zeros(sizeof(u64));

	push_global_variable_offset("grug_fn_name", offset);
	offset += sizeof(u64);
	push_zeros(sizeof(u64));

	push_global_variable_offset("grug_fn_path", offset);
	offset += sizeof(u64);
	push_zeros(sizeof(u64));

	if (is_runtime_error_handler_used) {
		push_global_variable_offset("grug_runtime_error_handler", offset);
		// offset += sizeof(u64);
		push_zeros(sizeof(u64));
	}

	hash_global_variable_offsets();

	got_size = bytes_size - got_offset;
}

// See https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-42444.html
static void push_dynamic_entry(u64 tag, u64 value) {
	push_64(tag);
	push_64(value);
}

static void push_dynamic(void) {
	grug_log_section(".dynamic");

	size_t entry_size = 0x10;
	dynamic_size = 11 * entry_size;

	if (has_plt()) {
		dynamic_size += 4 * entry_size;
	}
	if (has_rela_dyn()) {
		dynamic_size += 3 * entry_size;
	}

	size_t segment_2_to_3_offset = 0x1000;
	dynamic_offset = bytes_size + segment_2_to_3_offset - dynamic_size;
	if (has_got()) {
		// This subtracts the future got_size set by push_got()
		// TODO: Stop having these hardcoded here
		if (is_runtime_error_handler_used) {
			dynamic_offset -= sizeof(u64); // grug_runtime_error_handler
		}
		dynamic_offset -= sizeof(u64); // grug_fn_path
		dynamic_offset -= sizeof(u64); // grug_fn_name
		dynamic_offset -= sizeof(u64); // grug_has_runtime_error_happened
		dynamic_offset -= sizeof(u64); // grug_on_fns_in_safe_mode
	}

#ifndef OLD_LD
	if (has_got()) {
		dynamic_offset -= GOT_PLT_INTRO_SIZE;
	}
#endif

	push_zeros(dynamic_offset - bytes_size);

	push_dynamic_entry(DT_HASH, hash_offset);
	push_dynamic_entry(DT_STRTAB, dynstr_offset);
	push_dynamic_entry(DT_SYMTAB, dynsym_offset);
	push_dynamic_entry(DT_STRSZ, dynstr_size);
	push_dynamic_entry(DT_SYMENT, SYMTAB_ENTRY_SIZE);

	if (has_plt()) {
		push_64(DT_PLTGOT);
		pltgot_value_offset = bytes_size;
		push_64(PLACEHOLDER_64);

		push_dynamic_entry(DT_PLTRELSZ, PLT_ENTRY_SIZE * extern_fns_size);
		push_dynamic_entry(DT_PLTREL, DT_RELA);
		push_dynamic_entry(DT_JMPREL, rela_plt_offset);
	}

	if (has_rela_dyn()) {
		push_dynamic_entry(DT_RELA, rela_dyn_offset);
		push_dynamic_entry(DT_RELASZ, (ast.on_fns_size + extern_data_symbols_size + resources_size + 2 * entity_dependencies_size) * RELA_ENTRY_SIZE);
		push_dynamic_entry(DT_RELAENT, RELA_ENTRY_SIZE);

		size_t rela_count = ast.on_fns_size + resources_size + 2 * entity_dependencies_size;
		// tests/ok/global_id reaches this with rela_count == 0
		if (rela_count > 0) {
			push_dynamic_entry(DT_RELACOUNT, rela_count);
		}
	}

	// "Marks the end of the _DYNAMIC array."
	// From https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-42444.html
	push_dynamic_entry(DT_NULL, 0);

	// TODO: I have no clue what this 5 represents
	size_t padding = 5 * entry_size;

	size_t count = 0;
	count += resources_size > 0;
	count += entity_dependencies_size > 0;
	count += ast.on_fns_size > 0;

	if (count > 0) {
		padding -= entry_size;
	}

	push_zeros(padding);
}

static void push_text(void) {
	grug_log_section(".text");

	text_offset = bytes_size;

	backend_assert(bytes_size + codes_size < MAX_BYTES, "There are more than %d bytes, exceeding MAX_BYTES", MAX_BYTES);

	for (size_t i = 0; i < codes_size; i++) {
		bytes[bytes_size++] = codes[i];
	}

	push_alignment(8);
}

static void push_plt(void) {
	grug_log_section(".plt");

	// See this for an explanation: https://stackoverflow.com/q/76987336/13279557
	push_16(PUSH_REL);
	push_32(PLACEHOLDER_32);
	push_16(JMP_REL);
	push_32(PLACEHOLDER_32);
	push_32(NOP_32_BITS); // See https://reverseengineering.stackexchange.com/a/11973

	size_t pushed_plt_entries = 0;

	size_t offset = 0x10;
	for (size_t i = 0; i < BFD_HASH_BUCKET_SIZE; i++) {
		u32 chain_index = buckets_used_extern_fns[i];
		if (chain_index == UINT32_MAX) {
			continue;
		}

		while (true) {
			const char *name = used_extern_fns[chain_index];

			push_16(JMP_REL);
			push_32(PLACEHOLDER_32);
			push_byte(PUSH_32_BITS);
			push_32(pushed_plt_entries++);
			push_byte(JMP_32_BIT_OFFSET);
			push_game_fn_offset(name, offset);
			size_t offset_to_start_of_plt = -offset - 0x10;
			push_32(offset_to_start_of_plt);
			offset += 0x10;

			chain_index = chains_used_extern_fns[chain_index];
			if (chain_index == UINT32_MAX) {
				break;
			}
		}
	}

	hash_game_fn_offsets();

	plt_size = bytes_size - plt_offset;
}

static void push_rela(u64 offset, u64 info, u64 addend) {
	push_64(offset);
	push_64(info);
	push_64(addend);
}

// Source:
// https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblk/index.html#chapter6-1235
// https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-54839.html
static void push_rela_plt(void) {
	grug_log_section(".rela.plt");

	rela_plt_offset = bytes_size;

	for (size_t shuffled_symbol_index = 0; shuffled_symbol_index < symbols_size; shuffled_symbol_index++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[shuffled_symbol_index];

		if (symbol_index < first_used_extern_fn_symbol_index || symbol_index >= first_used_extern_fn_symbol_index + extern_fns_size) {
			continue;
		}

		// `1 +` skips the first symbol, which is always undefined
		size_t dynsym_index = 1 + shuffled_symbol_index;

		push_rela(PLACEHOLDER_64, ELF64_R_INFO(dynsym_index, R_X86_64_JUMP_SLOT), 0);
	}

	rela_plt_size = bytes_size - rela_plt_offset;
}

// Source: https://stevens.netmeister.org/631/elf.html
static void push_rela_dyn(void) {
	grug_log_section(".rela.dyn");

	for (size_t i = 0; i < ast.grug_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(ast.grug_entity->on_functions[i].name);
		if (on_fn) {
			push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
		}
	}

	for (size_t i = 0; i < resources_size; i++) {
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
	}

	// "entities" symbol
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
	}

	// "entity_types" symbol
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
	}

	// Idk why, but nasm seems to always push the symbols in the reverse order
	// Maybe this should use symbol_index_to_shuffled_symbol_index?
	for (size_t i = extern_data_symbols_size; i > 0; i--) {
		// `1 +` skips the first symbol, which is always undefined
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(1 + symbol_index_to_shuffled_symbol_index[first_extern_data_symbol_index + i - 1], R_X86_64_GLOB_DAT), PLACEHOLDER_64);
	}

	rela_dyn_size = bytes_size - rela_dyn_offset;
}

static void push_dynstr(void) {
	grug_log_section(".dynstr");

	dynstr_offset = bytes_size;

	// .dynstr always starts with a '\0'
	dynstr_size = 1;

	push_byte(0);
	for (size_t i = 0; i < symbols_size; i++) {
		const char *symbol = symbols[i];

		push_string_bytes(symbol);
		dynstr_size += strlen(symbol) + 1;
	}
}

static u32 get_nbucket(void) {
	// From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elflink.c;h=6db6a9c0b4702c66d73edba87294e2a59ffafcf5;hb=refs/heads/master#l6560
	//
	// Array used to determine the number of hash table buckets to use
	// based on the number of symbols there are. If there are fewer than
	// 3 symbols we use 1 bucket, fewer than 17 symbols we use 3 buckets,
	// fewer than 37 we use 17 buckets, and so forth. We never use more
	// than MAX_HASH_BUCKETS (32771) buckets.
	static const u32 nbucket_options[] = {
		1, 3, 17, 37, 67, 97, 131, 197, 263, 521, 1031, 2053, 4099, 8209, 16411, MAX_HASH_BUCKETS, 0
	};

	u32 nbucket = 0;

	for (size_t i = 0; nbucket_options[i] != 0; i++) {
		nbucket = nbucket_options[i];

		if (symbols_size < nbucket_options[i + 1]) {
			break;
		}
	}

	return nbucket;
}

// See my blog post: https://mynameistrez.github.io/2024/06/19/array-based-hash-table-in-c.html
static void push_hash(void) {
	grug_log_section(".hash");

	hash_offset = bytes_size;

	u32 nbucket = get_nbucket();
	push_32(nbucket);

	u32 nchain = 1 + symbols_size; // `1 + `, because index 0 is always STN_UNDEF (the value 0)
	push_32(nchain);

	static u32 buckets[MAX_HASH_BUCKETS];
	memset(buckets, 0, nbucket * sizeof(u32));

	static u32 chains[MAX_SYMBOLS + 1]; // +1, because [0] is STN_UNDEF

	size_t chains_size = 0;

	chains[chains_size++] = 0; // The first entry in the chain is always STN_UNDEF

	for (size_t i = 0; i < symbols_size; i++) {
		u32 bucket_index = elf_hash(shuffled_symbols[i]) % nbucket;

		chains[chains_size] = buckets[bucket_index];

		buckets[bucket_index] = chains_size++;
	}

	for (size_t i = 0; i < nbucket; i++) {
		push_32(buckets[i]);
	}

	for (size_t i = 0; i < chains_size; i++) {
		push_32(chains[i]);
	}

	hash_size = bytes_size - hash_offset;

	push_alignment(8);
}

static void push_section_header(u32 name_offset, u32 type, u64 flags, u64 address, u64 offset, u64 size, u32 link, u32 info, u64 alignment, u64 entry_size) {
	push_32(name_offset);
	push_32(type);
	push_64(flags);
	push_64(address);
	push_64(offset);
	push_64(size);
	push_32(link);
	push_32(info);
	push_64(alignment);
	push_64(entry_size);
}

static void push_section_headers(void) {
	grug_log_section("Section headers");

	section_headers_offset = bytes_size;

	// Null section
	push_zeros(0x40);

	// .hash: Hash section
	push_section_header(hash_shstrtab_offset, SHT_HASH, SHF_ALLOC, hash_offset, hash_offset, hash_size, shindex_dynsym, 0, 8, 4);

	// .dynsym: Dynamic linker symbol table section
	push_section_header(dynsym_shstrtab_offset, SHT_DYNSYM, SHF_ALLOC, dynsym_offset, dynsym_offset, dynsym_size, shindex_dynstr, 1, 8, 24);

	// .dynstr: String table section
	push_section_header(dynstr_shstrtab_offset, SHT_STRTAB, SHF_ALLOC, dynstr_offset, dynstr_offset, dynstr_size, SHN_UNDEF, 0, 1, 0);

	if (has_rela_dyn()) {
		// .rela.dyn: Relative variable table section
		push_section_header(rela_dyn_shstrtab_offset, SHT_RELA, SHF_ALLOC, rela_dyn_offset, rela_dyn_offset, rela_dyn_size, shindex_dynsym, 0, 8, 24);
	}

	if (has_plt()) {
		// .rela.plt: Relative procedure (function) linkage table section
		push_section_header(rela_plt_shstrtab_offset, SHT_RELA, SHF_ALLOC | SHF_INFO_LINK, rela_plt_offset, rela_plt_offset, rela_plt_size, shindex_dynsym, shindex_got_plt, 8, 24);

		// .plt: Procedure linkage table section
		push_section_header(plt_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, plt_offset, plt_offset, plt_size, SHN_UNDEF, 0, 16, 16);
	}

	// .text: Code section
	push_section_header(text_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text_offset, text_offset, text_size, SHN_UNDEF, 0, 16, 0);

	// .eh_frame: Exception stack unwinding section
	push_section_header(eh_frame_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC, eh_frame_offset, eh_frame_offset, 0, SHN_UNDEF, 0, 8, 0);

	// .dynamic: Dynamic linking information section
	push_section_header(dynamic_shstrtab_offset, SHT_DYNAMIC, SHF_WRITE | SHF_ALLOC, dynamic_offset, dynamic_offset, dynamic_size, shindex_dynstr, 0, 8, 16);

	if (has_got()) {
		// .got: Global offset table section
		push_section_header(got_shstrtab_offset, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, got_offset, got_offset, got_size, SHN_UNDEF, 0, 8, 8);

		// .got.plt: Global offset table procedure linkage table section
		push_section_header(got_plt_shstrtab_offset, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, got_plt_offset, got_plt_offset, got_plt_size, SHN_UNDEF, 0, 8, 8);
	}

	// .data: Data section
	push_section_header(data_shstrtab_offset, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, data_offset, data_offset, data_size, SHN_UNDEF, 0, 8, 0);

	// .symtab: Symbol table section
	// The "link" argument is the section header index of the associated string table
	push_section_header(symtab_shstrtab_offset, SHT_SYMTAB, 0, 0, symtab_offset, symtab_size, shindex_strtab, symtab_index_first_global, 8, SYMTAB_ENTRY_SIZE);

	// .strtab: String table section
	push_section_header(strtab_shstrtab_offset, SHT_PROGBITS | SHT_SYMTAB, 0, 0, strtab_offset, strtab_size, SHN_UNDEF, 0, 1, 0);

	// .shstrtab: Section header string table section
	push_section_header(shstrtab_shstrtab_offset, SHT_PROGBITS | SHT_SYMTAB, 0, 0, shstrtab_offset, shstrtab_size, SHN_UNDEF, 0, 1, 0);
}

static void push_dynsym(void) {
	grug_log_section(".dynsym");

	dynsym_offset = bytes_size;

	// Null entry
	push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), SHN_UNDEF, 0);

	dynsym_placeholders_offset = bytes_size;
	for (size_t i = 0; i < symbols_size; i++) {
		push_symbol_entry(PLACEHOLDER_32, PLACEHOLDER_16, PLACEHOLDER_16, PLACEHOLDER_32);
	}

	dynsym_size = bytes_size - dynsym_offset;
}

static void push_program_header(u32 type, u32 flags, u64 offset, u64 virtual_address, u64 physical_address, u64 file_size, u64 mem_size, u64 alignment) {
	push_32(type);
	push_32(flags);
	push_64(offset);
	push_64(virtual_address);
	push_64(physical_address);
	push_64(file_size);
	push_64(mem_size);
	push_64(alignment);
}

static void push_program_headers(void) {
	grug_log_section("Program headers");

	// Segment 0
	// .hash, .dynsym, .dynstr, .rela.dyn, .rela.plt
	// 0x40 to 0x78
	push_program_header(PT_LOAD, PF_R, 0, 0, 0, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// Segment 1
	// .plt, .text
	// 0x78 to 0xb0
	push_program_header(PT_LOAD, PF_R | PF_X, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// Segment 2
	// .eh_frame
	// 0xb0 to 0xe8
	push_program_header(PT_LOAD, PF_R, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 0, 0, 0x1000);

	// Segment 3
	// .dynamic, .got, .got.plt, .data
	// 0xe8 to 0x120
	push_program_header(PT_LOAD, PF_R | PF_W, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// Segment 4
	// .dynamic
	// 0x120 to 0x158
	push_program_header(PT_DYNAMIC, PF_R | PF_W, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 8);

	// Segment 5
	// empty segment for GNU_STACK
	// We only need GNU_STACK because of a breaking change that was recently made by GNU C Library version 2.41
	// See https://github.com/ValveSoftware/Source-1-Games/issues/6978#issuecomment-2631834285
	// 0x158 to 0x190
	push_program_header(PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 0, 0x10);

	// Segment 6
	// .dynamic, .got
	// 0x190 to 0x1c8
	push_program_header(PT_GNU_RELRO, PF_R, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 1);
}

static void push_elf_header(void) {
	grug_log_section("ELF header");

	// Magic number
	// 0x0 to 0x4
	push_byte(0x7f);
	push_byte('E');
	push_byte('L');
	push_byte('F');

	// 64-bit
	// 0x4 to 0x5
	push_byte(2);

	// Little-endian
	// 0x5 to 0x6
	push_byte(1);

	// Version
	// 0x6 to 0x7
	push_byte(1);

	// SysV OS ABI
	// 0x7 to 0x8
	push_byte(0);

	// Padding
	// 0x8 to 0x10
	push_zeros(8);

	// Shared object
	// 0x10 to 0x12
	push_byte(ET_DYN);
	push_byte(0);

	// x86-64 instruction set architecture
	// 0x12 to 0x14
	push_byte(0x3E);
	push_byte(0);

	// Original version of ELF
	// 0x14 to 0x18
	push_byte(1);
	push_zeros(3);

	// Execution entry point address
	// 0x18 to 0x20
	push_zeros(8);

	// Program header table offset
	// 0x20 to 0x28
	push_byte(0x40);
	push_zeros(7);

	// Section header table offset
	// 0x28 to 0x30
	push_64(PLACEHOLDER_64);

	// Processor-specific flags
	// 0x30 to 0x34
	push_zeros(4);

	// ELF header size
	// 0x34 to 0x36
	push_byte(0x40);
	push_byte(0);

	// Single program header size
	// 0x36 to 0x38
	push_byte(0x38);
	push_byte(0);

	// Number of program header entries
	// 0x38 to 0x3a
	push_byte(7);
	push_byte(0);

	// Single section header entry size
	// 0x3a to 0x3c
	push_byte(0x40);
	push_byte(0);

	// Number of section header entries
	// 0x3c to 0x3e
	push_byte(11 + 2 * has_got() + has_rela_dyn() + 2 * has_plt());
	push_byte(0);

	// Index of entry with section names
	// 0x3e to 0x40
	push_byte(10 + 2 * has_got() + has_rela_dyn() + 2 * has_plt());
	push_byte(0);
}

static void push_bytes(void) {
	// 0x0 to 0x40
	push_elf_header();

	// 0x40 to 0x190
	push_program_headers();

	push_hash();

	push_dynsym();

	push_dynstr();

	if (has_rela_dyn()) {
		push_alignment(8);
	}

	rela_dyn_offset = bytes_size;
	if (has_rela_dyn()) {
		push_rela_dyn();
	}

	if (has_plt()) {
		push_rela_plt();
	}

	segment_0_size = bytes_size;

	size_t next_segment_offset = round_to_power_of_2(bytes_size, 0x1000);
	push_zeros(next_segment_offset - bytes_size);

	plt_offset = bytes_size;
	if (has_plt()) {
		push_plt();
	}

	push_text();

	eh_frame_offset = round_to_power_of_2(bytes_size, 0x1000);
	push_zeros(eh_frame_offset - bytes_size);

	push_dynamic();

	if (has_got()) {
		push_got();
		push_got_plt();
	}

	push_data();

	push_symtab();

	push_strtab();

	push_shstrtab();

	push_section_headers();
}

static void init_data_offsets(void) {
	size_t i = 0;
	size_t offset = 0;

	// "globals_size" symbol
	data_offsets[i++] = offset;
	offset += sizeof(u64);

	// "on_fns" function address symbols
	if (ast.grug_entity->on_function_count > 0) {
		data_offsets[i++] = offset;
		for (size_t on_fn_index = 0; on_fn_index < ast.grug_entity->on_function_count; on_fn_index++) {
			offset += sizeof(size_t);
		}
	}

	// data strings
	for (size_t string_index = 0; string_index < data_strings_size; string_index++) {
		data_string_offsets[string_index] = offset;
		const char *string = data_strings[string_index];
		offset += strlen(string) + 1;
	}

	// "resources_size" symbol
	size_t excess = offset % sizeof(u64); // Alignment
	if (excess > 0) {
		offset += sizeof(u64) - excess;
	}
	data_offsets[i++] = offset;
	offset += sizeof(u64);

	// "resources" symbol
	if (resources_size > 0) {
		data_offsets[i++] = offset;
		for (size_t resource_index = 0; resource_index < resources_size; resource_index++) {
			offset += sizeof(size_t);
		}
	}

	// "entities_size" symbol
	data_offsets[i++] = offset;
	offset += sizeof(u64);

	if (entity_dependencies_size > 0) {
		// "entities" symbol
		data_offsets[i++] = offset;
		for (size_t entity_dependency_index = 0; entity_dependency_index < entity_dependencies_size; entity_dependency_index++) {
			offset += sizeof(size_t);
		}

		// "entity_types" symbol
		data_offsets[i++] = offset;
		for (size_t entity_dependency_index = 0; entity_dependency_index < entity_dependencies_size; entity_dependency_index++) {
			offset += sizeof(size_t);
		}
	}

	data_size = offset;
}

static void init_symbol_name_strtab_offsets(void) {
	for (size_t i = 0, offset = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];
		const char *symbol = symbols[symbol_index];

		symbol_name_strtab_offsets[symbol_index] = offset;
		offset += strlen(symbol) + 1;
	}
}

static void push_shuffled_symbol(const char *shuffled_symbol) {
	backend_assert(shuffled_symbols_size < MAX_SYMBOLS, "There are more than %d symbols, exceeding MAX_SYMBOLS", MAX_SYMBOLS);

	shuffled_symbols[shuffled_symbols_size++] = shuffled_symbol;
}

// See my blog post: https://mynameistrez.github.io/2024/06/19/array-based-hash-table-in-c.html
// See https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l618)
static void generate_shuffled_symbols(void) {
	static u32 buckets[BFD_HASH_BUCKET_SIZE];

	memset(buckets, 0, sizeof(buckets));

	static u32 chains[MAX_SYMBOLS + 1]; // +1, because [0] is STN_UNDEF

	size_t chains_size = 0;

	chains[chains_size++] = 0; // The first entry in the chain is always STN_UNDEF

	for (size_t i = 0; i < symbols_size; i++) {
		u32 hash = bfd_hash(symbols[i]);
		u32 bucket_index = hash % BFD_HASH_BUCKET_SIZE;

		chains[chains_size] = buckets[bucket_index];

		buckets[bucket_index] = chains_size++;
	}

	for (size_t i = 0; i < BFD_HASH_BUCKET_SIZE; i++) {
		u32 chain_index = buckets[i];
		if (chain_index == 0) {
			continue;
		}

		while (true) {
			const char *symbol = symbols[chain_index - 1];

			shuffled_symbol_index_to_symbol_index[shuffled_symbols_size] = chain_index - 1;
			symbol_index_to_shuffled_symbol_index[chain_index - 1] = shuffled_symbols_size;

			push_shuffled_symbol(symbol);

			chain_index = chains[chain_index];
			if (chain_index == 0) {
				break;
			}
		}
	}
}

static void init_symbol_name_dynstr_offsets(void) {
	for (size_t i = 0, offset = 1; i < symbols_size; i++) {
		const char *symbol = symbols[i];

		symbol_name_dynstr_offsets[i] = offset;
		offset += strlen(symbol) + 1;
	}
}

static void push_symbol(const char *symbol) {
	backend_assert(symbols_size < MAX_SYMBOLS, "There are more than %d symbols, exceeding MAX_SYMBOLS", MAX_SYMBOLS);

	symbols[symbols_size++] = symbol;
}

static void init_section_header_indices(void) {
	size_t shindex = 1;

	shindex_hash = shindex++;
	shindex_dynsym = shindex++;
	shindex_dynstr = shindex++;
	if (has_rela_dyn()) {
		shindex_rela_dyn = shindex++;
	}
	if (has_plt()) {
		shindex_rela_plt = shindex++;
		shindex_plt = shindex++;
	}
	shindex_text = shindex++;
	shindex_eh_frame = shindex++;
	shindex_dynamic = shindex++;
	if (has_got()) {
		shindex_got = shindex++;
		shindex_got_plt = shindex++;
	}
	shindex_data = shindex++;
	shindex_symtab = shindex++;
	shindex_strtab = shindex++;
	shindex_shstrtab = shindex++;
}

static void generate_shared_object(const char *dll_path) {
	text_size = codes_size;

	reset_generate_shared_object();

	init_section_header_indices();

	push_symbol("globals_size");
	data_symbols_size++;

	if (ast.grug_entity->on_function_count > 0) {
		push_symbol("on_fns");
		data_symbols_size++;
	}

	push_symbol("resources_size");
	data_symbols_size++;

	if (resources_size > 0) {
		push_symbol("resources");
		data_symbols_size++;
	}

	push_symbol("entities_size");
	data_symbols_size++;

	if (entity_dependencies_size != entity_types_size) {
		backend_unreachable();
	}

	if (entity_dependencies_size > 0) {
		push_symbol("entities");
		data_symbols_size++;

		push_symbol("entity_types");
		data_symbols_size++;
	}

	first_extern_data_symbol_index = data_symbols_size;
	if (has_got()) {
		if (is_runtime_error_handler_used) {
			push_symbol("grug_runtime_error_handler");
			extern_data_symbols_size++;
		}

		push_symbol("grug_fn_path");
		extern_data_symbols_size++;

		push_symbol("grug_fn_name");
		extern_data_symbols_size++;

		push_symbol("grug_has_runtime_error_happened");
		extern_data_symbols_size++;

		push_symbol("grug_on_fns_in_safe_mode");
		extern_data_symbols_size++;
	}

	first_used_extern_fn_symbol_index = first_extern_data_symbol_index + extern_data_symbols_size;
	for (size_t i = 0; i < extern_fns_size; i++) {
		push_symbol(used_extern_fns[i]);
	}

	push_symbol("init_globals");

	on_fns_symbol_offset = symbols_size;
	for (size_t i = 0; i < ast.on_fns_size; i++) {
		push_symbol(ast.on_fns[i].fn_name);
	}

	for (size_t i = 0; i < ast.helper_fns_size; i++) {
		push_symbol(get_safe_helper_fn_name(ast.helper_fns[i].fn_name));
		push_symbol(get_fast_helper_fn_name(ast.helper_fns[i].fn_name));
	}

	init_symbol_name_dynstr_offsets();

	generate_shuffled_symbols();

	init_symbol_name_strtab_offsets();

	init_data_offsets();

	hash_on_fns();

	push_bytes();

	patch_bytes();

	FILE *f = fopen(dll_path, "w");
	backend_assert(f, "fopen: %s", strerror(errno));
	backend_assert(fwrite(bytes, sizeof(u8), bytes_size, f) > 0, "fwrite error");
	backend_assert(fclose(f) == 0, "fclose: %s", strerror(errno));
}

////// BACKEND API

static char dll_root_dir_path[STUPID_MAX_PATH];
static bool is_grug_backend_initialized = false;

USED_BY_PROGRAMS bool grug_init_backend_linux(const char *dll_dir_path);
bool grug_init_backend_linux(const char *dll_dir_path) {
	if (setjmp(backend_error_jmp_buffer)) {
		return true;
	}

	assert(!is_grug_backend_initialized && "grug_init_backend_linux() can't be called more than once");

	assert(!strchr(dll_dir_path, '\\') && "grug_init_backend_linux() its dll_dir_path can't contain backslashes, so replace them with '/'");
	assert(dll_dir_path[strlen(dll_dir_path) - 1] != '/' && "grug_init_backend_linux() its dll_dir_path can't have a trailing '/'");

	assert(strlen(dll_dir_path) + 1 <= STUPID_MAX_PATH && "grug_init_backend_linux() its dll_dir_path exceeds the maximum path length");
	memcpy(dll_root_dir_path, dll_dir_path, strlen(dll_dir_path) + 1);

	is_grug_backend_initialized = true;

	return false;
}

static void try_create_parent_dirs(const char *file_path) {
	char parent_dir_path[STUPID_MAX_PATH];
	size_t i = 0;

	errno = 0;
	while (*file_path) {
		parent_dir_path[i] = *file_path;
		parent_dir_path[i + 1] = '\0';

		if (*file_path == '/' || *file_path == '\\') {
			backend_assert(mkdir(parent_dir_path, 0775) != -1 || errno == EEXIST, "mkdir: %s", strerror(errno));
		}

		file_path++;
		i++;
	}
}

static char *get_dll_path(void) {
	static char dll_path_[STUPID_MAX_PATH];
	char *dll_path = dll_path_;
	size_t len = 0;

	// TODO: Figure out how this string should be unhardcoded. Maybe a #define, or adding a setter fn?
	// Let's say dlls_root="mod_dlls"
	const char *dlls_root = "mod_dlls"; // TODO: UNHARDCODE!
	const size_t dlls_root_len = strlen(dlls_root);

	// dll_path now becomes "mod_dlls/"
	backend_assert(dlls_root_len + 1 + 1 <= STUPID_MAX_PATH, "There are more than %d characters in dll_path_ due to dlls_root '%s', exceeding STUPID_MAX_PATH", STUPID_MAX_PATH, dlls_root);
	memcpy(dll_path, dlls_root, dlls_root_len + 1);
	dll_path += dlls_root_len;
	len += dlls_root_len;
	*dll_path++ = '/';
	len++;

	// Let's say mods_root="mods"
	const char *mods_root = ast.mods_root_dir_path;
	const size_t mods_root_len = strlen(mods_root);

	// Let's say grug_path="mods/guns/ak47-Gun.grug"
	const char *grug_path = ast.grug_file_path;

	// TODO: Is it possible that one of them is an absolute path, while the other isn't?
	// Assert that grug_path is prefixed by mods_root.
	backend_assert(memcmp(grug_path, mods_root, mods_root_len) == 0, "The grug_path '%s' is not prefixed by the mods_root '%s'", grug_path, mods_root);

	const char *grug_subpath = grug_path + mods_root_len;
	const size_t grug_subpath_len = strlen(grug_subpath);

	// TODO: Use chatgpt to help scan for any potential bugs in my implementation.
	// TODO: Use chatgpt to help refactor this function.

	// dll_path now becomes "mod_dlls/guns/ak47-Gun.grug"
	backend_assert(len + grug_subpath_len + 1 <= STUPID_MAX_PATH, "There are more than %d characters in dll_path_ due to grug_path '%s', exceeding STUPID_MAX_PATH", STUPID_MAX_PATH, grug_path);
	memcpy(dll_path, grug_subpath, grug_subpath_len + 1);
	dll_path += grug_subpath_len;
	len += grug_subpath_len;

	// The code that called this backend function has already checked
	// that the file ends with ".grug"
	char *extension = strrchr(dll_path, '.');
	assert(extension);
	assert(extension[0] == '.');

	// This can't write out of bounds, since ".so" is shorter than ".grug"
	memcpy(extension + 1, "so", sizeof("so"));

	return dll_path_;
}

static bool load(struct grug_ast *ast_) {
	if (setjmp(backend_error_jmp_buffer)) {
		return true;
	}

	assert(is_grug_backend_initialized && "You forgot to call grug_init_backend_linux() once at program startup");

	ast = *ast_;

	compile(ast.grug_file_path);

	const char *dll_path = get_dll_path();

	// If the dll doesn't exist, try to create the parent directories
	struct stat dll_stat;
	bool dll_exists = stat(dll_path, &dll_stat) == 0;
	if (!dll_exists) {
		errno = 0;
		if (access(dll_path, F_OK) && errno == ENOENT) {
			try_create_parent_dirs(dll_path);
			errno = 0;
		}
		backend_assert(errno == 0 || errno == ENOENT, "access: %s", strerror(errno));
	}

	generate_shared_object(dll_path);

	return false;
}

USED_BY_PROGRAMS struct grug_backend grug_backend_linux = {
	.load = load,
};
