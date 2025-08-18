// NOTE: DON'T EDIT THIS FILE! IT IS AUTOMATICALLY REGENERATED BASED ON THE FILES IN src/
// Regenerated on 2025-08-18T15:35:45Z

//// GRUG DOCUMENTATION
//
// See the bottom of this file for the MIT license
//
// See my YouTube video explaining and showcasing grug: https://youtu.be/4oUToVXR2Vo
//
// See [my blog post](https://mynameistrez.github.io/2024/02/29/creating-the-perfect-modding-language.html) for an introduction to the grug modding language.
//
// You can find its test suite [here](https://github.com/MyNameIsTrez/grug-tests).
//
// In VS Code, you can install the extension simply called `grug` to get syntax highlighting and a grug file icon.
//
// ## Contributing
//
// Create an issue in [grug's GitHub repository](https://github.com/MyNameIsTrez/grug), before you make any pull request.
//
// This gives everyone the chance to discuss it, and prevents your hard work from being rejected.
//
// ## Sections
//
// This file is composed of sections, which you can jump between by searching for `////` in the file:
//
// 1. GRUG DOCUMENTATION
// 2. INCLUDES AND DEFINES
// 3. UTILS
// 4. RUNTIME ERROR HANDLING
// 5. JSON
// 6. PARSING MOD API JSON
// 7. READING
// 8. TOKENIZATION
// 9. PARSING
// 10. DUMPING AST
// 11. APPLYING AST
// 12. TYPE PROPAGATION
// 13. COMPILING
// 14. LINKING
// 15. HOT RELOADING
//
// ## Programs showcasing grug
//
// - [grug mod loader for Minecraft](https://github.com/MyNameIsTrez/grug-mod-loader-for-minecraft)
// - [Box2D and raylib game](https://github.com/MyNameIsTrez/grug-box2d-and-raylib-game)
// - [terminal game: C/C++](https://github.com/MyNameIsTrez/grug-terminal-game-c-cpp)
// - [terminal game: Python](https://github.com/MyNameIsTrez/grug-terminal-game-python)
// - [terminal game: Java](https://github.com/MyNameIsTrez/grug-terminal-game-java)
// - [grug benchmarks](https://github.com/MyNameIsTrez/grug-benchmarks)

//// INCLUDES AND DEFINES

#define _XOPEN_SOURCE 700 // This is just so VS Code can find CLOCK_PROCESS_CPUTIME_ID

#include "grug.h"

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

// "The problem is that you can't meaningfully define a constant like this
// in a header file. The maximum path size is actually to be something
// like a filesystem limitation, or at the very least a kernel parameter.
// This means that it's a dynamic value, not something preordained."
// https://eklitzke.org/path-max-is-tricky
#define STUPID_MAX_PATH 4096

static bool streq(const char *a, const char *b);

#define grug_error(...) {\
	if (snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__) < 0) {\
		abort();\
	}\
	\
	grug_error.grug_c_line_number = __LINE__;\
	\
	grug_error.has_changed =\
		!streq(grug_error.msg, previous_grug_error.msg)\
	 || !streq(grug_error.path, previous_grug_error.path)\
	 || grug_error.grug_c_line_number != previous_grug_error.grug_c_line_number;\
	\
	memcpy(previous_grug_error.msg, grug_error.msg, sizeof(grug_error.msg));\
	memcpy(previous_grug_error.path, grug_error.path, sizeof(grug_error.path));\
	previous_grug_error.grug_c_line_number = grug_error.grug_c_line_number;\
	\
	longjmp(error_jmp_buffer, 1);\
}

#define grug_assert(condition, ...) {\
	if (!(condition)) {\
		grug_error(__VA_ARGS__);\
	}\
}

#ifdef CRASH_ON_UNREACHABLE
#define grug_unreachable() {\
	assert(false && "This line of code is supposed to be unreachable. Please report this bug to the grug developers!");\
}
#else
#define grug_unreachable() {\
	grug_error("This line of code in grug.c:%d is supposed to be unreachable. Please report this bug to the grug developers!", __LINE__);\
}
#endif

#ifdef LOGGING
#define grug_log(...) fprintf(stderr, __VA_ARGS__)
#else
#define grug_log(...) {\
	_Pragma("GCC diagnostic push")\
	_Pragma("GCC diagnostic ignored \"-Wunused-value\"")\
	__VA_ARGS__;\
	_Pragma("GCC diagnostic pop")\
}
#endif

#define USED_BY_MODS
#define USED_BY_PROGRAMS

#define BFD_HASH_BUCKET_SIZE 4051 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l345

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

//// RUNTIME ERROR HANDLING

static char runtime_error_reason[420];

static uint64_t on_fn_time_limit_ms;
static size_t on_fn_time_limit_sec;
static size_t on_fn_time_limit_ns;

USED_BY_MODS grug_runtime_error_handler_t grug_runtime_error_handler = NULL;

static const char *grug_get_runtime_error_reason(enum grug_runtime_error_type type) {
	switch (type) {
		case GRUG_ON_FN_DIVISION_BY_ZERO:
			return "Division of an i32 by 0";
		case GRUG_ON_FN_STACK_OVERFLOW:
			return "Stack overflow, so check for accidental infinite recursion";
		case GRUG_ON_FN_TIME_LIMIT_EXCEEDED: {
			snprintf(runtime_error_reason, sizeof(runtime_error_reason), "Took longer than %" PRIu64 " milliseconds to run", on_fn_time_limit_ms);
			return runtime_error_reason;
		}
		case GRUG_ON_FN_OVERFLOW:
			return "i32 overflow";
		case GRUG_ON_FN_GAME_FN_ERROR:
			return runtime_error_reason;
	}
	grug_unreachable();
}

USED_BY_MODS void grug_call_runtime_error_handler(enum grug_runtime_error_type type);
void grug_call_runtime_error_handler(enum grug_runtime_error_type type) {
	const char *reason = grug_get_runtime_error_reason(type);

	grug_runtime_error_handler(reason, type, grug_fn_name, grug_fn_path);
}

//// JSON

#define JSON_MAX_CHARACTERS 420420
#define JSON_MAX_TOKENS 420420
#define JSON_MAX_NODES 420420
#define JSON_MAX_FIELDS 420420
#define JSON_MAX_CHILD_NODES 1337
#define JSON_MAX_STRINGS_CHARACTERS 420420
#define JSON_MAX_RECURSION_DEPTH 42

#define json_error(error) {\
	grug_error("JSON error: %s: %s", json_file_path, json_error_messages[error]);\
}

#define json_assert(condition, error) {\
	if (!(condition)) {\
		json_error(error);\
	}\
}

struct json_array {
	struct json_node *values;
	size_t value_count;
};

struct json_object {
	struct json_field *fields;
	size_t field_count;
};

struct json_field {
	const char *key;
	struct json_node *value;
};

struct json_node {
	enum {
		JSON_NODE_STRING,
		JSON_NODE_ARRAY,
		JSON_NODE_OBJECT,
	} type;
	union {
		const char *string;
		struct json_array array;
		struct json_object object;
	};
};

enum json_error {
	JSON_NO_ERROR,
	JSON_ERROR_FAILED_TO_OPEN_FILE,
	JSON_ERROR_FAILED_TO_CLOSE_FILE,
	JSON_ERROR_FILE_EMPTY,
	JSON_ERROR_FILE_TOO_BIG,
	JSON_ERROR_FILE_READING_ERROR,
	JSON_ERROR_UNRECOGNIZED_CHARACTER,
	JSON_ERROR_UNCLOSED_STRING,
	JSON_ERROR_DUPLICATE_KEY,
	JSON_ERROR_TOO_MANY_TOKENS,
	JSON_ERROR_TOO_MANY_NODES,
	JSON_ERROR_TOO_MANY_FIELDS,
	JSON_ERROR_TOO_MANY_CHILD_NODES,
	JSON_ERROR_MAX_RECURSION_DEPTH_EXCEEDED,
	JSON_ERROR_TRAILING_COMMA,
	JSON_ERROR_EXPECTED_ARRAY_CLOSE,
	JSON_ERROR_EXPECTED_OBJECT_CLOSE,
	JSON_ERROR_EXPECTED_COLON,
	JSON_ERROR_EXPECTED_VALUE,
	JSON_ERROR_UNEXPECTED_STRING,
	JSON_ERROR_UNEXPECTED_ARRAY_OPEN,
	JSON_ERROR_UNEXPECTED_ARRAY_CLOSE,
	JSON_ERROR_UNEXPECTED_OBJECT_OPEN,
	JSON_ERROR_UNEXPECTED_OBJECT_CLOSE,
	JSON_ERROR_UNEXPECTED_COMMA,
	JSON_ERROR_UNEXPECTED_COLON,
	JSON_ERROR_UNEXPECTED_EXTRA_CHARACTER,
};

static const char *json_error_messages[] = {
	[JSON_NO_ERROR] = "No error",
	[JSON_ERROR_FAILED_TO_OPEN_FILE] = "Failed to open file",
	[JSON_ERROR_FAILED_TO_CLOSE_FILE] = "Failed to close file",
	[JSON_ERROR_FILE_EMPTY] = "File is empty",
	[JSON_ERROR_FILE_TOO_BIG] = "File is too big",
	[JSON_ERROR_FILE_READING_ERROR] = "File reading error",
	[JSON_ERROR_UNRECOGNIZED_CHARACTER] = "Unrecognized character",
	[JSON_ERROR_UNCLOSED_STRING] = "Unclosed string",
	[JSON_ERROR_DUPLICATE_KEY] = "Duplicate key",
	[JSON_ERROR_TOO_MANY_TOKENS] = "Too many tokens",
	[JSON_ERROR_TOO_MANY_NODES] = "Too many nodes",
	[JSON_ERROR_TOO_MANY_FIELDS] = "Too many fields",
	[JSON_ERROR_TOO_MANY_CHILD_NODES] = "Too many child nodes",
	[JSON_ERROR_MAX_RECURSION_DEPTH_EXCEEDED] = "Max recursion depth exceeded",
	[JSON_ERROR_TRAILING_COMMA] = "Trailing comma",
	[JSON_ERROR_EXPECTED_ARRAY_CLOSE] = "Expected ']'",
	[JSON_ERROR_EXPECTED_OBJECT_CLOSE] = "Expected '}'",
	[JSON_ERROR_EXPECTED_COLON] = "Expected colon",
	[JSON_ERROR_EXPECTED_VALUE] = "Expected value",
	[JSON_ERROR_UNEXPECTED_STRING] = "Unexpected string",
	[JSON_ERROR_UNEXPECTED_ARRAY_OPEN] = "Unexpected '['",
	[JSON_ERROR_UNEXPECTED_ARRAY_CLOSE] = "Unexpected ']'",
	[JSON_ERROR_UNEXPECTED_OBJECT_OPEN] = "Unexpected '{'",
	[JSON_ERROR_UNEXPECTED_OBJECT_CLOSE] = "Unexpected '}'",
	[JSON_ERROR_UNEXPECTED_COMMA] = "Unexpected ','",
	[JSON_ERROR_UNEXPECTED_COLON] = "Unexpected ':'",
	[JSON_ERROR_UNEXPECTED_EXTRA_CHARACTER] = "Unexpected extra character",
};

static const char *json_file_path;

static size_t json_recursion_depth;

static char json_text[JSON_MAX_CHARACTERS];
static size_t json_text_size;

enum json_token_type {
	TOKEN_TYPE_STRING,
	TOKEN_TYPE_ARRAY_OPEN,
	TOKEN_TYPE_ARRAY_CLOSE,
	TOKEN_TYPE_OBJECT_OPEN,
	TOKEN_TYPE_OBJECT_CLOSE,
	TOKEN_TYPE_COMMA,
	TOKEN_TYPE_COLON,
};

struct json_token {
	enum json_token_type type;
	const char *str;
};
static struct json_token json_tokens[JSON_MAX_TOKENS];
static size_t json_tokens_size;

static struct json_node json_nodes[JSON_MAX_NODES];
static size_t json_nodes_size;

static struct json_field json_fields[JSON_MAX_FIELDS];
static size_t json_fields_size;

static u32 json_buckets[JSON_MAX_FIELDS];
static u32 json_chains[JSON_MAX_FIELDS];

static char json_strings[JSON_MAX_STRINGS_CHARACTERS];
static size_t json_strings_size;

static struct json_node json_parse_string(size_t *i);
static struct json_node json_parse_array(size_t *i);

static void json_push_node(struct json_node node) {
	json_assert(json_nodes_size < JSON_MAX_NODES, JSON_ERROR_TOO_MANY_NODES);
	json_nodes[json_nodes_size++] = node;
}

static void json_push_field(struct json_field field) {
	json_assert(json_fields_size < JSON_MAX_FIELDS, JSON_ERROR_TOO_MANY_FIELDS);
	json_fields[json_fields_size++] = field;
}

static bool is_duplicate_key(struct json_field *child_fields, size_t field_count, const char *key) {
	u32 i = json_buckets[elf_hash(key) % field_count];

	while (true) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (streq(key, child_fields[i].key)) {
			break;
		}

		i = json_chains[i];
	}

	return true;
}

static void check_duplicate_keys(struct json_field *child_fields, size_t field_count) {
	memset(json_buckets, 0xff, field_count * sizeof(u32));

	for (size_t i = 0; i < field_count; i++) {
		const char *key = child_fields[i].key;

		json_assert(!is_duplicate_key(child_fields, field_count, key), JSON_ERROR_DUPLICATE_KEY);

		u32 bucket_index = elf_hash(key) % field_count;

		json_chains[i] = json_buckets[bucket_index];

		json_buckets[bucket_index] = i;
	}
}

static struct json_node json_parse_object(size_t *i) {
	struct json_node node;

	node.type = JSON_NODE_OBJECT;
	(*i)++;

	json_recursion_depth++;
	json_assert(json_recursion_depth <= JSON_MAX_RECURSION_DEPTH, JSON_ERROR_MAX_RECURSION_DEPTH_EXCEEDED);

	node.object.field_count = 0;

	struct json_field child_fields[JSON_MAX_CHILD_NODES];

	bool seen_key = false;
	bool seen_colon = false;
	bool seen_value = false;
	bool seen_comma = false;

	struct json_field field;

	struct json_node string;
	struct json_node array;
	struct json_node object;

	while (*i < json_tokens_size) {
		struct json_token *token = json_tokens + *i;

		switch (token->type) {
		case TOKEN_TYPE_STRING:
			if (!seen_key) {
				seen_key = true;
				field.key = token->str;
				(*i)++;
			} else if (seen_colon && !seen_value) {
				seen_value = true;
				seen_comma = false;
				string = json_parse_string(i);
				field.value = json_nodes + json_nodes_size;
				json_push_node(string);
				json_assert(node.object.field_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
				child_fields[node.object.field_count++] = field;
			} else {
				json_error(JSON_ERROR_UNEXPECTED_STRING);
			}
			break;
		case TOKEN_TYPE_ARRAY_OPEN:
			if (seen_colon && !seen_value) {
				seen_value = true;
				seen_comma = false;
				array = json_parse_array(i);
				field.value = json_nodes + json_nodes_size;
				json_push_node(array);
				json_assert(node.object.field_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
				child_fields[node.object.field_count++] = field;
			} else {
				json_error(JSON_ERROR_UNEXPECTED_ARRAY_OPEN);
			}
			break;
		case TOKEN_TYPE_ARRAY_CLOSE:
			json_error(JSON_ERROR_UNEXPECTED_ARRAY_CLOSE);
		case TOKEN_TYPE_OBJECT_OPEN:
			if (seen_colon && !seen_value) {
				seen_value = true;
				seen_comma = false;
				object = json_parse_object(i);
				field.value = json_nodes + json_nodes_size;
				json_push_node(object);
				json_assert(node.object.field_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
				child_fields[node.object.field_count++] = field;
			} else {
				json_error(JSON_ERROR_UNEXPECTED_OBJECT_OPEN);
			}
			break;
		case TOKEN_TYPE_OBJECT_CLOSE:
			if (seen_key && !seen_colon) {
				json_error(JSON_ERROR_EXPECTED_COLON);
			} else if (seen_colon && !seen_value) {
				json_error(JSON_ERROR_EXPECTED_VALUE);
			} else if (seen_comma) {
				json_error(JSON_ERROR_TRAILING_COMMA);
			}
			check_duplicate_keys(child_fields, node.object.field_count);
			node.object.fields = json_fields + json_fields_size;
			for (size_t field_index = 0; field_index < node.object.field_count; field_index++) {
				json_push_field(child_fields[field_index]);
			}
			(*i)++;
			json_recursion_depth--;
			return node;
		case TOKEN_TYPE_COMMA:
			json_assert(seen_value, JSON_ERROR_UNEXPECTED_COMMA);
			seen_key = false;
			seen_colon = false;
			seen_value = false;
			seen_comma = true;
			(*i)++;
			break;
		case TOKEN_TYPE_COLON:
			json_assert(seen_key, JSON_ERROR_UNEXPECTED_COLON);
			seen_colon = true;
			(*i)++;
			break;
		}
	}

	json_error(JSON_ERROR_EXPECTED_OBJECT_CLOSE);
}

static struct json_node json_parse_array(size_t *i) {
	struct json_node node;

	node.type = JSON_NODE_ARRAY;
	(*i)++;

	json_recursion_depth++;
	json_assert(json_recursion_depth <= JSON_MAX_RECURSION_DEPTH, JSON_ERROR_MAX_RECURSION_DEPTH_EXCEEDED);

	node.array.value_count = 0;

	struct json_node child_nodes[JSON_MAX_CHILD_NODES];

	bool seen_value = false;
	bool seen_comma = false;

	while (*i < json_tokens_size) {
		struct json_token *token = json_tokens + *i;

		switch (token->type) {
		case TOKEN_TYPE_STRING:
			json_assert(!seen_value, JSON_ERROR_UNEXPECTED_STRING);
			seen_value = true;
			seen_comma = false;
			json_assert(node.array.value_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
			child_nodes[node.array.value_count++] = json_parse_string(i);
			break;
		case TOKEN_TYPE_ARRAY_OPEN:
			json_assert(!seen_value, JSON_ERROR_UNEXPECTED_ARRAY_OPEN);
			seen_value = true;
			seen_comma = false;
			json_assert(node.array.value_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
			child_nodes[node.array.value_count++] = json_parse_array(i);
			break;
		case TOKEN_TYPE_ARRAY_CLOSE:
			json_assert(!seen_comma, JSON_ERROR_TRAILING_COMMA);
			node.array.values = json_nodes + json_nodes_size;
			for (size_t value_index = 0; value_index < node.array.value_count; value_index++) {
				json_push_node(child_nodes[value_index]);
			}
			(*i)++;
			json_recursion_depth--;
			return node;
		case TOKEN_TYPE_OBJECT_OPEN:
			json_assert(!seen_value, JSON_ERROR_UNEXPECTED_OBJECT_OPEN);
			seen_value = true;
			seen_comma = false;
			json_assert(node.array.value_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
			child_nodes[node.array.value_count++] = json_parse_object(i);
			break;
		case TOKEN_TYPE_OBJECT_CLOSE:
			json_error(JSON_ERROR_UNEXPECTED_OBJECT_CLOSE);
		case TOKEN_TYPE_COMMA:
			json_assert(seen_value, JSON_ERROR_UNEXPECTED_COMMA);
			seen_value = false;
			seen_comma = true;
			(*i)++;
			break;
		case TOKEN_TYPE_COLON:
			json_error(JSON_ERROR_UNEXPECTED_COLON);
		}
	}

	json_error(JSON_ERROR_EXPECTED_ARRAY_CLOSE);
}

static struct json_node json_parse_string(size_t *i) {
	struct json_node node;

	node.type = JSON_NODE_STRING;

	struct json_token *token = json_tokens + *i;
	node.string = token->str;

	(*i)++;

	return node;
}

static struct json_node json_parse(size_t *i) {
	struct json_token *t = json_tokens + *i;
	struct json_node node;

	switch (t->type) {
	case TOKEN_TYPE_STRING:
		node = json_parse_string(i);
		break;
	case TOKEN_TYPE_ARRAY_OPEN:
		node = json_parse_array(i);
		break;
	case TOKEN_TYPE_ARRAY_CLOSE:
		json_error(JSON_ERROR_UNEXPECTED_ARRAY_CLOSE);
	case TOKEN_TYPE_OBJECT_OPEN:
		node = json_parse_object(i);
		break;
	case TOKEN_TYPE_OBJECT_CLOSE:
		json_error(JSON_ERROR_UNEXPECTED_OBJECT_CLOSE);
	case TOKEN_TYPE_COMMA:
		json_error(JSON_ERROR_UNEXPECTED_COMMA);
	case TOKEN_TYPE_COLON:
		json_error(JSON_ERROR_UNEXPECTED_COLON);
	}

	json_assert(*i >= json_tokens_size, JSON_ERROR_UNEXPECTED_EXTRA_CHARACTER);

	return node;
}

static const char *json_push_string(const char *slice_start, size_t length) {
	grug_assert(json_strings_size + length < JSON_MAX_STRINGS_CHARACTERS, "There are more than %d characters in the json_strings array, exceeding JSON_MAX_STRINGS_CHARACTERS", JSON_MAX_STRINGS_CHARACTERS);

	const char *new_str = json_strings + json_strings_size;

	for (size_t i = 0; i < length; i++) {
		json_strings[json_strings_size++] = slice_start[i];
	}
	json_strings[json_strings_size++] = '\0';

	return new_str;
}

static void json_push_token(enum json_token_type type, size_t offset, size_t length) {
	json_assert(json_tokens_size < JSON_MAX_TOKENS, JSON_ERROR_TOO_MANY_TOKENS);
	json_tokens[json_tokens_size++] = (struct json_token){
		.type = type,
		.str = json_push_string(json_text + offset, length),
	};
}

static void json_tokenize(void) {
	size_t i = 0;

	while (i < json_text_size) {
		if (json_text[i] == '"') {
			size_t string_start_index = i;

			while (++i < json_text_size && json_text[i] != '"') {}

			json_assert(json_text[i] == '"', JSON_ERROR_UNCLOSED_STRING);

			json_push_token(
				TOKEN_TYPE_STRING,
				string_start_index + 1,
				i - string_start_index - 1
			);
		} else if (json_text[i] == '[') {
			json_push_token(TOKEN_TYPE_ARRAY_OPEN, i, 1);
		} else if (json_text[i] == ']') {
			json_push_token(TOKEN_TYPE_ARRAY_CLOSE, i, 1);
		} else if (json_text[i] == '{') {
			json_push_token(TOKEN_TYPE_OBJECT_OPEN, i, 1);
		} else if (json_text[i] == '}') {
			json_push_token(TOKEN_TYPE_OBJECT_CLOSE, i, 1);
		} else if (json_text[i] == ',') {
			json_push_token(TOKEN_TYPE_COMMA, i, 1);
		} else if (json_text[i] == ':') {
			json_push_token(TOKEN_TYPE_COLON, i, 1);
		} else if (!isspace(json_text[i])) {
			json_error(JSON_ERROR_UNRECOGNIZED_CHARACTER);
		}
		i++;
	}
}

static void json_read_text(const char *file_path) {
	FILE *f = fopen(file_path, "r");
	if (!f) {
		grug_error("JSON error: %s '%s'", json_error_messages[JSON_ERROR_FAILED_TO_OPEN_FILE], file_path);
	}

	json_text_size = fread(
		json_text,
		sizeof(char),
		JSON_MAX_CHARACTERS,
		f
	);

	int is_eof = feof(f);
	int err = ferror(f);

	json_assert(fclose(f) == 0, JSON_ERROR_FAILED_TO_CLOSE_FILE);

	json_assert(json_text_size != 0, JSON_ERROR_FILE_EMPTY);
	json_assert(is_eof && json_text_size != JSON_MAX_CHARACTERS, JSON_ERROR_FILE_TOO_BIG);
	json_assert(err == 0, JSON_ERROR_FILE_READING_ERROR);

	json_text[json_text_size] = '\0';
}

static void json_reset(void) {
	json_file_path = NULL;
	json_recursion_depth = 0;
	json_text_size = 0;
	json_tokens_size = 0;
	json_nodes_size = 0;
	json_strings_size = 0;
	json_fields_size = 0;
}

static void json(const char *file_path, struct json_node *returned) {
	json_reset();

	json_file_path = file_path;

	json_read_text(file_path);

	json_tokenize();

	size_t token_index = 0;
	*returned = json_parse(&token_index);
}

//// PARSING MOD API JSON

#define MAX_GRUG_ENTITIES 420420
#define MAX_GRUG_ON_FUNCTIONS 420420
#define MAX_GRUG_GAME_FUNCTIONS 420420
#define MAX_GRUG_ARGUMENTS 420420

enum type {
	type_void,
	type_bool,
	type_i32,
	type_f32,
	type_string,
	type_id,
	type_resource,
	type_entity,
};
static size_t type_sizes[] = {
	[type_bool] = sizeof(bool),
	[type_i32] = sizeof(i32),
	[type_f32] = sizeof(float),
	[type_string] = sizeof(const char *),
	[type_id] = sizeof(u64),
	[type_resource] = sizeof(const char *),
	[type_entity] = sizeof(const char *),
};

struct grug_on_function {
	const char *name;
	struct argument *arguments;
	size_t argument_count;
};

struct grug_entity {
	const char *name;
	struct grug_on_function *on_functions;
	size_t on_function_count;
};

struct grug_game_function {
	const char *name;
	enum type return_type;
	const char *return_type_name;
	struct argument *arguments;
	size_t argument_count;
};

struct argument {
	const char *name;
	enum type type;
	const char *type_name;
	union {
		const char *resource_extension; // This is optional
		const char *entity_type; // This is optional
	};
};

static struct grug_entity grug_entities[MAX_GRUG_ENTITIES];
static size_t grug_entities_size;

static struct grug_on_function grug_on_functions[MAX_GRUG_ON_FUNCTIONS];
static size_t grug_on_functions_size;

static struct grug_game_function grug_game_functions[MAX_GRUG_GAME_FUNCTIONS];
static size_t grug_game_functions_size;
static u32 buckets_game_fns[MAX_GRUG_GAME_FUNCTIONS];
static u32 chains_game_fns[MAX_GRUG_GAME_FUNCTIONS];

static struct argument grug_arguments[MAX_GRUG_ARGUMENTS];
static size_t grug_arguments_size;

static char mod_api_strings[JSON_MAX_STRINGS_CHARACTERS];
static size_t mod_api_strings_size;

static void push_grug_entity(struct grug_entity fn) {
	grug_assert(grug_entities_size < MAX_GRUG_ENTITIES, "There are more than %d entities in mod_api.json, exceeding MAX_GRUG_ENTITIES", MAX_GRUG_ENTITIES);
	grug_entities[grug_entities_size++] = fn;
}

static void push_grug_on_function(struct grug_on_function fn) {
	grug_assert(grug_on_functions_size < MAX_GRUG_ON_FUNCTIONS, "There are more than %d on_ functions in mod_api.json, exceeding MAX_GRUG_ON_FUNCTIONS", MAX_GRUG_ON_FUNCTIONS);
	grug_on_functions[grug_on_functions_size++] = fn;
}

static struct grug_game_function *get_grug_game_fn(const char *name) {
	if (grug_game_functions_size == 0) {
		return NULL;
	}

	u32 i = buckets_game_fns[elf_hash(name) % grug_game_functions_size];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, grug_game_functions[i].name)) {
			break;
		}

		i = chains_game_fns[i];
	}

	return grug_game_functions + i;
}

static void hash_game_fns(void) {
	memset(buckets_game_fns, 0xff, grug_game_functions_size * sizeof(u32));

	for (size_t i = 0; i < grug_game_functions_size; i++) {
		const char *name = grug_game_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_game_functions_size;

		chains_game_fns[i] = buckets_game_fns[bucket_index];

		buckets_game_fns[bucket_index] = i;
	}
}

static void push_grug_game_function(struct grug_game_function fn) {
	grug_assert(grug_game_functions_size < MAX_GRUG_GAME_FUNCTIONS, "There are more than %d game functions in mod_api.json, exceeding MAX_GRUG_GAME_FUNCTIONS", MAX_GRUG_GAME_FUNCTIONS);
	grug_game_functions[grug_game_functions_size++] = fn;
}

static void push_grug_argument(struct argument argument) {
	grug_assert(grug_arguments_size < MAX_GRUG_ARGUMENTS, "There are more than %d grug arguments, exceeding MAX_GRUG_ARGUMENTS", MAX_GRUG_ARGUMENTS);
	grug_arguments[grug_arguments_size++] = argument;
}

static void check_custom_id_is_pascal(const char *type_name) {
	// The first character must always be uppercase.
	grug_assert(isupper(type_name[0]), "'%s' seems like a custom ID type, but isn't in PascalCase", type_name);

	// Custom IDs only consist of uppercase and lowercase characters.
	for (const char *p = type_name; *p; p++) {
		char c = *p;
		grug_assert(isupper(c) || islower(c) || isdigit(c), "'%s' seems like a custom ID type, but it contains '%c', which isn't uppercase/lowercase/a digit", type_name, c);
	}
}

static void check_custom_id_type_capitalization(const char *type_name) {
	// If it is not a custom ID, return.
	if (streq(type_name, "bool")
	 || streq(type_name, "i32")
	 || streq(type_name, "f32")
	 || streq(type_name, "string")
	 || streq(type_name, "resource")
	 || streq(type_name, "entity")
	 || streq(type_name, "id")) {
		return;
	}

	check_custom_id_is_pascal(type_name);
}

static const char *push_mod_api_string(const char *old_str) {
	size_t length = strlen(old_str);

	grug_assert(mod_api_strings_size + length < JSON_MAX_STRINGS_CHARACTERS, "There are more than %d characters in the mod_api_strings array, exceeding JSON_MAX_STRINGS_CHARACTERS", JSON_MAX_STRINGS_CHARACTERS);

	const char *new_str = mod_api_strings + mod_api_strings_size;

	memcpy(mod_api_strings + mod_api_strings_size, old_str, length + 1);
	mod_api_strings_size += length + 1;

	return new_str;
}

static enum type parse_type(const char *type) {
	if (streq(type, "bool")) {
		return type_bool;
	}
	if (streq(type, "i32")) {
		return type_i32;
	}
	if (streq(type, "f32")) {
		return type_f32;
	}
	if (streq(type, "string")) {
		return type_string;
	}
	if (streq(type, "resource")) {
		return type_resource;
	}
	if (streq(type, "entity")) {
		return type_entity;
	}
	return type_id;
}

static void init_game_fns(struct json_object fns) {
	for (size_t fn_index = 0; fn_index < fns.field_count; fn_index++) {
		struct grug_game_function grug_fn = {0};

		grug_fn.name = push_mod_api_string(fns.fields[fn_index].key);
		grug_assert(!streq(grug_fn.name, ""), "\"game_functions\" its function names must not be an empty string");
		grug_assert(!starts_with(grug_fn.name, "on_"), "\"game_functions\" its function names must not start with 'on_'");

		grug_assert(fns.fields[fn_index].value->type == JSON_NODE_OBJECT, "\"game_functions\" its array must only contain objects");
		struct json_object fn = fns.fields[fn_index].value->object;
		grug_assert(fn.field_count >= 1, "\"game_functions\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 3, "\"game_functions\" its objects must not have more than 3 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"game_functions\" its functions must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"game_functions\" its function descriptions must be strings");
		const char *description = push_mod_api_string(field->value->string);
		grug_assert(!streq(description, ""), "\"game_functions\" its function descriptions must not be an empty string");

		bool seen_return_type = false;

		if (fn.field_count > 1) {
			field++;

			if (streq(field->key, "return_type")) {
				grug_assert(field->value->type == JSON_NODE_STRING, "\"game_functions\" its function return types must be strings");
				grug_fn.return_type = parse_type(field->value->string);
				grug_fn.return_type_name = push_mod_api_string(field->value->string);
				check_custom_id_type_capitalization(grug_fn.return_type_name);
				grug_assert(grug_fn.return_type != type_resource, "\"game_functions\" its function return types must not be 'resource'");
				grug_assert(grug_fn.return_type != type_entity, "\"game_functions\" its function return types must not be 'entity'");
				seen_return_type = true;
				field++;
			} else {
				grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second field was something other than \"return_type\" and \"arguments\"");
			}
		} else {
			grug_fn.return_type = type_void;
		}

		if ((!seen_return_type && fn.field_count > 1) || fn.field_count > 2) {
			grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second or third field was something other than \"arguments\"");

			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"game_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->array.value_count;
			grug_assert(grug_fn.argument_count > 0, "\"game_functions\" its \"arguments\" array must not be empty (just remove the \"arguments\" key entirely)");

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct argument grug_arg = {0};

				grug_assert(value->type == JSON_NODE_OBJECT, "\"game_functions\" its function arguments must only contain objects");
				grug_assert(value->object.field_count >= 2, "\"game_functions\" must have the function argument fields \"name\" and \"type\"");
				grug_assert(value->object.field_count <= 3, "\"game_functions\" its function arguments can't have more than 3 fields");
				struct json_field *argument_field = value->object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"game_functions\" its function arguments must always have \"name\" as their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.name = push_mod_api_string(argument_field->value->string);
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"game_functions\" its function arguments must always have \"type\" as their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->string);
				grug_arg.type_name = push_mod_api_string(argument_field->value->string);
				check_custom_id_type_capitalization(grug_arg.type_name);
				argument_field++;

				if (grug_arg.type == type_resource) {
					grug_assert(value->object.field_count == 3 && streq(argument_field->key, "resource_extension"), "\"game_functions\" its function arguments has a \"type\" field with the value \"resource\", which means a \"resource_extension\" field is required");
					grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function argument fields must always have string values");
					grug_arg.resource_extension = push_mod_api_string(argument_field->value->string);
				} else if (grug_arg.type == type_entity) {
					grug_assert(value->object.field_count == 3 && streq(argument_field->key, "entity_type"), "\"game_functions\" its function arguments has a \"type\" field with the value \"entity\", which means an \"entity_type\" field is required");
					grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function argument fields must always have string values");
					grug_arg.entity_type = push_mod_api_string(argument_field->value->string);
				} else {
					grug_assert(value->object.field_count == 2, "\"game_functions\" its function argument fields had an unexpected 3rd \"%s\" field", argument_field->key);
				}

				push_grug_argument(grug_arg);
				value++;
			}
		}

		push_grug_game_function(grug_fn);
	}

