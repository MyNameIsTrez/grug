#include "12_type_propagation.c"

//// COMPILING

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
#define MAX_RESOURCES 420420
#define MAX_HELPER_FN_MODE_NAMES_CHARACTERS 420420
#define MAX_LOOP_DEPTH 420
#define MAX_BREAK_STATEMENTS_PER_LOOP 420

#define NEXT_INSTRUCTION_OFFSET sizeof(u32)

// 0xDEADBEEF in little-endian
#define PLACEHOLDER_8 0xDE
#define PLACEHOLDER_16 0xADDE
#define PLACEHOLDER_32 0xEFBEADDE
#define PLACEHOLDER_64 0xEFBEADDEEFBEADDE

// We use a limit of 64 KiB, since native JNI methods can use up to 80 KiB
// without a risk of a JVM crash:
// See https://pangin.pro/posts/stack-overflow-handling
#define GRUG_STACK_LIMIT 0x10000

#define NS_PER_MS 1000000
#define MS_PER_SEC 1000
#define NS_PER_SEC 1000000000

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
}

static const char *get_helper_fn_mode_name(const char *name, bool safe) {
	size_t length = strlen(name);

	grug_assert(helper_fn_mode_names_size + length + (sizeof("_safe") - 1) < MAX_HELPER_FN_MODE_NAMES_CHARACTERS, "There are more than %d characters in the helper_fn_mode_names array, exceeding MAX_HELPER_FN_MODE_NAMES_CHARACTERS", MAX_HELPER_FN_MODE_NAMES_CHARACTERS);

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
	grug_assert(helper_fn_offsets_size < MAX_HELPER_FN_OFFSETS, "There are more than %d helper functions, exceeding MAX_HELPER_FN_OFFSETS", MAX_HELPER_FN_OFFSETS);

	helper_fn_offsets[helper_fn_offsets_size++] = (struct offset){
		.name = fn_name,
		.offset = offset,
	};
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
	grug_assert(helper_fn_calls_size < MAX_HELPER_FN_CALLS, "There are more than %d helper function calls, exceeding MAX_HELPER_FN_CALLS", MAX_HELPER_FN_CALLS);

	helper_fn_calls[helper_fn_calls_size++] = (struct offset){
		.name = fn_name,
		.offset = codes_offset,
	};
}

static const char *push_used_extern_fn_symbol(const char *name, bool is_game_fn) {
	size_t length = strlen(name);
	size_t fn_prefix_length = is_game_fn ? sizeof(GAME_FN_PREFIX) - 1 : 0;

	grug_assert(used_extern_fn_symbols_size + fn_prefix_length + length < MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS, "There are more than %d characters in the used_extern_fn_symbols array, exceeding MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS", MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS);

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
	grug_assert(extern_fn_calls_size < MAX_GAME_FN_CALLS, "There are more than %d game function calls, exceeding MAX_GAME_FN_CALLS", MAX_GAME_FN_CALLS);

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
	grug_assert(data_string_codes_size < MAX_DATA_STRING_CODES, "There are more than %d data string code bytes, exceeding MAX_DATA_STRING_CODES", MAX_DATA_STRING_CODES);

	data_string_codes[data_string_codes_size++] = (struct data_string_code){
		.string = string,
		.code_offset = code_offset,
	};
}

