#include "02_includes_and_defines.c"

//// UTILS

typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
typedef float f32;

USED_BY_PROGRAMS struct grug_error grug_error;
USED_BY_PROGRAMS bool grug_loading_error_in_grug_file;

static struct grug_error previous_grug_error;
static jmp_buf error_jmp_buffer;

static char mods_root_dir_path[STUPID_MAX_PATH];
static char dll_root_dir_path[STUPID_MAX_PATH];

static bool streq(const char *a, const char *b) {
	return strcmp(a, b) == 0;
}

static bool starts_with(const char *haystack, const char *needle) {
	return strncmp(haystack, needle, strlen(needle)) == 0;
}

static bool ends_with(const char *haystack, const char *needle) {
	size_t len_haystack = strlen(haystack);
	size_t len_needle = strlen(needle);
	if (len_haystack < len_needle) {
		return false;
	}
	return strncmp(haystack + len_haystack - len_needle, needle, len_needle) == 0;
}

// From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elf.c#l193
static u32 elf_hash(const char *namearg) {
	u32 h = 0;

	for (const unsigned char *name = (const unsigned char *) namearg; *name; name++) {
		h = (h << 4) + *name;
		h ^= (h >> 24) & 0xf0;
	}

	return h & 0x0fffffff;
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

static const char *get_file_extension(const char *filename) {
	const char *ext = strrchr(filename, '.');
	if (ext) {
		return ext;
	}
	return "";
}

static void print_dlerror(const char *function_name) {
	const char *err = dlerror();
	grug_assert(err, "dlerror() was asked to find an error string, but it couldn't find one");
	grug_error("%s: %s", function_name, err);
}

static void *get_dll_symbol(void *dll, const char *symbol_name) {
	return dlsym(dll, symbol_name);
}