	hash_game_fns();
}

static void init_on_fns(struct json_object fns) {
	for (size_t fn_index = 0; fn_index < fns.field_count; fn_index++) {
		struct grug_on_function grug_fn = {0};

		grug_fn.name = push_mod_api_string(fns.fields[fn_index].key);
		grug_assert(!streq(grug_fn.name, ""), "\"on_functions\" its function names must not be an empty string");
		grug_assert(starts_with(grug_fn.name, "on_"), "\"on_functions\" its function names must start with 'on_'");

		grug_assert(fns.fields[fn_index].value->type == JSON_NODE_OBJECT, "\"on_functions\" its array must only contain objects");
		struct json_object fn = fns.fields[fn_index].value->object;
		grug_assert(fn.field_count >= 1, "\"on_functions\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 2, "\"on_functions\" its objects must not have more than 2 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"on_functions\" its functions must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"on_functions\" its function descriptions must be strings");
		const char *description = push_mod_api_string(field->value->string);
		grug_assert(!streq(description, ""), "\"on_functions\" its function descriptions must not be an empty string");

		if (fn.field_count > 1) {
			field++;

			grug_assert(streq(field->key, "arguments"), "\"on_functions\" its functions must have \"arguments\" as the second field");
			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"on_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->array.value_count;

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct argument grug_arg = {0};

				grug_assert(value->type == JSON_NODE_OBJECT, "\"on_functions\" its function arguments must only contain objects");
				grug_assert(value->object.field_count == 2, "\"on_functions\" its function arguments must only contain a name and type field");
				struct json_field *argument_field = value->object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"on_functions\" its function arguments must always have \"name\" as their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.name = push_mod_api_string(argument_field->value->string);
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"on_functions\" its function arguments must always have \"type\" as their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->string);
				grug_arg.type_name = push_mod_api_string(argument_field->value->string);
				check_custom_id_type_capitalization(grug_arg.type_name);
				grug_assert(grug_arg.type != type_resource, "\"on_functions\" its function argument types must not be 'resource'");
				grug_assert(grug_arg.type != type_entity, "\"on_functions\" its function argument types must not be 'entity'");
				argument_field++;

				push_grug_argument(grug_arg);
				value++;
			}
		}

		push_grug_on_function(grug_fn);
	}
}

static void init_entities(struct json_object entities) {
	for (size_t entity_field_index = 0; entity_field_index < entities.field_count; entity_field_index++) {
		struct grug_entity entity = {0};

		entity.name = push_mod_api_string(entities.fields[entity_field_index].key);
		grug_assert(!streq(entity.name, ""), "\"entities\" its names must not be an empty string");
		check_custom_id_type_capitalization(entity.name);

		grug_assert(entities.fields[entity_field_index].value->type == JSON_NODE_OBJECT, "\"entities\" must only contain object values");
		struct json_object fn = entities.fields[entity_field_index].value->object;
		grug_assert(fn.field_count >= 1, "\"entities\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 2, "\"entities\" its objects must not have more than 2 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"entities\" must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"entities\" its descriptions must be strings");
		const char *description = push_mod_api_string(field->value->string);
		grug_assert(!streq(description, ""), "\"entities\" its descriptions must not be an empty string");

		if (fn.field_count > 1) {
			field++;
			grug_assert(streq(field->key, "on_functions"), "\"entities\" its second field was something other than \"on_functions\"");
			grug_assert(field->value->type == JSON_NODE_OBJECT, "\"entities\" its \"on_functions\" field must have an object as its value");
			entity.on_functions = grug_on_functions + grug_on_functions_size;
			entity.on_function_count = field->value->object.field_count;
			init_on_fns(field->value->object);
		}

		push_grug_entity(entity);
	}
}

static void parse_mod_api_json(const char *mod_api_json_path) {
	struct json_node node;
	json(mod_api_json_path, &node);

	grug_assert(node.type == JSON_NODE_OBJECT, "mod_api.json its root must be an object");
	struct json_object root_object = node.object;

	grug_assert(root_object.field_count == 2, "mod_api.json must only have these 2 fields, in this order: \"entities\", \"game_functions\"");

	struct json_field *field = root_object.fields;

	grug_assert(streq(field->key, "entities"), "mod_api.json its root object must have \"entities\" as its first field");
	grug_assert(field->value->type == JSON_NODE_OBJECT, "mod_api.json its \"entities\" field must have an object as its value");
	init_entities(field->value->object);
	field++;

	grug_assert(streq(field->key, "game_functions"), "mod_api.json its root object must have \"game_functions\" as its third field");
	grug_assert(field->value->type == JSON_NODE_OBJECT, "mod_api.json its \"game_functions\" field must have an object as its value");
	init_game_fns(field->value->object);
}

//// READING

#define MAX_CHARACTERS 420420

static char grug_text[MAX_CHARACTERS];

static void read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	grug_assert(f, "fopen: %s", strerror(errno));

	grug_assert(fseek(f, 0, SEEK_END) == 0, "fseek: %s", strerror(errno));

	long count = ftell(f);
	grug_assert(count != -1, "ftell: %s", strerror(errno));
	grug_assert(count < MAX_CHARACTERS, "There are more than %d characters in the grug file, exceeding MAX_CHARACTERS", MAX_CHARACTERS);

	rewind(f);

	size_t bytes_read = fread(grug_text, sizeof(char), count, f);
	grug_assert(bytes_read == (size_t)count || feof(f), "fread error");

	grug_text[count] = '\0';

	grug_assert(fclose(f) == 0, "fclose: %s", strerror(errno));
}

//// TOKENIZATION

#define MAX_TOKENS 420420
#define MAX_TOKEN_STRINGS_CHARACTERS 420420
#define SPACES_PER_INDENT 4

enum token_type {
	OPEN_PARENTHESIS_TOKEN,
	CLOSE_PARENTHESIS_TOKEN,
	OPEN_BRACE_TOKEN,
	CLOSE_BRACE_TOKEN,
	PLUS_TOKEN,
	MINUS_TOKEN,
	MULTIPLICATION_TOKEN,
	DIVISION_TOKEN,
	REMAINDER_TOKEN,
	COMMA_TOKEN,
	COLON_TOKEN,
	NEWLINE_TOKEN,
	EQUALS_TOKEN,
	NOT_EQUALS_TOKEN,
	ASSIGNMENT_TOKEN,
	GREATER_OR_EQUAL_TOKEN,
	GREATER_TOKEN,
	LESS_OR_EQUAL_TOKEN,
	LESS_TOKEN,
	AND_TOKEN,
	OR_TOKEN,
	NOT_TOKEN,
	TRUE_TOKEN,
	FALSE_TOKEN,
	IF_TOKEN,
	ELSE_TOKEN,
	WHILE_TOKEN,
	BREAK_TOKEN,
	RETURN_TOKEN,
	CONTINUE_TOKEN,
	SPACE_TOKEN,
	INDENTATION_TOKEN,
	STRING_TOKEN,
	WORD_TOKEN,
	I32_TOKEN,
	F32_TOKEN,
	COMMENT_TOKEN,
};

struct token {
	enum token_type type;
	const char *str;
};
static const char *get_token_type_str[] = {
	[OPEN_PARENTHESIS_TOKEN] = "OPEN_PARENTHESIS_TOKEN",
	[CLOSE_PARENTHESIS_TOKEN] = "CLOSE_PARENTHESIS_TOKEN",
	[OPEN_BRACE_TOKEN] = "OPEN_BRACE_TOKEN",
	[CLOSE_BRACE_TOKEN] = "CLOSE_BRACE_TOKEN",
	[PLUS_TOKEN] = "PLUS_TOKEN",
	[MINUS_TOKEN] = "MINUS_TOKEN",
	[MULTIPLICATION_TOKEN] = "MULTIPLICATION_TOKEN",
	[DIVISION_TOKEN] = "DIVISION_TOKEN",
	[REMAINDER_TOKEN] = "REMAINDER_TOKEN",
	[COMMA_TOKEN] = "COMMA_TOKEN",
	[COLON_TOKEN] = "COLON_TOKEN",
	[NEWLINE_TOKEN] = "NEWLINE_TOKEN",
	[EQUALS_TOKEN] = "EQUALS_TOKEN",
	[NOT_EQUALS_TOKEN] = "NOT_EQUALS_TOKEN",
	[ASSIGNMENT_TOKEN] = "ASSIGNMENT_TOKEN",
	[GREATER_OR_EQUAL_TOKEN] = "GREATER_OR_EQUAL_TOKEN",
	[GREATER_TOKEN] = "GREATER_TOKEN",
	[LESS_OR_EQUAL_TOKEN] = "LESS_OR_EQUAL_TOKEN",
	[LESS_TOKEN] = "LESS_TOKEN",
	[AND_TOKEN] = "AND_TOKEN",
	[OR_TOKEN] = "OR_TOKEN",
	[NOT_TOKEN] = "NOT_TOKEN",
	[TRUE_TOKEN] = "TRUE_TOKEN",
	[FALSE_TOKEN] = "FALSE_TOKEN",
	[IF_TOKEN] = "IF_TOKEN",
	[ELSE_TOKEN] = "ELSE_TOKEN",
	[WHILE_TOKEN] = "WHILE_TOKEN",
	[BREAK_TOKEN] = "BREAK_TOKEN",
	[RETURN_TOKEN] = "RETURN_TOKEN",
	[CONTINUE_TOKEN] = "CONTINUE_TOKEN",
	[SPACE_TOKEN] = "SPACE_TOKEN",
	[INDENTATION_TOKEN] = "INDENTATION_TOKEN",
	[STRING_TOKEN] = "STRING_TOKEN",
	[WORD_TOKEN] = "WORD_TOKEN",
	[I32_TOKEN] = "I32_TOKEN",
	[F32_TOKEN] = "F32_TOKEN",
	[COMMENT_TOKEN] = "COMMENT_TOKEN",
};
static struct token tokens[MAX_TOKENS];
static size_t tokens_size;

static char token_strings[MAX_TOKEN_STRINGS_CHARACTERS];
static size_t token_strings_size;

static void reset_tokenization(void) {
	tokens_size = 0;
	token_strings_size = 0;
}

static size_t max_size_t(size_t a, size_t b) {
	if (a > b) {
		return a;
	}
	return b;
}

static struct token peek_token(size_t token_index) {
	grug_assert(token_index < tokens_size, "token_index %zu was out of bounds in peek_token()", token_index);
	return tokens[token_index];
}

static struct token consume_token(size_t *token_index_ptr) {
	return peek_token((*token_index_ptr)++);
}

static void print_tokens(void) {
	size_t longest_token_type_len = 0;
	for (size_t i = 0; i < tokens_size; i++) {
		struct token token = peek_token(i);
		const char *token_type_str = get_token_type_str[token.type];
		longest_token_type_len = max_size_t(strlen(token_type_str), longest_token_type_len);
	}

	// Leave enough space for the word "index", but if the index exceeds 99999, add extra spaces
	// In pseudocode this does longest_index = max(floor(log10(tokens.size)), strlen("index"))
	size_t longest_index = 1;
	size_t n = tokens_size;
	while (true) {
		n /= 10;
		if (n == 0) {
			break;
		}
		longest_index++;
	}
	longest_index = max_size_t(longest_index, strlen("index"));

	grug_log("| %-*s | %-*s | str\n", (int)longest_index, "index", (int)longest_token_type_len, "type");

	for (size_t i = 0; i < tokens_size; i++) {
		struct token token = peek_token(i);

		grug_log("| %*zu ", (int)longest_index, i);

		const char *token_type_str = get_token_type_str[token.type];
		grug_log("| %*s ", (int)longest_token_type_len, token_type_str);

		grug_log("| '%s'\n", token.type == NEWLINE_TOKEN ? "\\n" : token.str);
	}
}

// Here are some examples, where the part in <> indicates the character_index character
// "" => 1
// "<a>" => 1
// "a<b>" => 1
// "<\n>" => 1
// "\n<a>" => 2
// "\n<\n>" => 2
static size_t get_character_line_number(size_t character_index) {
	size_t line_number = 1;

	for (size_t i = 0; i < character_index; i++) {
		if (grug_text[i] == '\n' || (grug_text[i] == '\r' && grug_text[i + 1] == '\n')) {
			line_number++;
		}
	}

	return line_number;
}

static const char *get_escaped_char(const char *str) {
	switch (*str) {
	case '\f':
		return "\\f";
	case '\n':
		return "\\n";
	case '\r':
		return "\\r";
	case '\t':
		return "\\t";
	case '\v':
		return "\\v";
	}
	return str;
}

static bool is_escaped_char(char c) {
	return isspace(c) && c != ' ';
}

static bool is_end_of_word(char c) {
	return !isalnum(c) && c != '_';
}

static const char *push_token_string(const char *slice_start, size_t length) {
	grug_assert(token_strings_size + length < MAX_TOKEN_STRINGS_CHARACTERS, "There are more than %d characters in the token_strings array, exceeding MAX_TOKEN_STRINGS_CHARACTERS", MAX_TOKEN_STRINGS_CHARACTERS);

	const char *new_str = token_strings + token_strings_size;

	for (size_t i = 0; i < length; i++) {
		token_strings[token_strings_size++] = slice_start[i];
	}
	token_strings[token_strings_size++] = '\0';

	return new_str;
}

static void push_token(enum token_type type, const char *str, size_t len) {
	grug_assert(tokens_size < MAX_TOKENS, "There are more than %d tokens in the grug file, exceeding MAX_TOKENS", MAX_TOKENS);
	tokens[tokens_size++] = (struct token){
		.type = type,
		.str = push_token_string(str, len),
	};
}

