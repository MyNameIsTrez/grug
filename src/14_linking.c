//// LINKING

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

static thread_local u64 grug_max_rsp;
static thread_local struct timespec grug_current_time;
static thread_local struct timespec grug_max_time;

static void reset_generate_shared_object(void) {
	symbols_size = 0;
	data_symbols_size = 0;
	extern_data_symbols_size = 0;
	shuffled_symbols_size = 0;
	bytes_size = 0;
	game_fn_offsets_size = 0;
	global_variable_offsets_size = 0;
}

USED_BY_MODS bool grug_is_time_limit_exceeded(void);
USED_BY_MODS bool grug_is_time_limit_exceeded(void) {
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &grug_current_time);

	if (grug_current_time.tv_sec < grug_max_time.tv_sec) {
		return false;
	}

	if (grug_current_time.tv_sec > grug_max_time.tv_sec) {
		return true;
	}

	return grug_current_time.tv_nsec > grug_max_time.tv_nsec;
}

USED_BY_MODS void grug_set_time_limit(void);
USED_BY_MODS void grug_set_time_limit(void) {
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &grug_max_time);

	grug_max_time.tv_sec += on_fn_time_limit_sec;

	grug_max_time.tv_nsec += on_fn_time_limit_ns;

	if (grug_max_time.tv_nsec >= NS_PER_SEC) {
		grug_max_time.tv_nsec -= NS_PER_SEC;
		grug_max_time.tv_sec++;
	}
}

USED_BY_MODS u64* grug_get_max_rsp_addr(void);
USED_BY_MODS u64* grug_get_max_rsp_addr(void) {
    return &grug_max_rsp;
}

USED_BY_MODS u64 grug_get_max_rsp(void);
USED_BY_MODS u64 grug_get_max_rsp(void) {
    return grug_max_rsp;
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
	if (on_fns_size == 0) {
		return NULL;
	}

	u32 i = buckets_on_fns[elf_hash(name) % on_fns_size];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, on_fns[i].fn_name)) {
			break;
		}

		i = chains_on_fns[i];
	}

	return on_fns + i;
}

static void hash_on_fns(void) {
	memset(buckets_on_fns, 0xff, on_fns_size * sizeof(u32));

	for (size_t i = 0; i < on_fns_size; i++) {
		const char *name = on_fns[i].fn_name;

		grug_assert(!get_on_fn(name), "The function '%s' was defined several times in the same file", name);

		u32 bucket_index = elf_hash(name) % on_fns_size;

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
	for (size_t i = 0; i < grug_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(grug_entity->on_functions[i].name);
		if (on_fn) {
			size_t on_fn_index = on_fn - on_fns;

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
	grug_assert(game_fn_offsets_size < MAX_GAME_FN_OFFSETS, "There are more than %d game functions, exceeding MAX_GAME_FN_OFFSETS", MAX_GAME_FN_OFFSETS);

	game_fn_offsets[game_fn_offsets_size++] = (struct offset){
		.name = fn_name,
		.offset = offset,
	};
}

static bool has_got(void) {
	return global_variables_size > 1 || on_fns_size > 0;
}

// Used for both .plt and .rela.plt
static bool has_plt(void) {
	return extern_fn_calls_size > 0;
}

static bool has_rela_dyn(void) {
	return global_variables_size > 1 || on_fns_size > 0 || resources_size > 0 || entity_dependencies_size > 0;
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
	grug_assert(global_variable_offsets_size < MAX_GLOBAL_VARIABLE_OFFSETS, "There are more than %d game functions, exceeding MAX_GLOBAL_VARIABLE_OFFSETS", MAX_GLOBAL_VARIABLE_OFFSETS);

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
	grug_assert(bytes_size < MAX_BYTES, "There are more than %d bytes, exceeding MAX_BYTES", MAX_BYTES);

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
	push_64(globals_bytes);

	// "on_fns" function addresses
	size_t previous_on_fn_index = 0;
	for (size_t i = 0; i < grug_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(grug_entity->on_functions[i].name);
		if (on_fn) {
			size_t on_fn_index = on_fn - on_fns;
			grug_assert(previous_on_fn_index <= on_fn_index, "The function '%s' needs to be moved before/after a different on_ function, according to the entity '%s' in mod_api.json", on_fn->fn_name, grug_entity->name);
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
		push_dynamic_entry(DT_RELASZ, (on_fns_size + extern_data_symbols_size + resources_size + 2 * entity_dependencies_size) * RELA_ENTRY_SIZE);
		push_dynamic_entry(DT_RELAENT, RELA_ENTRY_SIZE);

		size_t rela_count = on_fns_size + resources_size + 2 * entity_dependencies_size;
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
	count += on_fns_size > 0;

	if (count > 0) {
		padding -= entry_size;
	}

	push_zeros(padding);
}

static void push_text(void) {
	grug_log_section(".text");

	text_offset = bytes_size;

	grug_assert(bytes_size + codes_size < MAX_BYTES, "There are more than %d bytes, exceeding MAX_BYTES", MAX_BYTES);

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

	for (size_t i = 0; i < grug_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(grug_entity->on_functions[i].name);
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
	if (grug_entity->on_function_count > 0) {
		data_offsets[i++] = offset;
		for (size_t on_fn_index = 0; on_fn_index < grug_entity->on_function_count; on_fn_index++) {
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
	grug_assert(shuffled_symbols_size < MAX_SYMBOLS, "There are more than %d symbols, exceeding MAX_SYMBOLS", MAX_SYMBOLS);

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
	grug_assert(symbols_size < MAX_SYMBOLS, "There are more than %d symbols, exceeding MAX_SYMBOLS", MAX_SYMBOLS);

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

	if (grug_entity->on_function_count > 0) {
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
		grug_unreachable();
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
	for (size_t i = 0; i < on_fns_size; i++) {
		push_symbol(on_fns[i].fn_name);
	}

	for (size_t i = 0; i < helper_fns_size; i++) {
		push_symbol(get_safe_helper_fn_name(helper_fns[i].fn_name));
		push_symbol(get_fast_helper_fn_name(helper_fns[i].fn_name));
	}

	init_symbol_name_dynstr_offsets();

	generate_shuffled_symbols();

	init_symbol_name_strtab_offsets();

	init_data_offsets();

	hash_on_fns();

	push_bytes();

	patch_bytes();

	FILE *f = fopen(dll_path, "w");
	grug_assert(f, "fopen: %s", strerror(errno));
	grug_assert(fwrite(bytes, sizeof(u8), bytes_size, f) > 0, "fwrite error");
	grug_assert(fclose(f) == 0, "fclose: %s", strerror(errno));
}