static void compile_byte(u8 byte) {
	grug_assert(codes_size < MAX_CODES, "There are more than %d code bytes, exceeding MAX_CODES", MAX_CODES);

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

static void move_arguments(struct argument *fn_arguments, size_t argument_count) {
	size_t integer_argument_index = 0;
	size_t float_argument_index = 0;

	// Every function starts with `push rbp`, `mov rbp, rsp`,
	// so because calling a function always pushes the return address (8 bytes),
	// and the `push rbp` also pushes 8 bytes, the spilled args start at `rbp-0x10`
	size_t spill_offset = 0x10;

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		struct argument arg = fn_arguments[argument_index];

		size_t offset = get_local_variable(arg.name)->offset;

		// We skip EDI/RDI, since that is reserved by the secret global variables pointer
		switch (arg.type) {
			case type_void:
			case type_resource:
			case type_entity:
				grug_unreachable();
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
	grug_assert(loop_depth > 0, "There is a break statement that isn't inside of a while loop");

	struct loop_break_statements *loop_break_statements = &loop_break_statements_stack[loop_depth - 1];

	grug_assert(loop_break_statements->break_statements_size < MAX_BREAK_STATEMENTS_PER_LOOP, "There are more than %d break statements in one of the while loops, exceeding MAX_BREAK_STATEMENTS_PER_LOOP", MAX_BREAK_STATEMENTS_PER_LOOP);

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
	grug_assert(used_extern_global_variables_size < MAX_USED_EXTERN_GLOBAL_VARIABLES, "There are more than %d usages of game global variables, exceeding MAX_USED_EXTERN_GLOBAL_VARIABLES", MAX_USED_EXTERN_GLOBAL_VARIABLES);

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
	grug_assert(loop_depth > 0, "There is a continue statement that isn't inside of a while loop");
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

	grug_assert(loop_depth < MAX_LOOP_DEPTH, "There are more than %d while loops nested inside each other, exceeding MAX_LOOP_DEPTH", MAX_LOOP_DEPTH);
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

	bool calls_helper_fn = get_helper_fn(fn_name) != NULL;

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
	bool calls_game_fn = game_fn != NULL;
	assert(calls_helper_fn || calls_game_fn);


	bool returns_float = false;
	if (calls_game_fn) {
		push_game_fn_call(fn_name, codes_size);

		returns_float = game_fn->return_type == type_f32;
	} else {
		struct helper_fn *helper_fn = get_helper_fn(fn_name);
		if (helper_fn) {
			push_helper_fn_call(get_helper_fn_mode_name(fn_name, !compiling_fast_mode), codes_size);
			returns_float = helper_fn->return_type == type_f32;
		} else {
			grug_unreachable();
		}
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
			grug_unreachable();
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
			grug_unreachable();
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
			grug_unreachable();
	}
}

static void push_entity_dependency(u32 string_index) {
	grug_assert(entity_dependencies_size < MAX_ENTITY_DEPENDENCIES, "There are more than %d entity dependencies, exceeding MAX_ENTITY_DEPENDENCIES", MAX_ENTITY_DEPENDENCIES);

	entity_dependencies[entity_dependencies_size++] = string_index;
}

static void push_resource(u32 string_index) {
	grug_assert(resources_size < MAX_RESOURCES, "There are more than %d resources, exceeding MAX_RESOURCES", MAX_RESOURCES);

	resources[resources_size++] = string_index;
}

static const char *push_entity_dependency_string(const char *string) {
	static char entity[MAX_ENTITY_DEPENDENCY_NAME_LENGTH];

	if (strchr(string, ':')) {
		grug_assert(strlen(string) + 1 <= sizeof(entity), "There are more than %d characters in the entity string '%s', exceeding MAX_ENTITY_DEPENDENCY_NAME_LENGTH", MAX_ENTITY_DEPENDENCY_NAME_LENGTH, string);

		memcpy(entity, string, strlen(string) + 1);
	} else {
		snprintf(entity, sizeof(entity), "%s:%s", mod, string);
	}

	size_t length = strlen(entity);

	grug_assert(entity_dependency_strings_size + length < MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS, "There are more than %d characters in the entity_dependency_strings array, exceeding MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS", MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS);

	const char *entity_str = entity_dependency_strings + entity_dependency_strings_size;

	for (size_t i = 0; i < length; i++) {
		entity_dependency_strings[entity_dependency_strings_size++] = entity[i];
	}
	entity_dependency_strings[entity_dependency_strings_size++] = '\0';

	return entity_str;
}

static const char *push_resource_string(const char *string) {
	static char resource[STUPID_MAX_PATH];
	grug_assert(snprintf(resource, sizeof(resource), "%s/%s/%s", mods_root_dir_path, mod, string) >= 0, "Filling the variable 'resource' failed");

	size_t length = strlen(resource);

	grug_assert(resource_strings_size + length < MAX_RESOURCE_STRINGS_CHARACTERS, "There are more than %d characters in the resource_strings array, exceeding MAX_RESOURCE_STRINGS_CHARACTERS", MAX_RESOURCE_STRINGS_CHARACTERS);

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
				// can have with different "entity_type" values in mod_api.json
				// (namely, game fn 1 might have "car", and game fn 2 the empty string "")
				push_entity_dependency(get_data_string_index(string));
			}

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case IDENTIFIER_EXPR: {
			struct variable *var = get_local_variable(expr.literal.string);
			if (var) {
				switch (var->type) {
					case type_void:
					case type_resource:
					case type_entity:
						grug_unreachable();
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
					grug_unreachable();
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
			grug_unreachable();
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
			grug_assert(!compiled_init_globals_fn, "Global id variables can't be reassigned");
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
		add_local_variable(variable_statement.name, variable_statement.type, variable_statement.type_name);
	}

	struct variable *var = get_local_variable(variable_statement.name);
	if (var) {
		switch (var->type) {
			case type_void:
			case type_resource:
			case type_entity:
				grug_unreachable();
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

	mark_local_variables_unreachable(body_statements, statement_count);
}

static void calc_max_local_variable_stack_usage(struct statement *body_statements, size_t statement_count) {
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				if (statement.variable_statement.has_type) {
					stack_frame_bytes += type_sizes[statement.variable_statement.type];

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
			assert(stack_frame_bytes >= type_sizes[statement.variable_statement.type]);
			stack_frame_bytes -= type_sizes[statement.variable_statement.type];
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

static void compile_on_fn_impl(const char *fn_name, struct argument *fn_arguments, size_t argument_count, struct statement *body_statements, size_t body_statement_count, const char *grug_path, bool on_fn_calls_helper_fn, bool on_fn_contains_while_loop) {
	add_argument_variables(fn_arguments, argument_count);

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
	add_argument_variables(fn_arguments, argument_count);

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
	// If there are no other global variables or global config calls,
	// take a shortcut
	if (global_variables_size == 1 && global_config_calls_size == 0) {
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
	
	for (size_t i = 0; i < global_config_calls_size; i++) {
		struct expr global = global_config_calls[i];

		compile_expr(global);
	}
	assert(pushed == 0);

	for (size_t i = 0; i < global_variable_statements_size; i++) {
		struct global_variable_statement global = global_variable_statements[i];

		compile_expr(global.assignment_expr);

		compile_global_variable_statement(global.name);
	}
	assert(pushed == 0);

	compile_function_epilogue();

	overwrite_jmp_address_32(skip_safe_code_offset, codes_size);

	compiling_fast_mode = true;
	for (size_t i = 0; i < global_config_calls_size; i++) {
		struct expr global = global_config_calls[i];

		compile_expr(global);
	}

	for (size_t i = 0; i < global_variable_statements_size; i++) {
		struct global_variable_statement global = global_variable_statements[i];

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

	for (size_t on_fn_index = 0; on_fn_index < on_fns_size; on_fn_index++) {
		struct on_fn fn = on_fns[on_fn_index];

		compile_on_fn(fn, grug_path);

		text_offsets[text_offset_index++] = text_offset;
		text_offset = codes_size;
	}

	for (size_t helper_fn_index = 0; helper_fn_index < helper_fns_size; helper_fn_index++) {
		struct helper_fn fn = helper_fns[helper_fn_index];

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