static void tokenize(void) {
	reset_tokenization();

	size_t i = 0;
	while (grug_text[i]) {
		if (       grug_text[i] == '(') {
			push_token(OPEN_PARENTHESIS_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == ')') {
			push_token(CLOSE_PARENTHESIS_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '{') {
			push_token(OPEN_BRACE_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '}') {
			push_token(CLOSE_BRACE_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '+') {
			push_token(PLUS_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '-') {
			push_token(MINUS_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '*') {
			push_token(MULTIPLICATION_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '/') {
			push_token(DIVISION_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '%') {
			push_token(REMAINDER_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == ',') {
			push_token(COMMA_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == ':') {
			push_token(COLON_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '\n') {
			push_token(NEWLINE_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '=' && grug_text[i + 1] == '=') {
			push_token(EQUALS_TOKEN, grug_text+i, 2);
			i += 2;
		} else if (grug_text[i] == '!' && grug_text[i + 1] == '=') {
			push_token(NOT_EQUALS_TOKEN, grug_text+i, 2);
			i += 2;
		} else if (grug_text[i] == '=') {
			push_token(ASSIGNMENT_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '>' && grug_text[i + 1] == '=') {
			push_token(GREATER_OR_EQUAL_TOKEN, grug_text+i, 2);
			i += 2;
		} else if (grug_text[i] == '>') {
			push_token(GREATER_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i] == '<' && grug_text[i + 1] == '=') {
			push_token(LESS_OR_EQUAL_TOKEN, grug_text+i, 2);
			i += 2;
		} else if (grug_text[i] == '<') {
			push_token(LESS_TOKEN, grug_text+i, 1);
			i += 1;
		} else if (grug_text[i + 0] == 'a' && grug_text[i + 1] == 'n' && grug_text[i + 2] == 'd' && is_end_of_word(grug_text[i + 3])) {
			push_token(AND_TOKEN, grug_text+i, 3);
			i += 3;
		} else if (grug_text[i + 0] == 'o' && grug_text[i + 1] == 'r' && is_end_of_word(grug_text[i + 2])) {
			push_token(OR_TOKEN, grug_text+i, 2);
			i += 2;
		} else if (grug_text[i + 0] == 'n' && grug_text[i + 1] == 'o' && grug_text[i + 2] == 't' && is_end_of_word(grug_text[i + 3])) {
			push_token(NOT_TOKEN, grug_text+i, 3);
			i += 3;
		} else if (grug_text[i + 0] == 't' && grug_text[i + 1] == 'r' && grug_text[i + 2] == 'u' && grug_text[i + 3] == 'e' && is_end_of_word(grug_text[i + 4])) {
			push_token(TRUE_TOKEN, grug_text+i, 4);
			i += 4;
		} else if (grug_text[i + 0] == 'f' && grug_text[i + 1] == 'a' && grug_text[i + 2] == 'l' && grug_text[i + 3] == 's' && grug_text[i + 4] == 'e' && is_end_of_word(grug_text[i + 5])) {
			push_token(FALSE_TOKEN, grug_text+i, 5);
			i += 5;
		} else if (grug_text[i + 0] == 'i' && grug_text[i + 1] == 'f' && is_end_of_word(grug_text[i + 2])) {
			push_token(IF_TOKEN, grug_text+i, 2);
			i += 2;
		} else if (grug_text[i + 0] == 'e' && grug_text[i + 1] == 'l' && grug_text[i + 2] == 's' && grug_text[i + 3] == 'e' && is_end_of_word(grug_text[i + 4])) {
			push_token(ELSE_TOKEN, grug_text+i, 4);
			i += 4;
		} else if (grug_text[i + 0] == 'w' && grug_text[i + 1] == 'h' && grug_text[i + 2] == 'i' && grug_text[i + 3] == 'l' && grug_text[i + 4] == 'e' && is_end_of_word(grug_text[i + 5])) {
			push_token(WHILE_TOKEN, grug_text+i, 5);
			i += 5;
		} else if (grug_text[i + 0] == 'b' && grug_text[i + 1] == 'r' && grug_text[i + 2] == 'e' && grug_text[i + 3] == 'a' && grug_text[i + 4] == 'k' && is_end_of_word(grug_text[i + 5])) {
			push_token(BREAK_TOKEN, grug_text+i, 5);
			i += 5;
		} else if (grug_text[i + 0] == 'r' && grug_text[i + 1] == 'e' && grug_text[i + 2] == 't' && grug_text[i + 3] == 'u' && grug_text[i + 4] == 'r' && grug_text[i + 5] == 'n' && is_end_of_word(grug_text[i + 6])) {
			push_token(RETURN_TOKEN, grug_text+i, 6);
			i += 6;
		} else if (grug_text[i + 0] == 'c' && grug_text[i + 1] == 'o' && grug_text[i + 2] == 'n' && grug_text[i + 3] == 't' && grug_text[i + 4] == 'i' && grug_text[i + 5] == 'n' && grug_text[i + 6] == 'u' && grug_text[i + 7] == 'e' && is_end_of_word(grug_text[i + 8])) {
			push_token(CONTINUE_TOKEN, grug_text+i, 8);
			i += 8;
		} else if (grug_text[i] == ' ') {
			if (grug_text[i + 1] != ' ') {
				push_token(SPACE_TOKEN, grug_text+i, 1);
				i += 1;
				continue;
			}

			const char *str = grug_text+i;
			size_t old_i = i;

			do {
				i++;
			} while (grug_text[i] == ' ');

			size_t spaces = i - old_i;

			grug_assert(spaces % SPACES_PER_INDENT == 0, "Encountered %zu spaces, while indentation expects multiples of %d spaces, on line %zu", spaces, SPACES_PER_INDENT, get_character_line_number(i));

			push_token(INDENTATION_TOKEN, str, spaces);
		} else if (grug_text[i] == '\"') {
			const char *str = grug_text+i + 1;
			size_t old_i = i + 1;

			size_t open_double_quote_index = i;

			do {
				i++;
				grug_assert(grug_text[i] != '\0', "Unclosed \" on line %zu", get_character_line_number(open_double_quote_index + 1));
			} while (grug_text[i] != '\"');
			i++;

			push_token(STRING_TOKEN, str, i - old_i - 1);
		} else if (isalpha(grug_text[i]) || grug_text[i] == '_') {
			const char *str = grug_text+i;
			size_t old_i = i;

			do {
				i++;
			} while (isalnum(grug_text[i]) || grug_text[i] == '_');

			push_token(WORD_TOKEN, str, i - old_i);
		} else if (isdigit(grug_text[i])) {
			const char *str = grug_text+i;
			size_t old_i = i;

			bool seen_period = false;

			i++;
			while (isdigit(grug_text[i]) || grug_text[i] == '.') {
				if (grug_text[i] == '.') {
					grug_assert(!seen_period, "Encountered two '.' periods in a number on line %zu", get_character_line_number(i));
					seen_period = true;
				}
				i++;
			}

			if (seen_period) {
				grug_assert(grug_text[i - 1] != '.', "Missing digit after decimal point in '%.*s'", (int)(i - old_i), str);
				push_token(F32_TOKEN, str, i - old_i);
			} else {
				push_token(I32_TOKEN, str, i - old_i);
			}
		} else if (grug_text[i] == '#') {
			i++;

			grug_assert(grug_text[i] == ' ', "Expected a single space after the '#' on line %zu", get_character_line_number(i));
			i++;

			const char *str = grug_text+i;
			size_t old_i = i;

			while (true) {
				if (!isprint(grug_text[i])) {
					if (grug_text[i] == '\r' || grug_text[i] == '\n' || grug_text[i] == '\0') {
						break;
					}

					grug_error("Unexpected unprintable character '%.*s' on line %zu", is_escaped_char(grug_text[i]) ? 2 : 1, get_escaped_char(&grug_text[i]), get_character_line_number(i + 1));
				}
				i++;
			}

			size_t len = i - old_i;

			grug_assert(len > 0, "Expected the comment to contain some text on line %zu", get_character_line_number(i));

			grug_assert(!isspace(grug_text[i - 1]), "A comment has trailing whitespace on line %zu", get_character_line_number(i));

			push_token(COMMENT_TOKEN, str, len);
		} else {
			grug_error("Unrecognized character '%.*s' on line %zu", is_escaped_char(grug_text[i]) ? 2 : 1, get_escaped_char(&grug_text[i]), get_character_line_number(i + 1));
		}
	}
}

//// PARSING

#define MAX_EXPRS 420420
#define MAX_STATEMENTS 420420
#define MAX_GLOBAL_STATEMENTS 420420
#define MAX_ARGUMENTS 420420
#define MAX_ON_FNS 420420
#define MAX_HELPER_FNS 420420
#define MAX_GLOBAL_VARIABLES 420420
#define MAX_CALLED_HELPER_FN_NAMES 420420
#define MAX_CALL_ARGUMENTS_PER_STACK_FRAME 69
#define MAX_STATEMENTS_PER_SCOPE 1337
#define MAX_PARSING_DEPTH 100

#define INCREASE_PARSING_DEPTH() parsing_depth++; grug_assert(parsing_depth < MAX_PARSING_DEPTH, "There is a function that contains more than %d levels of nested expressions", MAX_PARSING_DEPTH)
#define DECREASE_PARSING_DEPTH() assert(parsing_depth > 0); parsing_depth--

struct literal_expr {
	union {
		const char *string;
		i32 i32;
		struct {
			f32 value;
			const char *string;
		} f32;
	};
};

struct unary_expr {
	enum token_type operator;
	struct expr *expr;
};

struct binary_expr {
	struct expr *left_expr;
	enum token_type operator;
	struct expr *right_expr;
};

struct call_expr {
	const char *fn_name;
	struct expr *arguments;
	size_t argument_count;
};

enum expr_type {
	TRUE_EXPR,
	FALSE_EXPR,
	STRING_EXPR,
	RESOURCE_EXPR,
	ENTITY_EXPR,
	IDENTIFIER_EXPR,
	I32_EXPR,
	F32_EXPR,
	UNARY_EXPR,
	BINARY_EXPR,
	LOGICAL_EXPR,
	CALL_EXPR,
	PARENTHESIZED_EXPR,
};
struct expr {
	enum expr_type type;
	enum type result_type;
	const char *result_type_name;
	union {
		struct literal_expr literal;
		struct unary_expr unary;
		struct binary_expr binary;
		struct call_expr call;
		struct expr *parenthesized;
	};
};
static const char *get_expr_type_str[] = {
	[TRUE_EXPR] = "TRUE_EXPR",
	[FALSE_EXPR] = "FALSE_EXPR",
	[STRING_EXPR] = "STRING_EXPR",
	[RESOURCE_EXPR] = "RESOURCE_EXPR",
	[ENTITY_EXPR] = "ENTITY_EXPR",
	[IDENTIFIER_EXPR] = "IDENTIFIER_EXPR",
	[I32_EXPR] = "I32_EXPR",
	[F32_EXPR] = "F32_EXPR",
	[UNARY_EXPR] = "UNARY_EXPR",
	[BINARY_EXPR] = "BINARY_EXPR",
	[LOGICAL_EXPR] = "LOGICAL_EXPR",
	[CALL_EXPR] = "CALL_EXPR",
	[PARENTHESIZED_EXPR] = "PARENTHESIZED_EXPR",
};
static struct expr exprs[MAX_EXPRS];
static size_t exprs_size;

struct variable_statement {
	const char *name;
	enum type type;
	const char *type_name;
	bool has_type;
	struct expr *assignment_expr;
};

struct call_statement {
	struct expr *expr;
};

struct if_statement {
	struct expr condition;
	struct statement *if_body_statements;
	size_t if_body_statement_count;
	struct statement *else_body_statements;
	size_t else_body_statement_count;
};

struct return_statement {
	struct expr *value;
	bool has_value;
};

struct while_statement {
	struct expr condition;
	struct statement *body_statements;
	size_t body_statement_count;
};

enum statement_type {
	VARIABLE_STATEMENT,
	CALL_STATEMENT,
	IF_STATEMENT,
	RETURN_STATEMENT,
	WHILE_STATEMENT,
	BREAK_STATEMENT,
	CONTINUE_STATEMENT,
	EMPTY_LINE_STATEMENT,
	COMMENT_STATEMENT,
};
struct statement {
	enum statement_type type;
	union {
		struct variable_statement variable_statement;
		struct call_statement call_statement;
		struct if_statement if_statement;
		struct return_statement return_statement;
		struct while_statement while_statement;
		const char *comment;
	};
};
static const char *get_statement_type_str[] = {
	[VARIABLE_STATEMENT] = "VARIABLE_STATEMENT",
	[CALL_STATEMENT] = "CALL_STATEMENT",
	[IF_STATEMENT] = "IF_STATEMENT",
	[RETURN_STATEMENT] = "RETURN_STATEMENT",
	[WHILE_STATEMENT] = "WHILE_STATEMENT",
	[BREAK_STATEMENT] = "BREAK_STATEMENT",
	[CONTINUE_STATEMENT] = "CONTINUE_STATEMENT",
	[EMPTY_LINE_STATEMENT] = "EMPTY_LINE_STATEMENT",
	[COMMENT_STATEMENT] = "COMMENT_STATEMENT",
};
static struct statement statements[MAX_STATEMENTS];
static size_t statements_size;

enum global_statement_type {
	GLOBAL_VARIABLE,
	GLOBAL_ON_FN,
	GLOBAL_HELPER_FN,
	GLOBAL_EMPTY_LINE,
	GLOBAL_COMMENT,
};
struct global_statement {
	enum global_statement_type type;
	union {
		struct global_variable_statement *global_variable;
		struct on_fn *on_fn;
		struct helper_fn *helper_fn;
		const char *comment;
	};
};
static const char *get_global_statement_type_str[] = {
	[GLOBAL_VARIABLE] = "GLOBAL_VARIABLE",
	[GLOBAL_ON_FN] = "GLOBAL_ON_FN",
	[GLOBAL_HELPER_FN] = "GLOBAL_HELPER_FN",
	[GLOBAL_EMPTY_LINE] = "GLOBAL_EMPTY_LINE",
	[GLOBAL_COMMENT] = "GLOBAL_COMMENT",
};
static struct global_statement global_statements[MAX_GLOBAL_STATEMENTS];
static size_t global_statements_size;

static struct argument arguments[MAX_ARGUMENTS];
static size_t arguments_size;

struct on_fn {
	const char *fn_name;
	struct argument *arguments;
	size_t argument_count;
	struct statement *body_statements;
	size_t body_statement_count;
	bool calls_helper_fn;
	bool contains_while_loop;
};
static struct on_fn on_fns[MAX_ON_FNS];
static size_t on_fns_size;

struct helper_fn {
	const char *fn_name;
	struct argument *arguments;
	size_t argument_count;
	enum type return_type;
	const char *return_type_name;
	struct statement *body_statements;
	size_t body_statement_count;
};
static struct helper_fn helper_fns[MAX_HELPER_FNS];
static size_t helper_fns_size;
static u32 buckets_helper_fns[MAX_HELPER_FNS];
static u32 chains_helper_fns[MAX_HELPER_FNS];

struct global_variable_statement {
	const char *name;
	enum type type;
	const char *type_name;
	struct expr assignment_expr;
};
static struct global_variable_statement global_variable_statements[MAX_GLOBAL_VARIABLES];
static size_t global_variable_statements_size;

static size_t indentation;

static const char *called_helper_fn_names[MAX_CALLED_HELPER_FN_NAMES];
static size_t called_helper_fn_names_size;

static u32 buckets_called_helper_fn_names[MAX_CALLED_HELPER_FN_NAMES];
static u32 chains_called_helper_fn_names[MAX_CALLED_HELPER_FN_NAMES];

static size_t parsing_depth;

static void reset_parsing(void) {
	exprs_size = 0;
	statements_size = 0;
	global_statements_size = 0;
	arguments_size = 0;
	on_fns_size = 0;
	helper_fns_size = 0;
	global_variable_statements_size = 0;
	called_helper_fn_names_size = 0;
	memset(buckets_called_helper_fn_names, 0xff, sizeof(buckets_called_helper_fn_names));
	parsing_depth = 0;
}

static struct helper_fn *get_helper_fn(const char *name) {
	if (helper_fns_size == 0) {
		return NULL;
	}

	u32 i = buckets_helper_fns[elf_hash(name) % helper_fns_size];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, helper_fns[i].fn_name)) {
			break;
		}

		i = chains_helper_fns[i];
	}

	return helper_fns + i;
}

static void hash_helper_fns(void) {
	memset(buckets_helper_fns, 0xff, helper_fns_size * sizeof(u32));

	for (size_t i = 0; i < helper_fns_size; i++) {
		const char *name = helper_fns[i].fn_name;

		grug_assert(!get_helper_fn(name), "The function '%s' was defined several times in the same file", name);

		u32 bucket_index = elf_hash(name) % helper_fns_size;

		chains_helper_fns[i] = buckets_helper_fns[bucket_index];

		buckets_helper_fns[bucket_index] = i;
	}
}

static struct helper_fn *push_helper_fn(struct helper_fn helper_fn) {
	grug_assert(helper_fns_size < MAX_HELPER_FNS, "There are more than %d helper_fns in the grug file, exceeding MAX_HELPER_FNS", MAX_HELPER_FNS);
	helper_fns[helper_fns_size] = helper_fn;
	return helper_fns + helper_fns_size++;
}

static struct on_fn *push_on_fn(struct on_fn on_fn) {
	grug_assert(on_fns_size < MAX_ON_FNS, "There are more than %d on_fns in the grug file, exceeding MAX_ON_FNS", MAX_ON_FNS);
	on_fns[on_fns_size] = on_fn;
	return on_fns + on_fns_size++;
}

static struct statement *push_statement(struct statement statement) {
	grug_assert(statements_size < MAX_STATEMENTS, "There are more than %d statements in the grug file, exceeding MAX_STATEMENTS", MAX_STATEMENTS);
	statements[statements_size] = statement;
	return statements + statements_size++;
}

static struct expr *push_expr(struct expr expr) {
	grug_assert(exprs_size < MAX_EXPRS, "There are more than %d exprs in the grug file, exceeding MAX_EXPRS", MAX_EXPRS);
	exprs[exprs_size] = expr;
	return exprs + exprs_size++;
}

// Here are some examples, where the part in <> indicates the token_index token
// "" => 1
// "<a>" => 1
// "a<b>" => 1
// "<\n>" => 1
// "\n<a>" => 2
// "\n<\n>" => 2
static size_t get_token_line_number(size_t token_index) {
	assert(token_index < tokens_size);

	size_t line_number = 1;

	for (size_t i = 0; i < token_index; i++) {
		if (tokens[i].type == NEWLINE_TOKEN) {
			line_number++;
		}
	}

	return line_number;
}

static void assert_token_type(size_t token_index, enum token_type expected_type) {
	struct token token = peek_token(token_index);
	grug_assert(token.type == expected_type, "Expected token type %s, but got %s on line %zu", get_token_type_str[expected_type], get_token_type_str[token.type], get_token_line_number(token_index));
}

static void consume_token_type(size_t *token_index_ptr, enum token_type expected_type) {
	assert_token_type((*token_index_ptr)++, expected_type);
}

static void consume_newline(size_t *token_index_ptr) {
	assert_token_type(*token_index_ptr, NEWLINE_TOKEN);
	(*token_index_ptr)++;
}

static void consume_space(size_t *token_index_ptr) {
	assert_token_type(*token_index_ptr, SPACE_TOKEN);
	(*token_index_ptr)++;
}

static void consume_indentation(size_t *token_index_ptr) {
	assert_token_type(*token_index_ptr, INDENTATION_TOKEN);

	size_t spaces = strlen(peek_token(*token_index_ptr).str);

	grug_assert(spaces == indentation * SPACES_PER_INDENT, "Expected %zu spaces, but got %zu spaces on line %zu", indentation * SPACES_PER_INDENT, spaces, get_token_line_number(*token_index_ptr));

	(*token_index_ptr)++;
}

static bool is_end_of_block(size_t *token_index_ptr) {
	assert(indentation > 0);

	struct token token = peek_token(*token_index_ptr);
	if (token.type == CLOSE_BRACE_TOKEN) {
		return true;
	} else if (token.type == NEWLINE_TOKEN) {
		return false;
	}

	grug_assert(token.type == INDENTATION_TOKEN, "Expected indentation, or an empty line, or '}', but got '%s' on line %zu", token.str, get_token_line_number(*token_index_ptr));

	size_t spaces = strlen(token.str);
	return spaces == (indentation - 1) * SPACES_PER_INDENT;
}

static f32 str_to_f32(const char *str) {
	char *end;
	errno = 0;
	float f = strtof(str, &end);

	if (errno == ERANGE) {
		if (f == HUGE_VALF) {
			grug_error("The f32 %s is too big", str);
		}

		// The token can't ever start with a minus sign
		assert(f != -HUGE_VALF);

		grug_error("The f32 %s is too close to zero", str);
	}

	// An f32 token only gets created when it starts with a digit,
	// so strtof() should at the very least have incremented `end` by 1
	assert(str != end);

	// This function can't ever have trailing characters,
	// since the number was tokenized
	assert(*end == '\0');

	return f;
}

// Inspiration: https://stackoverflow.com/a/12923949/13279557
static i32 str_to_i32(const char *str) {
	char *end;
	errno = 0;
	long n = strtol(str, &end, 10);

	grug_assert(n <= INT32_MAX && !(errno == ERANGE && n == LONG_MAX), "The i32 %s is too big, which has a maximum value of %d", str, INT32_MAX);

	// This function can't ever return a negative number,
	// since the minus symbol gets tokenized separately
	assert(errno != ERANGE);
	assert(n >= 0);

	// This function can't ever have trailing characters,
	// since the number was tokenized
	assert(*end == '\0');

	return n;
}

static struct expr parse_expression(size_t *i);

static struct expr parse_primary(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct token token = peek_token(*i);

	struct expr expr = {0};

	switch (token.type) {
		case OPEN_PARENTHESIS_TOKEN:
			(*i)++;
			expr.type = PARENTHESIZED_EXPR;
			expr.parenthesized = push_expr(parse_expression(i));
			consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);
			break;
		case TRUE_TOKEN:
			(*i)++;
			expr.type = TRUE_EXPR;
			break;
		case FALSE_TOKEN:
			(*i)++;
			expr.type = FALSE_EXPR;
			break;
		case STRING_TOKEN:
			(*i)++;
			expr.type = STRING_EXPR;
			expr.literal.string = token.str;
			break;
		case WORD_TOKEN:
			(*i)++;
			expr.type = IDENTIFIER_EXPR;
			expr.literal.string = token.str;
			break;
		case I32_TOKEN:
			(*i)++;
			expr.type = I32_EXPR;
			expr.literal.i32 = str_to_i32(token.str);
			break;
		case F32_TOKEN:
			(*i)++;
			expr.type = F32_EXPR;
			expr.literal.f32.value = str_to_f32(token.str);
			expr.literal.f32.string = token.str;
			break;
		default:
			grug_error("Expected a primary expression token, but got token type %s on line %zu", get_token_type_str[token.type], get_token_line_number(*i));
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static void push_called_helper_fn_name(const char *name) {
	grug_assert(called_helper_fn_names_size < MAX_CALLED_HELPER_FN_NAMES, "There are more than %d called helper function names, exceeding MAX_CALLED_HELPER_FN_NAMES", MAX_CALLED_HELPER_FN_NAMES);

	called_helper_fn_names[called_helper_fn_names_size++] = name;
}

static bool seen_called_helper_fn_name(const char *name) {
	if (called_helper_fn_names_size == 0) {
		return false;
	}

	u32 i = buckets_called_helper_fn_names[elf_hash(name) % MAX_CALLED_HELPER_FN_NAMES];

	while (true) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (streq(name, called_helper_fn_names[i])) {
			break;
		}

		i = chains_called_helper_fn_names[i];
	}

	return true;
}

static void add_called_helper_fn_name(const char *name) {
	if (!seen_called_helper_fn_name(name)) {
		u32 bucket_index = elf_hash(name) % MAX_CALLED_HELPER_FN_NAMES;

		chains_called_helper_fn_names[called_helper_fn_names_size] = buckets_called_helper_fn_names[bucket_index];

		buckets_called_helper_fn_names[bucket_index] = called_helper_fn_names_size;

		push_called_helper_fn_name(name);
	}
}

static struct expr parse_call(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_primary(i);

	struct token token = peek_token(*i);
	if (token.type != OPEN_PARENTHESIS_TOKEN) {
		DECREASE_PARSING_DEPTH();
		return expr;
	}

	(*i)++;

	grug_assert(expr.type == IDENTIFIER_EXPR, "Unexpected open parenthesis after non-identifier expression type %s on line %zu", get_expr_type_str[expr.type], get_token_line_number(*i - 2));
	expr.type = CALL_EXPR;

	expr.call.fn_name = expr.literal.string;

	if (starts_with(expr.call.fn_name, "helper_")) {
		add_called_helper_fn_name(expr.call.fn_name);
	}

	expr.call.argument_count = 0;

	token = peek_token(*i);
	if (token.type == CLOSE_PARENTHESIS_TOKEN) {
		(*i)++;
		DECREASE_PARSING_DEPTH();
		return expr;
	}

	struct expr local_call_arguments[MAX_CALL_ARGUMENTS_PER_STACK_FRAME];

	while (true) {
		struct expr call_argument = parse_expression(i);

		grug_assert(expr.call.argument_count < MAX_CALL_ARGUMENTS_PER_STACK_FRAME, "There are more than %d arguments to a function call in one of the grug file's stack frames, exceeding MAX_CALL_ARGUMENTS_PER_STACK_FRAME", MAX_CALL_ARGUMENTS_PER_STACK_FRAME);
		local_call_arguments[expr.call.argument_count++] = call_argument;

		token = peek_token(*i);
		if (token.type != COMMA_TOKEN) {
			assert_token_type(*i, CLOSE_PARENTHESIS_TOKEN);
			(*i)++;
			break;
		}
		(*i)++;
		consume_space(i);
	}

	expr.call.arguments = exprs + exprs_size;
	for (size_t argument_index = 0; argument_index < expr.call.argument_count; argument_index++) {
		(void)push_expr(local_call_arguments[argument_index]);
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct expr parse_unary(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct token token = peek_token(*i);
	if (token.type == MINUS_TOKEN
	 || token.type == NOT_TOKEN) {
		(*i)++;
		if (token.type == NOT_TOKEN) {
			consume_space(i);
		}

		struct expr expr = {0};

		expr.unary.operator = token.type;
		expr.unary.expr = push_expr(parse_unary(i));
		expr.type = UNARY_EXPR;

		DECREASE_PARSING_DEPTH();
		return expr;
	}

	DECREASE_PARSING_DEPTH();
	return parse_call(i);
}

static struct expr parse_factor(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_unary(i);

	while (peek_token(*i).type == SPACE_TOKEN && (
		   peek_token(*i + 1).type == MULTIPLICATION_TOKEN
		|| peek_token(*i + 1).type == DIVISION_TOKEN
		|| peek_token(*i + 1).type == REMAINDER_TOKEN)) {
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = consume_token(i).type;
		consume_space(i);
		expr.binary.right_expr = push_expr(parse_unary(i));
		expr.type = BINARY_EXPR;
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct expr parse_term(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_factor(i);

	while (peek_token(*i).type == SPACE_TOKEN && (
		   peek_token(*i + 1).type == PLUS_TOKEN
		|| peek_token(*i + 1).type == MINUS_TOKEN)) {
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = consume_token(i).type;
		consume_space(i);
		expr.binary.right_expr = push_expr(parse_factor(i));
		expr.type = BINARY_EXPR;
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct expr parse_comparison(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_term(i);

	while (peek_token(*i).type == SPACE_TOKEN && (
		   peek_token(*i + 1).type == GREATER_OR_EQUAL_TOKEN
		|| peek_token(*i + 1).type == GREATER_TOKEN
		|| peek_token(*i + 1).type == LESS_OR_EQUAL_TOKEN
		|| peek_token(*i + 1).type == LESS_TOKEN)) {
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = consume_token(i).type;
		consume_space(i);
		expr.binary.right_expr = push_expr(parse_term(i));
		expr.type = BINARY_EXPR;
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct expr parse_equality(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_comparison(i);

	while (peek_token(*i).type == SPACE_TOKEN && (
		   peek_token(*i + 1).type == EQUALS_TOKEN
		|| peek_token(*i + 1).type == NOT_EQUALS_TOKEN)) {
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = consume_token(i).type;
		consume_space(i);
		expr.binary.right_expr = push_expr(parse_comparison(i));
		expr.type = BINARY_EXPR;
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct expr parse_and(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_equality(i);

	while (peek_token(*i).type == SPACE_TOKEN && peek_token(*i + 1).type == AND_TOKEN) {
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = consume_token(i).type;
		consume_space(i);
		expr.binary.right_expr = push_expr(parse_equality(i));
		expr.type = LOGICAL_EXPR;
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct expr parse_or(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_and(i);

	while (peek_token(*i).type == SPACE_TOKEN && peek_token(*i + 1).type == OR_TOKEN) {
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = consume_token(i).type;
		consume_space(i);
		expr.binary.right_expr = push_expr(parse_and(i));
		expr.type = LOGICAL_EXPR;
	}

	DECREASE_PARSING_DEPTH();
	return expr;
}

// Recursive descent parsing inspired by the book Crafting Interpreters:
// https://craftinginterpreters.com/parsing-expressions.html#recursive-descent-parsing
static struct expr parse_expression(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct expr expr = parse_or(i);
	DECREASE_PARSING_DEPTH();
	return expr;
}

static struct statement *parse_statements(size_t *i, size_t *body_statement_count);

static struct statement parse_while_statement(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct statement statement = {0};
	statement.type = WHILE_STATEMENT;

	consume_space(i);
	statement.while_statement.condition = parse_expression(i);

	statement.while_statement.body_statements = parse_statements(i, &statement.while_statement.body_statement_count);

	DECREASE_PARSING_DEPTH();
	return statement;
}

static struct statement parse_if_statement(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct statement statement = {0};
	statement.type = IF_STATEMENT;

	consume_space(i);
	statement.if_statement.condition = parse_expression(i);

	statement.if_statement.if_body_statements = parse_statements(i, &statement.if_statement.if_body_statement_count);

	if (peek_token(*i).type == SPACE_TOKEN) {
		(*i)++;

		consume_token_type(i, ELSE_TOKEN);

		if (peek_token(*i).type == SPACE_TOKEN && peek_token(*i + 1).type == IF_TOKEN) {
			(*i) += 2;

			statement.if_statement.else_body_statement_count = 1;

			statement.if_statement.else_body_statements = push_statement(parse_if_statement(i));
		} else {
			statement.if_statement.else_body_statements = parse_statements(i, &statement.if_statement.else_body_statement_count);
		}
	}

	DECREASE_PARSING_DEPTH();
	return statement;
}

static struct variable_statement parse_local_variable(size_t *i) {
	struct variable_statement local = {0};

	size_t name_token_index = *i;
	local.name = consume_token(i).str;

	if (peek_token(*i).type == COLON_TOKEN) {
		(*i)++;

		grug_assert(!streq(local.name, "me"), "The local variable 'me' has to have its name changed to something else, since grug already declares that variable");

		consume_space(i);
		struct token type_token = consume_token(i);
		grug_assert(type_token.type == WORD_TOKEN, "Expected a word token after the colon on line %zu", get_token_line_number(name_token_index));

		local.has_type = true;
		local.type = parse_type(type_token.str);
		local.type_name = type_token.str;
		grug_assert(local.type != type_resource, "The variable '%s' can't have 'resource' as its type", local.name);
		grug_assert(local.type != type_entity, "The variable '%s' can't have 'entity' as its type", local.name);
	}

	grug_assert(peek_token(*i).type == SPACE_TOKEN, "The variable '%s' was not assigned a value on line %zu", local.name, get_token_line_number(name_token_index));

	consume_space(i);
	consume_token_type(i, ASSIGNMENT_TOKEN);

	grug_assert(!streq(local.name, "me"), "Assigning a new value to the entity's 'me' variable is not allowed");

	consume_space(i);
	local.assignment_expr = push_expr(parse_expression(i));

	return local;
}

static struct global_variable_statement *push_global_variable(struct global_variable_statement global_variable) {
	grug_assert(global_variable_statements_size < MAX_GLOBAL_VARIABLES, "There are more than %d global variables in the grug file, exceeding MAX_GLOBAL_VARIABLES", MAX_GLOBAL_VARIABLES);
	global_variable_statements[global_variable_statements_size] = global_variable;
	return global_variable_statements + global_variable_statements_size++;
}

static struct global_variable_statement parse_global_variable(size_t *i) {
	struct global_variable_statement global = {0};

	size_t name_token_index = *i;
	global.name = consume_token(i).str;

	grug_assert(!streq(global.name, "me"), "The global variable 'me' has to have its name changed to something else, since grug already declares that variable");

	assert_token_type(*i, COLON_TOKEN);
	consume_token(i);

	consume_space(i);
	assert_token_type(*i, WORD_TOKEN);
	struct token type_token = consume_token(i);
	global.type = parse_type(type_token.str);
	global.type_name = type_token.str;

	grug_assert(global.type != type_resource, "The global variable '%s' can't have 'resource' as its type", global.name);
	grug_assert(global.type != type_entity, "The global variable '%s' can't have 'entity' as its type", global.name);

	grug_assert(peek_token(*i).type == SPACE_TOKEN, "The global variable '%s' was not assigned a value on line %zu", global.name, get_token_line_number(name_token_index));

	consume_space(i);
	assert_token_type(*i, ASSIGNMENT_TOKEN);
	consume_token(i);

	consume_space(i);
	global.assignment_expr = parse_expression(i);

	return global;
}

static struct statement parse_statement(size_t *i) {
	INCREASE_PARSING_DEPTH();
	struct token switch_token = peek_token(*i);

	struct statement statement = {0};
	switch (switch_token.type) {
		case WORD_TOKEN: {
			struct token token = peek_token(*i + 1);
			if (token.type == OPEN_PARENTHESIS_TOKEN) {
				statement.type = CALL_STATEMENT;
				struct expr expr = parse_call(i);
				statement.call_statement.expr = push_expr(expr);
			} else if (token.type == COLON_TOKEN || token.type == SPACE_TOKEN) {
				statement.type = VARIABLE_STATEMENT;
				statement.variable_statement = parse_local_variable(i);
			} else {
				grug_error("Expected '(', or ':', or ' =' after the word '%s' on line %zu", switch_token.str, get_token_line_number(*i));
			}

			break;
		}
		case IF_TOKEN:
			(*i)++;
			statement = parse_if_statement(i);
			break;
		case RETURN_TOKEN: {
			(*i)++;
			statement.type = RETURN_STATEMENT;

			struct token token = peek_token(*i);
			if (token.type == NEWLINE_TOKEN) {
				statement.return_statement.has_value = false;
			} else {
				statement.return_statement.has_value = true;
				consume_space(i);
				statement.return_statement.value = push_expr(parse_expression(i));
			}

			break;
		}
		case WHILE_TOKEN:
			(*i)++;
			statement = parse_while_statement(i);
			break;
		case BREAK_TOKEN:
			(*i)++;
			statement.type = BREAK_STATEMENT;
			break;
		case CONTINUE_TOKEN:
			(*i)++;
			statement.type = CONTINUE_STATEMENT;
			break;
		case NEWLINE_TOKEN:
			(*i)++;
			statement.type = EMPTY_LINE_STATEMENT;
			break;
		case COMMENT_TOKEN:
			(*i)++;
			statement.type = COMMENT_STATEMENT;
			statement.comment = switch_token.str;
			break;
		default:
			grug_error("Expected a statement token, but got token type %s on line %zu", get_token_type_str[switch_token.type], get_token_line_number(*i - 1));
	}

	DECREASE_PARSING_DEPTH();
	return statement;
}

static struct statement *parse_statements(size_t *i, size_t *body_statement_count) {
	INCREASE_PARSING_DEPTH();
	consume_space(i);
	consume_token_type(i, OPEN_BRACE_TOKEN);

	consume_newline(i);

	// This local array is necessary, cause an IF statement can contain its own statements
	struct statement local_statements[MAX_STATEMENTS_PER_SCOPE];
	*body_statement_count = 0;

	indentation++;

	bool seen_newline = false;
	bool newline_allowed = false;

	while (true) {
		if (is_end_of_block(i)) {
			break;
		}

		if (peek_token(*i).type == NEWLINE_TOKEN) {
			grug_assert(newline_allowed, "Unexpected empty line, on line %zu", get_token_line_number(*i));
			(*i)++;

			seen_newline = true;

			// Disallow consecutive empty lines
			newline_allowed = false;

			struct statement statement = {
				.type = EMPTY_LINE_STATEMENT,
			};

			grug_assert(*body_statement_count < MAX_STATEMENTS_PER_SCOPE, "There are more than %d statements in one of the grug file's scopes, exceeding MAX_STATEMENTS_PER_SCOPE", MAX_STATEMENTS_PER_SCOPE);
			local_statements[(*body_statement_count)++] = statement;
		} else {
			newline_allowed = true;

			consume_indentation(i);

			struct statement statement = parse_statement(i);

			grug_assert(*body_statement_count < MAX_STATEMENTS_PER_SCOPE, "There are more than %d statements in one of the grug file's scopes, exceeding MAX_STATEMENTS_PER_SCOPE", MAX_STATEMENTS_PER_SCOPE);
			local_statements[(*body_statement_count)++] = statement;

			consume_token_type(i, NEWLINE_TOKEN);
		}
	}

	grug_assert(!seen_newline || newline_allowed, "Unexpected empty line, on line %zu", get_token_line_number(newline_allowed ? *i : *i - 1));

	assert(indentation > 0);
	indentation--;

	struct statement *first_statement = statements + statements_size;
	for (size_t statement_index = 0; statement_index < *body_statement_count; statement_index++) {
		push_statement(local_statements[statement_index]);
	}

	if (indentation > 0) {
		consume_indentation(i);
	}
	consume_token_type(i, CLOSE_BRACE_TOKEN);

	DECREASE_PARSING_DEPTH();
	return first_statement;
}

static struct argument *push_argument(struct argument argument) {
	grug_assert(arguments_size < MAX_ARGUMENTS, "There are more than %d arguments in the grug file, exceeding MAX_ARGUMENTS", MAX_ARGUMENTS);
	arguments[arguments_size] = argument;
	return arguments + arguments_size++;
}

static struct argument *parse_arguments(size_t *i, size_t *argument_count) {
	struct argument argument = {.name = consume_token(i).str};

	consume_token_type(i, COLON_TOKEN);

	consume_space(i);
	assert_token_type(*i, WORD_TOKEN);

	const char *type_name = consume_token(i).str;
	argument.type = parse_type(type_name);
	argument.type_name = type_name;
	grug_assert(argument.type != type_resource, "The argument '%s' can't have 'resource' as its type", argument.name);
	grug_assert(argument.type != type_entity, "The argument '%s' can't have 'entity' as its type", argument.name);
	struct argument *first_argument = push_argument(argument);
	(*argument_count)++;

	// Every argument after the first one starts with a comma
	while (true) {
		if (peek_token(*i).type != COMMA_TOKEN) {
			break;
		}
		(*i)++;

		consume_space(i);
		assert_token_type(*i, WORD_TOKEN);
		argument.name = consume_token(i).str;

		consume_token_type(i, COLON_TOKEN);

		consume_space(i);
		assert_token_type(*i, WORD_TOKEN);
		type_name = consume_token(i).str;
		argument.type = parse_type(type_name);
		argument.type_name = type_name;

		grug_assert(argument.type != type_resource, "The argument '%s' can't have 'resource' as its type", argument.name);
		grug_assert(argument.type != type_entity, "The argument '%s' can't have 'entity' as its type", argument.name);

		push_argument(argument);
		(*argument_count)++;
	}

	return first_argument;
}

static bool is_empty_function(struct statement *body_statements, size_t count) {
	for (size_t i = 0; i < count; i++) {
		struct statement statement = body_statements[i];

		if (statement.type != EMPTY_LINE_STATEMENT && statement.type != COMMENT_STATEMENT) {
			return false;
		}
	}
	return true;
}

static struct helper_fn parse_helper_fn(size_t *i) {
	struct helper_fn fn = {0};

	struct token token = consume_token(i);
	fn.fn_name = token.str;

	if (!seen_called_helper_fn_name(fn.fn_name)) {
		grug_error("%s() is defined before the first time it gets called", fn.fn_name);
	}

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		fn.arguments = parse_arguments(i, &fn.argument_count);
	}

	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	assert_token_type(*i, SPACE_TOKEN);
	token = peek_token(*i + 1);
	fn.return_type = type_void;
	if (token.type == WORD_TOKEN) {
		(*i) += 2;
		fn.return_type = parse_type(token.str);
		fn.return_type_name = token.str;
		grug_assert(fn.return_type != type_resource, "The function '%s' can't have 'resource' as its return type", fn.fn_name);
		grug_assert(fn.return_type != type_entity, "The function '%s' can't have 'entity' as its return type", fn.fn_name);
	}

	indentation = 0;
	fn.body_statements = parse_statements(i, &fn.body_statement_count);

	grug_assert(!is_empty_function(fn.body_statements, fn.body_statement_count), "%s() can't be empty", fn.fn_name);

	return fn;
}

static struct on_fn parse_on_fn(size_t *i) {
	struct on_fn fn = {0};

	struct token token = consume_token(i);
	fn.fn_name = token.str;

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		fn.arguments = parse_arguments(i, &fn.argument_count);
	}

	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	indentation = 0;
	fn.body_statements = parse_statements(i, &fn.body_statement_count);

	grug_assert(!is_empty_function(fn.body_statements, fn.body_statement_count), "%s() can't be empty", fn.fn_name);

	return fn;
}

static void push_global_statement(struct global_statement global) {
	grug_assert(global_statements_size < MAX_GLOBAL_STATEMENTS, "There are more than %d global statements in the grug file, exceeding MAX_GLOBAL_STATEMENTS", MAX_GLOBAL_STATEMENTS);
	global_statements[global_statements_size++] = global;
}

static void parse(void) {
	reset_parsing();

	bool seen_on_fn = false;
	bool seen_newline = false;

	bool newline_allowed = false;
	bool newline_required = false;

	bool just_seen_global = false;

	size_t i = 0;
	while (i < tokens_size) {
		struct token token = peek_token(i);
		enum token_type type = token.type;

		if (type == WORD_TOKEN && i + 1 < tokens_size && peek_token(i + 1).type == COLON_TOKEN) {
			grug_assert(!seen_on_fn, "Move the global variable '%s' so it is above the on_ functions", token.str);

			// Make having an empty line between globals optional
			grug_assert(!newline_required || just_seen_global, "Expected an empty line, on line %zu", get_token_line_number(i));

			struct global_variable_statement variable = parse_global_variable(&i);

			newline_allowed = true;
			newline_required = true;

			just_seen_global = true;

			struct global_statement global = {
				.type = GLOBAL_VARIABLE,
				.global_variable = push_global_variable(variable),
			};
			push_global_statement(global);
			consume_token_type(&i, NEWLINE_TOKEN);
		} else if (type == WORD_TOKEN && starts_with(token.str, "on_") && i + 1 < tokens_size && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(helper_fns_size == 0, "%s() must be defined before all helper_ functions", token.str);
			grug_assert(!newline_required, "Expected an empty line, on line %zu", get_token_line_number(i));

			struct on_fn fn = parse_on_fn(&i);

			seen_on_fn = true;

			newline_allowed = true;
			newline_required = true;

			just_seen_global = false;

			struct global_statement global = {
				.type = GLOBAL_ON_FN,
				.on_fn = push_on_fn(fn),
			};
			push_global_statement(global);
			consume_token_type(&i, NEWLINE_TOKEN);
		} else if (type == WORD_TOKEN && starts_with(token.str, "helper_") && i + 1 < tokens_size && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(!newline_required, "Expected an empty line, on line %zu", get_token_line_number(i));

			struct helper_fn fn = parse_helper_fn(&i);

			newline_allowed = true;
			newline_required = true;

			just_seen_global = false;

			struct global_statement global = {
				.type = GLOBAL_HELPER_FN,
				.helper_fn = push_helper_fn(fn),
			};
			push_global_statement(global);
			consume_token_type(&i, NEWLINE_TOKEN);
		} else if (type == NEWLINE_TOKEN) {
			grug_assert(newline_allowed, "Unexpected empty line, on line %zu", get_token_line_number(i));

			seen_newline = true;

			// Disallow consecutive empty lines
			newline_allowed = false;
			newline_required = false;

			just_seen_global = false;

			struct global_statement global = {
				.type = GLOBAL_EMPTY_LINE,
			};
			push_global_statement(global);

			i++;
		} else if (type == COMMENT_TOKEN) {
			newline_allowed = true;

			// Deliberately not commenting these in,
			// since we want their state to stay whatever it was
			// newline_required = false or true;
			// just_seen_global = false;

			struct global_statement global = {
				.type = GLOBAL_COMMENT,
				.comment = token.str,
			};
			push_global_statement(global);

			i++;
			consume_token_type(&i, NEWLINE_TOKEN);
		} else {
			grug_error("Unexpected token '%s' on line %zu", token.str, get_token_line_number(i));
		}
	}

	grug_assert(!seen_newline || newline_allowed, "Unexpected empty line, on line %zu", get_token_line_number(newline_allowed ? i : i - 1));

	assert(parsing_depth == 0);

	hash_helper_fns();
}

//// DUMPING AST

#define dump(...) {\
	if (fprintf(dumped_stream, __VA_ARGS__) < 0) {\
		abort();\
	}\
}

static FILE *dumped_stream;

static void dump_expr(struct expr expr);

static void dump_parenthesized_expr(struct expr *parenthesized_expr) {
	dump("\"expr\":{");
	dump_expr(*parenthesized_expr);
	dump("}");
}

static void dump_call_expr(struct call_expr call_expr) {
	dump("\"name\":\"%s\"", call_expr.fn_name);

	if (call_expr.argument_count > 0) {
		dump(",\"arguments\":[");
		for (size_t i = 0; i < call_expr.argument_count; i++) {
			if (i > 0) {
				dump(",");
			}
			dump("{");
			dump_expr(call_expr.arguments[i]);
			dump("}");
		}
		dump("]");
	}
}

static void dump_binary_expr(struct binary_expr binary_expr) {
	dump("\"left_expr\":{");
	dump_expr(*binary_expr.left_expr);
	dump("},");
	dump("\"operator\":\"%s\",", get_token_type_str[binary_expr.operator]);
	dump("\"right_expr\":{");
	dump_expr(*binary_expr.right_expr);
	dump("}");
}

static void dump_expr(struct expr expr) {
	dump("\"type\":\"%s\"", get_expr_type_str[expr.type]);

	switch (expr.type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
			break;
		case STRING_EXPR:
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
		case IDENTIFIER_EXPR:
			dump(",\"str\":\"%s\"", expr.literal.string);
			break;
		case I32_EXPR:
			dump(",\"value\":\"%d\"", expr.literal.i32);
			break;
		case F32_EXPR:
			dump(",\"value\":\"%s\"", expr.literal.f32.string);
			break;
		case UNARY_EXPR:
			dump(",\"operator\":\"%s\",", get_token_type_str[expr.unary.operator]);
			dump("\"expr\":{");
			dump_expr(*expr.unary.expr);
			dump("}");
			break;
		case BINARY_EXPR:
		case LOGICAL_EXPR:
			dump(",");
			dump_binary_expr(expr.binary);
			break;
		case CALL_EXPR:
			dump(",");
			dump_call_expr(expr.call);
			break;
		case PARENTHESIZED_EXPR:
			dump(",");
			dump_parenthesized_expr(expr.parenthesized);
			break;
	}
}

static void dump_statements(struct statement *body_statements, size_t statement_count) {
	for (size_t i = 0; i < statement_count; i++) {
		if (i > 0) {
			dump(",");
		}

		dump("{");

		struct statement statement = body_statements[i];

		dump("\"type\":\"%s\"", get_statement_type_str[statement.type]);

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				dump(",\"name\":\"%s\"", statement.variable_statement.name);

				if (statement.variable_statement.has_type) {
					dump(",\"variable_type\":\"%s\"", statement.variable_statement.type_name);
				}

				dump(",\"assignment\":{");
				dump_expr(*statement.variable_statement.assignment_expr);
				dump("}");

				break;
			case CALL_STATEMENT:
				dump(",");
				dump_call_expr(statement.call_statement.expr->call);
				break;
			case IF_STATEMENT:
				dump(",\"condition\":{");
				dump_expr(statement.if_statement.condition);
				dump("}");

				if (statement.if_statement.if_body_statement_count > 0) {
					dump(",\"if_statements\":[");
					dump_statements(statement.if_statement.if_body_statements, statement.if_statement.if_body_statement_count);
					dump("]");
				}

				if (statement.if_statement.else_body_statement_count > 0) {
					dump(",\"else_statements\":[");
					dump_statements(statement.if_statement.else_body_statements, statement.if_statement.else_body_statement_count);
					dump("]");
				}

				break;
			case RETURN_STATEMENT:
				if (statement.return_statement.has_value) {
					dump(",\"expr\":{");
					dump_expr(*statement.return_statement.value);
					dump("}");
				}
				break;
			case WHILE_STATEMENT:
				dump(",\"condition\":{");
				dump_expr(statement.while_statement.condition);
				dump("},");

				dump("\"statements\":[");
				dump_statements(statement.while_statement.body_statements, statement.while_statement.body_statement_count);
				dump("]");

				break;
			case COMMENT_STATEMENT:
				dump(",\"comment\":\"%s\"", statement.comment);
				break;
			case BREAK_STATEMENT:
			case CONTINUE_STATEMENT:
			case EMPTY_LINE_STATEMENT:
				break;
		}

		dump("}");
	}
}

static void dump_arguments(struct argument *arguments_offset, size_t argument_count) {
	if (argument_count == 0) {
		return;
	}

	dump(",\"arguments\":[");

	for (size_t i = 0; i < argument_count; i++) {
		if (i > 0) {
			dump(",");
		}

		dump("{");

		struct argument arg = arguments_offset[i];

		dump("\"name\":\"%s\",", arg.name);
		dump("\"type\":\"%s\"", arg.type_name);

		dump("}");
	}

	dump("]");
}

static void dump_global_statement(struct global_statement global) {
	dump("{");

	dump("\"type\":\"%s\"", get_global_statement_type_str[global.type]);

	switch (global.type) {
		case GLOBAL_VARIABLE: {
			struct global_variable_statement global_variable = *global.global_variable;

			dump(",\"name\":\"%s\",", global_variable.name);

			dump("\"variable_type\":\"%s\",", global_variable.type_name);

			dump("\"assignment\":{");
			dump_expr(global_variable.assignment_expr);
			dump("}");

			break;
		}
		case GLOBAL_ON_FN: {
			struct on_fn fn = *global.on_fn;

			dump(",\"name\":\"%s\"", fn.fn_name);

			dump_arguments(fn.arguments, fn.argument_count);

			dump(",\"statements\":[");
			dump_statements(fn.body_statements, fn.body_statement_count);
			dump("]");

			break;
		}
		case GLOBAL_HELPER_FN: {
			struct helper_fn fn = *global.helper_fn;

			dump(",\"name\":\"%s\"", fn.fn_name);

			dump_arguments(fn.arguments, fn.argument_count);

			dump(",");
			if (fn.return_type) {
				dump("\"return_type\":\"%s\",", fn.return_type_name);
			}

			dump("\"statements\":[");
			dump_statements(fn.body_statements, fn.body_statement_count);
			dump("]");

			break;
		}
		case GLOBAL_COMMENT:
			dump(",\"comment\":\"%s\"", global.comment);
			break;
		case GLOBAL_EMPTY_LINE:
			break;
	}

	dump("}");
}

static void dump_file_to_opened_json(const char *input_grug_path) {
	read_file(input_grug_path);

	tokenize();

	parse();

	dump("[");

	for (size_t i = 0; i < global_statements_size; i++) {
		if (i > 0) {
			dump(",");
		}

		dump_global_statement(global_statements[i]);
	}

	dump("]\n");
}

bool grug_dump_file_to_json(const char *input_grug_path, const char *output_json_path) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	dumped_stream = fopen(output_json_path, "w");
	grug_assert(dumped_stream, "fopen: %s", strerror(errno));

	dump_file_to_opened_json(input_grug_path);

	grug_assert(fclose(dumped_stream) == 0, "fclose: %s", strerror(errno));

	return false;
}

static void dump_mods_to_opened_json(const char *dir_path) {
	DIR *dirp = opendir(dir_path);
	grug_assert(dirp, "opendir(\"%s\"): %s", dir_path, strerror(errno));

	struct dirent *dp;

	size_t seen_dir_count = 0;
	errno = 0;
	while ((dp = readdir(dirp))) {
		const char *name = dp->d_name;

		if (streq(name, ".") || streq(name, "..")) {
			continue;
		}

		char entry_path[STUPID_MAX_PATH];
		snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, name);

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

		if (S_ISDIR(entry_stat.st_mode)) {
			if (seen_dir_count == 0) {
				dump("\"dirs\":{");
			} else {
				dump(",");
			}

			dump("\"%s\":{", name);
			dump_mods_to_opened_json(entry_path);
			dump("}");

			seen_dir_count++;
		}
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));
	if (seen_dir_count > 0) {
		dump("}");
	}

	size_t seen_file_count = 0;
	rewinddir(dirp);
	errno = 0;
	while ((dp = readdir(dirp))) {
		const char *name = dp->d_name;

		if (streq(name, ".") || streq(name, "..")) {
			continue;
		}

		char entry_path[STUPID_MAX_PATH];
		snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, name);

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

		if (S_ISREG(entry_stat.st_mode) && streq(get_file_extension(name), ".grug")) {
			if (seen_file_count == 0) {
				if (seen_dir_count > 0) {
					dump(",");
				}

				dump("\"files\":{");
			} else {
				dump(",");
			}

			dump("\"%s\":", name);
			dump_file_to_opened_json(entry_path);

			seen_file_count++;
		}
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));
	if (seen_file_count > 0) {
		dump("}");
	}

	closedir(dirp);
}

bool grug_dump_mods_to_json(const char *input_mods_path, const char *output_json_path) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	dumped_stream = fopen(output_json_path, "w");
	grug_assert(dumped_stream, "fopen: %s", strerror(errno));

	dump("{");

	dump_mods_to_opened_json(input_mods_path);

	dump("}");

	grug_assert(fclose(dumped_stream) == 0, "fclose: %s", strerror(errno));

	return false;
}

//// APPLYING AST

#define apply(...) {\
	if (fprintf(applied_stream, __VA_ARGS__) < 0) {\
		abort();\
	}\
}

static FILE *applied_stream;

static enum statement_type get_statement_type_from_str(const char *str) {
	if (streq(str, "VARIABLE_STATEMENT")) {
		return VARIABLE_STATEMENT;
	} else if (streq(str, "CALL_STATEMENT")) {
		return CALL_STATEMENT;
	} else if (streq(str, "IF_STATEMENT")) {
		return IF_STATEMENT;
	} else if (streq(str, "RETURN_STATEMENT")) {
		return RETURN_STATEMENT;
	} else if (streq(str, "WHILE_STATEMENT")) {
		return WHILE_STATEMENT;
	} else if (streq(str, "BREAK_STATEMENT")) {
		return BREAK_STATEMENT;
	} else if (streq(str, "CONTINUE_STATEMENT")) {
		return CONTINUE_STATEMENT;
	} else if (streq(str, "EMPTY_LINE_STATEMENT")) {
		return EMPTY_LINE_STATEMENT;
	} else if (streq(str, "COMMENT_STATEMENT")) {
		return COMMENT_STATEMENT;
	}
	grug_unreachable();
}

static enum token_type get_unary_token_type_from_str(const char *str) {
	if (streq(str, "MINUS_TOKEN")) {
		return MINUS_TOKEN;
	} else if (streq(str, "NOT_TOKEN")) {
		return NOT_TOKEN;
	}
	grug_unreachable();
}

static const char *get_logical_operator_from_token(const char *str) {
	if (streq(str, "AND_TOKEN")) {
		return "and";
	} else if (streq(str, "OR_TOKEN")) {
		return "or";
	}
	grug_unreachable();
}

static const char *get_binary_operator_from_token(const char *str) {
	if (streq(str, "PLUS_TOKEN")) {
		return "+";
	} else if (streq(str, "MINUS_TOKEN")) {
		return "-";
	} else if (streq(str, "MULTIPLICATION_TOKEN")) {
		return "*";
	} else if (streq(str, "DIVISION_TOKEN")) {
		return "/";
	} else if (streq(str, "REMAINDER_TOKEN")) {
		return "%";
	} else if (streq(str, "EQUALS_TOKEN")) {
		return "==";
	} else if (streq(str, "NOT_EQUALS_TOKEN")) {
		return "!=";
	} else if (streq(str, "GREATER_OR_EQUAL_TOKEN")) {
		return ">=";
	} else if (streq(str, "GREATER_TOKEN")) {
		return ">";
	} else if (streq(str, "LESS_OR_EQUAL_TOKEN")) {
		return "<=";
	} else if (streq(str, "LESS_TOKEN")) {
		return "<";
	}
	grug_unreachable();
}

static enum expr_type get_expr_type_from_str(const char *str) {
	if (streq(str, "TRUE_EXPR")) {
		return TRUE_EXPR;
	} else if (streq(str, "FALSE_EXPR")) {
		return FALSE_EXPR;
	} else if (streq(str, "STRING_EXPR")) {
		return STRING_EXPR;
	} else if (streq(str, "IDENTIFIER_EXPR")) {
		return IDENTIFIER_EXPR;
	} else if (streq(str, "I32_EXPR")) {
		return I32_EXPR;
	} else if (streq(str, "F32_EXPR")) {
		return F32_EXPR;
	} else if (streq(str, "UNARY_EXPR")) {
		return UNARY_EXPR;
	} else if (streq(str, "BINARY_EXPR")) {
		return BINARY_EXPR;
	} else if (streq(str, "LOGICAL_EXPR")) {
		return LOGICAL_EXPR;
	} else if (streq(str, "CALL_EXPR")) {
		return CALL_EXPR;
	} else if (streq(str, "PARENTHESIZED_EXPR")) {
		return PARENTHESIZED_EXPR;
	}
	grug_unreachable();
}

static void apply_expr(struct json_node expr);

static void apply_expr(struct json_node expr) {
	grug_assert(expr.type == JSON_NODE_OBJECT, "input_json_path its exprs are supposed to be an object");

	size_t field_count = expr.object.field_count;
	grug_assert(field_count > 0, "input_json_path its exprs are supposed to have at least a \"type\" field");

	grug_assert(streq(expr.object.fields[0].key, "type"), "input_json_path its exprs are supposed to have \"type\" as their first field");

	grug_assert(expr.object.fields[0].value->type == JSON_NODE_STRING, "input_json_path its exprs are supposed to have a \"type\" with type string");

	const char *type = expr.object.fields[0].value->string;

	switch (get_expr_type_from_str(type)) {
		case TRUE_EXPR:
			grug_assert(field_count == 1, "input_json_path its TRUE_EXPRs are supposed to have exactly 1 field");
			apply("true");
			break;
		case FALSE_EXPR:
			grug_assert(field_count == 1, "input_json_path its FALSE_EXPRs are supposed to have exactly 1 field");
			apply("false");
			break;
		case STRING_EXPR:
			grug_assert(field_count == 2, "input_json_path its STRING_EXPRs are supposed to have exactly 2 fields");

			grug_assert(streq(expr.object.fields[1].key, "str"), "input_json_path its STRING_EXPRs are supposed to have \"str\" as their second field");

			grug_assert(expr.object.fields[1].value->type == JSON_NODE_STRING, "input_json_path its STRING_EXPRs are supposed to have a \"str\" with type string");

			apply("\"%s\"", expr.object.fields[1].value->string);

			break;
		case IDENTIFIER_EXPR:
			grug_assert(field_count == 2, "input_json_path its IDENTIFIER_EXPRs are supposed to have exactly 2 fields");

			grug_assert(streq(expr.object.fields[1].key, "str"), "input_json_path its IDENTIFIER_EXPRs are supposed to have \"str\" as their second field");

			grug_assert(expr.object.fields[1].value->type == JSON_NODE_STRING, "input_json_path its IDENTIFIER_EXPRs are supposed to have a \"str\" with type string");

			const char *identifier = expr.object.fields[1].value->string;
			grug_assert(strlen(identifier) > 0, "input_json_path its IDENTIFIER_EXPRs are not supposed to have an empty \"str\" string");

			apply("%s", identifier);

			break;
		case I32_EXPR: {
			grug_assert(field_count == 2, "input_json_path its I32_EXPRs are supposed to have exactly 2 fields");

			grug_assert(streq(expr.object.fields[1].key, "value"), "input_json_path its I32_EXPRs are supposed to have \"value\" as their second field");

			grug_assert(expr.object.fields[1].value->type == JSON_NODE_STRING, "input_json_path its I32_EXPRs are supposed to have a \"value\" with type string");

			const char *i32_string = expr.object.fields[1].value->string;
			grug_assert(strlen(i32_string) > 0, "input_json_path its I32_EXPRs are not supposed to have an empty \"value\" string");

			apply("%s", i32_string);

			break;
		}
		case F32_EXPR:
			grug_assert(field_count == 2, "input_json_path its F32_EXPRs are supposed to have exactly 2 fields");

			grug_assert(streq(expr.object.fields[1].key, "value"), "input_json_path its F32_EXPRs are supposed to have \"value\" as their second field");

			grug_assert(expr.object.fields[1].value->type == JSON_NODE_STRING, "input_json_path its F32_EXPRs are supposed to have a \"value\" with type string");

			const char *f32_string = expr.object.fields[1].value->string;
			grug_assert(strlen(f32_string) > 0, "input_json_path its F32_EXPRs are not supposed to have an empty \"value\" string");

			apply("%s", f32_string);

			break;
		case UNARY_EXPR:
			grug_assert(field_count == 3, "input_json_path its UNARY_EXPRs are supposed to have exactly 3 fields");

			grug_assert(streq(expr.object.fields[1].key, "operator"), "input_json_path its UNARY_EXPRs are supposed to have \"operator\" as their second field");

			grug_assert(expr.object.fields[1].value->type == JSON_NODE_STRING, "input_json_path its UNARY_EXPRs are supposed to have an \"operator\" with type string");

			enum token_type operator = get_unary_token_type_from_str(expr.object.fields[1].value->string);

			if (operator == MINUS_TOKEN) {
				apply("-");
			} else if (operator == NOT_TOKEN) {
				apply("not ");
			} else {
				grug_unreachable();
			}

			grug_assert(streq(expr.object.fields[2].key, "expr"), "input_json_path its UNARY_EXPRs are supposed to have \"expr\" as their third field");

			apply_expr(*expr.object.fields[2].value);

			break;
		case BINARY_EXPR:
			grug_assert(field_count == 4, "input_json_path its BINARY_EXPRs are supposed to have exactly 4 fields");

			grug_assert(streq(expr.object.fields[1].key, "left_expr"), "input_json_path its I32_EXPRs are supposed to have \"left_expr\" as their second field");
			grug_assert(expr.object.fields[1].value->type == JSON_NODE_OBJECT, "input_json_path its I32_EXPRs are supposed to have a \"left_expr\" with type object");
			apply_expr(*expr.object.fields[1].value);

			grug_assert(streq(expr.object.fields[2].key, "operator"), "input_json_path its I32_EXPRs are supposed to have \"operator\" as their third field");
			grug_assert(expr.object.fields[2].value->type == JSON_NODE_STRING, "input_json_path its I32_EXPRs are supposed to have an \"operator\" with type string");
			apply(" %s ", get_binary_operator_from_token(expr.object.fields[2].value->string));

			grug_assert(streq(expr.object.fields[3].key, "right_expr"), "input_json_path its I32_EXPRs are supposed to have \"right_expr\" as their fourth field");
			grug_assert(expr.object.fields[3].value->type == JSON_NODE_OBJECT, "input_json_path its I32_EXPRs are supposed to have a \"right_expr\" with type object");
			apply_expr(*expr.object.fields[3].value);

			break;
		case LOGICAL_EXPR:
			grug_assert(field_count == 4, "input_json_path its LOGICAL_EXPRs are supposed to have exactly 4 fields");

			grug_assert(streq(expr.object.fields[1].key, "left_expr"), "input_json_path its LOGICAL_EXPRs are supposed to have \"left_expr\" as their second field");

			apply_expr(*expr.object.fields[1].value);

			grug_assert(streq(expr.object.fields[2].key, "operator"), "input_json_path its LOGICAL_EXPRs are supposed to have \"operator\" as their third field");

			grug_assert(expr.object.fields[2].value->type == JSON_NODE_STRING, "input_json_path its LOGICAL_EXPRs are supposed to have an \"operator\" with type string");

			apply(" %s ", get_logical_operator_from_token(expr.object.fields[2].value->string));

			grug_assert(streq(expr.object.fields[3].key, "right_expr"), "input_json_path its LOGICAL_EXPRs are supposed to have \"right_expr\" as their fourth field");

			apply_expr(*expr.object.fields[3].value);

			break;
		case CALL_EXPR: {
			grug_assert(field_count == 2 || field_count == 3, "input_json_path its CALL_EXPRs are supposed to have 2 or 3 fields");

			grug_assert(streq(expr.object.fields[1].key, "name"), "input_json_path its CALL_EXPRs are supposed to have \"name\" as their second field");

			grug_assert(expr.object.fields[1].value->type == JSON_NODE_STRING, "input_json_path its CALL_EXPRs are supposed to have a \"name\" with type string");

			const char *name = expr.object.fields[1].value->string;

			apply("%s(", name);

			if (field_count == 3) {
				grug_assert(streq(expr.object.fields[2].key, "arguments"), "input_json_path its CALL_EXPRs are supposed to have \"arguments\" as their third field");

				struct json_node args_node = *expr.object.fields[2].value;

				grug_assert(args_node.type == JSON_NODE_ARRAY, "input_json_path its call expr arguments are supposed to be an array");

				struct json_node *args = args_node.array.values;

				for (size_t i = 0; i < args_node.array.value_count; i++) {
					if (i > 0) {
						apply(", ");
					}

					apply_expr(args[i]);
				}
			}

			apply(")");

			break;
		}
		case PARENTHESIZED_EXPR:
			grug_assert(field_count == 2, "input_json_path its PARENTHESIZED_EXPRs are supposed to have exactly 2 fields");

			grug_assert(streq(expr.object.fields[1].key, "expr"), "input_json_path its PARENTHESIZED_EXPRs are supposed to have \"expr\" as their second field");

			apply("(");

			apply_expr(*expr.object.fields[1].value);

			apply(")");

			break;
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
			grug_unreachable();
	}
}

static void apply_indentation(void) {
	for (size_t i = 0; i < indentation; i++) {
		apply("    ");
	}
}

static struct json_node *try_get_else_if(struct json_node node) {
	grug_assert(node.type == JSON_NODE_ARRAY, "input_json_path its \"else_statements\" must be an array");

	grug_assert(node.array.value_count > 0, "input_json_path its \"else_statements\" is supposed to contain at least one value");

	grug_assert(node.array.values[0].type == JSON_NODE_OBJECT, "input_json_path its \"else_statements\" is supposed to only contain objects");

	grug_assert(node.array.values[0].object.field_count > 1, "input_json_path its \"else_statements\" its object is supposed to contain at least a \"type\" and \"condition\" field");

	grug_assert(streq(node.array.values[0].object.fields[0].key, "type"), "input_json_path its \"else_statements\" its object is supposed to contain \"type\" as the first field");

	grug_assert(node.array.values[0].object.fields[0].value->type == JSON_NODE_STRING, "input_json_path its \"else_statements\" its object its \"type\" must be a string");

	if (streq(node.array.values[0].object.fields[0].value->string, "IF_STATEMENT")) {
		return &node.array.values[0];
	}

	return NULL;
}

static void apply_statements(struct json_node node);

static void apply_comment(struct json_field *statement, size_t field_count) {
	grug_assert(field_count == 2, "input_json_path its root array values its comments are supposed to only have a \"comment\" field after the \"type\" field");

	grug_assert(streq(statement[1].key, "comment"), "input_json_path its array value its second comment field must be \"comment\", but got \"%s\"", statement[1].key);

	grug_assert(statement[1].value->type == JSON_NODE_STRING, "input_json_path its \"comments\" values are supposed to be strings");

	const char *comment = statement[1].value->string;

	apply("# %s\n", comment);
}

static void apply_if_statement(struct json_field *statement, size_t field_count) {
	grug_assert(field_count >= 2 && field_count <= 4, "input_json_path its IF_STATEMENT is supposed to have between 2 and 4 fields");

	apply("if ");

	grug_assert(streq(statement[1].key, "condition"), "input_json_path its IF_STATEMENT is supposed to have \"condition\" as their second field, but got \"%s\"", statement[1].key);

	apply_expr(*statement[1].value);

	apply(" {\n");

	struct json_node *if_statements_node = NULL;
	struct json_node *else_statements_node = NULL;

	if (field_count > 2) {
		if (streq(statement[2].key, "if_statements")) {
			if_statements_node = statement[2].value;

			if (field_count > 3) {
				if (streq(statement[3].key, "else_statements")) {
					else_statements_node = statement[3].value;
				} else {
					grug_error("input_json_path its IF_STATEMENT its fourth optional field must be \"else_statements\", but got \"%s\"", statement[3].key);
				}
			}
		} else if (streq(statement[2].key, "else_statements")) {
			grug_assert(field_count == 3, "input_json_path its IF_STATEMENT its \"else_statements\" field isn't supposed to have another field after it");

			else_statements_node = statement[2].value;
		} else {
			grug_error("input_json_path its IF_STATEMENT its third optional field must be either \"if_statements\" or \"else_statements\", but got \"%s\"", statement[2].key);
		}
	}

	if (if_statements_node) {
		apply_statements(*if_statements_node);
	}

	if (else_statements_node) {
		apply_indentation();

		apply("} else ");

		struct json_node *else_if_node = try_get_else_if(*else_statements_node);

		if (else_if_node) {
			apply_if_statement(else_if_node->object.fields, else_if_node->object.field_count);
		} else {
			apply("{\n");
			apply_statements(*else_statements_node);
			apply_indentation();
			apply("}\n");
		}
	} else {
		apply_indentation();
		apply("}\n");
	}
}

static void apply_statement(enum statement_type type, size_t field_count, struct json_field *statement) {
	switch (type) {
		case VARIABLE_STATEMENT: {
			grug_assert(field_count == 3 || field_count == 4, "input_json_path its VARIABLE_STATEMENTs are supposed to have 3 or 4 fields");

			grug_assert(streq(statement[1].key, "name"), "input_json_path its VARIABLE_STATEMENTs are supposed to have \"name\" as their second field, but got \"%s\"", statement[1].key);

			grug_assert(statement[1].value->type == JSON_NODE_STRING, "input_json_path its VARIABLE_STATEMENTs its \"name\" fields are supposed to be a string");

			const char *name = statement[1].value->string;
			grug_assert(strlen(name) > 0, "input_json_path its VARIABLE_STATEMENTs its \"name\" fields are not supposed to be an empty string");

			apply("%s", name);

			if (streq(statement[2].key, "variable_type")) {
				grug_assert(field_count == 4, "input_json_path its VARIABLE_STATEMENTs its \"variable_type\" fields are supposed to have an \"assignment\" field after it");

				grug_assert(statement[2].value->type == JSON_NODE_STRING, "input_json_path its VARIABLE_STATEMENTs its \"variable_type\" fields are supposed to be a string");

				apply(": %s", statement[2].value->string);

				grug_assert(streq(statement[3].key, "assignment"), "input_json_path its VARIABLE_STATEMENTs its fourth field must be \"assignment\", but got \"%s\"", statement[3].key);

				apply(" = ");

				apply_expr(*statement[3].value);
			} else if (streq(statement[2].key, "assignment")) {
				grug_assert(field_count == 3, "input_json_path its VARIABLE_STATEMENTs its \"assignment\" fields aren't supposed to have a field after it");

				apply(" = ");

				apply_expr(*statement[2].value);
			} else {
				grug_error("input_json_path its VARIABLE_STATEMENTs its third fields are supposed to be either \"variable_type\" or \"assignment\", but got \"%s\"", statement[2].key);
			}

			apply("\n");

			break;
		}
		case CALL_STATEMENT: {
			grug_assert(field_count == 2 || field_count == 3, "input_json_path its CALL_STATEMENTs are supposed to have either 2 or 3 fields");

			grug_assert(streq(statement[1].key, "name"), "input_json_path its CALL_STATEMENTs are supposed to have \"name\" as their second field, but got \"%s\"", statement[1].key);

			grug_assert(statement[1].value->type == JSON_NODE_STRING, "input_json_path its CALL_STATEMENTs are supposed to have a \"name\" with type string");

			const char *name = statement[1].value->string;

			apply("%s(", name);

			if (field_count == 3) {
				grug_assert(streq(statement[2].key, "arguments"), "input_json_path its CALL_STATEMENTs are supposed to have \"arguments\" as their third field, but got \"%s\"", statement[2].key);

				struct json_node args_node = *statement[2].value;

				grug_assert(args_node.type == JSON_NODE_ARRAY, "input_json_path its call expr arguments are supposed to be an array");

				struct json_node *args = args_node.array.values;

				for (size_t i = 0; i < args_node.array.value_count; i++) {
					if (i > 0) {
						apply(", ");
					}

					apply_expr(args[i]);
				}
			}

			apply(")\n");

			break;
		}
		case IF_STATEMENT:
			apply_if_statement(statement, field_count);
			break;
		case RETURN_STATEMENT:
			grug_assert(field_count == 1 || field_count == 2, "input_json_path its RETURN_STATEMENTs are supposed to have 1 or 2 fields");

			apply("return");

			if (field_count == 2) {
				apply(" ");

				grug_assert(streq(statement[1].key, "expr"), "input_json_path its RETURN_STATEMENTs are supposed to have \"expr\" as their second field, but got \"%s\"", statement[1].key);

				apply_expr(*statement[1].value);
			}

			apply("\n");

			break;
		case WHILE_STATEMENT:
			grug_assert(field_count == 3, "input_json_path its WHILE_STATEMENTs are supposed to have exactly 3 fields");

			apply("while ");

			grug_assert(streq(statement[1].key, "condition"), "input_json_path its WHILE_STATEMENTs are supposed to have \"condition\" as their second field, but got \"%s\"", statement[1].key);

			apply_expr(*statement[1].value);

			apply(" {\n");

			grug_assert(streq(statement[2].key, "statements"), "input_json_path its WHILE_STATEMENTs are supposed to have \"statements\" as their third field, but got \"%s\"", statement[2].key);

			apply_statements(*statement[2].value);

			apply_indentation();
			apply("}\n");

			break;
		case BREAK_STATEMENT:
			apply("break\n");
			break;
		case CONTINUE_STATEMENT:
			apply("continue\n");
			break;
		case COMMENT_STATEMENT:
			apply_comment(statement, field_count);
			break;
		case EMPTY_LINE_STATEMENT:
			grug_unreachable();
		}
}

static void apply_statements(struct json_node node) {
	grug_assert(node.type == JSON_NODE_ARRAY, "input_json_path its statements are supposed to be an array");

	struct json_node *statements_array = node.array.values;

	indentation++;

	for (size_t i = 0; i < node.array.value_count; i++) {
		grug_assert(statements_array[i].type == JSON_NODE_OBJECT, "input_json_path its statements are supposed to be an array of objects");

		size_t field_count = statements_array[i].object.field_count;
		grug_assert(field_count > 0, "input_json_path its statement objects are supposed to have at least a \"type\" field");

		struct json_field *statement = statements_array[i].object.fields;

		grug_assert(streq(statement[0].key, "type"), "input_json_path its statement objects are supposed to have \"type\" as their first field, but got \"%s\"", statement[0].key);

		grug_assert(statement[0].value->type == JSON_NODE_STRING, "input_json_path its statement objects are supposed to have a \"type\" with type string");

		enum statement_type type = get_statement_type_from_str(statement[0].value->string);

		if (type == EMPTY_LINE_STATEMENT) {
			apply("\n");
		} else {
			apply_indentation();

			apply_statement(type, field_count, statement);
		}
	}

	assert(indentation > 0);
	indentation--;
}

static void apply_arguments(struct json_node node) {
	grug_assert(node.type == JSON_NODE_ARRAY, "input_json_path its \"arguments\" must be an array");

	struct json_node *args = node.array.values;

	for (size_t i = 0; i < node.array.value_count; i++) {
		if (i > 0) {
			apply(", ");
		}

		grug_assert(args[i].type == JSON_NODE_OBJECT, "input_json_path its \"arguments\" values are supposed to be objects");

		struct json_object arg = args[i].object;

		grug_assert(arg.field_count == 2, "input_json_path its \"arguments\" are supposed to have exactly 2 fields");

		grug_assert(streq(arg.fields[0].key, "name"), "input_json_path its \"arguments\" its first field must be \"name\", but got \"%s\"", arg.fields[0].key);

		grug_assert(arg.fields[0].value->type == JSON_NODE_STRING, "input_json_path its \"arguments\" its \"name\" must be a string");

		const char *name = arg.fields[0].value->string;
		grug_assert(strlen(name) > 0, "input_json_path its \"arguments\" its \"name\" is not supposed to be an empty string");

		apply("%s", name);

		grug_assert(streq(arg.fields[1].key, "type"), "input_json_path its \"arguments\" its second field must be \"type\", but got \"%s\"", arg.fields[1].key);

		grug_assert(arg.fields[1].value->type == JSON_NODE_STRING, "input_json_path its \"arguments\" its type must be a string");

		const char *type = arg.fields[1].value->string;
		grug_assert(strlen(type) > 0, "input_json_path its \"arguments\" its type is not supposed to be an empty string");

		apply(": %s", type);
	}
}

static void apply_helper_fn(struct json_field *statement, size_t field_count) {
	grug_assert(field_count >= 2 && field_count <= 5, "input_json_path its GLOBAL_HELPER_FN is supposed to have between 2 and 5 (inclusive) fields");

	grug_assert(streq(statement[1].key, "name"), "input_json_path its GLOBAL_HELPER_FN its second field must be \"name\", but got \"%s\"", statement[1].key);
	grug_assert(statement[1].value->type == JSON_NODE_STRING, "input_json_path its GLOBAL_HELPER_FN its \"name\" must be a string");
	const char *name = statement[1].value->string;
	grug_assert(strlen(name) > 0, "input_json_path its GLOBAL_HELPER_FN its \"name\" is not supposed to be an empty string");
	apply("%s(", name);

	struct json_node *arguments_node = NULL;
	struct json_node *return_type_node = NULL;
	struct json_node *statements_node = NULL;

	if (field_count > 2) {
		if (streq(statement[2].key, "arguments")) {
			arguments_node = statement[2].value;

			if (field_count > 3) {
				if (streq(statement[3].key, "return_type")) {
					return_type_node = statement[3].value;

					if (field_count > 4) {
						if (streq(statement[4].key, "statements")) {
							statements_node = statement[4].value;
						} else {
							grug_error("input_json_path its GLOBAL_ON_FN its fifth optional field must be \"statements\", but got \"%s\"", statement[4].key);
						}
					}
				} else if (streq(statement[3].key, "statements")) {
					grug_assert(field_count == 4, "input_json_path its GLOBAL_HELPER_FN its \"statements\" field isn't supposed to have another field after it");

					statements_node = statement[3].value;
				} else {
					grug_error("input_json_path its GLOBAL_HELPER_FN its fourth optional field must be either \"return_type\" or \"statements\", but got \"%s\"", statement[3].key);
				}
			}
		} else if (streq(statement[2].key, "return_type")) {
			return_type_node = statement[2].value;

			if (field_count > 3) {
				grug_assert(streq(statement[3].key, "statements"), "input_json_path its GLOBAL_ON_FN its fourth optional field must be \"statements\", but got \"%s\"", statement[3].key);

				grug_assert(field_count == 4, "input_json_path its GLOBAL_HELPER_FN its \"statements\" field isn't supposed to have another field after it");

				statements_node = statement[3].value;
			}
		} else if (streq(statement[2].key, "statements")) {
			grug_assert(field_count == 3, "input_json_path its GLOBAL_HELPER_FN its \"statements\" field isn't supposed to have another field after it");

			statements_node = statement[2].value;
		} else {
			grug_error("input_json_path its GLOBAL_HELPER_FN its third optional field must be either \"arguments\", or \"return_type\", or \"statements\", but got \"%s\"", statement[2].key);
		}
	}

	if (arguments_node) {
		apply_arguments(*arguments_node);
	}

	apply(")");

	if (return_type_node) {
		grug_assert(return_type_node->type == JSON_NODE_STRING, "input_json_path its GLOBAL_HELPER_FN \"return_type\" must be a string");
		apply(" %s", return_type_node->string);
	}

	apply(" {\n");

	if (statements_node) {
		apply_statements(*statements_node);
	}

	apply("}\n");
}

static void apply_on_fn(struct json_field *statement, size_t field_count) {
	grug_assert(field_count >= 2 && field_count <= 4, "input_json_path its GLOBAL_ON_FN is supposed to have between 2 and 4 (inclusive) fields");

	grug_assert(streq(statement[1].key, "name"), "input_json_path its GLOBAL_ON_FN its second field must be \"name\"");
	grug_assert(statement[1].value->type == JSON_NODE_STRING, "input_json_path its GLOBAL_ON_FN its \"name\" must be a string");
	const char *name = statement[1].value->string;
	grug_assert(strlen(name) > 0, "input_json_path its GLOBAL_ON_FN its \"name\" is not supposed to be an empty string");
	apply("%s(", name);

	struct json_node *arguments_node = NULL;
	struct json_node *statements_node = NULL;

	if (field_count > 2) {
		if (streq(statement[2].key, "arguments")) {
			arguments_node = statement[2].value;

			if (field_count > 3) {
				if (streq(statement[3].key, "statements")) {
					statements_node = statement[3].value;
				} else {
					grug_error("input_json_path its GLOBAL_ON_FN its fourth optional field must be \"statements\", but got \"%s\"", statement[3].key);
				}
			}
		} else if (streq(statement[2].key, "statements")) {
			grug_assert(field_count == 3, "input_json_path its GLOBAL_ON_FN its \"statements\" field isn't supposed to have another field after it");

			statements_node = statement[2].value;
		} else {
			grug_error("input_json_path its GLOBAL_ON_FN its third optional field must be either \"arguments\" or \"statements\", but got \"%s\"", statement[2].key);
		}
	}

	if (arguments_node) {
		apply_arguments(*arguments_node);
	}

	apply(") {\n");

	if (statements_node) {
		apply_statements(*statements_node);
	}

	apply("}\n");
}

static void apply_global_variable(struct json_field *statement, size_t field_count) {
	grug_assert(field_count == 4, "input_json_path its GLOBAL_VARIABLEs are supposed to have exactly 4 fields");

	grug_assert(streq(statement[1].key, "name"), "input_json_path its GLOBAL_VARIABLE its first field must be \"name\", but got \"%s\"", statement[1].key);

	grug_assert(statement[1].value->type == JSON_NODE_STRING, "input_json_path its GLOBAL_VARIABLE its \"name\" must be a string");

	const char *name = statement[1].value->string;
	grug_assert(strlen(name) > 0, "input_json_path its GLOBAL_VARIABLE its \"name\" is not supposed to be an empty string");

	apply("%s", name);

	grug_assert(streq(statement[2].key, "variable_type"), "input_json_path its GLOBAL_VARIABLE its second field must be \"variable_type\", but got \"%s\"", statement[2].key);

	struct json_node *variable_type = statement[2].value;
	grug_assert(variable_type->type == JSON_NODE_STRING, "input_json_path its GLOBAL_VARIABLE its \"variable_type\" must be a string");
	grug_assert(strlen(variable_type->string) > 0, "input_json_path its GLOBAL_VARIABLE its \"variable_type\" is not supposed to be an empty string");

	apply(": %s = ", variable_type->string);

	grug_assert(streq(statement[3].key, "assignment"), "input_json_path its GLOBAL_VARIABLE its third field must be \"assignment\", but got \"%s\"", statement[3].key);

	struct json_node assignment = *statement[3].value;
	apply_expr(assignment);

	apply("\n");
}

static enum global_statement_type get_global_statement_type_from_str(const char *str) {
	if (streq(str, "GLOBAL_VARIABLE")) {
		return GLOBAL_VARIABLE;
	} else if (streq(str, "GLOBAL_ON_FN")) {
		return GLOBAL_ON_FN;
	} else if (streq(str, "GLOBAL_HELPER_FN")) {
		return GLOBAL_HELPER_FN;
	} else if (streq(str, "GLOBAL_EMPTY_LINE")) {
		return GLOBAL_EMPTY_LINE;
	} else if (streq(str, "GLOBAL_COMMENT")) {
		return GLOBAL_COMMENT;
	}
	grug_error("get_global_statement_type_from_str() was passed the string \"%s\", which isn't a global_statement_type", str);
}

static void apply_root(struct json_node node) {
	grug_assert(node.type == JSON_NODE_ARRAY, "input_json_path its root must be an array");

	size_t statement_count = node.array.value_count;

	indentation = 0;

	for (size_t i = 0; i < statement_count; i++) {
		grug_assert(node.array.values[i].type == JSON_NODE_OBJECT, "input_json_path its root array values are supposed to be objects")

		struct json_field *statement = node.array.values[i].object.fields;

		size_t field_count = node.array.values[i].object.field_count;

		grug_assert(field_count >= 1, "input_json_path its root array values are supposed to have at least a \"type\" field");

		grug_assert(streq(statement[0].key, "type"), "input_json_path its array value its first field must be \"type\", but got \"%s\"", statement[0].key);

		grug_assert(statement[0].value->type == JSON_NODE_STRING, "input_json_path its array value its \"type\" field must be a string");

		switch (get_global_statement_type_from_str(statement[0].value->string)) {
			case GLOBAL_VARIABLE:
				apply_global_variable(statement, field_count);
				break;
			case GLOBAL_ON_FN:
				apply_on_fn(statement, field_count);
				break;
			case GLOBAL_HELPER_FN:
				apply_helper_fn(statement, field_count);
				break;
			case GLOBAL_EMPTY_LINE:
				apply("\n");
				break;
			case GLOBAL_COMMENT:
				apply_comment(statement, field_count);
				break;
		}
	}

	assert(indentation == 0);
}

static void generate_file_from_opened_json(const char *output_grug_path, struct json_node node) {
	applied_stream = fopen(output_grug_path, "w");
	grug_assert(applied_stream, "fopen: %s", strerror(errno));

	apply_root(node);

	grug_assert(fclose(applied_stream) == 0, "fclose: %s", strerror(errno));
}

bool grug_generate_file_from_json(const char *input_json_path, const char *output_grug_path) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	struct json_node node;
	json(input_json_path, &node);

	generate_file_from_opened_json(output_grug_path, node);

	return false;
}

static void generate_mods_from_opened_json(const char *mods_dir_path, struct json_node node) {
	grug_assert(node.type == JSON_NODE_OBJECT, "input_json_path contained %s, while a directory object was expected", node.type == JSON_NODE_ARRAY ? "an array" : "a string");

	grug_assert(mkdir(mods_dir_path, 0775) != -1 || errno == EEXIST, "mkdir: %s", strerror(errno));

	size_t field_count = node.object.field_count;

	grug_assert(field_count == 1 || field_count == 2, "input_json_path its directory objects are supposed to have 1 or 2 fields");

	struct json_field *node_fields = node.object.fields;

	struct json_node *dirs_node = NULL;
	struct json_node *files_node = NULL;

	if (streq(node_fields[0].key, "dirs")) {
		dirs_node = node_fields[0].value;

		if (field_count == 2) {
			grug_assert(streq(node_fields[1].key, "files"), "input_json_path its second field must be \"files\", but got \"%s\"", node_fields[1].key);

			files_node = node_fields[1].value;
		}
	} else if (streq(node_fields[0].key, "files")) {
		grug_assert(field_count == 1, "input_json_path its object its \"files\" field isn't supposed to have another field after it");

		files_node = node_fields[0].value;
	} else {
		grug_error("input_json_path its first field must be either \"dirs\" or \"files\", but got \"%s\"", node_fields[0].key);
	}

	char entry_path[STUPID_MAX_PATH];

	if (dirs_node) {
		grug_assert(dirs_node->type == JSON_NODE_OBJECT, "input_json_path its \"dirs\" value was %s, while an object containing subdirectories was expected", dirs_node->type == JSON_NODE_ARRAY ? "an array" : "a string");

		for (size_t i = 0; i < dirs_node->object.field_count; i++) {
			grug_assert(dirs_node->object.fields[i].key[0] != '\0', "input_json_path its subdirectories must not be empty strings");

			snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_dir_path, dirs_node->object.fields[i].key);

			generate_mods_from_opened_json(entry_path, *dirs_node->object.fields[i].value);
		}
	}

	if (files_node) {
		grug_assert(files_node->type == JSON_NODE_OBJECT, "input_json_path its \"files\" value was %s, while an object containing files was expected", files_node->type == JSON_NODE_ARRAY ? "an array" : "a string");

		for (size_t i = 0; i < files_node->object.field_count; i++) {
			grug_assert(files_node->object.fields[i].key[0] != '\0', "input_json_path its files must not be empty strings");

			grug_assert(streq(get_file_extension(files_node->object.fields[i].key), ".grug"), "input_json_path its file names must have the extension \".grug\"");

			snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_dir_path, files_node->object.fields[i].key);

			generate_file_from_opened_json(entry_path, *files_node->object.fields[i].value);
		}
	}
}

bool grug_generate_mods_from_json(const char *input_json_path, const char *output_mods_path) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	struct json_node node;
	json(input_json_path, &node);

	generate_mods_from_opened_json(output_mods_path, node);

	return false;
}

//// TYPE PROPAGATION

#define MAX_VARIABLES_PER_FUNCTION 420420
#define MAX_ENTITY_DEPENDENCY_NAME_LENGTH 420
#define MAX_ENTITY_DEPENDENCIES 420420
#define MAX_DATA_STRINGS 420420
#define MAX_FILE_ENTITY_TYPE_LENGTH 420

#define GLOBAL_VARIABLES_POINTER_SIZE sizeof(void *)

struct variable {
	const char *name;
	enum type type;
	const char *type_name;
	size_t offset;
};
static struct variable variables[MAX_VARIABLES_PER_FUNCTION];
static size_t variables_size;
static u32 buckets_variables[MAX_VARIABLES_PER_FUNCTION];
static u32 chains_variables[MAX_VARIABLES_PER_FUNCTION];

static struct variable global_variables[MAX_GLOBAL_VARIABLES];
static size_t global_variables_size;
static size_t globals_bytes;
static u32 buckets_global_variables[MAX_GLOBAL_VARIABLES];
static u32 chains_global_variables[MAX_GLOBAL_VARIABLES];

static size_t stack_frame_bytes;
static size_t max_stack_frame_bytes;

static enum type fn_return_type;
static const char *fn_return_type_name;
static const char *filled_fn_name;

static struct grug_entity *grug_entity;

static u32 buckets_entity_on_fns[MAX_ON_FNS];
static u32 chains_entity_on_fns[MAX_ON_FNS];

static const char *mod;
static char file_entity_type[MAX_FILE_ENTITY_TYPE_LENGTH];

static u32 entity_types[MAX_ENTITY_DEPENDENCIES];
static size_t entity_types_size;

static const char *data_strings[MAX_DATA_STRINGS];
static size_t data_strings_size;

static u32 buckets_data_strings[MAX_DATA_STRINGS];
static u32 chains_data_strings[MAX_DATA_STRINGS];

static bool *parsed_fn_calls_helper_fn_ptr;
static bool *parsed_fn_contains_while_loop_ptr;

static void reset_filling(void) {
	global_variables_size = 0;
	globals_bytes = 0;
	memset(buckets_global_variables, 0xff, sizeof(buckets_global_variables));
	entity_types_size = 0;
	data_strings_size = 0;
	memset(buckets_data_strings, 0xff, sizeof(buckets_data_strings));
}

static void push_data_string(const char *string) {
	grug_assert(data_strings_size < MAX_DATA_STRINGS, "There are more than %d data strings, exceeding MAX_DATA_STRINGS", MAX_DATA_STRINGS);

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

	grug_assert(entity_types_size < MAX_ENTITY_DEPENDENCIES, "There are more than %d entity types, exceeding MAX_ENTITY_DEPENDENCIES", MAX_ENTITY_DEPENDENCIES);

	entity_types[entity_types_size++] = get_data_string_index(entity_type);
}

static void fill_expr(struct expr *expr);

static void validate_entity_string(const char *string) {
	grug_assert(string[0] != '\0', "Entities can't be empty strings");

	const char *mod_name = mod;
	const char *entity_name = string;

	const char *colon = strchr(string, ':');
	if (colon) {
		static char temp_mod_name[MAX_ENTITY_DEPENDENCY_NAME_LENGTH];

		size_t len = colon - string;
		grug_assert(len > 0, "Entity '%s' is missing a mod name", string);

		grug_assert(len < MAX_ENTITY_DEPENDENCY_NAME_LENGTH, "There are more than %d characters in the entity '%s', exceeding MAX_ENTITY_DEPENDENCY_NAME_LENGTH", MAX_ENTITY_DEPENDENCY_NAME_LENGTH, string);
		memcpy(temp_mod_name, string, len);
		temp_mod_name[len] = '\0';

		mod_name = temp_mod_name;

		grug_assert(*entity_name != '\0', "Entity '%s' specifies the mod name '%s', but it is missing an entity name after the ':'", string, mod_name);

		entity_name = colon + 1;

		grug_assert(*entity_name != '\0', "Entity '%s' specifies the mod name '%s', but it is missing an entity name after the ':'", string, mod_name);

		grug_assert(!streq(mod_name, mod), "Entity '%s' its mod name '%s' is invalid, since the file it is in refers to its own mod; just change it to '%s'", string, mod_name, entity_name);
	}

	for (size_t i = 0; mod_name[i] != '\0'; i++) {
		char c = mod_name[i];

		grug_assert(islower(c) || isdigit(c) || c == '_' || c == '-', "Entity '%s' its mod name contains the invalid character '%c'", string, c);
	}

	for (size_t i = 0; entity_name[i] != '\0'; i++) {
		char c = entity_name[i];

		grug_assert(islower(c) || isdigit(c) || c == '_' || c == '-', "Entity '%s' its entity name contains the invalid character '%c'", string, c);
	}
}

static void validate_resource_string(const char *string, const char *resource_extension) {
	grug_assert(string[0] != '\0', "Resources can't be empty strings");

	grug_assert(string[0] != '/', "Remove the leading slash from the resource \"%s\"", string);

	size_t string_len = strlen(string);

	grug_assert(string[string_len - 1] != '/', "Remove the trailing slash from the resource \"%s\"", string);

	grug_assert(!strchr(string, '\\'), "Replace the '\\' with '/' in the resource \"%s\"", string);

	grug_assert(!strstr(string, "//"), "Replace the '//' with '/' in the resource \"%s\"", string);

	const char *dot = strchr(string, '.');
	if (dot) {
		if (dot == string) {
			grug_assert(string_len != 1 && string[1] != '/', "Remove the '.' from the resource \"%s\"", string);
		} else if (dot[-1] == '/') {
			grug_assert(dot[1] != '/' && dot[1] != '\0', "Remove the '.' from the resource \"%s\"", string);
		}
	}

	const char *dotdot = strstr(string, "..");
	if (dotdot) {
		if (dotdot == string) {
			grug_assert(string_len != 2 && string[2] != '/', "Remove the '..' from the resource \"%s\"", string);
		} else if (dotdot[-1] == '/') {
			grug_assert(dotdot[2] != '/' && dotdot[2] != '\0', "Remove the '..' from the resource \"%s\"", string);
		}
	}

	grug_assert(ends_with(string, resource_extension), "The resource '%s' was supposed to have the extension '%s'", string, resource_extension);
}

static bool is_wrong_type(enum type a, enum type b, const char *a_name, const char *b_name) {
	// i32 != string, so it is the wrong type.
	if (a != b) {
		return true;
	}

	// i32 is not a custom id, so we know for certain it is the right type.
	if (a != type_id) {
		return false;
	}

	// gun != car means we know for certain there is a mismatch.
	return (!streq(a_name, b_name));
}

static void check_arguments(struct argument *params, size_t param_count, struct call_expr call_expr) {
	const char *name = call_expr.fn_name;

	grug_assert(call_expr.argument_count >= param_count, "Function call '%s' expected the argument '%s' with type %s", name, params[call_expr.argument_count].name, params[call_expr.argument_count].type_name);

	grug_assert(call_expr.argument_count <= param_count, "Function call '%s' got an unexpected extra argument with type %s", name, call_expr.arguments[param_count].result_type_name);

	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		struct expr *arg = &call_expr.arguments[argument_index];
		struct argument param = params[argument_index];

		if (arg->type == STRING_EXPR && param.type == type_resource) {
			arg->result_type = type_resource;
			arg->result_type_name = "resource";
			arg->type = RESOURCE_EXPR;
			validate_resource_string(arg->literal.string, param.resource_extension);
		} else if (arg->type == STRING_EXPR && param.type == type_entity) {
			arg->result_type = type_entity;
			arg->result_type_name = "entity";
			arg->type = ENTITY_EXPR;
			validate_entity_string(arg->literal.string);
			push_entity_type(param.entity_type);
		}

		grug_assert(arg->result_type != type_void, "Function call '%s' expected the type %s for argument '%s', but got a function call that doesn't return anything", name, param.type_name, param.name);

		if (!streq(param.type_name, "id") && is_wrong_type(arg->result_type, param.type, arg->result_type_name, param.type_name)) {
			grug_error("Function call '%s' expected the type %s for argument '%s', but got %s", name, param.type_name, param.name, arg->result_type_name);
		}
	}
}

static void fill_call_expr(struct expr *expr) {
	struct call_expr call_expr = expr->call;

	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		fill_expr(&call_expr.arguments[argument_index]);
	}

	const char *name = call_expr.fn_name;

	if (starts_with(name, "helper_")) {
		*parsed_fn_calls_helper_fn_ptr = true;
	}

	struct helper_fn *helper_fn = get_helper_fn(name);
	if (helper_fn) {
		expr->result_type = helper_fn->return_type;
		expr->result_type_name = helper_fn->return_type_name;

		check_arguments(helper_fn->arguments, helper_fn->argument_count, call_expr);

		return;
	}

	struct grug_game_function *game_fn = get_grug_game_fn(name);
	if (game_fn) {
		expr->result_type = game_fn->return_type;
		expr->result_type_name = game_fn->return_type_name;

		check_arguments(game_fn->arguments, game_fn->argument_count, call_expr);

		return;
	}

	if (starts_with(name, "on_")) {
		grug_error("Mods aren't allowed to call their own on_ functions, but '%s' was called", name);
	} else {
		grug_error("The function '%s' does not exist", name);
	}
}

static void fill_binary_expr(struct expr *expr) {
	assert(expr->type == BINARY_EXPR || expr->type == LOGICAL_EXPR);
	struct binary_expr binary_expr = expr->binary;

	fill_expr(binary_expr.left_expr);
	fill_expr(binary_expr.right_expr);

	// TODO: Add tests for also not being able to use unary operators on strings
	if (binary_expr.left_expr->result_type == type_string) {
		grug_assert(binary_expr.operator == EQUALS_TOKEN || binary_expr.operator == NOT_EQUALS_TOKEN, "You can't use the %s operator on a string", get_token_type_str[binary_expr.operator]);
	}

	bool id = streq(binary_expr.left_expr->result_type_name, "id") || streq(binary_expr.right_expr->result_type_name, "id");
	if (!id && is_wrong_type(binary_expr.left_expr->result_type, binary_expr.right_expr->result_type, binary_expr.left_expr->result_type_name, binary_expr.right_expr->result_type_name)) {
		grug_error("The left and right operand of a binary expression ('%s') must have the same type, but got %s and %s", get_token_type_str[binary_expr.operator], binary_expr.left_expr->result_type_name, binary_expr.right_expr->result_type_name);
	}

	switch (binary_expr.operator) {
		case EQUALS_TOKEN:
		case NOT_EQUALS_TOKEN:
			expr->result_type = type_bool;
			expr->result_type_name = "bool";
			break;

		case GREATER_OR_EQUAL_TOKEN:
		case GREATER_TOKEN:
		case LESS_OR_EQUAL_TOKEN:
		case LESS_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_i32 || binary_expr.left_expr->result_type == type_f32, "'%s' operator expects i32 or f32", get_token_type_str[binary_expr.operator]);
			expr->result_type = type_bool;
			expr->result_type_name = "bool";
			break;

		case AND_TOKEN:
		case OR_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_bool, "'%s' operator expects bool", get_token_type_str[binary_expr.operator]);
			expr->result_type = type_bool;
			expr->result_type_name = "bool";
			break;

		case PLUS_TOKEN:
		case MINUS_TOKEN:
		case MULTIPLICATION_TOKEN:
		case DIVISION_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_i32 || binary_expr.left_expr->result_type == type_f32, "'%s' operator expects i32 or f32", get_token_type_str[binary_expr.operator]);
			expr->result_type = binary_expr.left_expr->result_type;
			expr->result_type_name = binary_expr.left_expr->result_type_name;
			break;
		case REMAINDER_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_i32, "'%%' operator expects i32");
			expr->result_type = type_i32;
			expr->result_type_name = "i32";
			break;
		case OPEN_PARENTHESIS_TOKEN:
		case CLOSE_PARENTHESIS_TOKEN:
		case OPEN_BRACE_TOKEN:
		case CLOSE_BRACE_TOKEN:
		case COMMA_TOKEN:
		case COLON_TOKEN:
		case NEWLINE_TOKEN:
		case ASSIGNMENT_TOKEN:
		case NOT_TOKEN:
		case TRUE_TOKEN:
		case FALSE_TOKEN:
		case IF_TOKEN:
		case ELSE_TOKEN:
		case WHILE_TOKEN:
		case BREAK_TOKEN:
		case RETURN_TOKEN:
		case CONTINUE_TOKEN:
		case SPACE_TOKEN:
		case INDENTATION_TOKEN:
		case STRING_TOKEN:
		case WORD_TOKEN:
		case I32_TOKEN:
		case F32_TOKEN:
		case COMMENT_TOKEN:
			grug_unreachable();
	}
}

static struct variable *get_global_variable(const char *name) {
	u32 i = buckets_global_variables[elf_hash(name) % MAX_GLOBAL_VARIABLES];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, global_variables[i].name)) {
			break;
		}

		i = chains_global_variables[i];
	}

	return global_variables + i;
}

static void add_global_variable(const char *name, enum type type, const char *type_name) {
	// TODO: Print the exact grug file path, function and line number
	grug_assert(global_variables_size < MAX_GLOBAL_VARIABLES, "There are more than %d global variables in a grug file, exceeding MAX_GLOBAL_VARIABLES", MAX_GLOBAL_VARIABLES);

	grug_assert(!get_global_variable(name), "The global variable '%s' shadows an earlier global variable with the same name, so change the name of one of them", name);

	global_variables[global_variables_size] = (struct variable){
		.name = name,
		.type = type,
		.type_name = type_name,
		.offset = globals_bytes,
	};

	globals_bytes += type_sizes[type];

	u32 bucket_index = elf_hash(name) % MAX_GLOBAL_VARIABLES;

	chains_global_variables[global_variables_size] = buckets_global_variables[bucket_index];

	buckets_global_variables[bucket_index] = global_variables_size++;
}

static struct variable *get_local_variable(const char *name) {
	if (variables_size == 0) {
		return NULL;
	}

	u32 i = buckets_variables[elf_hash(name) % MAX_VARIABLES_PER_FUNCTION];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		// When a scope block is exited, the local variables in it aren't reachable anymore.
		// These unreachable local variables are marked with an offset of SIZE_MAX.
		// It is possible for a new local variable with the same name to be added after the block,
		// which is why we still keep looping in that case.
		if (streq(name, variables[i].name) && variables[i].offset != SIZE_MAX) {
			break;
		}

		i = chains_variables[i];
	}

	return variables + i;
}

static struct variable *get_variable(const char *name) {
	struct variable *var = get_local_variable(name);
	if (!var) {
		var = get_global_variable(name);
	}
	return var;
}

static void fill_expr(struct expr *expr) {
	switch (expr->type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
			expr->result_type = type_bool;
			expr->result_type_name = "bool";
			break;
		case STRING_EXPR:
			expr->result_type = type_string;
			expr->result_type_name = "string";
			break;
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
			grug_unreachable();
		case IDENTIFIER_EXPR: {
			struct variable *var = get_variable(expr->literal.string);
			grug_assert(var, "The variable '%s' does not exist", expr->literal.string);
			expr->result_type = var->type;
			expr->result_type_name = var->type_name;
			break;
		}
		case I32_EXPR:
			expr->result_type = type_i32;
			expr->result_type_name = "i32";
			break;
		case F32_EXPR:
			expr->result_type = type_f32;
			expr->result_type_name = "f32";
			break;
		case UNARY_EXPR:
			if (expr->unary.expr->type == UNARY_EXPR) {
				grug_assert(expr->unary.operator != expr->unary.expr->unary.operator, "Found '%s' directly next to another '%s', which can be simplified by just removing both of them", get_token_type_str[expr->unary.operator], get_token_type_str[expr->unary.expr->unary.operator]);
			}

			fill_expr(expr->unary.expr);
			expr->result_type = expr->unary.expr->result_type;
			expr->result_type_name = expr->unary.expr->result_type_name;

			if (expr->unary.operator == NOT_TOKEN) {
				grug_assert(expr->result_type == type_bool, "Found 'not' before %s, but it can only be put before a bool", expr->result_type_name);
			} else if (expr->unary.operator == MINUS_TOKEN) {
				grug_assert(expr->result_type == type_i32 || expr->result_type == type_f32, "Found '-' before %s, but it can only be put before an i32 or f32", expr->result_type_name);
			} else {
				grug_unreachable();
			}

			break;
		case BINARY_EXPR:
		case LOGICAL_EXPR:
			fill_binary_expr(expr);
			break;
		case CALL_EXPR:
			fill_call_expr(expr);
			break;
		case PARENTHESIZED_EXPR:
			fill_expr(expr->parenthesized);
			expr->result_type = expr->parenthesized->result_type;
			expr->result_type_name = expr->parenthesized->result_type_name;
			break;
	}
}

static void add_local_variable(const char *name, enum type type, const char *type_name) {
	// TODO: Print the exact grug file path, function and line number
	grug_assert(variables_size < MAX_VARIABLES_PER_FUNCTION, "There are more than %d variables in a function, exceeding MAX_VARIABLES_PER_FUNCTION", MAX_VARIABLES_PER_FUNCTION);

	grug_assert(!get_local_variable(name), "The local variable '%s' shadows an earlier local variable with the same name, so change the name of one of them", name);
	grug_assert(!get_global_variable(name), "The local variable '%s' shadows an earlier global variable with the same name, so change the name of one of them", name);

	stack_frame_bytes += type_sizes[type];

	variables[variables_size] = (struct variable){
		.name = name,
		.type = type,
		.type_name = type_name,

		// This field is used by the "COMPILING" section to track the stack location of a local variable.
		// The "TYPE PROPAGATION" section only checks whether it is SIZE_MAX,
		// since that indicates that the variable is unreachable, due to having exited the scope block.
		.offset = stack_frame_bytes,
	};

	u32 bucket_index = elf_hash(name) % MAX_VARIABLES_PER_FUNCTION;

	chains_variables[variables_size] = buckets_variables[bucket_index];

	buckets_variables[bucket_index] = variables_size++;
}

static void fill_variable_statement(struct variable_statement variable_statement) {
	// This has to happen before the add_local_variable() we do below,
	// because `a: i32 = a` should throw
	fill_expr(variable_statement.assignment_expr);

	struct variable *var = get_variable(variable_statement.name);

	if (variable_statement.has_type) {
		grug_assert(!var, "The variable '%s' already exists", variable_statement.name);

		if (!streq(variable_statement.type_name, "id") && is_wrong_type(variable_statement.type, variable_statement.assignment_expr->result_type, variable_statement.type_name, variable_statement.assignment_expr->result_type_name)) {
			grug_error("Can't assign %s to '%s', which has type %s", variable_statement.assignment_expr->result_type_name, variable_statement.name, variable_statement.type_name);
		}

		add_local_variable(variable_statement.name, variable_statement.type, variable_statement.type_name);
	} else {
		grug_assert(var, "Can't assign to the variable '%s', since it does not exist", variable_statement.name);

		if (!streq(var->type_name, "id") && is_wrong_type(var->type, variable_statement.assignment_expr->result_type, var->type_name, variable_statement.assignment_expr->result_type_name)) {
			grug_error("Can't assign %s to '%s', which has type %s", variable_statement.assignment_expr->result_type_name, var->name, var->type_name);
		}
	}
}

static void mark_local_variables_unreachable(struct statement *body_statements, size_t statement_count) {
	// Mark all local variables in this exited scope block as being unreachable.
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		if (statement.type == VARIABLE_STATEMENT && statement.variable_statement.has_type) {
			struct variable *var = get_local_variable(statement.variable_statement.name);
			assert(var);

			var->offset = SIZE_MAX;

			// Even though we have already calculated the final stack frame size in advance
			// before we started compiling the function's body, we are still calling add_local_variable()
			// during the compilation of the function body. And that fn uses stack_frame_bytes.
			assert(stack_frame_bytes >= type_sizes[var->type]);
			stack_frame_bytes -= type_sizes[var->type];
		}
	}
}

static void fill_statements(struct statement *body_statements, size_t statement_count) {
	for (size_t i = 0; i < statement_count; i++) {
		struct statement statement = body_statements[i];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				fill_variable_statement(statement.variable_statement);
				break;
			case CALL_STATEMENT:
				fill_call_expr(statement.call_statement.expr);
				break;
			case IF_STATEMENT:
				fill_expr(&statement.if_statement.condition);

				fill_statements(statement.if_statement.if_body_statements, statement.if_statement.if_body_statement_count);

				if (statement.if_statement.else_body_statement_count > 0) {
					fill_statements(statement.if_statement.else_body_statements, statement.if_statement.else_body_statement_count);
				}

				break;
			case RETURN_STATEMENT:
				if (statement.return_statement.has_value) {
					// Entered for statement `return 42`
					fill_expr(statement.return_statement.value);

					grug_assert(fn_return_type != type_void, "Function '%s' wasn't supposed to return any value", filled_fn_name);

					if (!streq(fn_return_type_name, "id") && is_wrong_type(statement.return_statement.value->result_type, fn_return_type, statement.return_statement.value->result_type_name, fn_return_type_name)) {
						grug_error("Function '%s' is supposed to return %s, not %s", filled_fn_name, fn_return_type_name, statement.return_statement.value->result_type_name);
					}
				} else {
					// Entered for statement `return`
					grug_assert(fn_return_type == type_void, "Function '%s' is supposed to return a value of type %s", filled_fn_name, fn_return_type_name);
				}
				break;
			case WHILE_STATEMENT:
				fill_expr(&statement.while_statement.condition);

				fill_statements(statement.while_statement.body_statements, statement.while_statement.body_statement_count);

				*parsed_fn_contains_while_loop_ptr = true;

				break;
			case BREAK_STATEMENT:
			case CONTINUE_STATEMENT:
			case EMPTY_LINE_STATEMENT:
			case COMMENT_STATEMENT:
				break;
		}
	}

	mark_local_variables_unreachable(body_statements, statement_count);
}

static void add_argument_variables(struct argument *fn_arguments, size_t argument_count) {
	variables_size = 0;
	memset(buckets_variables, 0xff, sizeof(buckets_variables));

	stack_frame_bytes = GLOBAL_VARIABLES_POINTER_SIZE;
	max_stack_frame_bytes = stack_frame_bytes;

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		struct argument arg = fn_arguments[argument_index];
		add_local_variable(arg.name, arg.type, arg.type_name);

		max_stack_frame_bytes += type_sizes[arg.type];
	}
}

static void fill_helper_fns(void) {
	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
		struct helper_fn fn = helper_fns[fn_index];

		fn_return_type = fn.return_type;
		fn_return_type_name = fn.return_type_name;

		filled_fn_name = fn.fn_name;

		add_argument_variables(fn.arguments, fn.argument_count);

		fill_statements(fn.body_statements, fn.body_statement_count);

		// Unlike fill_statements() its RETURN_STATEMENT case,
		// this checks whether a return statement *is missing* at the end of the function
		if (fn.return_type != type_void) {
			grug_assert(fn.body_statement_count > 0, "Function '%s' is supposed to return %s as its last line", filled_fn_name, fn_return_type_name);

			struct statement last_statement = fn.body_statements[fn.body_statement_count - 1];

			grug_assert(last_statement.type == RETURN_STATEMENT, "Function '%s' is supposed to return %s as its last line", filled_fn_name, fn_return_type_name);
		}
	}
}

static struct grug_on_function *get_entity_on_fn(const char *name) {
	if (grug_entity->on_function_count == 0) {
		return NULL;
	}

	u32 i = buckets_entity_on_fns[elf_hash(name) % grug_entity->on_function_count];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, grug_entity->on_functions[i].name)) {
			break;
		}

		i = chains_entity_on_fns[i];
	}

	return grug_entity->on_functions + i;
}

static void hash_entity_on_fns(void) {
	memset(buckets_entity_on_fns, 0xff, grug_entity->on_function_count * sizeof(u32));

	for (size_t i = 0; i < grug_entity->on_function_count; i++) {
		const char *name = grug_entity->on_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_entity->on_function_count;

		chains_entity_on_fns[i] = buckets_entity_on_fns[bucket_index];

		buckets_entity_on_fns[bucket_index] = i;
	}
}

static void fill_on_fns(void) {
	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
		struct on_fn *fn = &on_fns[fn_index];

		fn_return_type = type_void;

		const char *name = fn->fn_name;
		filled_fn_name = name;

		struct grug_on_function *entity_on_fn = get_entity_on_fn(on_fns[fn_index].fn_name);

		grug_assert(entity_on_fn, "The function '%s' was not was not declared by entity '%s' in mod_api.json", on_fns[fn_index].fn_name, file_entity_type);

		struct argument *args = fn->arguments;
		size_t arg_count = fn->argument_count;

		struct argument *params = entity_on_fn->arguments;
		size_t param_count = entity_on_fn->argument_count;

		grug_assert(arg_count >= param_count, "Function '%s' expected the parameter '%s' with type %s", name, params[arg_count].name, params[arg_count].type_name);

		grug_assert(arg_count <= param_count, "Function '%s' got an unexpected extra parameter '%s' with type %s", name, args[param_count].name, args[param_count].type_name);

		for (size_t argument_index = 0; argument_index < arg_count; argument_index++) {
			struct argument param = params[argument_index];

			const char *arg_name = args[argument_index].name;
			grug_assert(streq(arg_name, param.name), "Function '%s' its '%s' parameter was supposed to be named '%s'", name, arg_name, param.name);

			enum type arg_type = args[argument_index].type;
			const char *arg_type_name = args[argument_index].type_name;

			if (is_wrong_type(arg_type, param.type, arg_type_name, param.type_name)) {
				grug_error("Function '%s' its '%s' parameter was supposed to have the type %s, but got %s", name, param.name, param.type_name, arg_type_name);
			}
		}

		add_argument_variables(args, arg_count);

		parsed_fn_calls_helper_fn_ptr = &fn->calls_helper_fn;
		parsed_fn_contains_while_loop_ptr = &fn->contains_while_loop;

		fill_statements(fn->body_statements, fn->body_statement_count);
	}
}

// Check that the global variable's assigned value doesn't contain a call nor identifier
static void check_global_expr(struct expr *expr, const char *name) {
	switch (expr->type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
		case STRING_EXPR:
		case I32_EXPR:
		case F32_EXPR:
		case IDENTIFIER_EXPR:
			break;
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
			grug_unreachable();
		case UNARY_EXPR:
			check_global_expr(expr->unary.expr, name);
			break;
		case BINARY_EXPR:
		case LOGICAL_EXPR:
			check_global_expr(expr->binary.left_expr, name);
			check_global_expr(expr->binary.right_expr, name);
			break;
		case CALL_EXPR:
			// See tests/err/global_cant_call_helper_fn, tests/err/global_cant_call_on_fn, and tests/ok/global_id
			grug_assert(!starts_with(expr->call.fn_name, "helper_"), "The global variable '%s' isn't allowed to call helper functions", name);
			for (size_t i = 0; i < expr->call.argument_count; i++) {
				check_global_expr(&expr->call.arguments[i], name);
			}
			break;
		case PARENTHESIZED_EXPR:
			check_global_expr(expr->parenthesized, name);
			break;
	}
}

static void fill_global_variables(void) {
	add_global_variable("me", type_id, file_entity_type);

	for (size_t i = 0; i < global_variable_statements_size; i++) {
		struct global_variable_statement *global = &global_variable_statements[i];

		check_global_expr(&global->assignment_expr, global->name);

		fill_expr(&global->assignment_expr);

		// This won't be entered by a global `foo: id = get_opponent()`
		// See tests/err/global_id_cant_be_reassigned
		if (global->assignment_expr.type == IDENTIFIER_EXPR) {
			// See tests/err/global_cant_be_me
			grug_assert(!streq(global->assignment_expr.literal.string, "me"), "Global variables can't be assigned 'me'");
		}

		if (!streq(global->type_name, "id") && is_wrong_type(global->type, global->assignment_expr.result_type, global->type_name, global->assignment_expr.result_type_name)) {
			grug_error("Can't assign %s to '%s', which has type %s", global->assignment_expr.result_type_name, global->name, global->type_name);
		}

		add_global_variable(global->name, global->type, global->type_name);
	}
}

// TODO: This could be turned O(1) with a hash map
static struct grug_entity *get_grug_entity(const char *entity_type) {
	for (size_t i = 0; i < grug_entities_size; i++) {
		if (streq(entity_type, grug_entities[i].name)) {
			return grug_entities + i;
		}
	}
	return NULL;
}

static void fill_result_types(void) {
	reset_filling();

	grug_entity = get_grug_entity(file_entity_type);

	grug_assert(grug_entity, "The entity '%s' was not declared by mod_api.json", file_entity_type);

	hash_entity_on_fns();

	fill_global_variables();
	fill_on_fns();
	fill_helper_fns();
}

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
	// If there are no other global variables, take a shortcut
	if (global_variables_size == 1) {
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

	for (size_t i = 0; i < global_variable_statements_size; i++) {
		struct global_variable_statement global = global_variable_statements[i];

		compile_expr(global.assignment_expr);

		compile_global_variable_statement(global.name);
	}
	assert(pushed == 0);

	compile_function_epilogue();

	overwrite_jmp_address_32(skip_safe_code_offset, codes_size);

	compiling_fast_mode = true;
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

//// HOT RELOADING

#define MAX_ENTITIES 420420
#define MAX_ENTITY_STRINGS_CHARACTERS 420420
#define MAX_ENTITY_NAME_LENGTH 420
#define MAX_DIRECTORY_DEPTH 42

USED_BY_PROGRAMS struct grug_mod_dir grug_mods;

USED_BY_PROGRAMS struct grug_modified grug_reloads[MAX_RELOADS];
USED_BY_PROGRAMS size_t grug_reloads_size;

static const char *entities[MAX_ENTITIES];
static char entity_strings[MAX_ENTITY_STRINGS_CHARACTERS];
static size_t entity_strings_size;
static u32 buckets_entities[MAX_ENTITIES];
static u32 chains_entities[MAX_ENTITIES];
static struct grug_file entity_files[MAX_ENTITIES];
static size_t entities_size;

USED_BY_PROGRAMS struct grug_modified_resource grug_resource_reloads[MAX_RESOURCE_RELOADS];
USED_BY_PROGRAMS size_t grug_resource_reloads_size;

USED_BY_MODS const char *grug_fn_name;
USED_BY_MODS const char *grug_fn_path;

static bool is_grug_initialized = false;

static size_t directory_depth;

static void reset_regenerate_modified_mods(void) {
	grug_reloads_size = 0;
	entity_strings_size = 0;
	memset(buckets_entities, 0xff, sizeof(buckets_entities));
	entities_size = 0;
	grug_resource_reloads_size = 0;
	grug_fn_name = "OPTIMIZED OUT FUNCTION NAME";
	grug_fn_path = "OPTIMIZED OUT FUNCTION PATH";
	directory_depth = 0;
}

static void reload_resources_from_dll(const char *dll_path, i64 *resource_mtimes, size_t dll_resources_size) {
	void *dll = dlopen(dll_path, RTLD_NOW);
	if (!dll) {
		print_dlerror("dlopen");

		// Needed for clang's --analyze, since it doesn't recognize
		// that print_dlerror() its longjmp guarantees that `dll`
		// will always be non-null when this if-statement has *not* been entered
		return;
	}

	const char **dll_resources = get_dll_symbol(dll, "resources");
	if (!dll_resources) {
		if (dlclose(dll)) {
			print_dlerror("dlclose");
		}
		grug_error("Retrieving resources with get_dll_symbol() failed for %s", dll_path);
	}

	for (size_t i = 0; i < dll_resources_size; i++) {
		const char *resource = dll_resources[i];

		struct stat resource_stat;
		if (stat(resource, &resource_stat) == -1) {
			if (dlclose(dll)) {
				print_dlerror("dlclose");
			}
			grug_error("%s: %s", resource, strerror(errno));
		}

		if (resource_stat.st_mtime > resource_mtimes[i]) {
			resource_mtimes[i] = resource_stat.st_mtime;

			struct grug_modified_resource modified = {0};

			grug_assert(strlen(resource) + 1 <= sizeof(modified.path), "The resource '%s' exceeds the maximum path length of %zu", resource, sizeof(modified.path));
			memcpy(modified.path, resource, strlen(resource) + 1);

			if (grug_resource_reloads_size >= MAX_RESOURCE_RELOADS) {
				if (dlclose(dll)) {
					print_dlerror("dlclose");
				}
				grug_error("There are more than %d modified resources, exceeding MAX_RESOURCE_RELOADS", MAX_RESOURCE_RELOADS);
			}

			grug_resource_reloads[grug_resource_reloads_size++] = modified;
		}
	}

	if (dlclose(dll)) {
		print_dlerror("dlclose");
	}
}

static void regenerate_dll(const char *grug_path, const char *dll_path) {
	grug_log("# Regenerating %s\n", dll_path);

	grug_loading_error_in_grug_file = true;

	read_file(grug_path);
	grug_log("\n# Read text\n%s", grug_text);

	tokenize();
	grug_log("\n# Tokens\n");
#ifdef LOGGING
	print_tokens();
#else
	(void)print_tokens;
#endif

	parse();
	fill_result_types();

	compile(grug_path);

	grug_log("\n# Section offsets\n");
	generate_shared_object(dll_path);

	grug_loading_error_in_grug_file = false;
}

// Resetting previous_grug_error is necessary for this edge case:
// 1. Add a typo to a mod, causing a compilation error
// 2. Remove the typo, causing it to compile again
// 3. Add the exact same typo to the same line; we want this to show the earlier error again
static void reset_previous_grug_error(void) {
	previous_grug_error.msg[0] = '\0';
	previous_grug_error.path[0] = '\0';
	previous_grug_error.grug_c_line_number = 0;
}

static void initialize_file_entity_type(const char *grug_filename) {
	const char *dash = strchr(grug_filename, '-');

	grug_assert(dash && dash[1] != '\0', "'%s' is missing an entity type in its name; use a dash to specify it, like 'ak47-gun.grug'", grug_filename);

	const char *period = strchr(dash + 1, '.');
	grug_assert(period, "'%s' is missing a period in its filename", grug_filename);

	// "foo-.grug" has an entity_type_len of 0
	size_t entity_type_len = period - dash - 1;
	grug_assert(entity_type_len > 0, "'%s' is missing an entity type in its name; use a dash to specify it, like 'ak47-gun.grug'", grug_filename);

	grug_assert(entity_type_len < MAX_FILE_ENTITY_TYPE_LENGTH, "There are more than %d characters in the entity type of '%s', exceeding MAX_FILE_ENTITY_TYPE_LENGTH", MAX_FILE_ENTITY_TYPE_LENGTH, grug_filename);
	memcpy(file_entity_type, dash + 1, entity_type_len);
	file_entity_type[entity_type_len] = '\0';

	check_custom_id_is_pascal(file_entity_type);
}

static void set_grug_error_path(const char *grug_path) {
	// Since grug_error.path is the maximum path length of operating systems,
	// it shouldn't be possible for grug_path to exceed it
	assert(strlen(grug_path) + 1 <= sizeof(grug_error.path));

	memcpy(grug_error.path, grug_path, strlen(grug_path) + 1);
}

// This function just exists for the grug-tests repository
// It returns whether an error occurred
USED_BY_PROGRAMS bool grug_test_regenerate_dll(const char *grug_path, const char *dll_path, const char *mod_name);
bool grug_test_regenerate_dll(const char *grug_path, const char *dll_path, const char *mod_name) {
	assert(is_grug_initialized && "You forgot to call grug_init() once at program startup!");

	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	mod = mod_name;

	grug_loading_error_in_grug_file = false;

	set_grug_error_path(grug_path);

	const char *grug_filename = strrchr(grug_path, '/');
	grug_assert(grug_filename, "The grug file path '%s' does not contain a '/' character", grug_path);
	initialize_file_entity_type(grug_filename + 1);

	regenerate_dll(grug_path, dll_path);

	reset_previous_grug_error();

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
			grug_assert(mkdir(parent_dir_path, 0775) != -1 || errno == EEXIST, "mkdir: %s", strerror(errno));
		}

		file_path++;
		i++;
	}
}

static void free_file(struct grug_file file) {
	free((void *)file.name);
	free((void *)file.entity);
	free((void *)file.entity_type);

	if (file.dll && dlclose(file.dll)) {
		print_dlerror("dlclose");
	}

	free(file._resource_mtimes);
}

static void free_dir(struct grug_mod_dir dir) {
	free((void *)dir.name);

	for (size_t i = 0; i < dir.dirs_size; i++) {
		free_dir(dir.dirs[i]);
	}
	free(dir.dirs);

	for (size_t i = 0; i < dir.files_size; i++) {
		free_file(dir.files[i]);
	}
	free(dir.files);
}

static u32 get_entity_index(const char *entity) {
	if (entities_size == 0) {
		return UINT32_MAX;
	}

	u32 i = buckets_entities[elf_hash(entity) % MAX_ENTITIES];

	while (true) {
		if (i == UINT32_MAX) {
			return UINT32_MAX;
		}

		if (streq(entity, entities[i])) {
			break;
		}

		i = chains_entities[i];
	}

	return i;
}

struct grug_file *grug_get_entity_file(const char *entity) {
	u32 index = get_entity_index(entity);
	if (index == UINT32_MAX) {
		return NULL;
	}
	return &entity_files[index];
}

static void check_that_every_entity_exists(struct grug_mod_dir dir) {
	for (size_t i = 0; i < dir.files_size; i++) {
		struct grug_file file = dir.files[i];

		size_t *entities_size_ptr = get_dll_symbol(file.dll, "entities_size");
		grug_assert(entities_size_ptr, "Retrieving the entities_size variable with get_dll_symbol() failed for '%s'", file.name);

		if (*entities_size_ptr > 0) {
			const char **dll_entities = get_dll_symbol(file.dll, "entities");
			grug_assert(entities_size_ptr, "Retrieving the dll_entities variable with get_dll_symbol() failed for '%s'", file.name);

			const char **dll_entity_types = get_dll_symbol(file.dll, "entity_types");
			grug_assert(entities_size_ptr, "Retrieving the dll_entity_types variable with get_dll_symbol() failed for '%s'", file.name);

			for (size_t dll_entity_index = 0; dll_entity_index < *entities_size_ptr; dll_entity_index++) {
				const char *entity = dll_entities[dll_entity_index];

				u32 entity_index = get_entity_index(entity);

				grug_assert(entity_index != UINT32_MAX, "The entity '%s' does not exist", entity);

				const char *json_entity_type = dll_entity_types[dll_entity_index];

				struct grug_file other_file = entity_files[entity_index];

				grug_assert(*json_entity_type == '\0' || streq(other_file.entity_type, json_entity_type), "The entity '%s' has the type '%s', whereas the expected type from mod_api.json is '%s'", entity, other_file.entity_type, json_entity_type);
			}
		}
	}

	for (size_t i = 0; i < dir.dirs_size; i++) {
		check_that_every_entity_exists(dir.dirs[i]);
	}
}

static void push_reload(struct grug_modified modified) {
	grug_assert(grug_reloads_size < MAX_RELOADS, "There are more than %d modified grug files, exceeding MAX_RELOADS", MAX_RELOADS);
	grug_reloads[grug_reloads_size++] = modified;
}

// Returns `mod + ':' + grug_filename - "-<entity type>.grug"`
static const char *form_entity(const char *grug_filename) {
	static char entity_name[MAX_ENTITY_NAME_LENGTH];

	const char *dash = strrchr(grug_filename, '-');
	if (dash == NULL) {
		// The function initialize_file_entity_type() already checked for a missing dash
		grug_unreachable();
	}

	size_t entity_name_length = dash - grug_filename;

	grug_assert(entity_name_length < MAX_ENTITY_NAME_LENGTH, "There are more than %d entity name characters in the grug filename '%s', exceeding MAX_ENTITY_NAME_LENGTH", MAX_ENTITY_NAME_LENGTH, grug_filename);
	memcpy(entity_name, grug_filename, entity_name_length);
	entity_name[entity_name_length] = '\0';

	static char entity[MAX_ENTITY_NAME_LENGTH];
	grug_assert(snprintf(entity, sizeof(entity), "%s:%s", mod, entity_name) >= 0, "Filling the variable 'entity' failed");

	size_t entity_length = strlen(entity);

	grug_assert(entity_strings_size + entity_length < MAX_ENTITY_STRINGS_CHARACTERS, "There are more than %d characters in the entity_strings array, exceeding MAX_ENTITY_STRINGS_CHARACTERS", MAX_ENTITY_STRINGS_CHARACTERS);

	char *entity_str = entity_strings + entity_strings_size;

	memcpy(entity_str, entity, entity_length);
	entity_strings_size += entity_length;
	entity_strings[entity_strings_size++] = '\0';

	return entity_str;
}

static void add_entity(const char *grug_filename, struct grug_file *file) {
	grug_assert(entities_size < MAX_ENTITIES, "There are more than %d entities, exceeding MAX_ENTITIES", MAX_ENTITIES);

	const char *entity = form_entity(grug_filename);

	grug_assert(get_entity_index(entity) == UINT32_MAX, "The entity '%s' already exists, because there are two grug files called '%s' in the mod '%s'", entity, grug_filename, mod);

	u32 bucket_index = elf_hash(entity) % MAX_ENTITIES;

	chains_entities[entities_size] = buckets_entities[bucket_index];

	buckets_entities[bucket_index] = entities_size;

	// entity_files[] needs to take ownership of `file`,
	// since reload_modified_mod() can swap-remove the file
	entity_files[entities_size] = *file;

	entities[entities_size++] = entity;
}

static struct grug_file *push_file(struct grug_mod_dir *dir, struct grug_file file) {
	if (dir->files_size >= dir->_files_capacity) {
		dir->_files_capacity = dir->_files_capacity == 0 ? 1 : dir->_files_capacity * 2;
		dir->files = realloc(dir->files, dir->_files_capacity * sizeof(*dir->files));
		grug_assert(dir->files, "realloc: %s", strerror(errno));
	}
	dir->files[dir->files_size] = file;
	return &dir->files[dir->files_size++];
}

static struct grug_mod_dir *push_subdir(struct grug_mod_dir *dir, struct grug_mod_dir subdir) {
	if (dir->dirs_size >= dir->_dirs_capacity) {
		dir->_dirs_capacity = dir->_dirs_capacity == 0 ? 1 : dir->_dirs_capacity * 2;
		dir->dirs = realloc(dir->dirs, dir->_dirs_capacity * sizeof(*dir->dirs));
		grug_assert(dir->dirs, "realloc: %s", strerror(errno));
	}
	dir->dirs[dir->dirs_size] = subdir;
	return &dir->dirs[dir->dirs_size++];
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static struct grug_file *get_file(struct grug_mod_dir *dir, const char *name) {
	for (size_t i = 0; i < dir->files_size; i++) {
		if (streq(dir->files[i].name, name)) {
			return dir->files + i;
		}
	}
	return NULL;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static struct grug_mod_dir *get_subdir(struct grug_mod_dir *dir, const char *name) {
	for (size_t i = 0; i < dir->dirs_size; i++) {
		if (streq(dir->dirs[i].name, name)) {
			return dir->dirs + i;
		}
	}
	return NULL;
}

static struct grug_file *regenerate_file(struct grug_file *file, const char *dll_path, const char *grug_filename, struct grug_mod_dir *dir) {
	struct grug_file new_file = {0};

	new_file.dll = dlopen(dll_path, RTLD_NOW);
	if (!new_file.dll) {
		print_dlerror("dlopen");
	}

	size_t *globals_size_ptr = get_dll_symbol(new_file.dll, "globals_size");
	grug_assert(globals_size_ptr, "Retrieving the globals_size variable with get_dll_symbol() failed for %s", dll_path);
	new_file.globals_size = *globals_size_ptr;

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"
	new_file.init_globals_fn = get_dll_symbol(new_file.dll, "init_globals");
	#pragma GCC diagnostic pop
	grug_assert(new_file.init_globals_fn, "Retrieving the init_globals() function with get_dll_symbol() failed for %s", dll_path);

	// on_fns is optional, so don't check for NULL
	// Note that if an entity in mod_api.json specifies that it has on_fns that the modder can use,
	// on_fns is guaranteed NOT to be NULL!
	new_file.on_fns = get_dll_symbol(new_file.dll, "on_fns");

	size_t *resources_size_ptr = get_dll_symbol(new_file.dll, "resources_size");
	size_t dll_resources_size = *resources_size_ptr;

	if (file) {
		file->dll = new_file.dll;
		file->globals_size = new_file.globals_size;
		file->init_globals_fn = new_file.init_globals_fn;
		file->on_fns = new_file.on_fns;

		if (dll_resources_size > 0) {
			file->_resource_mtimes = realloc(file->_resource_mtimes, dll_resources_size * sizeof(i64));
			grug_assert(file->_resource_mtimes, "realloc: %s", strerror(errno));
		} else {
			// We can't use realloc() to do this
			// See https://stackoverflow.com/a/16760080/13279557
			free(file->_resource_mtimes);
			file->_resource_mtimes = NULL;
		}
	} else {
		new_file.name = strdup(grug_filename);
		grug_assert(new_file.name, "strdup: %s", strerror(errno));

		new_file.entity = strdup(form_entity(grug_filename));
		grug_assert(new_file.entity, "strdup: %s", strerror(errno));

		new_file.entity_type = strdup(file_entity_type);
		grug_assert(new_file.entity_type, "strdup: %s", strerror(errno));

		// We check dll_resources_size > 0, since whether malloc(0) returns NULL is implementation defined
		// See https://stackoverflow.com/a/1073175/13279557
		if (dll_resources_size > 0) {
			new_file._resource_mtimes = malloc(dll_resources_size * sizeof(i64));
			grug_assert(new_file._resource_mtimes, "malloc: %s", strerror(errno));
		}

		file = push_file(dir, new_file);
	}

	if (dll_resources_size > 0) {
		const char **dll_resources = get_dll_symbol(file->dll, "resources");

		// Initialize file->_resource_mtimes
		for (size_t i = 0; i < dll_resources_size; i++) {
			struct stat resource_stat;
			grug_assert(stat(dll_resources[i], &resource_stat) == 0, "%s: %s", dll_resources[i], strerror(errno));

			file->_resource_mtimes[i] = resource_stat.st_mtime;
		}
	}

	return file;
}

static void reload_grug_file(const char *dll_entry_path, i64 grug_file_mtime, const char *grug_filename, struct grug_mod_dir *dir, const char *grug_path) {
	initialize_file_entity_type(grug_filename);

	// Fill dll_path
	char dll_path[STUPID_MAX_PATH];
	grug_assert(strlen(dll_entry_path) + 1 <= STUPID_MAX_PATH, "There are more than %d characters in the dll_entry_path '%s', exceeding STUPID_MAX_PATH", STUPID_MAX_PATH, dll_entry_path);
	memcpy(dll_path, dll_entry_path, strlen(dll_entry_path) + 1);

	// Cast is safe because it indexes into stack-allocated memory
	char *extension = (char *)get_file_extension(dll_path);

	// The code that called this reload_grug_file() function has already checked
	// that the file ends with ".grug", so '.' will always be found here
	assert(extension[0] == '.');

	// We know that there's enough space, since ".so" is shorter than ".grug"
	memcpy(extension + 1, "so", sizeof("so"));

	struct stat dll_stat;
	bool dll_exists = stat(dll_path, &dll_stat) == 0;

	if (!dll_exists) {
		// If the dll doesn't exist, try to create the parent directories
		errno = 0;
		if (access(dll_path, F_OK) && errno == ENOENT) {
			try_create_parent_dirs(dll_path);
			errno = 0;
		}
		grug_assert(errno == 0 || errno == ENOENT, "access: %s", strerror(errno));
	}

	// If the dll doesn't exist or is outdated
	bool needs_regeneration = !dll_exists || grug_file_mtime > dll_stat.st_mtime;

	struct grug_file *file = get_file(dir, grug_filename);

	if (needs_regeneration || !file) {
		struct grug_modified modified = {0};

		set_grug_error_path(grug_path);

		if (needs_regeneration) {
			regenerate_dll(grug_path, dll_path);
		}

		if (file && file->dll) {
			modified.old_dll = file->dll;

			// This dlclose() needs to happen after the regenerate_dll() call,
			// since even if regenerate_dll() throws when a typo is introduced to a mod,
			// we want to keep the pre-typo DLL version open so the game doesn't crash
			//
			// This dlclose() needs to happen before the upcoming dlopen() call,
			// since the DLL won't be reloaded otherwise
			if (dlclose(file->dll)) {
				print_dlerror("dlclose");
			}

			// Not necessary, but makes debugging less confusing
			file->dll = NULL;
		}

		file = regenerate_file(file, dll_path, grug_filename, dir);

		// Let the game developer know that a grug file was recompiled
		if (needs_regeneration) {
			// Since modified.path is the maximum path length of operating systems,
			// it shouldn't be possible for grug_path to exceed it
			assert(strlen(grug_path) + 1 <= sizeof(modified.path));

			memcpy(modified.path, grug_path, strlen(grug_path) + 1);

			modified.file = *file;
			push_reload(modified);
		}
	}

	file->_seen = true;

	// Needed for grug_get_entitity_file() and check_that_every_entity_exists()
	add_entity(grug_filename, file);

	// Let the game developer know when they need to reload a resource
	if (file->_resources_size > 0) {
		reload_resources_from_dll(dll_path, file->_resource_mtimes, file->_resources_size);
	}
}

static void reload_modified_mod(const char *mods_dir_path, const char *dll_dir_path, struct grug_mod_dir *dir);

static void reload_entry(const char *name, const char *mods_dir_path, const char *dll_dir_path, struct grug_mod_dir *dir) {
	if (streq(name, ".") || streq(name, "..")) {
		return;
	}

	char entry_path[STUPID_MAX_PATH];
	snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_dir_path, name);

	char dll_entry_path[STUPID_MAX_PATH];
	snprintf(dll_entry_path, sizeof(dll_entry_path), "%s/%s", dll_dir_path, name);

	struct stat entry_stat;
	grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

	if (S_ISDIR(entry_stat.st_mode)) {
		struct grug_mod_dir *subdir = get_subdir(dir, name);

		if (!subdir) {
			struct grug_mod_dir inserted_subdir = {.name = strdup(name)};
			grug_assert(inserted_subdir.name, "strdup: %s", strerror(errno));
			subdir = push_subdir(dir, inserted_subdir);
		}

		subdir->_seen = true;

		reload_modified_mod(entry_path, dll_entry_path, subdir);
	} else if (S_ISREG(entry_stat.st_mode) && streq(get_file_extension(name), ".grug")) {
		reload_grug_file(dll_entry_path, entry_stat.st_mtime, name, dir, entry_path);
	}
}

static void reload_modified_mod(const char *mods_dir_path, const char *dll_dir_path, struct grug_mod_dir *dir) {
	directory_depth++;
	grug_assert(directory_depth < MAX_DIRECTORY_DEPTH, "There is a mod that contains more than %d levels of nested directories", MAX_DIRECTORY_DEPTH);

	DIR *dirp = opendir(mods_dir_path);
	grug_assert(dirp, "opendir(\"%s\"): %s", mods_dir_path, strerror(errno));

	for (size_t i = 0; i < dir->dirs_size; i++) {
		dir->dirs[i]._seen = false;
	}
	for (size_t i = 0; i < dir->files_size; i++) {
		dir->files[i]._seen = false;
	}

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		reload_entry(dp->d_name, mods_dir_path, dll_dir_path, dir);
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);

	// If the directory used to contain a subdirectory or file
	// that doesn't exist anymore, free it
	for (size_t i = dir->dirs_size; i > 0;) {
		i--;
		if (!dir->dirs[i]._seen) {
			free_dir(dir->dirs[i]);
			dir->dirs[i] = dir->dirs[--dir->dirs_size]; // Swap-remove
		}
	}
	for (size_t i = dir->files_size; i > 0;) {
		i--;
		if (!dir->files[i]._seen) {
			free_file(dir->files[i]);
			dir->files[i] = dir->files[--dir->files_size]; // Swap-remove
		}
	}

	assert(directory_depth > 0);
	directory_depth--;
}

static bool validate_about_file(const char *about_json_path) {
  // returns false if the about file dosent exist, raises a grug error if the about.json is invalid
	if (access(about_json_path, F_OK)) {
  	errno = 0;
  	return false;
	}

	struct json_node node;
	json(about_json_path, &node);

	grug_assert(node.type == JSON_NODE_OBJECT, "%s its root must be an object", about_json_path);
	struct json_object root_object = node.object;

	grug_assert(root_object.field_count >= 4, "%s must have at least these 4 fields, in this order: \"name\", \"version\", \"game_version\", \"author\"", about_json_path);

	struct json_field *field = root_object.fields;

	grug_assert(streq(field->key, "name"), "%s its root object must have \"name\" as its first field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"name\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"name\" field value must not be an empty string", about_json_path);
	field++;

	grug_assert(streq(field->key, "version"), "%s its root object must have \"version\" as its second field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"version\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"version\" field value must not be an empty string", about_json_path);
	field++;

	grug_assert(streq(field->key, "game_version"), "%s its root object must have \"game_version\" as its third field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"game_version\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"game_version\" field value must not be an empty string", about_json_path);
	field++;

	grug_assert(streq(field->key, "author"), "%s its root object must have \"author\" as its fourth field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"author\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"author\" field value must not be an empty string", about_json_path);
	field++;

	for (size_t i = 4; i < root_object.field_count; i++) {
		grug_assert(!streq(field->key, ""), "%s its %zuth field key must not be an empty string", about_json_path, i + 1);
		field++;
	}

	return true;
}

// Cases:
// 1. "" => ""
// 2. "/" => ""
// 3. "/a" => "a"
// 4. "/a/" => ""
// 5. "/a/b" => "b"
static const char *get_basename(const char *path) {
	const char *base = strrchr(path, '/');
	return base ? base + 1 : path;
}

static char entry_path[STUPID_MAX_PATH];
static char dll_entry_path[STUPID_MAX_PATH];

static void reload_modified_mods_dir(char *mods_root_path, char *dll_root_path, struct grug_mod_dir *dir) {
  if (mods_root_path != entry_path) { // this is a pointer comparison, sets up entry_path if needed
    grug_assert(snprintf(entry_path, sizeof(entry_path), "%s", mods_root_path) >= 0, "Filling the variable 'entry_path' failed");
  }

  if (dll_root_path != dll_entry_path) { // this is a pointer comparison, sets up entry_path if needed
    grug_assert(snprintf(dll_entry_path, sizeof(dll_entry_path), "%s", dll_root_path) >= 0, "Filling the variable 'entry_path' failed");
  }

	DIR *dirp = opendir(mods_root_path);
	grug_assert(dirp, "opendir(\"%s\"): %s", mods_root_path, strerror(errno));

	for (size_t i = 0; i < dir->dirs_size; i++) {
		dir->dirs[i]._seen = false;
	}

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		const char *name = /dp->d_name;

		if (streq(name, ".") || streq(name, "..")) {
			continue;
		}

		// static char entry_path[STUPID_MAX_PATH];
		// grug_assert(snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_root_path, name) >= 0, "Filling the variable 'entry_path' failed");

		int entry_start = strlen(entry_path);
		grug_assert(snprintf(&entry_path[entry_start], sizeof(entry_path) - entry_start, "/%s", name) >= 0, "Filling the variable 'entry_path' failed");

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

		int dll_entry_start = strlen(dll_entry_path);
		grug_assert(snprintf(&dll_entry_path[dll_entry_start], sizeof(dll_entry_path) - dll_entry_start, "/%s", name) >= 0, "Filling the variable 'entry_path' failed");

		if (S_ISDIR(entry_stat.st_mode)) {
			static char about_json_path[STUPID_MAX_PATH];
			grug_assert(snprintf(about_json_path, sizeof(about_json_path), "%s/about.json", entry_path) >= 0, "Filling the variable 'about_json_path' failed");

 			// This always returns NULL during the first call of reload_modified_mods()
 			struct grug_mod_dir *subdir = get_subdir(dir, name);

 			if (!subdir) {
  			struct grug_mod_dir inserted_subdir = { .name = strdup(entry_path) };
  			grug_assert(inserted_subdir.name, "strdup: %s", strerror(errno));
  			subdir = push_subdir(dir, inserted_subdir);
 			}

			if ((subdir->is_mod = validate_about_file(about_json_path))) {
			  mod = name;

			  printf("%s\n", about_json_path);

   			subdir->_seen = true;
   			reload_modified_mod(entry_path, dll_entry_path, subdir);
			} else {
   			subdir->_seen = true;
			  reload_modified_mods_dir(entry_path, dll_entry_path, subdir);
			}

      if (!subdir->is_mod) {
        grug_assert(subdir->files_size == 0, "Grug files must be contained in a valid mod directory, however no parent of '%s' has an about.json", entry_path)
      }
		} else if (S_ISREG(entry_stat.st_mode)) {
		  grug_assert(!streq(get_file_extension(entry_path), ".grug"), "Grug files must be contained in a valid mod directory, however no parent of '%s' has an about.json", entry_path)
		}

		entry_path[entry_start] = 0;
		dll_entry_path[dll_entry_start] = 0;
	}

	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);

	// If the directory used to contain a mod that doesn't exist anymore, free it
	for (size_t i = dir->dirs_size; i > 0;) {
		i--;
		if (!dir->dirs[i]._seen) {
			free_dir(dir->dirs[i]);
			dir->dirs[i] = dir->dirs[--dir->dirs_size]; // Swap-remove
		}
	}
}

static void reload_modified_mods(void) {
  entry_path[0] = 0;
	reload_modified_mods_dir(mods_root_dir_path, dll_root_dir_path, &grug_mods);
}

bool grug_init(grug_runtime_error_handler_t handler, const char *mod_api_json_path, const char *mods_dir_path, const char *dll_dir_path, uint64_t on_fn_time_limit_ms_) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	assert(handler && "grug_init() its grug_runtime_error_handler can't be NULL");
	grug_runtime_error_handler = handler;

	assert(!is_grug_initialized && "grug_init() can't be called more than once");

	assert(!strchr(mods_dir_path, '\\') && "grug_init() its mods_dir_path can't contain backslashes, so replace them with '/'");
	assert(mods_dir_path[strlen(mods_dir_path) - 1] != '/' && "grug_init() its mods_dir_path can't have a trailing '/'");

	assert(!strchr(dll_dir_path, '\\') && "grug_init() its dll_dir_path can't contain backslashes, so replace them with '/'");
	assert(dll_dir_path[strlen(dll_dir_path) - 1] != '/' && "grug_init() its dll_dir_path can't have a trailing '/'");

	parse_mod_api_json(mod_api_json_path);

	assert(strlen(mods_dir_path) + 1 <= STUPID_MAX_PATH && "grug_init() its mods_dir_path exceeds the maximum path length");
	memcpy(mods_root_dir_path, mods_dir_path, strlen(mods_dir_path) + 1);

	assert(strlen(dll_dir_path) + 1 <= STUPID_MAX_PATH && "grug_init() its dll_dir_path exceeds the maximum path length");
	memcpy(dll_root_dir_path, dll_dir_path, strlen(dll_dir_path) + 1);

	on_fn_time_limit_ms = on_fn_time_limit_ms_;
	on_fn_time_limit_sec = on_fn_time_limit_ms / MS_PER_SEC;
	on_fn_time_limit_ns = (on_fn_time_limit_ms % MS_PER_SEC) * NS_PER_MS;

	is_grug_initialized = true;

	return false;
}

bool grug_regenerate_modified_mods(void) {
	assert(is_grug_initialized && "You forgot to call grug_init() once at program startup!");

	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	reset_regenerate_modified_mods();

	grug_loading_error_in_grug_file = false;

	if (!grug_mods.name) {
		grug_mods.name = strdup(get_basename(mods_root_dir_path));
		grug_assert(grug_mods.name, "strdup: %s", strerror(errno));
	}

	reload_modified_mods();

	check_that_every_entity_exists(grug_mods);

	reset_previous_grug_error();

	return false;
}

USED_BY_MODS bool grug_has_runtime_error_happened = false;
void grug_game_function_error_happened(const char *message) {
	grug_has_runtime_error_happened = true;
	snprintf(runtime_error_reason, sizeof(runtime_error_reason), "%s", message);
}

USED_BY_MODS bool grug_on_fns_in_safe_mode = true;
void grug_set_on_fns_to_safe_mode(void) {
	grug_on_fns_in_safe_mode = true;
}
void grug_set_on_fns_to_fast_mode(void) {
	grug_on_fns_in_safe_mode = false;
}
bool grug_are_on_fns_in_safe_mode(void) {
	return grug_on_fns_in_safe_mode;
}
void grug_toggle_on_fns_mode(void) {
	grug_on_fns_in_safe_mode = !grug_on_fns_in_safe_mode;
}

// MIT License

// Copyright (c) 2024 MyNameIsTrez

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
