#include "grug.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_CHARACTERS_IN_FILE 420420
#define MAX_TOKENS_IN_FILE 420420
#define MAX_FIELDS_IN_FILE 420420
#define MAX_EXPRS_IN_FILE 420420
#define MAX_STATEMENTS_IN_FILE 420420
#define MAX_ARGUMENTS_IN_FILE 420420
#define MAX_HELPER_FNS_IN_FILE 420420
#define MAX_ON_FNS_IN_FILE 420420
#define MAX_GLOBAL_VARIABLES_IN_FILE 420420
#define SPACES_PER_INDENT 4
#define MAX_CALL_ARGUMENTS_PER_STACK_FRAME 69
#define MAX_STATEMENTS_PER_STACK_FRAME 1337
#define MAX_SERIALIZED_TO_C_CHARS 420420
#define MODS_DIR_PATH "mods"
#define DLL_DIR_PATH "mod_dlls"
#define MOD_API_JSON_PATH "mod_api.json"
#define UNREACHABLE_STR "This line of code is supposed to be unreachable. Please report this bug to the grug developers!"

// "The problem is that you can't meaningfully define a constant like this
// in a header file. The maximum path size is actually to be something
// like a filesystem limitation, or at the very least a kernel parameter.
// This means that it's a dynamic value, not something preordained."
// https://eklitzke.org/path-max-is-tricky
#define STUPID_MAX_PATH 4096

#define grug_error(...) {\
	int ret = snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__);\
	(void)ret;\
	grug_error.filename = __FILE__;\
	grug_error.line_number = __LINE__;\
	longjmp(error_jmp_buffer, 1);\
}

#define grug_assert(condition, ...) {\
	if (!(condition)) {\
		grug_error(__VA_ARGS__);\
	}\
}

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

typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

struct grug_error grug_error;
static jmp_buf error_jmp_buffer;

//// UTILS

#define TEMP_MAX_STRINGS_CHARACTERS 420420
#define BFD_HASH_BUCKET_SIZE 4051 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l345

static char temp_strings[TEMP_MAX_STRINGS_CHARACTERS];
static size_t temp_strings_size;

static void reset_utils(void) {
	temp_strings_size = 0;
}

// This string array gets reset by every regenerate_dll() call
static char *push_temp_string(char *slice_start, size_t length) {
	grug_assert(temp_strings_size + length < TEMP_MAX_STRINGS_CHARACTERS, "There are more than %d characters in the temp_strings array, exceeding TEMP_MAX_STRINGS_CHARACTERS", TEMP_MAX_STRINGS_CHARACTERS);

	char *new_str = temp_strings + temp_strings_size;

	for (size_t i = 0; i < length; i++) {
		temp_strings[temp_strings_size++] = slice_start[i];
	}
	temp_strings[temp_strings_size++] = '\0';

	return new_str;
}

static bool streq(char *a, char *b) {
	return strcmp(a, b) == 0;
}

static bool starts_with(char *a, char *b) {
	return strncmp(a, b, strlen(b)) == 0;
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
	len = 0;
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

static char *get_file_extension(char *filename) {
	char *ext = strrchr(filename, '.');
	if (ext) {
		return ext;
	}
	return "";
}

//// OPENING RESOURCES

// TODO: Also call this from parse_global_resources_fn()
// static void open_resource(char *path) {
// }

static void open_resources_recursively(char *dir_path) {
	DIR *dirp = opendir(dir_path);
	grug_assert(dirp, "opendir: %s", strerror(errno));

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		if (streq(dp->d_name, ".") || streq(dp->d_name, "..")) {
			continue;
		}

		char entry_path[STUPID_MAX_PATH];
		snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, dp->d_name);

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) != -1, "stat: %s", strerror(errno));

		if (S_ISDIR(entry_stat.st_mode)) {
			open_resources_recursively(entry_path);
		} else if (S_ISREG(entry_stat.st_mode) && streq(get_file_extension(dp->d_name), ".grug")) {
			printf("grug file: %s\n", entry_path);
		}
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);
}

static void open_resources(void) {
	printf("resources:\n");

	open_resources_recursively(MODS_DIR_PATH);
}

//// JSON

#define JSON_MAX_CHARACTERS_IN_FILE 420420
#define JSON_MAX_TOKENS 420420
#define JSON_MAX_NODES 420420
#define JSON_MAX_FIELDS 420420
#define JSON_MAX_CHILD_NODES 420
#define JSON_MAX_STRINGS_CHARACTERS 420420
#define JSON_MAX_RECURSION_DEPTH 42

#define json_error(error) {\
	grug_error("JSON error: %s", json_error_messages[error]);\
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
	char *key;
	struct json_node *value;
};

struct json_node {
	enum {
		JSON_NODE_STRING,
		JSON_NODE_ARRAY,
		JSON_NODE_OBJECT,
	} type;
	union {
		char *string;
		struct json_array array;
		struct json_object object;
	} data;
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

char *json_error_messages[] = {
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

static size_t json_recursion_depth;

static char json_text[JSON_MAX_CHARACTERS_IN_FILE];
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
	char *str;
};
static struct json_token json_tokens[JSON_MAX_TOKENS];
static size_t json_tokens_size;

struct json_node json_nodes[JSON_MAX_NODES];
static size_t json_nodes_size;

struct json_field json_fields[JSON_MAX_FIELDS];
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

static bool is_duplicate_key(struct json_field *child_fields, size_t field_count, char *key) {
	u32 i = json_buckets[elf_hash(key) % field_count];

	while (1) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (strcmp(key, child_fields[i].key) == 0) {
			break;
		}

		i = json_chains[i];
	}

	return true;
}

static void check_duplicate_keys(struct json_field *child_fields, size_t field_count) {
	memset(json_buckets, UINT32_MAX, field_count * sizeof(u32));

	for (size_t i = 0; i < field_count; i++) {
		char *key = child_fields[i].key;

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

	node.data.object.field_count = 0;

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
				json_assert(node.data.object.field_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
				child_fields[node.data.object.field_count++] = field;
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
				json_assert(node.data.object.field_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
				child_fields[node.data.object.field_count++] = field;
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
				json_assert(node.data.object.field_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
				child_fields[node.data.object.field_count++] = field;
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
			check_duplicate_keys(child_fields, node.data.object.field_count);
			node.data.object.fields = json_fields + json_fields_size;
			for (size_t field_index = 0; field_index < node.data.object.field_count; field_index++) {
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

	node.data.array.value_count = 0;

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
			json_assert(node.data.array.value_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
			child_nodes[node.data.array.value_count++] = json_parse_string(i);
			break;
		case TOKEN_TYPE_ARRAY_OPEN:
			json_assert(!seen_value, JSON_ERROR_UNEXPECTED_ARRAY_OPEN);
			seen_value = true;
			seen_comma = false;
			json_assert(node.data.array.value_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
			child_nodes[node.data.array.value_count++] = json_parse_array(i);
			break;
		case TOKEN_TYPE_ARRAY_CLOSE:
			json_assert(!seen_comma, JSON_ERROR_TRAILING_COMMA);
			node.data.array.values = json_nodes + json_nodes_size;
			for (size_t value_index = 0; value_index < node.data.array.value_count; value_index++) {
				json_push_node(child_nodes[value_index]);
			}
			(*i)++;
			json_recursion_depth--;
			return node;
		case TOKEN_TYPE_OBJECT_OPEN:
			json_assert(!seen_value, JSON_ERROR_UNEXPECTED_OBJECT_OPEN);
			seen_value = true;
			seen_comma = false;
			json_assert(node.data.array.value_count < JSON_MAX_CHILD_NODES, JSON_ERROR_TOO_MANY_CHILD_NODES);
			child_nodes[node.data.array.value_count++] = json_parse_object(i);
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
	node.data.string = token->str;

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

static char *json_push_string(char *slice_start, size_t length) {
	grug_assert(json_strings_size + length < JSON_MAX_STRINGS_CHARACTERS, "There are more than %d characters in the json_strings array, exceeding JSON_MAX_STRINGS_CHARACTERS", JSON_MAX_STRINGS_CHARACTERS);

	char *new_str = json_strings + json_strings_size;

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
	bool in_string = false;
	size_t string_start_index;

	while (i < json_text_size) {
		if (json_text[i] == '"') {
			if (in_string) {
				json_push_token(
					TOKEN_TYPE_STRING,
					string_start_index + 1,
					i - string_start_index - 1
				);
			} else {
				string_start_index = i;
			}
			in_string = !in_string;
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
		} else if (!isspace(json_text[i]) && !in_string) {
			json_error(JSON_ERROR_UNRECOGNIZED_CHARACTER);
		}
		i++;
	}

	json_assert(!in_string, JSON_ERROR_UNCLOSED_STRING);
}

static void json_read_text(char *json_file_path) {
	FILE *f = fopen(json_file_path, "r");
	json_assert(f, JSON_ERROR_FAILED_TO_OPEN_FILE);

	json_text_size = fread(
		json_text,
		sizeof(char),
		JSON_MAX_CHARACTERS_IN_FILE,
		f
	);

	int is_eof = feof(f);
	int err = ferror(f);

	json_assert(fclose(f) == 0, JSON_ERROR_FAILED_TO_CLOSE_FILE);

	json_assert(json_text_size != 0, JSON_ERROR_FILE_EMPTY);
	json_assert(is_eof && json_text_size != JSON_MAX_CHARACTERS_IN_FILE, JSON_ERROR_FILE_TOO_BIG);
	json_assert(err == 0, JSON_ERROR_FILE_READING_ERROR);

	json_text[json_text_size] = '\0';
}

static void json_reset(void) {
	json_recursion_depth = 0;
	json_text_size = 0;
	json_tokens_size = 0;
	json_nodes_size = 0;
	json_strings_size = 0;
	json_fields_size = 0;
}

void json(char *json_file_path, struct json_node *returned) {
	json_reset();

	json_read_text(json_file_path);

	json_tokenize();

	size_t token_index = 0;
	*returned = json_parse(&token_index);
}

//// PARSING MOD API JSON

#define MAX_GRUG_FUNCTIONS 420420
#define MAX_GRUG_ARGUMENTS 420420

enum type {
	type_void,
	type_i32,
	type_string,
};
static char *type_names[] = {
	[type_i32] = "i32",
	[type_string] = "string",
};
static size_t type_sizes[] = {
	[type_i32] = sizeof(int32_t),
	[type_string] = sizeof(char *),
};

struct grug_on_function {
	char *name;
	struct grug_argument *arguments;
	size_t argument_count;
};

struct grug_entity {
	char *name;
	struct grug_argument *arguments;
	size_t argument_count;
	struct grug_on_function *on_functions;
	size_t on_function_count;
};

struct grug_game_function {
	char *name;
	enum type return_type;
	struct grug_argument *arguments;
	size_t argument_count;
};

struct grug_argument {
	char *name;
	enum type type;
};

struct grug_on_function grug_on_functions[MAX_GRUG_FUNCTIONS];
static size_t grug_on_functions_size;

struct grug_entity grug_define_functions[MAX_GRUG_FUNCTIONS];
static size_t grug_define_functions_size;

struct grug_game_function grug_game_functions[MAX_GRUG_FUNCTIONS];
static size_t grug_game_functions_size;

struct grug_argument grug_arguments[MAX_GRUG_ARGUMENTS];
static size_t grug_arguments_size;

static void push_grug_on_function(struct grug_on_function fn) {
	grug_assert(grug_on_functions_size < MAX_GRUG_FUNCTIONS, "There are more than %d on_ functions in mod_api.json, exceeding MAX_GRUG_FUNCTIONS", MAX_GRUG_FUNCTIONS);
	grug_on_functions[grug_on_functions_size++] = fn;
}

static void push_grug_entity(struct grug_entity fn) {
	grug_assert(grug_define_functions_size < MAX_GRUG_FUNCTIONS, "There are more than %d define_ functions in mod_api.json, exceeding MAX_GRUG_FUNCTIONS", MAX_GRUG_FUNCTIONS);
	grug_define_functions[grug_define_functions_size++] = fn;
}

static void push_grug_game_function(struct grug_game_function fn) {
	grug_assert(grug_game_functions_size < MAX_GRUG_FUNCTIONS, "There are more than %d game functions in mod_api.json, exceeding MAX_GRUG_FUNCTIONS", MAX_GRUG_FUNCTIONS);
	grug_game_functions[grug_game_functions_size++] = fn;
}

static void push_grug_argument(struct grug_argument argument) {
	grug_assert(grug_arguments_size < MAX_GRUG_ARGUMENTS, "There are more than %d grug arguments, exceeding MAX_GRUG_ARGUMENTS", MAX_GRUG_ARGUMENTS);
	grug_arguments[grug_arguments_size++] = argument;
}

static enum type parse_type(char *type) {
	if (streq(type, "i32")) {
		return type_i32;
	}
	if (streq(type, "string")) {
		return type_string;
	}

	// TODO: Make sure to add any new types to this error message
	grug_error("Types must be one of i32/string");
}

static void init_game_fns(struct json_object fns) {
	for (size_t fn_index = 0; fn_index < fns.field_count; fn_index++) {
		struct grug_game_function grug_fn = {0};

		grug_fn.name = fns.fields[fn_index].key;
		grug_assert(!streq(grug_fn.name, ""), "\"game_functions\" its function names must not be an empty string");
		grug_assert(!starts_with(grug_fn.name, "on_"), "\"game_functions\" its function names must not start with 'on_'");

		grug_assert(fns.fields[fn_index].value->type == JSON_NODE_OBJECT, "\"game_functions\" its array must only contain objects");
		struct json_object fn = fns.fields[fn_index].value->data.object;
		grug_assert(fn.field_count >= 1, "\"game_functions\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 3, "\"game_functions\" its objects must not have more than 3 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"game_functions\" its functions must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"game_functions\" its function descriptions must be strings");
		char *description = field->value->data.string;
		grug_assert(!streq(description, ""), "\"game_functions\" its function descriptions must not be an empty string");

		bool seen_return_type = false;

		if (fn.field_count > 1) {
			field++;

			if (streq(field->key, "return_type")) {
				grug_assert(field->value->type == JSON_NODE_STRING, "\"game_functions\" its function return types must be strings");
				grug_fn.return_type = parse_type(field->value->data.string);
				seen_return_type = true;
				field++;
			} else {
				grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second field was something other than \"return_type\" and \"arguments\"");
			}
		}

		if ((!seen_return_type && fn.field_count > 1) || fn.field_count > 2) {
			grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second or third field was something other than \"arguments\"");

			grug_fn.return_type = type_void;

			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"game_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->data.array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->data.array.value_count;
			grug_assert(grug_fn.argument_count > 0, "\"game_functions\" its \"arguments\" array must not be empty (just remove the \"arguments\" key entirely)");

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct grug_argument grug_arg;

				grug_assert(value->type == JSON_NODE_OBJECT, "\"game_functions\" its function arguments must only contain objects");
				grug_assert(value->data.object.field_count == 2, "\"game_functions\" its function arguments must only contain a name and type field");
				struct json_field *argument_field = value->data.object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"game_functions\" its function arguments must always have \"name\" be their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.name = argument_field->value->data.string;
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"game_functions\" its function arguments must always have \"type\" be their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->data.string);
				argument_field++;

				push_grug_argument(grug_arg);
				value++;
			}
		}

		push_grug_game_function(grug_fn);
	}
}

static void init_on_fns(struct json_object fns) {
	for (size_t fn_index = 0; fn_index < fns.field_count; fn_index++) {
		struct grug_on_function grug_fn = {0};

		grug_fn.name = fns.fields[fn_index].key;
		grug_assert(!streq(grug_fn.name, ""), "\"on_functions\" its function names must not be an empty string");
		grug_assert(starts_with(grug_fn.name, "on_"), "\"on_functions\" its function names must start with 'on_'");

		grug_assert(fns.fields[fn_index].value->type == JSON_NODE_OBJECT, "\"on_functions\" its array must only contain objects");
		struct json_object fn = fns.fields[fn_index].value->data.object;
		grug_assert(fn.field_count >= 1, "\"on_functions\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 2, "\"on_functions\" its objects must not have more than 2 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"on_functions\" its functions must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"on_functions\" its function descriptions must be strings");
		char *description = field->value->data.string;
		grug_assert(!streq(description, ""), "\"on_functions\" its function descriptions must not be an empty string");

		if (fn.field_count > 1) {
			field++;

			grug_assert(streq(field->key, "arguments"), "\"on_functions\" its functions must have \"arguments\" as the second field");
			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"on_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->data.array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->data.array.value_count;

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct grug_argument grug_arg;

				grug_assert(value->type == JSON_NODE_OBJECT, "\"on_functions\" its function arguments must only contain objects");
				grug_assert(value->data.object.field_count == 2, "\"on_functions\" its function arguments must only contain a name and type field");
				struct json_field *argument_field = value->data.object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"on_functions\" its function arguments must always have \"name\" be their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.name = argument_field->value->data.string;
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"on_functions\" its function arguments must always have \"type\" be their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->data.string);
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

		entity.name = entities.fields[entity_field_index].key;
		grug_assert(!streq(entity.name, ""), "\"entities\" its names must not be an empty string");

		grug_assert(entities.fields[entity_field_index].value->type == JSON_NODE_OBJECT, "\"entities\" must only contain object values");
		struct json_object fn = entities.fields[entity_field_index].value->data.object;
		grug_assert(fn.field_count >= 1, "\"entities\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 3, "\"entities\" its objects must not have more than 3 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"entities\" must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"entities\" its descriptions must be strings");
		char *description = field->value->data.string;
		grug_assert(!streq(description, ""), "\"entities\" its descriptions must not be an empty string");

		bool seen_fields = false;

		if (fn.field_count > 1) {
			field++;

			if (streq(field->key, "fields")) {
				grug_assert(field->value->type == JSON_NODE_ARRAY, "\"entities\" its \"fields\" must be arrays");
				struct json_node *value = field->value->data.array.values;
				entity.arguments = grug_arguments + grug_arguments_size;
				entity.argument_count = field->value->data.array.value_count;

				for (size_t argument_index = 0; argument_index < entity.argument_count; argument_index++) {
					struct grug_argument grug_arg;

					grug_assert(value->type == JSON_NODE_OBJECT, "\"entities\" its arguments must only contain objects");
					grug_assert(value->data.object.field_count == 2, "\"entities\" its arguments must only contain a name and type field");
					struct json_field *arg_field = value->data.object.fields;

					grug_assert(streq(arg_field->key, "name"), "\"entities\" its arguments must always have \"name\" be their first field");
					grug_assert(arg_field->value->type == JSON_NODE_STRING, "\"entities\" its arguments must always have string values");
					grug_arg.name = arg_field->value->data.string;
					arg_field++;

					grug_assert(streq(arg_field->key, "type"), "\"entities\" its arguments must always have \"type\" be their second field");
					grug_assert(arg_field->value->type == JSON_NODE_STRING, "\"entities\" its arguments must always have string values");
					grug_arg.type = parse_type(arg_field->value->data.string);

					push_grug_argument(grug_arg);
					value++;
				}

				seen_fields = true;
				field++;
			} else {
				grug_assert(streq(field->key, "on_functions"), "\"entities\" its second field was something other than \"fields\" and \"on_functions\"");
			}
		}

		if ((!seen_fields && fn.field_count > 1) || fn.field_count > 2) {
			grug_assert(streq(field->key, "on_functions"), "\"entities\" its second or third field was something other than \"on_functions\"");
			grug_assert(field->value->type == JSON_NODE_OBJECT, "\"entities\" its \"on_functions\" field must have an object as its value");
			entity.on_functions = grug_on_functions + grug_on_functions_size;
			entity.on_function_count = field->value->data.object.field_count;
			init_on_fns(field->value->data.object);
		}

		push_grug_entity(entity);
	}
}

static void parse_mod_api_json(void) {
	struct json_node node;
	json(MOD_API_JSON_PATH, &node);

	grug_assert(node.type == JSON_NODE_OBJECT, "mod_api.json must start with an object");
	struct json_object root_object = node.data.object;

	grug_assert(root_object.field_count == 2, "mod_api.json must have these 2 fields, in this order: \"entities\", \"game_functions\"");

	struct json_field *field = root_object.fields;

	grug_assert(streq(field->key, "entities"), "mod_api.json its root object must have \"entities\" as its first field");
	grug_assert(field->value->type == JSON_NODE_OBJECT, "mod_api.json its \"entities\" field must have an object as its value");
	init_entities(field->value->data.object);
	field++;

	grug_assert(streq(field->key, "game_functions"), "mod_api.json its root object must have \"game_functions\" as its third field");
	grug_assert(field->value->type == JSON_NODE_OBJECT, "mod_api.json its \"game_functions\" field must have an object as its value");
	init_game_fns(field->value->data.object);
}

//// READING

static char *read_file(char *path) {
	FILE *f = fopen(path, "rb");
	grug_assert(f, "fopen: %s", strerror(errno));

	grug_assert(fseek(f, 0, SEEK_END) == 0, "fseek: %s", strerror(errno));

	long count = ftell(f);
	grug_assert(count != -1, "ftell: %s", strerror(errno));
	grug_assert(count < MAX_CHARACTERS_IN_FILE, "There are more than %d characters in the grug file, exceeding MAX_CHARACTERS_IN_FILE", MAX_CHARACTERS_IN_FILE);

	rewind(f);

	static char text[MAX_CHARACTERS_IN_FILE];

	size_t bytes_read = fread(text, sizeof(char), count, f);
	grug_assert(bytes_read == (size_t)count, "fread: %s", strerror(errno));

	text[count] = '\0';

	grug_assert(fclose(f) == 0, "fclose: %s", strerror(errno));

	return text;
}

//// TOKENIZATION

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
	PERIOD_TOKEN,
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
	LOOP_TOKEN,
	BREAK_TOKEN,
	RETURN_TOKEN,
	CONTINUE_TOKEN,
	SPACES_TOKEN,
	NEWLINES_TOKEN,
	STRING_TOKEN,
	WORD_TOKEN,
	NUMBER_TOKEN,
	COMMENT_TOKEN,
};

struct token {
	enum token_type type;
	char *str;
};
static char *get_token_type_str[] = {
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
	[PERIOD_TOKEN] = "PERIOD_TOKEN",
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
	[LOOP_TOKEN] = "LOOP_TOKEN",
	[BREAK_TOKEN] = "BREAK_TOKEN",
	[RETURN_TOKEN] = "RETURN_TOKEN",
	[CONTINUE_TOKEN] = "CONTINUE_TOKEN",
	[SPACES_TOKEN] = "SPACES_TOKEN",
	[NEWLINES_TOKEN] = "NEWLINES_TOKEN",
	[STRING_TOKEN] = "STRING_TOKEN",
	[WORD_TOKEN] = "WORD_TOKEN",
	[NUMBER_TOKEN] = "NUMBER_TOKEN",
	[COMMENT_TOKEN] = "COMMENT_TOKEN",
};
static struct token tokens[MAX_TOKENS_IN_FILE];
static size_t tokens_size;

static void reset_tokenization(void) {
	tokens_size = 0;
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
		char *token_type_str = get_token_type_str[token.type];
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

		char *token_type_str = get_token_type_str[token.type];
		grug_log("| %*s ", (int)longest_token_type_len, token_type_str);

		if (token.type == NEWLINES_TOKEN) {
			grug_log("| '");
			for (size_t j = 0; j < strlen(token.str); j++) {
				grug_log("\\n");
			}
			grug_log("'\n");
		} else {
			grug_log("| '%s'\n", token.str);
		}
	}
}

static char *get_escaped_char(char *str) {
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

static void push_token(enum token_type type, char *str, size_t len) {
	grug_assert(tokens_size < MAX_TOKENS_IN_FILE, "There are more than %d tokens in the grug file, exceeding MAX_TOKENS_IN_FILE", MAX_TOKENS_IN_FILE);
	tokens[tokens_size++] = (struct token){
		.type = type,
		.str = push_temp_string(str, len),
	};
}

static void tokenize(char *grug_text) {
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
		} else if (grug_text[i] == '.') {
			push_token(PERIOD_TOKEN, grug_text+i, 1);
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
		} else if (grug_text[i + 0] == 'l' && grug_text[i + 1] == 'o' && grug_text[i + 2] == 'o' && grug_text[i + 3] == 'p' && is_end_of_word(grug_text[i + 4])) {
			push_token(LOOP_TOKEN, grug_text+i, 4);
			i += 4;
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
			char *str = grug_text+i;
			size_t old_i = i;

			do {
				i++;
			} while (grug_text[i] == ' ');

			push_token(SPACES_TOKEN, str, i - old_i);
		} else if (grug_text[i] == '\n') {
			char *str = grug_text+i;
			size_t old_i = i;

			do {
				i++;
			} while (grug_text[i] == '\n');

			push_token(NEWLINES_TOKEN, str, i - old_i);
		} else if (grug_text[i] == '\"') {
			char *str = grug_text+i + 1;
			size_t old_i = i + 1;

			size_t open_double_quote_index = i;

			do {
				i++;
				grug_assert(grug_text[i] != '\0', "Unclosed \" at character %zu of the grug text file", open_double_quote_index + 1);
			} while (grug_text[i] != '\"');
			i++;

			push_token(STRING_TOKEN, str, i - old_i - 1);
		} else if (isalpha(grug_text[i]) || grug_text[i] == '_') {
			char *str = grug_text+i;
			size_t old_i = i;

			do {
				i++;
			} while (isalnum(grug_text[i]) || grug_text[i] == '_');

			push_token(WORD_TOKEN, str, i - old_i);
		} else if (isdigit(grug_text[i])) {
			char *str = grug_text+i;
			size_t old_i = i;

			bool seen_period = false;

			do {
				i++;

				if (grug_text[i] == '.') {
					grug_assert(!seen_period, "Encountered two '.' periods in a number at character %zu of the grug text file", i);
					seen_period = true;
				}
			} while (isdigit(grug_text[i]));

			push_token(NUMBER_TOKEN, str, i - old_i);
		} else if (grug_text[i] == ';') {
			char *str = grug_text+i;
			size_t old_i = i;

			while (true) {
				i++;
				if (!isprint(grug_text[i])) {
					if (grug_text[i] == '\n' || grug_text[i] == '\0') {
						break;
					}

					grug_error("Unexpected unprintable character '%.*s' at character %zu of the grug text file", is_escaped_char(grug_text[i]) ? 2 : 1, get_escaped_char(&grug_text[i]), i + 1);
				}
			}

			push_token(COMMENT_TOKEN, str, i - old_i);
		} else {
			grug_error("Unrecognized character '%.*s' at character %zu of the grug text file", is_escaped_char(grug_text[i]) ? 2 : 1, get_escaped_char(&grug_text[i]), i + 1);
		}
	}
}

//// VERIFY AND TRIM SPACES

static void assert_token_type(size_t token_index, unsigned int expected_type) {
	struct token token = peek_token(token_index);
	grug_assert(token.type == expected_type, "Expected token type %s, but got %s at token index %zu", get_token_type_str[expected_type], get_token_type_str[token.type], token_index);
}

static void assert_spaces(size_t token_index, size_t expected_spaces) {
	assert_token_type(token_index, SPACES_TOKEN);

	struct token token = peek_token(token_index);
	grug_assert(strlen(token.str) == expected_spaces, "Expected %zu space%s, but got %zu at token index %zu", expected_spaces, expected_spaces > 1 ? "s" : "", strlen(token.str), token_index);
}

// Trims whitespace tokens after verifying that the formatting is correct.
// 1. The whitespace indentation follows the block scope nesting, like in Python.
// 2. There aren't any leading/trailing/missing/extra spaces.
static void verify_and_trim_spaces(void) {
	size_t i = 0;
	size_t new_index = 0;
	int depth = 0;

	while (i < tokens_size) {
		struct token token = peek_token(i);

		switch (token.type) {
			case OPEN_PARENTHESIS_TOKEN:
			case CLOSE_PARENTHESIS_TOKEN:
			case OPEN_BRACE_TOKEN:
				break;
			case CLOSE_BRACE_TOKEN: {
				depth--;
				grug_assert(depth >= 0, "Expected a '{' to match the '}' at token index %zu", i + 1);
				if (depth > 0) {
					assert_spaces(i - 1, depth * SPACES_PER_INDENT);
				}
				break;
			}
			case PLUS_TOKEN:
			case MINUS_TOKEN:
			case MULTIPLICATION_TOKEN:
			case DIVISION_TOKEN:
			case REMAINDER_TOKEN:
				break;
			case COMMA_TOKEN: {
				grug_assert(i + 1 < tokens_size, "Expected something after the comma at token index %zu", i);

				struct token next_token = peek_token(i + 1);
				grug_assert(next_token.type == NEWLINES_TOKEN || next_token.type == SPACES_TOKEN, "Expected a single newline or space after the comma, but got token type %s at token index %zu", get_token_type_str[next_token.type], i + 1);
				grug_assert(strlen(next_token.str) == 1, "Expected one newline or space, but got several after the comma at token index %zu", i + 1);

				if (next_token.type == SPACES_TOKEN) {
					grug_assert(i + 2 < tokens_size, "Expected text after the comma and space at token index %zu", i);

					next_token = peek_token(i + 2);
					switch (next_token.type) {
						case OPEN_PARENTHESIS_TOKEN:
						case MINUS_TOKEN:
						case STRING_TOKEN:
						case WORD_TOKEN:
						case NUMBER_TOKEN:
							break;
						default:
							grug_error("Unexpected token type %s after the comma and space, at token index %zu", get_token_type_str[next_token.type], i + 2);
					}
				}
				break;
			}
			case COLON_TOKEN:
			case EQUALS_TOKEN:
			case NOT_EQUALS_TOKEN:
			case ASSIGNMENT_TOKEN:
			case GREATER_OR_EQUAL_TOKEN:
			case GREATER_TOKEN:
			case LESS_OR_EQUAL_TOKEN:
			case LESS_TOKEN:
			case AND_TOKEN:
			case OR_TOKEN:
			case NOT_TOKEN:
			case TRUE_TOKEN:
			case FALSE_TOKEN:
			case IF_TOKEN:
			case ELSE_TOKEN:
			case LOOP_TOKEN:
			case BREAK_TOKEN:
			case RETURN_TOKEN:
			case CONTINUE_TOKEN:
				break;
			case SPACES_TOKEN: {
				grug_assert(i + 1 < tokens_size, "Expected another token after the space at token index %zu", i);

				struct token next_token = peek_token(i + 1);
				switch (next_token.type) {
					case OPEN_PARENTHESIS_TOKEN:
					case CLOSE_PARENTHESIS_TOKEN:
						break;
					case OPEN_BRACE_TOKEN:
						depth++;
						assert_spaces(i, 1);
						break;
					case CLOSE_BRACE_TOKEN:
						break;
					case PLUS_TOKEN:
						assert_spaces(i, 1);
						break;
					case MINUS_TOKEN:
						break;
					case MULTIPLICATION_TOKEN:
					case DIVISION_TOKEN:
					case REMAINDER_TOKEN:
					case COMMA_TOKEN:
						assert_spaces(i, 1);
						break;
					case COLON_TOKEN:
					case EQUALS_TOKEN:
					case NOT_EQUALS_TOKEN:
					case ASSIGNMENT_TOKEN:
					case GREATER_OR_EQUAL_TOKEN:
					case GREATER_TOKEN:
					case LESS_OR_EQUAL_TOKEN:
					case LESS_TOKEN:
					case AND_TOKEN:
					case OR_TOKEN:
					case NOT_TOKEN:
					case TRUE_TOKEN:
					case FALSE_TOKEN:
						break;
					case IF_TOKEN:
						assert_spaces(i, depth * SPACES_PER_INDENT);
						break;
					case ELSE_TOKEN:
						assert_spaces(i, 1);
						break;
					case LOOP_TOKEN:
					case BREAK_TOKEN:
					case RETURN_TOKEN:
					case CONTINUE_TOKEN:
						assert_spaces(i, depth * SPACES_PER_INDENT);
						break;
					case SPACES_TOKEN:
						grug_error(UNREACHABLE_STR);
					case NEWLINES_TOKEN:
						grug_error("Unexpected trailing whitespace '%s' at token index %zu", token.str, i);
					case STRING_TOKEN:
						break;
					case PERIOD_TOKEN:
						assert_spaces(i, depth * SPACES_PER_INDENT);
						break;
					case WORD_TOKEN:
						break;
					case NUMBER_TOKEN:
						break;
					case COMMENT_TOKEN:
						// TODO: Ideally we'd assert there only ever being 1 space,
						// but the problem is that a standalone comment is allowed to have indentation
						// assert_spaces(i, 1);

						grug_assert(strlen(next_token.str) >= 2 && next_token.str[1] == ' ', "Expected the comment token '%s' to start with a space character at token index %zu", next_token.str, i + 1);

						grug_assert(strlen(next_token.str) >= 3 && !isspace(next_token.str[2]), "Expected the comment token '%s' to have a text character directly after the space at token index %zu", next_token.str, i + 1);

						grug_assert(!isspace(next_token.str[strlen(next_token.str) - 1]), "Unexpected trailing whitespace in the comment token '%s' at token index %zu", next_token.str, i + 1);

						break;
				}
				break;
			}
			case NEWLINES_TOKEN:
			case STRING_TOKEN:
			case PERIOD_TOKEN:
			case WORD_TOKEN:
			case NUMBER_TOKEN:
			case COMMENT_TOKEN:
				break;
		}

		// We're trimming all spaces in a single pass by copying every
		// non-space token to the start
		if (token.type != SPACES_TOKEN) {
			tokens[new_index] = token;
			new_index++;
		}

		i++;
	}

	grug_assert(depth == 0, "There were more '{' than '}'");

	tokens_size = new_index;
}

//// PARSING

struct literal_expr {
	union {
		char *string;
		i32 i32; // TODO: Support other number types
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
	char *fn_name;
	struct expr *arguments;
	size_t argument_count;
};

struct expr {
	enum {
		TRUE_EXPR,
		FALSE_EXPR,
		STRING_EXPR,
		IDENTIFIER_EXPR,
		NUMBER_EXPR,
		UNARY_EXPR,
		BINARY_EXPR,
		LOGICAL_EXPR,
		CALL_EXPR,
		PARENTHESIZED_EXPR,
	} type;
	union {
		struct literal_expr literal;
		struct unary_expr unary;
		struct binary_expr binary;
		struct call_expr call;
		struct expr *parenthesized;
	};
};
static char *get_expr_type_str[] = {
	[TRUE_EXPR] = "TRUE_EXPR",
	[FALSE_EXPR] = "FALSE_EXPR",
	[STRING_EXPR] = "STRING_EXPR",
	[IDENTIFIER_EXPR] = "IDENTIFIER_EXPR",
	[NUMBER_EXPR] = "NUMBER_EXPR",
	[UNARY_EXPR] = "UNARY_EXPR",
	[BINARY_EXPR] = "BINARY_EXPR",
	[LOGICAL_EXPR] = "LOGICAL_EXPR",
	[CALL_EXPR] = "CALL_EXPR",
	[PARENTHESIZED_EXPR] = "PARENTHESIZED_EXPR",
};
static struct expr exprs[MAX_EXPRS_IN_FILE];
static size_t exprs_size;

struct field {
	char *key;
	struct expr expr_value;
};
static struct field fields[MAX_FIELDS_IN_FILE];
static size_t fields_size;

struct compound_literal {
	struct field *fields;
	size_t field_count;
};

struct variable_statement {
	char *name;
	char *type;
	bool has_type;
	struct expr *assignment_expr;
	bool has_assignment;
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

struct loop_statement {
	struct statement *body_statements;
	size_t body_statement_count;
};

struct statement {
	enum {
		VARIABLE_STATEMENT,
		CALL_STATEMENT,
		IF_STATEMENT,
		RETURN_STATEMENT,
		LOOP_STATEMENT,
		BREAK_STATEMENT,
		CONTINUE_STATEMENT,
	} type;
	union {
		struct variable_statement variable_statement;
		struct call_statement call_statement;
		struct if_statement if_statement;
		struct return_statement return_statement;
		struct loop_statement loop_statement;
	};
};
static char *get_statement_type_str[] = {
	[VARIABLE_STATEMENT] = "VARIABLE_STATEMENT",
	[CALL_STATEMENT] = "CALL_STATEMENT",
	[IF_STATEMENT] = "IF_STATEMENT",
	[RETURN_STATEMENT] = "RETURN_STATEMENT",
	[LOOP_STATEMENT] = "LOOP_STATEMENT",
	[BREAK_STATEMENT] = "BREAK_STATEMENT",
	[CONTINUE_STATEMENT] = "CONTINUE_STATEMENT",
};
static struct statement statements[MAX_STATEMENTS_IN_FILE];
static size_t statements_size;

struct argument {
	char *type;
	char *name;
};
static struct argument arguments[MAX_ARGUMENTS_IN_FILE];
static size_t arguments_size;

struct parsed_define_fn {
	char *return_type;
	struct compound_literal returned_compound_literal;
};
static struct parsed_define_fn define_fn;

struct on_fn {
	char *fn_name;
	struct argument *arguments;
	size_t argument_count;
	struct statement *body_statements;
	size_t body_statement_count;
};
static struct on_fn on_fns[MAX_ON_FNS_IN_FILE];
static size_t on_fns_size;

struct helper_fn {
	char *fn_name;
	struct argument *arguments;
	size_t argument_count;
	char *return_type;
	struct statement *body_statements;
	size_t body_statement_count;
};
static struct helper_fn helper_fns[MAX_HELPER_FNS_IN_FILE];
static size_t helper_fns_size;

struct global_variable {
	char *name;
	enum type type;
	struct expr assignment_expr;
};
static struct global_variable global_variables[MAX_GLOBAL_VARIABLES_IN_FILE];
static size_t global_variables_size;

static void reset_parsing(void) {
	exprs_size = 0;
	fields_size = 0;
	statements_size = 0;
	arguments_size = 0;
	on_fns_size = 0;
	helper_fns_size = 0;
	global_variables_size = 0;
}

static void push_helper_fn(struct helper_fn helper_fn) {
	grug_assert(helper_fns_size < MAX_HELPER_FNS_IN_FILE, "There are more than %d helper_fns in the grug file, exceeding MAX_HELPER_FNS_IN_FILE", MAX_HELPER_FNS_IN_FILE);
	helper_fns[helper_fns_size++] = helper_fn;
}

static void push_on_fn(struct on_fn on_fn) {
	grug_assert(on_fns_size < MAX_ON_FNS_IN_FILE, "There are more than %d on_fns in the grug file, exceeding MAX_ON_FNS_IN_FILE", MAX_ON_FNS_IN_FILE);
	on_fns[on_fns_size++] = on_fn;
}

static struct statement *push_statement(struct statement statement) {
	grug_assert(statements_size < MAX_STATEMENTS_IN_FILE, "There are more than %d statements in the grug file, exceeding MAX_STATEMENTS_IN_FILE", MAX_STATEMENTS_IN_FILE);
	statements[statements_size] = statement;
	return statements + statements_size++;
}

static struct expr *push_expr(struct expr expr) {
	grug_assert(exprs_size < MAX_EXPRS_IN_FILE, "There are more than %d exprs in the grug file, exceeding MAX_EXPRS_IN_FILE", MAX_EXPRS_IN_FILE);
	exprs[exprs_size] = expr;
	return exprs + exprs_size++;
}

static void potentially_skip_comment(size_t *i) {
	struct token token = peek_token(*i);
	if (token.type == COMMENT_TOKEN) {
		(*i)++;
	}
}

static void consume_token_type(size_t *token_index_ptr, unsigned int expected_type) {
	assert_token_type((*token_index_ptr)++, expected_type);
}

static void consume_1_newline(size_t *token_index_ptr) {
	assert_token_type(*token_index_ptr, NEWLINES_TOKEN);

	struct token token = peek_token(*token_index_ptr);
	grug_assert(strlen(token.str) == 1, "Expected 1 newline, but got %zu at token index %zu", strlen(token.str), *token_index_ptr);

	(*token_index_ptr)++;
}

// TODO: Use, or remove
// Inspiration: https://stackoverflow.com/a/12923949/13279557
// static i64 str_to_i64(char *str) {
// 	char *end;
// 	errno = 0;
// 	long long n = strtoll(str, &end, 10);

// 	grug_assert(n <= INT64_MAX && !(errno == ERANGE && n == LLONG_MAX), "The number %s is too big for an i64, which has a maximum value of %d", str, INT64_MAX);

// 	// This function can't ever return a negative number,
// 	// since the minus symbol gets tokenized separately
// 	assert(errno != ERANGE);
// 	assert(n >= 0);

// 	// This function can't ever have trailing characters,
// 	// since the number was tokenized
// 	assert(*end == '\0');

// 	return n;
// }

// Inspiration: https://stackoverflow.com/a/12923949/13279557
static i32 str_to_i32(char *str) {
	char *end;
	errno = 0;
	long n = strtol(str, &end, 10);

	grug_assert(n <= INT32_MAX && !(errno == ERANGE && n == LONG_MAX), "The number %s is too big for an i32, which has a maximum value of %d", str, INT32_MAX);

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
	struct token token = peek_token(*i);

	struct expr expr = {0};

	switch (token.type) {
		case OPEN_PARENTHESIS_TOKEN:
			(*i)++;
			expr.type = PARENTHESIZED_EXPR;
			expr.parenthesized = push_expr(parse_expression(i));
			consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);
			return expr;
		case TRUE_TOKEN:
			(*i)++;
			expr.type = TRUE_EXPR;
			return expr;
		case FALSE_TOKEN:
			(*i)++;
			expr.type = FALSE_EXPR;
			return expr;
		case STRING_TOKEN:
			(*i)++;
			expr.type = STRING_EXPR;
			expr.literal.string = token.str;
			return expr;
		case WORD_TOKEN:
			(*i)++;
			expr.type = IDENTIFIER_EXPR;
			expr.literal.string = token.str;
			return expr;
		case NUMBER_TOKEN:
			(*i)++;
			expr.type = NUMBER_EXPR;
			expr.literal.i32 = str_to_i32(token.str);
			return expr;
		default:
			grug_error("Expected a primary expression token, but got token type %s at token index %zu", get_token_type_str[token.type], *i);
	}
}

static struct expr parse_call(size_t *i) {
	struct expr expr = parse_primary(i);

	struct token token = peek_token(*i);
	if (token.type == OPEN_PARENTHESIS_TOKEN) {
		(*i)++;

		grug_assert(expr.type == IDENTIFIER_EXPR, "Unexpected open parenthesis after non-identifier expression type %s at token index %zu", get_expr_type_str[expr.type], *i - 2);
		expr.type = CALL_EXPR;

		expr.call.fn_name = expr.literal.string;

		expr.call.argument_count = 0;

		token = peek_token(*i);
		if (token.type == CLOSE_PARENTHESIS_TOKEN) {
			(*i)++;
		} else {
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
			}

			expr.call.arguments = exprs + exprs_size;
			for (size_t argument_index = 0; argument_index < expr.call.argument_count; argument_index++) {
				push_expr(local_call_arguments[argument_index]);
			}
		}
	}

	return expr;
}

static struct expr parse_member(size_t *i) {
	struct expr expr = parse_call(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != PERIOD_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = PERIOD_TOKEN;
		expr.binary.right_expr = push_expr(parse_call(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static struct expr parse_unary(size_t *i) {
	struct token token = peek_token(*i);
	if (token.type == MINUS_TOKEN
	 || token.type == NOT_TOKEN) {
		(*i)++;
		struct expr expr = {0};

		expr.unary.operator = token.type;
		expr.unary.expr = push_expr(parse_unary(i));
		expr.type = UNARY_EXPR;

		return expr;
	}

	return parse_member(i);
}

static struct expr parse_factor(size_t *i) {
	struct expr expr = parse_unary(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != MULTIPLICATION_TOKEN
		 && token.type != DIVISION_TOKEN
		 && token.type != REMAINDER_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = token.type;
		expr.binary.right_expr = push_expr(parse_unary(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static struct expr parse_term(size_t *i) {
	struct expr expr = parse_factor(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != PLUS_TOKEN
		 && token.type != MINUS_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = token.type;
		expr.binary.right_expr = push_expr(parse_factor(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static struct expr parse_comparison(size_t *i) {
	struct expr expr = parse_term(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != GREATER_OR_EQUAL_TOKEN
		 && token.type != GREATER_TOKEN
		 && token.type != LESS_OR_EQUAL_TOKEN
		 && token.type != LESS_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = token.type;
		expr.binary.right_expr = push_expr(parse_term(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static struct expr parse_equality(size_t *i) {
	struct expr expr = parse_comparison(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != EQUALS_TOKEN
		 && token.type != NOT_EQUALS_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = token.type;
		expr.binary.right_expr = push_expr(parse_comparison(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static struct expr parse_and(size_t *i) {
	struct expr expr = parse_equality(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != AND_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = token.type;
		expr.binary.right_expr = push_expr(parse_equality(i));
		expr.type = LOGICAL_EXPR;
	}

	return expr;
}

static struct expr parse_or(size_t *i) {
	struct expr expr = parse_and(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type != OR_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary.left_expr = push_expr(expr);
		expr.binary.operator = token.type;
		expr.binary.right_expr = push_expr(parse_and(i));
		expr.type = LOGICAL_EXPR;
	}

	return expr;
}

// Recursive descent parsing inspired by the book Crafting Interpreters:
// https://craftinginterpreters.com/parsing-expressions.html#recursive-descent-parsing
static struct expr parse_expression(size_t *i) {
	return parse_or(i);
}

static struct statement *parse_statements(size_t *i, size_t *body_statement_count);

static struct statement parse_if_statement(size_t *i) {
	struct statement statement = {0};
	statement.type = IF_STATEMENT;
	statement.if_statement.condition = parse_expression(i);

	statement.if_statement.if_body_statements = parse_statements(i, &statement.if_statement.if_body_statement_count);

	if (peek_token(*i).type == ELSE_TOKEN) {
		(*i)++;

		if (peek_token(*i).type == IF_TOKEN) {
			(*i)++;

			statement.if_statement.else_body_statement_count = 1;

			struct statement else_if_statement = parse_if_statement(i);
			statement.if_statement.else_body_statements = push_statement(else_if_statement);
		} else {
			statement.if_statement.else_body_statements = parse_statements(i, &statement.if_statement.else_body_statement_count);
		}
	}

	return statement;
}

static struct variable_statement parse_variable_statement(size_t *i) {
	struct variable_statement variable_statement = {0};

	struct token name_token = consume_token(i);
	variable_statement.name = name_token.str;

	struct token token = peek_token(*i);
	if (token.type == COLON_TOKEN) {
		(*i)++;

		struct token type_token = consume_token(i);
		grug_assert(type_token.type == WORD_TOKEN, "Expected a word token after the colon at token index %zu", *i - 3);

		variable_statement.has_type = true;
		variable_statement.type = type_token.str;
	}

	token = peek_token(*i);
	if (token.type == ASSIGNMENT_TOKEN) {
		(*i)++;
		variable_statement.has_assignment = true;
		variable_statement.assignment_expr = push_expr(parse_expression(i));
	}

	return variable_statement;
}

static void push_global_variable(struct global_variable global_variable) {
	grug_assert(global_variables_size < MAX_GLOBAL_VARIABLES_IN_FILE, "There are more than %d global variables in the grug file, exceeding MAX_GLOBAL_VARIABLES_IN_FILE", MAX_GLOBAL_VARIABLES_IN_FILE);
	global_variables[global_variables_size++] = global_variable;
}

static void parse_global_variable(size_t *i) {
	struct global_variable global_variable = {0};

	struct token name_token = consume_token(i);
	global_variable.name = name_token.str;

	assert_token_type(*i, COLON_TOKEN);
	consume_token(i);

	assert_token_type(*i, WORD_TOKEN);
	struct token type_token = consume_token(i);
	global_variable.type = parse_type(type_token.str);

	assert_token_type(*i, ASSIGNMENT_TOKEN);
	consume_token(i);

	global_variable.assignment_expr = parse_expression(i);

	push_global_variable(global_variable);
}

static struct statement parse_statement(size_t *i) {
	struct token switch_token = peek_token(*i);

	struct statement statement = {0};
	switch (switch_token.type) {
		case WORD_TOKEN: {
			struct token token = peek_token(*i + 1);
			if (token.type == OPEN_PARENTHESIS_TOKEN) {
				statement.type = CALL_STATEMENT;
				struct expr expr = parse_call(i);
				statement.call_statement.expr = push_expr(expr);
			} else if (token.type == COLON_TOKEN || token.type == ASSIGNMENT_TOKEN) {
				statement.type = VARIABLE_STATEMENT;
				statement.variable_statement = parse_variable_statement(i);
			} else {
				grug_error("Expected '(' or ':' or ' =' after the word '%s' at token index %zu", switch_token.str, *i);
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
			if (token.type == NEWLINES_TOKEN) {
				statement.return_statement.has_value = false;
			} else {
				statement.return_statement.has_value = true;
				statement.return_statement.value = push_expr(parse_expression(i));
			}

			break;
		}
		case LOOP_TOKEN:
			(*i)++;
			statement.type = LOOP_STATEMENT;
			statement.loop_statement.body_statements = parse_statements(i, &statement.loop_statement.body_statement_count);
			break;
		case BREAK_TOKEN:
			(*i)++;
			statement.type = BREAK_STATEMENT;
			break;
		case CONTINUE_TOKEN:
			(*i)++;
			statement.type = CONTINUE_STATEMENT;
			break;
		default:
			grug_error("Expected a statement token, but got token type %s at token index %zu", get_token_type_str[switch_token.type], *i - 1);
	}

	return statement;
}

static struct statement *parse_statements(size_t *i, size_t *body_statement_count) {
	consume_token_type(i, OPEN_BRACE_TOKEN);
	potentially_skip_comment(i);

	consume_1_newline(i);

	// This local array is necessary, cause an IF or LOOP substatement can contain its own statements
	struct statement local_statements[MAX_STATEMENTS_PER_STACK_FRAME];
	*body_statement_count = 0;

	while (true) {
		struct token token = peek_token(*i);
		if (token.type == CLOSE_BRACE_TOKEN) {
			break;
		}

		if (token.type != COMMENT_TOKEN) {
			struct statement statement = parse_statement(i);

			grug_assert(*body_statement_count < MAX_STATEMENTS_PER_STACK_FRAME, "There are more than %d statements in one of the grug file's stack frames, exceeding MAX_STATEMENTS_PER_STACK_FRAME", MAX_STATEMENTS_PER_STACK_FRAME);
			local_statements[(*body_statement_count)++] = statement;
		}
		potentially_skip_comment(i);

		consume_token_type(i, NEWLINES_TOKEN);
	}

	struct statement *first_statement = statements + statements_size;
	for (size_t statement_index = 0; statement_index < *body_statement_count; statement_index++) {
		push_statement(local_statements[statement_index]);
	}

	consume_token_type(i, CLOSE_BRACE_TOKEN);

	if (peek_token(*i).type != ELSE_TOKEN) {
		potentially_skip_comment(i);
	}

	return first_statement;
}

static struct argument *push_argument(struct argument argument) {
	grug_assert(arguments_size < MAX_ARGUMENTS_IN_FILE, "There are more than %d arguments in the grug file, exceeding MAX_ARGUMENTS_IN_FILE", MAX_ARGUMENTS_IN_FILE);
	arguments[arguments_size] = argument;
	return arguments + arguments_size++;
}

static struct argument *parse_arguments(size_t *i, size_t *argument_count) {
	struct token token = consume_token(i);
	struct argument argument = {.name = token.str};

	consume_token_type(i, COLON_TOKEN);

	assert_token_type(*i, WORD_TOKEN);
	token = consume_token(i);

	argument.type = token.str;
	struct argument *first_argument = push_argument(argument);
	(*argument_count)++;

	// Every argument after the first one starts with a comma
	while (true) {
		token = peek_token(*i);
		if (token.type != COMMA_TOKEN) {
			break;
		}
		(*i)++;

		assert_token_type(*i, WORD_TOKEN);
		token = consume_token(i);
		argument.name = token.str;

		consume_token_type(i, COLON_TOKEN);

		assert_token_type(*i, WORD_TOKEN);
		token = consume_token(i);
		argument.type = token.str;
		push_argument(argument);
		(*argument_count)++;
	}

	return first_argument;
}

static void parse_helper_fn(size_t *i) {
	struct helper_fn fn = {0};

	struct token token = consume_token(i);
	fn.fn_name = token.str;

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		fn.arguments = parse_arguments(i, &fn.argument_count);
	}

	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		(*i)++;
		fn.return_type = token.str;
	}

	fn.body_statements = parse_statements(i, &fn.body_statement_count);

	push_helper_fn(fn);
}

static void parse_on_fn(size_t *i) {
	struct on_fn fn = {0};

	struct token token = consume_token(i);
	fn.fn_name = token.str;

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		fn.arguments = parse_arguments(i, &fn.argument_count);
	}

	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	fn.body_statements = parse_statements(i, &fn.body_statement_count);

	push_on_fn(fn);
}

static void push_field(struct field field) {
	grug_assert(fields_size < MAX_FIELDS_IN_FILE, "There are more than %d fields in the grug file, exceeding MAX_FIELDS_IN_FILE", MAX_FIELDS_IN_FILE);
	fields[fields_size++] = field;
}

static struct compound_literal parse_compound_literal(size_t *i) {
	(*i)++;
	potentially_skip_comment(i);

	struct compound_literal compound_literal = {.fields = fields + fields_size};

	consume_1_newline(i);

	while (true) {
		struct token token = peek_token(*i);
		if (token.type == CLOSE_BRACE_TOKEN) {
			break;
		}

		consume_token_type(i, PERIOD_TOKEN);

		assert_token_type(*i, WORD_TOKEN);
		token = peek_token(*i);
		struct field field = {.key = token.str};
		(*i)++;

		consume_token_type(i, ASSIGNMENT_TOKEN);

		token = peek_token(*i);
		grug_assert(token.type == STRING_TOKEN || token.type == NUMBER_TOKEN, "Expected token type STRING_TOKEN or NUMBER_TOKEN, but got %s at token index %zu", get_token_type_str[token.type], *i);
		field.expr_value = parse_expression(i);
		push_field(field);
		compound_literal.field_count++;

		consume_token_type(i, COMMA_TOKEN);
		potentially_skip_comment(i);

		consume_1_newline(i);
	}

	consume_token_type(i, CLOSE_BRACE_TOKEN);
	potentially_skip_comment(i);

	consume_1_newline(i);

	return compound_literal;
}

static void parse_define_fn(size_t *i) {
	// Parse the function's signature
	consume_token(i); // The function name is always "define"

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);
	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	assert_token_type(*i, WORD_TOKEN);
	struct token token = consume_token(i);
	define_fn.return_type = token.str;

	consume_token_type(i, OPEN_BRACE_TOKEN);
	potentially_skip_comment(i);

	consume_1_newline(i);

	// Parse the body of the function
	consume_token_type(i, RETURN_TOKEN);

	assert_token_type(*i, OPEN_BRACE_TOKEN);
	define_fn.returned_compound_literal = parse_compound_literal(i);

	consume_token_type(i, CLOSE_BRACE_TOKEN);
	potentially_skip_comment(i);
}

static void parse_global_resources_fn(size_t *i) {
	consume_token(i); // The function name is always "global_resources"
	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);
	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);
	consume_token_type(i, WORD_TOKEN);
	consume_token_type(i, OPEN_BRACE_TOKEN);
	potentially_skip_comment(i);
	consume_1_newline(i);
	consume_token_type(i, RETURN_TOKEN);
	assert_token_type(*i, OPEN_BRACE_TOKEN);
	parse_compound_literal(i);
	consume_token_type(i, CLOSE_BRACE_TOKEN);
	potentially_skip_comment(i);
}

static void parse(void) {
	reset_parsing();

	bool seen_define_fn = false;
	bool seen_global_resources_fn = false;

	size_t i = 0;
	while (i < tokens_size) {
		struct token token = peek_token(i);
		int type = token.type;

		if (       type == WORD_TOKEN && streq(token.str, "global_resources") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(!seen_global_resources_fn, "There can't be more than one global_resources function in a grug file");
			grug_assert(!seen_define_fn, "Move the define_ function below the global_resources function");
			parse_global_resources_fn(&i);
			seen_global_resources_fn = true;
		} else if (type == WORD_TOKEN && streq(token.str, "define") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(!seen_define_fn, "There can't be more than one define_ function in a grug file");
			parse_define_fn(&i);
			seen_define_fn = true;
		} else if (type == WORD_TOKEN && starts_with(token.str, "on_") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(seen_define_fn, "Move the on_ function '%s' below the define_ function", token.str);
			parse_on_fn(&i);
		} else if (type == WORD_TOKEN && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			parse_helper_fn(&i);
		} else if (type == WORD_TOKEN && peek_token(i + 1).type == COLON_TOKEN) {
			grug_assert(seen_define_fn, "Move the global variable '%s' below the define_ function", token.str);
			parse_global_variable(&i);
		} else if (type == COMMENT_TOKEN) {
			i++;
		} else if (type == NEWLINES_TOKEN) {
			i++;
		} else {
			grug_error("Unexpected token '%s' at token index %zu in parse()", token.str, i);
		}
	}

	grug_assert(seen_define_fn, "Every grug file requires exactly one define_ function");
}

//// PRINTING AST

static void print_expr(struct expr expr);

static void print_parenthesized_expr(struct expr *parenthesized_expr) {
	grug_log("\"expr\":{");
	print_expr(*parenthesized_expr);
	grug_log("}");
}

static void print_call_expr(struct call_expr call_expr) {
	grug_log("\"fn_name\":\"%s\",", call_expr.fn_name);

	grug_log("\"arguments\":[");
	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		if (argument_index > 0) {
			grug_log(",");
		}
		grug_log("{");
		print_expr(call_expr.arguments[argument_index]);
		grug_log("}");
	}
	grug_log("]");
}

static void print_binary_expr(struct binary_expr binary_expr) {
	grug_log("\"left_expr\":{");
	print_expr(*binary_expr.left_expr);
	grug_log("},");
	grug_log("\"operator\":\"%s\",", get_token_type_str[binary_expr.operator]);
	grug_log("\"right_expr\":{");
	print_expr(*binary_expr.right_expr);
	grug_log("}");
}

static void print_expr(struct expr expr) {
	grug_log("\"type\":\"%s\"", get_expr_type_str[expr.type]);

	switch (expr.type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
			break;
		case STRING_EXPR:
		case IDENTIFIER_EXPR:
			grug_log(",");
			grug_log("\"str\":\"%s\"", expr.literal.string);
			break;
		case NUMBER_EXPR:
			grug_log(",");
			grug_log("\"value\":%d", expr.literal.i32);
			break;
		case UNARY_EXPR:
			grug_log(",");
			grug_log("\"operator\":\"%s\",", get_token_type_str[expr.unary.operator]);
			grug_log("\"expr\":{");
			print_expr(*expr.unary.expr);
			grug_log("}");
			break;
		case BINARY_EXPR:
		case LOGICAL_EXPR:
			grug_log(",");
			print_binary_expr(expr.binary);
			break;
		case CALL_EXPR:
			grug_log(",");
			print_call_expr(expr.call);
			break;
		case PARENTHESIZED_EXPR:
			grug_log(",");
			print_parenthesized_expr(expr.parenthesized);
			break;
	}
}

static void print_statements(struct statement *statements_offset, size_t statement_count) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		if (statement_index > 0) {
			grug_log(",");
		}

		grug_log("{");

		struct statement statement = statements_offset[statement_index];

		grug_log("\"type\":\"%s\"", get_statement_type_str[statement.type]);

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				grug_log(",");
				grug_log("\"variable_name\":\"%s\",", statement.variable_statement.name);

				if (statement.variable_statement.has_type) {
					grug_log("\"variable_type\":\"%s\",", statement.variable_statement.type);
				}

				if (statement.variable_statement.has_assignment) {
					grug_log("\"assignment\":{");
					print_expr(*statement.variable_statement.assignment_expr);
					grug_log("}");
				}

				break;
			case CALL_STATEMENT:
				grug_log(",");
				print_call_expr(statement.call_statement.expr->call);
				break;
			case IF_STATEMENT:
				grug_log(",");
				grug_log("\"condition\":{");
				print_expr(statement.if_statement.condition);
				grug_log("},");

				grug_log("\"if_statements\":[");
				print_statements(statement.if_statement.if_body_statements, statement.if_statement.if_body_statement_count);
				grug_log("],");

				if (statement.if_statement.else_body_statement_count > 0) {
					grug_log("\"else_statements\":[");
					print_statements(statement.if_statement.else_body_statements, statement.if_statement.else_body_statement_count);
					grug_log("]");
				}

				break;
			case RETURN_STATEMENT:
				if (statement.return_statement.has_value) {
					struct expr return_expr = *statement.return_statement.value;
					grug_log(",");
					grug_log("\"expr\":{");
					print_expr(return_expr);
					grug_log("}");
				}
				break;
			case LOOP_STATEMENT:
				grug_log(",");
				grug_log("\"statements\":[");
				print_statements(statement.loop_statement.body_statements, statement.loop_statement.body_statement_count);
				grug_log("]");
				break;
			case BREAK_STATEMENT:
				break;
			case CONTINUE_STATEMENT:
				break;
		}

		grug_log("}");
	}
}

static void print_arguments(struct argument *arguments_offset, size_t argument_count) {
	grug_log("\"arguments\":[");

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		if (argument_index > 0) {
			grug_log(",");
		}

		grug_log("{");

		struct argument arg = arguments_offset[argument_index];

		grug_log("\"name\":\"%s\",", arg.name);
		grug_log("\"type\":\"%s\"", arg.type);

		grug_log("}");
	}

	grug_log("]");
}

static void print_helper_fns(void) {
	grug_log("\"helper_fns\":[");

	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
		if (fn_index > 0) {
			grug_log(",");
		}

		grug_log("{");

		struct helper_fn fn = helper_fns[fn_index];

		grug_log("\"fn_name\":\"%s\",", fn.fn_name);

		print_arguments(fn.arguments, fn.argument_count);

		grug_log(",");
		if (fn.return_type) {
			grug_log("\"return_type\":\"%s\",", fn.return_type);
		}

		grug_log("\"statements\":[");
		print_statements(fn.body_statements, fn.body_statement_count);
		grug_log("]");

		grug_log("}");
	}

	grug_log("]");
}

static void print_on_fns(void) {
	grug_log("\"on_fns\":[");

	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
		if (fn_index > 0) {
			grug_log(",");
		}

		grug_log("{");

		struct on_fn fn = on_fns[fn_index];

		grug_log("\"fn_name\":\"%s\",", fn.fn_name);

		print_arguments(fn.arguments, fn.argument_count);

		grug_log(",");
		grug_log("\"statements\":[");
		print_statements(fn.body_statements, fn.body_statement_count);
		grug_log("]");

		grug_log("}");
	}

	grug_log("]");
}

static void print_global_variables(void) {
	grug_log("\"global_variables\":{");

	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
		if (global_variable_index > 0) {
			grug_log(",");
		}

		struct global_variable global_variable = global_variables[global_variable_index];

		grug_log("\"%s\":{", global_variable.name);

		grug_log("\"type\":\"%s\",", type_names[global_variable.type]);

		grug_log("\"assignment\":{");
		print_expr(global_variable.assignment_expr);
		grug_log("}");

		grug_log("}");
	}

	grug_log("}");
}

static void print_fields(struct compound_literal compound_literal) {
	grug_log("\"fields\":[");

	for (size_t field_index = 0; field_index < compound_literal.field_count; field_index++) {
		if (field_index > 0) {
			grug_log(",");
		}

		grug_log("{");

		struct field field = compound_literal.fields[field_index];

		grug_log("\"name\":\"%s\",", field.key);

		grug_log("\"value\":{");
		print_expr(field.expr_value);
		grug_log("}");

		grug_log("}");
	}

	grug_log("]");
}

static void print_define_fn(void) {
	grug_log("\"entity\":{");

	grug_log("\"name\":\"%s\",", define_fn.return_type);

	print_fields(define_fn.returned_compound_literal);

	grug_log("}");
}

static void print_ast(void) {
	grug_log("{");

	print_define_fn();
	grug_log(",");
	print_global_variables();
	grug_log(",");
	print_on_fns();
	grug_log(",");
	print_helper_fns();

	grug_log("}\n");
}

//// COMPILING

#define MAX_SYMBOLS 420420
#define MAX_CODES 420420
#define MAX_DATA_STRINGS 420420
#define MAX_DATA_STRING_CODES 420420
#define MAX_GAME_FN_CALLS 420420
#define MAX_HELPER_FN_CALLS 420420
#define MAX_USED_GAME_FNS 420
#define MAX_HELPER_FN_OFFSETS 420420
#define MAX_STACK_SIZE 420420

// 0xDEADBEEF in little-endian
#define PLACEHOLDER_16 0xADDE
#define PLACEHOLDER_32 0xEFBEADDE
#define PLACEHOLDER_64 0xEFBEADDEEFBEADDE

enum code {
	CALL = 0xe8, // call foo
	RET = 0xc3, // ret
	MOV_TO_RDI_PTR = 0x47c7, // mov dword [rdi+offset], n

	PUSH_RAX = 0x50, // push rax

	ADD_RBX_TO_RAX = 0xd80148, // add rax, rbx
	SUBTRACT_RBX_FROM_RAX = 0xd82948, // sub rax, rbx
	MULTIPLY_RAX_BY_RBX = 0xebf748, // imul rbx

	CQO_CLEAR_BEFORE_DIVISION = 0x9948, // cqo
	DIVIDE_RAX_BY_RBX = 0xfbf748, // idiv rbx
	MOV_RDX_TO_RAX = 0xd08948, // mov rax, rdx

	CMP_RAX_WITH_RBX = 0xd83948, // cmp rax, rbx

	NEGATE_RAX = 0xd8f748, // neg rax

	TEST_RAX_IS_ZERO = 0xc08548, // test rax, rax

	JE_32_BIT_OFFSET = 0x840f, // je strict $+0xn
	JNE_32_BIT_OFFSET = 0x850f, // jne strict $+0xn
	JMP_32_BIT_OFFSET = 0xe9, // jmp $+0xn

	SETE_AL = 0xc0940f, // sete al
	SETNE_AL = 0xc0950f, // setne al
	SETGT_AL = 0xc09f0f, // setg al
	SETGE_AL = 0xc09d0f, // setge al
	SETLT_AL = 0xc09c0f, // setl al
	SETLE_AL = 0xc09e0f, // setle al

	POP_RBX = 0x5b, // pop rbx

	POP_RDI = 0x5f, // pop rdi
	POP_RSI = 0x5e, // pop rsi
	POP_RDX = 0x5a, // pop rdx
	POP_RCX = 0x59, // pop rcx
	POP_R8 = 0x5841, // pop r8
	POP_R9 = 0x5941, // pop r9

	XOR_CLEAR_EAX = 0xc031, // xor eax, eax
	MOV_1_TO_EAX = 0x1b8, // mov eax, 1

	MOV_TO_EAX = 0xb8, // mov eax, n

	MOVABS_TO_RDI = 0xbf48, // mov rdi, n
	MOVABS_TO_RSI = 0xbe48, // mov rsi, n
	MOVABS_TO_RDX = 0xba48, // mov rdx, n
	MOVABS_TO_RCX = 0xb948, // mov rcx, n
	MOVABS_TO_R8 = 0xb849, // mov r8, n
	MOVABS_TO_R9 = 0xb949, // mov r9, n

	LEA_TO_RDI = 0x3d8d48, // lea rdi, [rel strings+offset]
	LEA_TO_RSI = 0x358d48, // lea rsi, [rel strings+offset]
	LEA_TO_RDX = 0x158d48, // lea rdx, [rel strings+offset]
	LEA_TO_RCX = 0x0d8d48, // lea rcx, [rel strings+offset]
	LEA_TO_R8 = 0x058d4c, // lea r8, [rel strings+offset]
	LEA_TO_R9 = 0x0d8d4c, // lea r9, [rel strings+offset]
};

struct data_string_code {
	char *string;
	size_t code_offset;
};

static size_t text_offsets[MAX_SYMBOLS];

static u8 codes[MAX_CODES];
static size_t codes_size;

static char *define_fn_name;
static struct grug_entity *grug_define_entity;

static u32 buckets_define_on_fns[MAX_ON_FNS_IN_FILE];
static u32 chains_define_on_fns[MAX_ON_FNS_IN_FILE];

static char *data_strings[MAX_DATA_STRINGS];
static size_t data_strings_size;

// TODO: Replace this 420 define with the total number of data strings in the entire file
#define MAX_BUCKETS_DATA_STRINGS 420
static u32 buckets_data_strings[MAX_DATA_STRINGS];
static u32 chains_data_strings[MAX_DATA_STRINGS];

static struct data_string_code data_string_codes[MAX_DATA_STRING_CODES];
static size_t data_string_codes_size;

struct fn_call {
	char *fn_name;
	size_t codes_offset;
};
static struct fn_call game_fn_calls[MAX_GAME_FN_CALLS];
static size_t game_fn_calls_size;
static struct fn_call helper_fn_calls[MAX_HELPER_FN_CALLS];
static size_t helper_fn_calls_size;

static u32 buckets_game_fns[MAX_GRUG_FUNCTIONS];
static u32 chains_game_fns[MAX_GRUG_FUNCTIONS];

static char *used_game_fns[MAX_USED_GAME_FNS];
static size_t used_game_fns_size;
static u32 buckets_used_game_fns[BFD_HASH_BUCKET_SIZE];
static u32 chains_used_game_fns[MAX_USED_GAME_FNS];

struct fn_offset {
	char *fn_name;
	size_t offset;
};

static struct fn_offset helper_fn_offsets[MAX_HELPER_FN_OFFSETS];
static size_t helper_fn_offsets_size;
static u32 buckets_helper_fn_offsets[MAX_HELPER_FN_OFFSETS];
static u32 chains_helper_fn_offsets[MAX_HELPER_FN_OFFSETS];

static size_t stack_size;

static void reset_compiling(void) {
	codes_size = 0;
	data_strings_size = 0;
	data_string_codes_size = 0;
	game_fn_calls_size = 0;
	helper_fn_calls_size = 0;
	used_game_fns_size = 0;
	helper_fn_offsets_size = 0;
}

static size_t get_helper_fn_offset(char *name) {
	u32 i = buckets_helper_fn_offsets[elf_hash(name) % helper_fn_offsets_size];

	while (1) {
		assert(i != UINT32_MAX && "get_helper_fn_offset() isn't supposed to ever fail");

		if (streq(name, helper_fn_offsets[i].fn_name)) {
			break;
		}

		i = chains_helper_fn_offsets[i];
	}

	return helper_fn_offsets[i].offset;
}

static void hash_helper_fn_offsets(void) {
	memset(buckets_helper_fn_offsets, UINT32_MAX, helper_fn_offsets_size * sizeof(u32));

	for (size_t i = 0; i < helper_fn_offsets_size; i++) {
		char *name = helper_fn_offsets[i].fn_name;

		u32 bucket_index = elf_hash(name) % helper_fn_offsets_size;

		chains_helper_fn_offsets[i] = buckets_helper_fn_offsets[bucket_index];

		buckets_helper_fn_offsets[bucket_index] = i;
	}
}

static void push_helper_fn_offset(char *fn_name, size_t offset) {
	grug_assert(helper_fn_offsets_size < MAX_HELPER_FN_OFFSETS, "There are more than %d helper functions, exceeding MAX_HELPER_FN_OFFSETS", MAX_HELPER_FN_OFFSETS);

	helper_fn_offsets[helper_fn_offsets_size++] = (struct fn_offset){
		.fn_name = fn_name,
		.offset = offset,
	};
}

static bool has_used_game_fn(char *name) {
	u32 i = buckets_used_game_fns[bfd_hash(name) % BFD_HASH_BUCKET_SIZE];

	while (1) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (streq(name, game_fn_calls[i].fn_name)) {
			break;
		}

		i = chains_used_game_fns[i];
	}

	return true;
}

static void hash_used_game_fns(void) {
	memset(buckets_used_game_fns, UINT32_MAX, BFD_HASH_BUCKET_SIZE * sizeof(u32));

	for (size_t i = 0; i < game_fn_calls_size; i++) {
		char *name = game_fn_calls[i].fn_name;

		if (has_used_game_fn(name)) {
			continue;
		}

		used_game_fns[used_game_fns_size] = name;

		u32 bucket_index = bfd_hash(name) % BFD_HASH_BUCKET_SIZE;

		chains_used_game_fns[used_game_fns_size] = buckets_used_game_fns[bucket_index];

		buckets_used_game_fns[bucket_index] = used_game_fns_size++;
	}
}

static bool is_game_fn(char *name) {
	u32 i = buckets_game_fns[elf_hash(name) % grug_game_functions_size];

	while (1) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (streq(name, grug_game_functions[i].name)) {
			break;
		}

		i = chains_game_fns[i];
	}

	return true;
}

static void hash_game_fns(void) {
	memset(buckets_game_fns, UINT32_MAX, grug_game_functions_size * sizeof(u32));

	for (size_t i = 0; i < grug_game_functions_size; i++) {
		char *name = grug_game_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_game_functions_size;

		chains_game_fns[i] = buckets_game_fns[bucket_index];

		buckets_game_fns[bucket_index] = i;
	}
}

static void push_helper_fn_call(char *fn_name, size_t codes_offset) {
	grug_assert(helper_fn_calls_size < MAX_HELPER_FN_CALLS, "There are more than %d helper function calls, exceeding MAX_HELPER_FN_CALLS", MAX_HELPER_FN_CALLS);

	helper_fn_calls[helper_fn_calls_size++] = (struct fn_call){
		.fn_name = fn_name,
		.codes_offset = codes_offset,
	};
}

static void push_game_fn_call(char *fn_name, size_t codes_offset) {
	grug_assert(game_fn_calls_size < MAX_GAME_FN_CALLS, "There are more than %d game function calls, exceeding MAX_GAME_FN_CALLS", MAX_GAME_FN_CALLS);

	game_fn_calls[game_fn_calls_size++] = (struct fn_call){
		.fn_name = fn_name,
		.codes_offset = codes_offset,
	};
}

static void push_data_string_code(char *string, size_t code_offset) {
	grug_assert(data_string_codes_size < MAX_DATA_STRING_CODES, "There are more than %d data string code bytes, exceeding MAX_DATA_STRING_CODES", MAX_DATA_STRING_CODES);

	data_string_codes[data_string_codes_size++] = (struct data_string_code){
		.string = string,
		.code_offset = code_offset,
	};
}

static void compile_push_byte(u8 byte) {
	grug_assert(codes_size < MAX_CODES, "There are more than %d code bytes, exceeding MAX_CODES", MAX_CODES);

	codes[codes_size++] = byte;
}

static void compile_push_number(u64 n, size_t byte_count) {
	for (; byte_count-- > 0; n >>= 8) {
		compile_push_byte(n & 0xff); // Little-endian
	}
}

static void stack_pop_arguments(size_t argument_count) {
	if (argument_count == 0) {
		return;
	}

	// TODO: Support more arguments
	grug_assert(argument_count <= 6, "Currently grug only supports up to 6 function arguments");

	assert(stack_size >= argument_count);
	stack_size -= argument_count;

	switch (argument_count) {
		case 6:
			compile_push_number(POP_R9, 2);
			__attribute__((__fallthrough__));
		case 5:
			compile_push_number(POP_R8, 2);
			__attribute__((__fallthrough__));
		case 4:
			compile_push_byte(POP_RCX);
			__attribute__((__fallthrough__));
		case 3:
			compile_push_byte(POP_RDX);
			__attribute__((__fallthrough__));
		case 2:
			compile_push_byte(POP_RSI);
			__attribute__((__fallthrough__));
		case 1:
			compile_push_byte(POP_RDI);
			return;
		default:
			grug_error(UNREACHABLE_STR);
	}
}

static void overwrite_jmp_address(size_t jump_address, size_t size) {
	size_t byte_count = 4;
	for (u32 n = size - (jump_address + byte_count); byte_count > 0; n >>= 8, byte_count--) {
		codes[jump_address++] = n & 0xff; // Little-endian
	}
}

static void stack_pop_rbx(void) {
	assert(stack_size > 0);
	--stack_size;

	compile_push_byte(POP_RBX);
}

static void stack_push_rax(void) {
	grug_assert(stack_size < MAX_STACK_SIZE, "There are more than %d stack values, exceeding MAX_STACK_SIZE", MAX_STACK_SIZE);
	stack_size++;

	compile_push_byte(PUSH_RAX);
}

static void compile_expr(struct expr expr);

static void compile_logical_expr(struct binary_expr logical_expr) {
	switch (logical_expr.operator) {
		case AND_TOKEN: {
			compile_expr(*logical_expr.left_expr);
			compile_push_number(TEST_RAX_IS_ZERO, 3);
			compile_push_number(JNE_32_BIT_OFFSET, 2);
			size_t expr_1_is_true_jump_offset = codes_size;
			compile_push_number(PLACEHOLDER_32, 4);
			compile_push_number(JMP_32_BIT_OFFSET, 1);
			size_t end_jump_offset = codes_size;
			compile_push_number(PLACEHOLDER_32, 4);
			overwrite_jmp_address(expr_1_is_true_jump_offset, codes_size);
			compile_expr(*logical_expr.right_expr);
			compile_push_number(TEST_RAX_IS_ZERO, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETNE_AL, 3);
			overwrite_jmp_address(end_jump_offset, codes_size);
			break;
		}
		case OR_TOKEN: {
			compile_expr(*logical_expr.left_expr);
			compile_push_number(TEST_RAX_IS_ZERO, 3);
			compile_push_number(JE_32_BIT_OFFSET, 2);
			size_t expr_1_is_false_jump_offset = codes_size;
			compile_push_number(PLACEHOLDER_32, 4);
			compile_push_number(MOV_1_TO_EAX, 5);
			compile_push_number(JMP_32_BIT_OFFSET, 1);
			size_t end_jump_offset = codes_size;
			compile_push_number(PLACEHOLDER_32, 4);
			overwrite_jmp_address(expr_1_is_false_jump_offset, codes_size);
			compile_expr(*logical_expr.right_expr);
			compile_push_number(TEST_RAX_IS_ZERO, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETNE_AL, 3);
			overwrite_jmp_address(end_jump_offset, codes_size);
			break;
		}
		default:
			grug_error(UNREACHABLE_STR);
	}
}

static void compile_binary_expr(struct binary_expr binary_expr) {
	compile_expr(*binary_expr.right_expr);
	stack_push_rax();
	compile_expr(*binary_expr.left_expr);
	stack_pop_rbx();

	switch (binary_expr.operator) {
		case PLUS_TOKEN:
			compile_push_number(ADD_RBX_TO_RAX, 3);
			break;
		case MINUS_TOKEN:
			compile_push_number(SUBTRACT_RBX_FROM_RAX, 3);
			break;
		case MULTIPLICATION_TOKEN:
			compile_push_number(MULTIPLY_RAX_BY_RBX, 3);
			break;
		case DIVISION_TOKEN:
			compile_push_number(CQO_CLEAR_BEFORE_DIVISION, 2);
			compile_push_number(DIVIDE_RAX_BY_RBX, 3);
			break;
		case REMAINDER_TOKEN:
			compile_push_number(CQO_CLEAR_BEFORE_DIVISION, 2);
			compile_push_number(DIVIDE_RAX_BY_RBX, 3);
			compile_push_number(MOV_RDX_TO_RAX, 3);
			break;
		case EQUALS_TOKEN:
			compile_push_number(CMP_RAX_WITH_RBX, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETE_AL, 3);
			break;
		case NOT_EQUALS_TOKEN:
			compile_push_number(CMP_RAX_WITH_RBX, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETNE_AL, 3);
			break;
		case GREATER_OR_EQUAL_TOKEN:
			compile_push_number(CMP_RAX_WITH_RBX, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETGE_AL, 3);
			break;
		case GREATER_TOKEN:
			compile_push_number(CMP_RAX_WITH_RBX, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETGT_AL, 3);
			break;
		case LESS_OR_EQUAL_TOKEN:
			compile_push_number(CMP_RAX_WITH_RBX, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETLE_AL, 3);
			break;
		case LESS_TOKEN:
			compile_push_number(CMP_RAX_WITH_RBX, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETLT_AL, 3);
			break;
		default:
			grug_error(UNREACHABLE_STR);
	}
}

static void compile_unary_expr(struct unary_expr unary_expr) {
	switch (unary_expr.operator) {
		case MINUS_TOKEN:
			compile_expr(*unary_expr.expr);
			compile_push_number(NEGATE_RAX, 3);
			break;
		case NOT_TOKEN:
			compile_expr(*unary_expr.expr);
			compile_push_number(TEST_RAX_IS_ZERO, 3);
			compile_push_number(MOV_TO_EAX, 1);
			compile_push_number(0, 4);
			compile_push_number(SETE_AL, 3);
			break;
		default:
			grug_error(UNREACHABLE_STR);
	}
}

static void compile_expr(struct expr expr) {
	switch (expr.type) {
		case TRUE_EXPR:
			compile_push_number(MOV_1_TO_EAX, 5);
			break;
		case FALSE_EXPR:
			compile_push_number(XOR_CLEAR_EAX, 2);
			break;
		case STRING_EXPR:
			assert(false);
			// serialize_append_slice(expr.string_literal_expr.str, expr.string_literal_expr.len);
			// break;
		case IDENTIFIER_EXPR:
			assert(false);
			// if (is_identifier_global(expr.string_literal_expr.str, expr.string_literal_expr.len)) {
			// 	serialize_append("globals->");
			// }
			// serialize_append_slice(expr.string_literal_expr.str, expr.string_literal_expr.len);
			// break;
		case NUMBER_EXPR: {
			i32 n = expr.literal.i32;
			if (n == 0) {
				compile_push_number(XOR_CLEAR_EAX, 2);
			} else if (n == 1) {
				compile_push_number(MOV_1_TO_EAX, 5);
			} else {
				compile_push_number(MOV_TO_EAX, 1);
				compile_push_number(n, 4);
			}
			break;
		}
		case UNARY_EXPR:
			compile_unary_expr(expr.unary);
			break;
		case BINARY_EXPR:
			compile_binary_expr(expr.binary);
			break;
		case LOGICAL_EXPR:
			compile_logical_expr(expr.binary);
			break;
		case CALL_EXPR:
			assert(false);
			// serialize_call_expr(expr.call_expr);
			// break;
		case PARENTHESIZED_EXPR:
			compile_expr(*expr.parenthesized);
			break;
	}
}

static void compile_call_expr(struct call_expr call_expr) {
	for (size_t i = 0; i < call_expr.argument_count; i++) {
		struct expr argument = call_expr.arguments[i];

		// TODO: Verify that the argument has the same type as the one in grug_define_entity

		compile_expr(argument);
		stack_push_rax();
	}

	stack_pop_arguments(call_expr.argument_count);

	compile_push_byte(CALL);
	if (is_game_fn(call_expr.fn_name)) {
		push_game_fn_call(call_expr.fn_name, codes_size);
	} else {
		push_helper_fn_call(call_expr.fn_name, codes_size);
	}
	compile_push_number(PLACEHOLDER_32, 4);
}

static void compile_statements(struct statement *statements_offset, size_t statement_count) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		struct statement statement = statements_offset[statement_index];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				assert(false);
// 				if (statement.variable_statement.has_type) {
// 					serialize_append_slice(statement.variable_statement.type, statement.variable_statement.type_len);
// 					serialize_append(" ");
// 				}

// 				if (is_identifier_global(statement.variable_statement.name, statement.variable_statement.name_len)) {
// 					serialize_append("globals->");
// 				}
// 				serialize_append_slice(statement.variable_statement.name, statement.variable_statement.name_len);

// 				if (statement.variable_statement.has_assignment) {
// 					serialize_append(" = ");
// 					serialize_expr(exprs[statement.variable_statement.assignment_expr_index]);
// 				}

// 				serialize_append(";");

// 				break;
			case CALL_STATEMENT:
				compile_call_expr(statement.call_statement.expr->call);
				break;
			case IF_STATEMENT:
				assert(false);
// 				serialize_append("if (");
// 				serialize_expr(statement.if_statement.condition);
// 				serialize_append(") {\n");
// 				serialize_statements(statement.if_statement.if_body_statements_offset, statement.if_statement.if_body_statement_count, depth + 1);
				
// 				if (statement.if_statement.else_body_statement_count > 0) {
// 					serialize_append_indents(depth);
// 					serialize_append("} else {\n");
// 					serialize_statements(statement.if_statement.else_body_statements_offset, statement.if_statement.else_body_statement_count, depth + 1);
// 				}

// 				serialize_append_indents(depth);
// 				serialize_append("}");

// 				break;
			case RETURN_STATEMENT:
				assert(false);
// 				serialize_append("return");
// 				if (statement.return_statement.has_value) {
// 					serialize_append(" ");
// 					struct expr return_expr = exprs[statement.return_statement.value_expr_index];
// 					serialize_expr(return_expr);
// 				}
// 				serialize_append(";");
// 				break;
			case LOOP_STATEMENT:
				assert(false);
// 				serialize_append("while (true) {\n");
// 				serialize_statements(statement.loop_statement.body_statements_offset, statement.loop_statement.body_statement_count, depth + 1);
// 				serialize_append_indents(depth);
// 				serialize_append("}");
// 				break;
			case BREAK_STATEMENT:
				assert(false);
// 				serialize_append("break;");
// 				break;
			case CONTINUE_STATEMENT:
				assert(false);
// 				serialize_append("continue;");
// 				break;
		}
	}
}

static void compile_returned_field(struct expr expr_value, size_t argument_index) {
	if (expr_value.type == NUMBER_EXPR) {
		compile_push_number((uint64_t[]){
			MOVABS_TO_RDI,
			MOVABS_TO_RSI,
			MOVABS_TO_RDX,
			MOVABS_TO_RCX,
			MOVABS_TO_R8,
			MOVABS_TO_R9,
		}[argument_index], 2);

		compile_push_number(expr_value.literal.i32, 8);
	} else if (expr_value.type == STRING_EXPR) {
		compile_push_number((uint64_t[]){
			LEA_TO_RDI,
			LEA_TO_RSI,
			LEA_TO_RDX,
			LEA_TO_RCX,
			LEA_TO_R8,
			LEA_TO_R9,
		}[argument_index], 3);

		// RIP-relative address of data string
		push_data_string_code(expr_value.literal.string, codes_size);
		compile_push_number(PLACEHOLDER_32, 4);
	} else {
		// TODO: Can modders somehow reach this?
		grug_error("Only number and strings can be returned right now");
	}
}

static void push_data_string(char *string) {
	grug_assert(data_strings_size < MAX_DATA_STRINGS, "There are more than %d data strings, exceeding MAX_DATA_STRINGS", MAX_DATA_STRINGS);

	data_strings[data_strings_size++] = string;
}

static u32 get_data_string_index(char *string) {
	u32 i = buckets_data_strings[elf_hash(string) % MAX_BUCKETS_DATA_STRINGS];

	while (1) {
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

static void init_data_strings(void) {
	size_t field_count = define_fn.returned_compound_literal.field_count;

	memset(buckets_data_strings, UINT32_MAX, MAX_BUCKETS_DATA_STRINGS * sizeof(u32));

	size_t chains_size = 0;

	for (size_t field_index = 0; field_index < field_count; field_index++) {
		struct field field = define_fn.returned_compound_literal.fields[field_index];

		if (field.expr_value.type == STRING_EXPR && get_data_string_index(field.expr_value.literal.string) == UINT32_MAX) {
			char *string = field.expr_value.literal.string;

			push_data_string(string);

			u32 bucket_index = elf_hash(string) % MAX_BUCKETS_DATA_STRINGS;

			chains_data_strings[chains_size] = buckets_data_strings[bucket_index];

			buckets_data_strings[bucket_index] = chains_size++;
		}
	}
}

static struct grug_on_function *get_define_on_fn(char *name) {
	u32 i = buckets_define_on_fns[elf_hash(name) % grug_define_entity->on_function_count];

	while (1) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, grug_define_entity->on_functions[i].name)) {
			break;
		}

		i = chains_define_on_fns[i];
	}

	return grug_define_entity->on_functions + i;
}

static void hash_define_on_fns(void) {
	memset(buckets_define_on_fns, UINT32_MAX, grug_define_entity->on_function_count * sizeof(u32));

	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		char *name = grug_define_entity->on_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_define_entity->on_function_count;

		chains_define_on_fns[i] = buckets_define_on_fns[bucket_index];

		buckets_define_on_fns[bucket_index] = i;
	}
}

static void init_define_fn_name(char *name) {
	grug_assert(temp_strings_size + sizeof("define_") - 1 + strlen(name) < TEMP_MAX_STRINGS_CHARACTERS, "There are more than %d characters in the strings array, exceeding TEMP_MAX_STRINGS_CHARACTERS", TEMP_MAX_STRINGS_CHARACTERS);

	define_fn_name = temp_strings + temp_strings_size;

	memcpy(temp_strings + temp_strings_size, "define_", sizeof("define_") - 1);
	temp_strings_size += sizeof("define_") - 1;

	for (size_t i = 0; i < strlen(name); i++) {
		temp_strings[temp_strings_size++] = name[i];
	}
	temp_strings[temp_strings_size++] = '\0';
}

static struct grug_entity *compile_get_entity(char *return_type) {
	for (size_t i = 0; i < grug_define_functions_size; i++) {
		if (streq(return_type, grug_define_functions[i].name)) {
			return grug_define_functions + i;
		}
	}
	return NULL;
}

static void compile(void) {
	reset_compiling();

	// Getting the used define fn's grug_entity
	grug_define_entity = compile_get_entity(define_fn.return_type);
	grug_assert(grug_define_entity, "The entity '%s' was not declared by mod_api.json", define_fn.return_type);
	grug_assert(grug_define_entity->argument_count == define_fn.returned_compound_literal.field_count, "The entity '%s' expects %zu fields, but got %zu", grug_define_entity->name, grug_define_entity->argument_count, define_fn.returned_compound_literal.field_count);
	init_define_fn_name(grug_define_entity->name);
	hash_define_on_fns();
	for (size_t on_fn_index = 0; on_fn_index < on_fns_size; on_fn_index++) {
		grug_assert(grug_define_entity->on_function_count != 0 && get_define_on_fn(on_fns[on_fn_index].fn_name), "The function '%s' was not was not declared by entity '%s' in mod_api.json", on_fns[on_fn_index].fn_name, define_fn.return_type);
	}

	init_data_strings();

	hash_game_fns();

	size_t text_offset_index = 0;
	size_t text_offset = 0;

	// define()
	size_t field_count = define_fn.returned_compound_literal.field_count;
	// TODO: Support more arguments
	grug_assert(field_count <= 6, "Currently grug only supports up to 6 function arguments");
	for (size_t field_index = 0; field_index < field_count; field_index++) {
		struct field field = define_fn.returned_compound_literal.fields[field_index];

		grug_assert(streq(field.key, grug_define_entity->arguments[field_index].name), "Field %zu named '%s' that you're returning from your define function must be renamed to '%s', according to the entity '%s' in mod_api.json", field_index + 1, field.key, grug_define_entity->arguments[field_index].name, grug_define_entity->name);

		// TODO: Verify that the argument has the same type as the one in grug_define_entity

		compile_returned_field(field.expr_value, field_index);
	}
	compile_push_byte(CALL);
	push_game_fn_call(define_fn_name, codes_size);
	compile_push_number(PLACEHOLDER_32, 4);
	compile_push_byte(RET);
	text_offsets[text_offset_index++] = text_offset;
	text_offset += codes_size;

	// init_globals()
	size_t start_codes_size = codes_size;
	size_t ptr_offset = 0;
	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
		struct global_variable global_variable = global_variables[global_variable_index];

		compile_push_number(MOV_TO_RDI_PTR, 2);

		// TODO: Add a grug test for this, cause I want it to be able to handle when ptr_offset is >= 256
		grug_assert(ptr_offset < 256, "Currently grug only supports up to 64 global variables");
		compile_push_byte(ptr_offset);
		ptr_offset += sizeof(u32);

		// TODO: Make it possible to retrieve .string_literal_expr here
		// TODO: Add test that only literals can initialize global variables, so no equations
		u64 value = global_variable.assignment_expr.literal.i32;

		compile_push_number(value, 4);
	}
	compile_push_byte(RET);
	text_offsets[text_offset_index++] = text_offset;
	text_offset += codes_size - start_codes_size;

	for (size_t on_fn_index = 0; on_fn_index < on_fns_size; on_fn_index++) {
		start_codes_size = codes_size;

		struct on_fn fn = on_fns[on_fn_index];

		compile_statements(fn.body_statements, fn.body_statement_count);
		compile_push_byte(RET);

		text_offsets[text_offset_index++] = text_offset;
		text_offset += codes_size - start_codes_size;
	}

	for (size_t helper_fn_index = 0; helper_fn_index < helper_fns_size; helper_fn_index++) {
		start_codes_size = codes_size;

		struct helper_fn fn = helper_fns[helper_fn_index];

		push_helper_fn_offset(fn.fn_name, codes_size);

		compile_statements(fn.body_statements, fn.body_statement_count);
		compile_push_byte(RET);

		text_offsets[text_offset_index++] = text_offset;
		text_offset += codes_size - start_codes_size;
	}

	hash_used_game_fns();
	hash_helper_fn_offsets();
}

// static char serialized[MAX_SERIALIZED_TO_C_CHARS + 1];
// static size_t serialized_size;

// static void serialize_append_slice(char *str, size_t len) {
// 	if (serialized_size + len > MAX_SERIALIZED_TO_C_CHARS) {
// 		grug_error("There are more than %d characters in the output C file, exceeding MAX_SERIALIZED_TO_C_CHARS", MAX_SERIALIZED_TO_C_CHARS);
// 	}
// 	memcpy(serialized + serialized_size, str, len);
// 	serialized_size += len;
// }

// static void serialize_append(char *str) {
// 	serialize_append_slice(str, strlen(str));
// }

// static void serialize_append_number(struct number_expr number_expr) {
// 	char buf[MAX_NUMBER_LEN];
// 	snprintf(buf, sizeof(buf), "%ld", number_expr.value);
// 	serialize_append(buf);
// }

// static void serialize_append_indents(size_t depth) {
// 	for (size_t i = 0; i < depth * SPACES_PER_INDENT; i++) {
// 		serialize_append(" ");
// 	}
// }

// static void serialize_expr(struct expr expr);

// static void serialize_operator(enum token_type operator);

// static void serialize_operator(enum token_type operator) {
// 	switch (operator) {
// 		case PLUS_TOKEN:
// 			serialize_append("+");
// 			return;
// 		case MINUS_TOKEN:
// 			serialize_append("-");
// 			return;
// 		case MULTIPLICATION_TOKEN:
// 			serialize_append("*");
// 			return;
// 		case DIVISION_TOKEN:
// 			serialize_append("/");
// 			return;
// 		case REMAINDER_TOKEN:
// 			serialize_append("%");
// 			return;
// 		case PERIOD_TOKEN:
// 			serialize_append(".");
// 			return;
// 		case EQUALS_TOKEN:
// 			serialize_append("==");
// 			return;
// 		case NOT_EQUALS_TOKEN:
// 			serialize_append("!=");
// 			return;
// 		case GREATER_OR_EQUAL_TOKEN:
// 			serialize_append(">=");
// 			return;
// 		case GREATER_TOKEN:
// 			serialize_append(">");
// 			return;
// 		case LESS_OR_EQUAL_TOKEN:
// 			serialize_append("<=");
// 			return;
// 		case LESS_TOKEN:
// 			serialize_append("<");
// 			return;
// 		case NOT_TOKEN:
// 			serialize_append("not");
// 			return;
// 		default:
// 			grug_error(UNREACHABLE_STR);
// 	}
// }

// static bool is_identifier_global(char *name, size_t len) {
// 	for (size_t i = 0; i < global_variables_size; i++) {
// 		struct global_variable global = global_variables[i];
// 		if (global.name_len == len && memcmp(global.name, name, len) == 0) {
// 			return true;
// 		}
// 	}
// 	return false;
// }

// static void serialize_arguments(size_t arguments_offset, size_t argument_count) {
// 	if (argument_count == 0) {
// 		return;
// 	}

// 	struct argument arg = arguments[arguments_offset];

// 	serialize_append_slice(arg.type, arg.type_len);
// 	serialize_append(" ");
// 	serialize_append_slice(arg.name, arg.name_len);

// 	for (size_t argument_index = 1; argument_index < argument_count; argument_index++) {
// 		arg = arguments[arguments_offset + argument_index];

// 		serialize_append(", ");
// 		serialize_append_slice(arg.type, arg.type_len);
// 		serialize_append(" ");
// 		serialize_append_slice(arg.name, arg.name_len);
// 	}
// }

// static void serialize_helper_fns(void) {
// 	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
// 		struct helper_fn fn = helper_fns[fn_index];

// 		serialize_append("\n");

// 		if (fn.return_type_len > 0) {
// 			serialize_append_slice(fn.return_type, fn.return_type_len);
// 		} else {
// 			serialize_append("void");
// 		}
		
// 		serialize_append(" ");
// 		serialize_append_slice(fn.fn_name, fn.fn_name_len);

// 		serialize_append("(");
// 		serialize_append("void *globals_void");
// 		if (fn.argument_count > 0) {
// 			serialize_append(", ");
// 		}
// 		serialize_arguments(fn.arguments_offset, fn.argument_count);
// 		serialize_append(") {\n");

// 		serialize_append_indents(1);
// 		serialize_append("struct globals *globals = globals_void;\n");

// 		serialize_append("\n");
// 		serialize_statements(fn.body_statements_offset, fn.body_statement_count, 1);

// 		serialize_append("}\n");
// 	}
// }

// static void serialize_exported_on_fns(void) {
// 	serialize_append("struct ");
// 	serialize_append_slice(define_fn.return_type, define_fn.return_type_len);
// 	serialize_append("_on_fns on_fns = {\n");

// 	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
// 		struct on_fn fn = on_fns[fn_index];

// 		serialize_append_indents(1);
// 		serialize_append(".");

// 		// Skip the "on_"
// 		serialize_append_slice(fn.fn_name + 3, fn.fn_name_len - 3);

// 		serialize_append(" = ");
// 		serialize_append_slice(fn.fn_name, fn.fn_name_len);
// 		serialize_append(",\n");
// 	}

// 	serialize_append("};\n");
// }

// static void serialize_on_fns(void) {
// 	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
// 		struct on_fn fn = on_fns[fn_index];

// 		serialize_append("\n");

// 		serialize_append("static void ");
// 		serialize_append_slice(fn.fn_name, fn.fn_name_len);

// 		serialize_append("(");
// 		serialize_append("void *globals_void");
// 		if (fn.argument_count > 0) {
// 			serialize_append(", ");
// 		}
// 		serialize_arguments(fn.arguments_offset, fn.argument_count);
// 		serialize_append(") {\n");

// 		serialize_append_indents(1);
// 		serialize_append("struct globals *globals = globals_void;\n");

// 		serialize_append("\n");
// 		serialize_statements(fn.body_statements_offset, fn.body_statement_count, 1);

// 		serialize_append("}\n");
// 	}
// }

// static void serialize_forward_declare_helper_fns(void) {
// 	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
// 		struct helper_fn fn = helper_fns[fn_index];

// 		if (fn.return_type_len > 0) {
// 			serialize_append_slice(fn.return_type, fn.return_type_len);
// 		} else {
// 			serialize_append("void");
// 		}

// 		serialize_append(" ");
// 		serialize_append_slice(fn.fn_name, fn.fn_name_len);

// 		serialize_append("(");
// 		serialize_append("void *globals_void");
// 		if (fn.argument_count > 0) {
// 			serialize_append(", ");
// 		}
// 		serialize_arguments(fn.arguments_offset, fn.argument_count);
// 		serialize_append(");\n");
// 	}
// }

// static void serialize_init_globals(void) {
// 	serialize_append("void init_globals(void *globals) {\n");

// 	serialize_append_indents(1);
// 	serialize_append("memcpy(globals, &(struct globals){\n");

// 	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
// 		struct global_variable global_variable = global_variables[global_variable_index];

// 		serialize_append_indents(2);

// 		serialize_append(".");
// 		serialize_append_slice(global_variable.name, global_variable.name_len);

// 		serialize_append(" = ");

// 		serialize_expr(global_variable.assignment_expr);

// 		serialize_append(",\n");
// 	}

// 	serialize_append_indents(1);
// 	serialize_append("}, sizeof(struct globals));\n");

// 	serialize_append("}\n");
// }

// static void serialize_get_globals_size(void) {
// 	serialize_append("size_t get_globals_size(void) {\n");
// 	serialize_append_indents(1);
// 	serialize_append("return sizeof(struct globals);\n");
// 	serialize_append("}\n");
// }

// static void serialize_global_variables(void) {
// 	serialize_append("struct globals {\n");

// 	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
// 		struct global_variable global_variable = global_variables[global_variable_index];

// 		serialize_append_indents(1);

// 		serialize_append_slice(global_variable.type, global_variable.type_len);
// 		serialize_append(" ");
// 		serialize_append_slice(global_variable.name, global_variable.name_len);

// 		serialize_append(";\n");
// 	}

// 	serialize_append("};\n");
// }

// static void serialize_define_type(void) {
// 	serialize_append("char *define_type = \"");
// 	serialize_append_slice(define_fn.return_type, define_fn.return_type_len);
// 	serialize_append("\";\n");
// }

// static void serialize_define_struct(void) {
// 	serialize_append("struct ");
// 	serialize_append_slice(define_fn.return_type, define_fn.return_type_len);
// 	serialize_append(" define = {\n");

// 	struct compound_literal compound_literal = define_fn.returned_compound_literal;

// 	for (size_t field_index = 0; field_index < compound_literal.field_count; field_index++) {
// 		struct field field = fields[compound_literal.fields_offset + field_index];

// 		serialize_append_indents(1);
// 		serialize_append(".");
// 		serialize_append_slice(field.key, field.key_len);
// 		serialize_append(" = ");
// 		serialize_expr(field.expr_value);
// 		serialize_append(",\n");
// 	}

// 	serialize_append("};\n");
// }

// static void serialize_to_c(void) {
// 	serialize_append("#include <stdint.h>\n");
// 	serialize_append("#include <string.h>\n");

// 	// TODO: Since grug doesn't know what structs the game has anymore,
// 	// remove this code
// 	serialize_append("\n");
// 	serialize_append("struct entity {\n");
// 	serialize_append("\tuint64_t a;\n");
// 	serialize_append("};\n");
	
// 	serialize_append("\n");
// 	serialize_define_type();

// 	serialize_append("\n");
// 	serialize_define_struct();

// 	serialize_append("\n");
// 	serialize_global_variables();

// 	serialize_append("\n");
// 	serialize_get_globals_size();

// 	serialize_append("\n");
// 	serialize_init_globals();

// 	if (helper_fns_size > 0) {
// 		serialize_append("\n");
// 		serialize_forward_declare_helper_fns();
// 	}

// 	if (on_fns_size > 0) {
// 		serialize_on_fns();
// 		serialize_append("\n");
// 		serialize_exported_on_fns();
// 	}

// 	if (helper_fns_size > 0) {
// 		serialize_helper_fns();
// 	}

// 	serialized[serialized_size] = '\0';
// }

//// LINKING

#define MAX_BYTES 420420
#define MAX_GAME_FN_OFFSETS 420420
#define MAX_HASH_BUCKETS 32771 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elflink.c;h=6db6a9c0b4702c66d73edba87294e2a59ffafcf5;hb=refs/heads/master#l6560

// TODO: Stop having these hardcoded!
#define PLT_OFFSET 0x1000
#define EH_FRAME_OFFSET 0x2000
#define DYNAMIC_OFFSET (on_fns_size > 0 ? 0x2ee0 : 0x2f10) // TODO: Unhardcode!
#define GOT_PLT_OFFSET 0x3000

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

enum opcodes {
	PUSH_BYTE = 0x68,
	JMP_ABS = 0xe9,
	JMP_REL = 0x25ff,
	PUSH_REL = 0x35ff,
	NOP = 0x401f0f,
};

static size_t shindex_hash;
static size_t shindex_dynsym;
static size_t shindex_dynstr;
static size_t shindex_rela_dyn;
static size_t shindex_rela_plt;
static size_t shindex_plt;
static size_t shindex_text;
static size_t shindex_eh_frame;
static size_t shindex_dynamic;
static size_t shindex_got_plt;
static size_t shindex_data;
static size_t shindex_symtab;
static size_t shindex_strtab;
static size_t shindex_shstrtab;

static char *symbols[MAX_SYMBOLS];
static size_t symbols_size;

static size_t on_fns_symbol_offset;

static size_t data_symbols_size;

static bool is_substrs[MAX_SYMBOLS];

static size_t symbol_name_dynstr_offsets[MAX_SYMBOLS];
static size_t symbol_name_strtab_offsets[MAX_SYMBOLS];

static u32 buckets_on_fns[MAX_ON_FNS_IN_FILE];
static u32 chains_on_fns[MAX_ON_FNS_IN_FILE];

static char *shuffled_symbols[MAX_SYMBOLS];
static size_t shuffled_symbols_size;

static size_t shuffled_symbol_index_to_symbol_index[MAX_SYMBOLS];
static size_t symbol_index_to_shuffled_symbol_index[MAX_SYMBOLS];

static size_t first_used_game_fn_symbol_index;

static size_t data_offsets[MAX_SYMBOLS];
static size_t data_string_offsets[MAX_SYMBOLS];

static u8 bytes[MAX_BYTES];
static size_t bytes_size;

static size_t symtab_index_first_global;

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
static size_t dynamic_size;
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
static size_t got_plt_shstrtab_offset;
static size_t data_shstrtab_offset;
static size_t symtab_shstrtab_offset;
static size_t strtab_shstrtab_offset;
static size_t shstrtab_shstrtab_offset;

static struct fn_offset game_fn_offsets[MAX_GAME_FN_OFFSETS];
static size_t game_fn_offsets_size;
static u32 buckets_game_fn_offsets[MAX_GAME_FN_OFFSETS];
static u32 chains_game_fn_offsets[MAX_GAME_FN_OFFSETS];

static void reset_generate_shared_object(void) {
	symbols_size = 0;
	data_symbols_size = 0;
	shuffled_symbols_size = 0;
	bytes_size = 0;
	game_fn_offsets_size = 0;
}

static void overwrite(u64 n, size_t bytes_offset, size_t overwrite_count) {
	for (size_t i = 0; i < overwrite_count; i++) {
		bytes[bytes_offset++] = n & 0xff; // Little-endian

		n >>= 8; // Shift right by one byte
	}
}

static void overwrite_16(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, 2);
}

static void overwrite_32(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, 4);
}

static void overwrite_64(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, 8);
}

static struct on_fn *get_on_fn(char *name) {
	u32 i = buckets_on_fns[elf_hash(name) % on_fns_size];

	while (1) {
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
	memset(buckets_on_fns, UINT32_MAX, on_fns_size * sizeof(u32));

	for (size_t i = 0; i < on_fns_size; i++) {
		char *name = on_fns[i].fn_name;

		grug_assert(!get_on_fn(name), "The function '%s' was defined several times in the same file", name);

		u32 bucket_index = elf_hash(name) % on_fns_size;

		chains_on_fns[i] = buckets_on_fns[bucket_index];

		buckets_on_fns[bucket_index] = i;
	}
}

// Needed future text_offset
static void patch_rela_dyn(void) {
	size_t return_type_data_size = strlen(define_fn.return_type) + 1;
	size_t globals_size_data_size = sizeof(uint64_t);
	size_t on_fn_data_offset = return_type_data_size + globals_size_data_size;

	size_t excess = on_fn_data_offset % sizeof(uint64_t); // Alignment
	if (excess > 0) {
		on_fn_data_offset += sizeof(uint64_t) - excess;
	}

	size_t bytes_offset = rela_dyn_offset;
	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		struct on_fn *on_fn = on_fns_size > 0 ? get_on_fn(grug_define_entity->on_functions[i].name) : NULL;
		if (on_fn) {
			size_t on_fn_index = on_fn - on_fns;
			size_t symbol_index = on_fns_symbol_offset + on_fn_index;
			size_t text_index = symbol_index - data_symbols_size - used_game_fns_size;

			overwrite_64(GOT_PLT_OFFSET + got_plt_size + on_fn_data_offset, bytes_offset);
			bytes_offset += sizeof(u64);
			overwrite_64(R_X86_64_RELATIVE, bytes_offset);
			bytes_offset += sizeof(u64);
			overwrite_64(text_offset + text_offsets[text_index], bytes_offset);
			bytes_offset += sizeof(u64);
		}
		on_fn_data_offset += sizeof(size_t);
	}
}

// Needed future data_offset and text_offset
static void patch_dynsym(void) {
	// The symbols are pushed in shuffled_symbols order
	size_t bytes_offset = dynsym_placeholders_offset;
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		bool is_data = symbol_index < data_symbols_size;
		bool is_extern = symbol_index < data_symbols_size + used_game_fns_size;

		u16 shndx = is_data ? shindex_data : is_extern ? SHN_UNDEF : shindex_text;
		u32 offset = is_data ? data_offset + data_offsets[symbol_index] : is_extern ? 0 : text_offset + text_offsets[symbol_index - data_symbols_size - used_game_fns_size];

		overwrite_32(symbol_name_dynstr_offsets[symbol_index], bytes_offset);
		bytes_offset += sizeof(u32);
		overwrite_16(ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), bytes_offset);
		bytes_offset += sizeof(u16);
		overwrite_16(shndx, bytes_offset);
		bytes_offset += sizeof(u16);
		overwrite_32(offset, bytes_offset);
		bytes_offset += sizeof(u32);

		bytes_offset += SYMTAB_ENTRY_SIZE - sizeof(u32) - sizeof(u16) - sizeof(u16) - sizeof(u32);
	}
}

static size_t get_game_fn_offset(char *name) {
	u32 i = buckets_game_fn_offsets[elf_hash(name) % game_fn_offsets_size];

	while (1) {
		assert(i != UINT32_MAX && "get_game_fn_offset() isn't supposed to ever fail");

		if (streq(name, game_fn_offsets[i].fn_name)) {
			break;
		}

		i = chains_game_fn_offsets[i];
	}

	return game_fn_offsets[i].offset;
}

static void hash_game_fn_offsets(void) {
	memset(buckets_game_fn_offsets, UINT32_MAX, game_fn_offsets_size * sizeof(u32));

	for (size_t i = 0; i < game_fn_offsets_size; i++) {
		char *name = game_fn_offsets[i].fn_name;

		u32 bucket_index = elf_hash(name) % game_fn_offsets_size;

		chains_game_fn_offsets[i] = buckets_game_fn_offsets[bucket_index];

		buckets_game_fn_offsets[bucket_index] = i;
	}
}

static void push_game_fn_offset(char *fn_name, size_t offset) {
	grug_assert(game_fn_offsets_size < MAX_GAME_FN_OFFSETS, "There are more than %d game functions, exceeding MAX_GAME_FN_OFFSETS", MAX_GAME_FN_OFFSETS);

	game_fn_offsets[game_fn_offsets_size++] = (struct fn_offset){
		.fn_name = fn_name,
		.offset = offset,
	};
}

// Needed future fn offsets and data_offset
static void patch_text(void) {
	size_t next_instruction_offset = 4;

	for (size_t i = 0; i < game_fn_calls_size; i++) {
		struct fn_call fn_call = game_fn_calls[i];
		size_t offset = text_offset + fn_call.codes_offset;
		size_t address_after_call_instruction = offset + next_instruction_offset;
		overwrite_32((PLT_OFFSET + get_game_fn_offset(fn_call.fn_name)) - address_after_call_instruction, offset);
	}

	for (size_t i = 0; i < helper_fn_calls_size; i++) {
		struct fn_call fn_call = helper_fn_calls[i];
		size_t offset = text_offset + fn_call.codes_offset;
		size_t address_after_call_instruction = offset + next_instruction_offset;
		overwrite_32((text_offset + get_helper_fn_offset(fn_call.fn_name)) - address_after_call_instruction, offset);
	}

	for (size_t i = 0; i < data_string_codes_size; i++) {
		struct data_string_code dsc = data_string_codes[i];
		char *string = dsc.string;
		size_t code_offset = dsc.code_offset;

		size_t string_index = get_data_string_index(string);
		assert(string_index != UINT32_MAX);

		size_t string_address = data_offset + data_string_offsets[string_index];

		// rip/PC (program counter) has +4,
		// because that's the address of the next instruction,
		// which is used for RIP-relative addressing
		size_t next_instruction_address = text_offset + code_offset + 4;

		size_t string_offset = string_address - next_instruction_address;

		overwrite_32(string_offset, text_offset + code_offset);
	}
}

static void patch_bytes(void) {
	// ELF section header table offset
	overwrite_64(section_headers_offset, 0x28);

	// Segment 0 its file_size
	overwrite_64(segment_0_size, 0x60);
	// Segment 0 its mem_size
	overwrite_64(segment_0_size, 0x68);

	// Segment 1 its file_size
	overwrite_64(plt_size + text_size, 0x98);
	// Segment 1 its mem_size
	overwrite_64(plt_size + text_size, 0xa0);

	// Segment 3 its file_size
	overwrite_64(dynamic_size + got_plt_size + data_size, 0x108);
	// Segment 3 its mem_size
	overwrite_64(dynamic_size + got_plt_size + data_size, 0x110);

	// Segment 4 its file_size
	overwrite_64(dynamic_size, 0x140);
	// Segment 4 its mem_size
	overwrite_64(dynamic_size, 0x148);

	// Segment 5 its file_size
	overwrite_64(dynamic_size, 0x178);
	// Segment 5 its mem_size
	overwrite_64(dynamic_size, 0x180);

	patch_dynsym();
	patch_rela_dyn();
	patch_text();
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
			push_byte(0x90);
		}
	}
}

static void push_alignment(size_t alignment) {
	size_t excess = bytes_size % alignment;
	if (excess > 0) {
		push_zeros(alignment - excess);
	}
}

static void push_string_bytes(char *str) {
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

	if (on_fns_size > 0) {
		rela_dyn_shstrtab_offset = offset;
		push_string_bytes(".rela.dyn");
		offset += sizeof(".rela.dyn");
	}

	rela_plt_shstrtab_offset = offset;
	push_string_bytes(".rela.plt");
	offset += sizeof(".rela") - 1;

	plt_shstrtab_offset = offset;
	offset += sizeof(".plt");

	text_shstrtab_offset = offset;
	push_string_bytes(".text");
	offset += sizeof(".text");

	eh_frame_shstrtab_offset = offset;
	push_string_bytes(".eh_frame");
	offset += sizeof(".eh_frame");

	dynamic_shstrtab_offset = offset;
	push_string_bytes(".dynamic");
	offset += sizeof(".dynamic");

	got_plt_shstrtab_offset = offset;
	push_string_bytes(".got.plt");
	offset += sizeof(".got.plt");

	data_shstrtab_offset = offset;
	push_string_bytes(".data");
	offset += sizeof(".data");

	shstrtab_size = bytes_size - shstrtab_offset;

	push_alignment(8);
}

static void push_strtab(char *grug_path) {
	grug_log_section(".strtab");

	strtab_offset = bytes_size;

	push_byte(0);
	push_string_bytes(grug_path);
	
	// Local symbols
	// TODO: Add loop

	push_string_bytes("_DYNAMIC");
	push_string_bytes("_GLOBAL_OFFSET_TABLE_");

	// Global symbols
	// TODO: Don't loop through local symbols
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		if (!is_substrs[symbol_index]) {
			push_string_bytes(shuffled_symbols[i]);
		}
	}

	strtab_size = bytes_size - strtab_offset;
}

static void push_number(u64 n, size_t byte_count) {
	for (; byte_count-- > 0; n >>= 8) {
		push_byte(n & 0xff); // Little-endian
	}
}

// See https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-79797/index.html
// See https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblj/index.html#chapter6-tbl-21
static void push_symbol_entry(u32 name, u16 info, u16 shndx, u32 offset) {
	push_number(name, sizeof(u32)); // Indexed into .strtab, because .symtab its "link" points to it
	push_number(info, sizeof(u16));
	push_number(shndx, sizeof(u16));
	push_number(offset, sizeof(u32)); // In executable and shared object files, st_value holds a virtual address

	push_zeros(SYMTAB_ENTRY_SIZE - sizeof(u32) - sizeof(u16) - sizeof(u16) - sizeof(u32));
}

static void push_symtab(char *grug_path) {
	grug_log_section(".symtab");

	symtab_offset = bytes_size;

	// Null entry
	push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), SHN_UNDEF, 0);

	// "<some_path>.s" entry
	push_symbol_entry(1, ELF32_ST_INFO(STB_LOCAL, STT_FILE), SHN_ABS, 0);

	// TODO: ? entry
	push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_FILE), SHN_ABS, 0);

	// TODO: Let this use path of the .grug file, instead of the .s that's used purely for testing purposes
	// The `1 +` is to skip the 0 byte that .strtab always starts with
	size_t name_offset = 1 + strlen(grug_path) + 1;

	// "_DYNAMIC" entry
	push_symbol_entry(name_offset, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), shindex_dynamic, DYNAMIC_OFFSET);
	name_offset += sizeof("_DYNAMIC");

	// "_GLOBAL_OFFSET_TABLE_" entry
	push_symbol_entry(name_offset, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), shindex_got_plt, GOT_PLT_OFFSET);
	name_offset += sizeof("_GLOBAL_OFFSET_TABLE_");

	symtab_index_first_global = 5;

	// The symbols are pushed in shuffled_symbols order
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		bool is_data = symbol_index < data_symbols_size;
		bool is_extern = symbol_index < data_symbols_size + used_game_fns_size;

		u16 shndx = is_data ? shindex_data : is_extern ? SHN_UNDEF : shindex_text;
		u32 offset = is_data ? data_offset + data_offsets[symbol_index] : is_extern ? 0 : text_offset + text_offsets[symbol_index - data_symbols_size - used_game_fns_size];

		push_symbol_entry(name_offset + symbol_name_strtab_offsets[symbol_index], ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), shndx, offset);
	}

	symtab_size = bytes_size - symtab_offset;
}

static void push_data(void) {
	grug_log_section(".data");

	data_offset = bytes_size;

	// "define_type" symbol
	push_string_bytes(define_fn.return_type);

	// "globals_size" symbol
	push_nasm_alignment(8);
	size_t globals_bytes = 0;
	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
		struct global_variable global_variable = global_variables[global_variable_index];
		globals_bytes += type_sizes[global_variable.type];
	}
	push_number(globals_bytes, 8);

	// "on_fns" function addresses
	size_t previous_on_fn_index = 0;
	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		struct on_fn *on_fn = on_fns_size > 0 ? get_on_fn(grug_define_entity->on_functions[i].name) : NULL;
		if (on_fn) {
			size_t on_fn_index = on_fn - on_fns;
			grug_assert(previous_on_fn_index <= on_fn_index, "The function '%s' was in the wrong order, according to the entity '%s' in mod_api.json", on_fn->fn_name, grug_define_entity->name);
			previous_on_fn_index = on_fn_index;

			size_t symbol_index = on_fns_symbol_offset + on_fn_index;
			size_t text_index = symbol_index - data_symbols_size - used_game_fns_size;
			push_number(text_offset + text_offsets[text_index], 8);
		} else {
			push_number(0x0, 8);
		}
	}

	// "strings" symbol
	for (size_t i = 0; i < data_strings_size; i++) {
		char *string = data_strings[i];
		push_string_bytes(string);
	}

	push_alignment(8);
}

static void push_got_plt(void) {
	grug_log_section(".got.plt");

	size_t got_plt_offset = bytes_size;

	push_number(DYNAMIC_OFFSET, 8);
	push_zeros(8); // TODO: What is this for?
	push_zeros(8); // TODO: What is this for?

	// 0x10 is the size of the first, special .plt entry
	// 0x6 is the offset every .plt entry has to their push instruction
	size_t offset = PLT_OFFSET + 0x10 + 0x6;

	for (size_t i = 0; i < used_game_fns_size; i++) {
		push_number(offset, 8); // text section address of push <i> instruction
		offset += 0x10; // 0x10 is the size of a .plt entry
	}

	got_plt_size = bytes_size - got_plt_offset;
}

// See https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-42444.html
static void push_dynamic_entry(u64 tag, u64 value) {
	push_number(tag, 8);
	push_number(value, 8);
}

static void push_dynamic(void) {
	grug_log_section(".dynamic");

	size_t dynamic_offset = bytes_size;

	push_dynamic_entry(DT_HASH, hash_offset);
	push_dynamic_entry(DT_STRTAB, dynstr_offset);
	push_dynamic_entry(DT_SYMTAB, dynsym_offset);
	push_dynamic_entry(DT_STRSZ, dynstr_size);
	push_dynamic_entry(DT_SYMENT, SYMTAB_ENTRY_SIZE);
	push_dynamic_entry(DT_PLTGOT, GOT_PLT_OFFSET);
	push_dynamic_entry(DT_PLTRELSZ, PLT_ENTRY_SIZE * used_game_fns_size);
	push_dynamic_entry(DT_PLTREL, DT_RELA);
	push_dynamic_entry(DT_JMPREL, rela_dyn_offset + ((on_fns_size > 0) ? RELA_ENTRY_SIZE * on_fns_size : 0));
	if (on_fns_size > 0) {
		push_dynamic_entry(DT_RELA, rela_dyn_offset);
		push_dynamic_entry(DT_RELASZ, RELA_ENTRY_SIZE * on_fns_size);
		push_dynamic_entry(DT_RELAENT, RELA_ENTRY_SIZE);
		push_dynamic_entry(DT_RELACOUNT, on_fns_size);
	}
	push_dynamic_entry(DT_NULL, 0);

	push_zeros(GOT_PLT_OFFSET - bytes_size);

	dynamic_size = bytes_size - dynamic_offset;
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

	plt_offset = bytes_size;

	// TODO: Figure out what these instructions represent
	push_number(PUSH_REL, 2);
	push_number(0x2002, 4);
	push_number(JMP_REL, 2);
	push_number(0x2004, 4);
	push_number(NOP, 4);

	size_t pushed_plt_entries = 0;
	size_t offset = 0x10;
	// The 0x18 here is from the first three addresses push_got_plt() pushes 
	size_t got_plt_fn_address = GOT_PLT_OFFSET + 0x18;

	for (size_t i = 0; i < BFD_HASH_BUCKET_SIZE; i++) {
		u32 chain_index = buckets_used_game_fns[i];
		if (chain_index == UINT32_MAX) {
			continue;
		}

		while (true) {
			char *name = used_game_fns[chain_index];

			push_number(JMP_REL, 2);
			size_t next_instruction_offset = 4;
			push_number(got_plt_fn_address - (bytes_size + next_instruction_offset), 4);
			got_plt_fn_address += 0x8;
			push_byte(PUSH_BYTE);
			push_number(pushed_plt_entries++, 4);
			push_byte(JMP_ABS);
			push_game_fn_offset(name, offset);
			size_t offset_to_start_of_plt = -offset - 0x10;
			push_number(offset_to_start_of_plt, 4);
			offset += 0x10;

			chain_index = chains_used_game_fns[chain_index];
			if (chain_index == UINT32_MAX) {
				break;
			}
		}
	}

	hash_game_fn_offsets();

	plt_size = bytes_size - plt_offset;
}

static void push_rela(u64 offset, u64 info, u64 addend) {
	push_number(offset, 8);
	push_number(info, 8);
	push_number(addend, 8);
}

// Source:
// https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblk/index.html#chapter6-1235
// https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-54839.html
static void push_rela_plt(void) {
	grug_log_section(".rela.plt");

	rela_plt_offset = bytes_size;

	size_t offset = GOT_PLT_OFFSET + 0x18; // +0x18 skips three special addresses that are always at the start
	for (size_t shuffled_symbol_index = 0; shuffled_symbol_index < symbols_size; shuffled_symbol_index++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[shuffled_symbol_index];

		if (symbol_index < first_used_game_fn_symbol_index || symbol_index >= first_used_game_fn_symbol_index + used_game_fns_size) {
			continue;
		}

		// `1 +` skips the first symbol, which is always undefined
		size_t dynsym_index = 1 + shuffled_symbol_index;

		push_rela(offset, ELF64_R_INFO(dynsym_index, R_X86_64_JUMP_SLOT), 0);
		offset += sizeof(u64);
	}

	segment_0_size = bytes_size;

	rela_plt_size = bytes_size - rela_plt_offset;
}

// Source: https://stevens.netmeister.org/631/elf.html
static void push_rela_dyn(void) {
	grug_log_section(".rela.dyn");

	rela_dyn_offset = bytes_size;

	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		struct on_fn *on_fn = on_fns_size > 0 ? get_on_fn(grug_define_entity->on_functions[i].name) : NULL;
		if (on_fn) {
			push_rela(PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64);
		}
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
		if (!is_substrs[i]) {
			push_string_bytes(symbols[i]);
			dynstr_size += strlen(symbols[i]) + 1;
		}
	}

	push_alignment(8);
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
	push_number(nbucket, 4);

	u32 nchain = 1 + symbols_size; // `1 + `, because index 0 is always STN_UNDEF (the value 0)
	push_number(nchain, 4);

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
		push_number(buckets[i], 4);
	}

	for (size_t i = 0; i < chains_size; i++) {
		push_number(chains[i], 4);
	}

	hash_size = bytes_size - hash_offset;

	push_alignment(8);
}

static void push_section_header(u32 name_offset, u32 type, u64 flags, u64 address, u64 offset, u64 size, u32 link, u32 info, u64 alignment, u64 entry_size) {
	push_number(name_offset, 4);
	push_number(type, 4);
	push_number(flags, 8);
	push_number(address, 8);
	push_number(offset, 8);
	push_number(size, 8);
	push_number(link, 4);
	push_number(info, 4);
	push_number(alignment, 8);
	push_number(entry_size, 8);
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

	if (on_fns_size > 0) {
		// .rela.dyn: Relative variable table section
		push_section_header(rela_dyn_shstrtab_offset, SHT_RELA, SHF_ALLOC, rela_dyn_offset, rela_dyn_offset, rela_dyn_size, shindex_dynsym, 0, 8, 24);
	}

	// .rela.plt: Relative procedure (function) linkage table section
	push_section_header(rela_plt_shstrtab_offset, SHT_RELA, SHF_ALLOC | SHF_INFO_LINK, rela_plt_offset, rela_plt_offset, rela_plt_size, shindex_dynsym, shindex_got_plt, 8, 24);

	// .plt: Procedure linkage table section
	push_section_header(plt_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, plt_offset, plt_offset, plt_size, SHN_UNDEF, 0, 16, 16);

	// .text: Code section
	push_section_header(text_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text_offset, text_offset, text_size, SHN_UNDEF, 0, 16, 0);

	// .eh_frame: Exception stack unwinding section
	push_section_header(eh_frame_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC, EH_FRAME_OFFSET, EH_FRAME_OFFSET, 0, SHN_UNDEF, 0, 8, 0);

	// .dynamic: Dynamic linking information section
	push_section_header(dynamic_shstrtab_offset, SHT_DYNAMIC, SHF_WRITE | SHF_ALLOC, DYNAMIC_OFFSET, DYNAMIC_OFFSET, dynamic_size, shindex_dynstr, 0, 8, 16);

	// .got.plt: Global offset table procedure linkage table section
	push_section_header(got_plt_shstrtab_offset, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, GOT_PLT_OFFSET, GOT_PLT_OFFSET, got_plt_size, SHN_UNDEF, 0, 8, 8);

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
	push_number(type, 4);
	push_number(flags, 4);
	push_number(offset, 8);
	push_number(virtual_address, 8);
	push_number(physical_address, 8);
	push_number(file_size, 8);
	push_number(mem_size, 8);
	push_number(alignment, 8);
}

static void push_program_headers(void) {
	grug_log_section("Program headers");

	// .hash, .dynsym, .dynstr, .rela.dyn, .rela.plt segment
	// 0x40 to 0x78
	push_program_header(PT_LOAD, PF_R, 0, 0, 0, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// .plt, .text segment
	// 0x78 to 0xb0
	push_program_header(PT_LOAD, PF_R | PF_X, PLT_OFFSET, PLT_OFFSET, PLT_OFFSET, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// .eh_frame segment
	// 0xb0 to 0xe8
	push_program_header(PT_LOAD, PF_R, EH_FRAME_OFFSET, EH_FRAME_OFFSET, EH_FRAME_OFFSET, 0, 0, 0x1000);

	// .dynamic, .got.plt, .data
	// 0xe8 to 0x120
	push_program_header(PT_LOAD, PF_R | PF_W, DYNAMIC_OFFSET, DYNAMIC_OFFSET, DYNAMIC_OFFSET, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// .dynamic segment
	// 0x120 to 0x158
	push_program_header(PT_DYNAMIC, PF_R | PF_W, DYNAMIC_OFFSET, DYNAMIC_OFFSET, DYNAMIC_OFFSET, PLACEHOLDER_64, PLACEHOLDER_64, 8);

	// .dynamic segment
	// 0x158 to 0x190
	push_program_header(PT_GNU_RELRO, PF_R, DYNAMIC_OFFSET, DYNAMIC_OFFSET, DYNAMIC_OFFSET, PLACEHOLDER_64, PLACEHOLDER_64, 1);
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
	push_number(PLACEHOLDER_64, 8);

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
	push_byte(6);
	push_byte(0);

	// Single section header entry size
	// 0x3a to 0x3c
	push_byte(0x40);
	push_byte(0);

	// Number of section header entries
	// 0x3c to 0x3e
	push_byte(14 + (on_fns_size > 0));
	push_byte(0);

	// Index of entry with section names
	// 0x3e to 0x40
	push_byte(13 + (on_fns_size > 0));
	push_byte(0);
}

static void push_bytes(char *grug_path) {
	// 0x0 to 0x40
	push_elf_header();

	// 0x40 to 0x190
	push_program_headers();

	push_hash();

	push_dynsym();

	push_dynstr();

	push_rela_dyn();

	push_rela_plt();

	push_zeros(PLT_OFFSET - bytes_size);
	push_plt();

	push_text();

	push_zeros(DYNAMIC_OFFSET - bytes_size);
	push_dynamic();

	push_got_plt();

	push_data();

	push_symtab(grug_path);

	push_strtab(grug_path);

	push_shstrtab();

	push_section_headers();
}

static void init_data_offsets(void) {
	size_t i = 0;
	size_t offset = 0;

	// "define_type" symbol
	data_offsets[i++] = offset;
	offset += strlen(define_fn.return_type) + 1;

	// "globals_size" symbol
	size_t excess = offset % sizeof(uint64_t); // Alignment
	if (excess > 0) {
		offset += sizeof(uint64_t) - excess;
	}
	data_offsets[i++] = offset;
	offset += sizeof(uint64_t);

	// "on_fns" function address symbols
	data_offsets[i] = offset; // This can deliberately be overwritten by the loop
	for (size_t on_fn_index = 0; on_fn_index < grug_define_entity->on_function_count; on_fn_index++) {
		data_offsets[i++] = offset;
		offset += sizeof(size_t);
	}

	// "strings" symbol
	if (data_strings_size > 0) {
		data_offsets[i++] = offset;
		for (size_t string_index = 0; string_index < data_strings_size; string_index++) {
			data_string_offsets[string_index] = offset;
			char *string = data_strings[string_index];
			offset += strlen(string) + 1;
		}
	}

	data_size = offset;
}

// haystack="a" , needle="a" => returns 0
// haystack="ab", needle="b" => returns 1
// haystack="a" , needle="b" => returns -1
// haystack="a" , needle="ab" => returns -1
static size_t get_ending_index(char *haystack, char *needle) {
  // Go to the end of the haystack and the needle
  char *hp = haystack;
  while (*hp) {
	hp++;
  }
  char *np = needle;
  while (*np) {
	np++;
  }

  // If the needle is longer than the haystack, it can't fit
  if (np - needle > hp - haystack) {
	return -1;
  }

  while (true) {
	// If one of the characters doesn't match
	if (*hp != *np) {
	  return -1;
	}

	// If the needle entirely fits into the end of the haystack,
	// return the index where needle starts in haystack
	if (np == needle) {
	  return hp - haystack; 
	}

	hp--;
	np--;
  }
}

static void init_symbol_name_strtab_offsets(void) {
	size_t offset = 0;

	static size_t parent_indices[MAX_SYMBOLS];
	static size_t substr_offsets[MAX_SYMBOLS];

	memset(parent_indices, -1, symbols_size * sizeof(size_t));

	// This function could be optimized from O(n^2) to O(n) with a hash table
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];
		char *symbol = symbols[symbol_index];

		size_t parent_index;
		size_t ending_index;
		for (parent_index = 0; parent_index < symbols_size; parent_index++) {
			if (symbol_index != parent_index) {
				ending_index = get_ending_index(symbols[parent_index], symbol);
				if (ending_index != (size_t)-1) {
					break;
				}
			}
		}

		// If symbol wasn't at the end of another symbol
		bool is_substr = parent_index != symbols_size;

		if (is_substr) {
			parent_indices[symbol_index] = parent_index;
			substr_offsets[symbol_index] = ending_index;
		} else {
			symbol_name_strtab_offsets[symbol_index] = offset;
			offset += strlen(symbol) + 1;
		}
	}

	// Now that all the parents have been given final offsets in .strtab,
	// it is clear what index their substring symbols have
	for (size_t i = 0; i < symbols_size; i++) {
		size_t parent_index = parent_indices[i];
		if (parent_index != (size_t)-1) {
			size_t parent_offset = symbol_name_strtab_offsets[parent_index];
			symbol_name_strtab_offsets[i] = parent_offset + substr_offsets[i];
		}
	}
}

static void push_shuffled_symbol(char *shuffled_symbol) {
	grug_assert(shuffled_symbols_size < MAX_SYMBOLS, "There are more than %d symbols, exceeding MAX_SYMBOLS", MAX_SYMBOLS);

	shuffled_symbols[shuffled_symbols_size++] = shuffled_symbol;
}

// See my blog post: https://mynameistrez.github.io/2024/06/19/array-based-hash-table-in-c.html
// See https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l618)
static void generate_shuffled_symbols(void) {
	static u32 buckets[BFD_HASH_BUCKET_SIZE];

	memset(buckets, 0, BFD_HASH_BUCKET_SIZE * sizeof(u32));

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
			char *symbol = symbols[chain_index - 1];

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
	size_t offset = 1;

	static size_t parent_indices[MAX_SYMBOLS];
	static size_t substr_offsets[MAX_SYMBOLS];

	memset(parent_indices, -1, symbols_size * sizeof(size_t));

	memset(is_substrs, false, symbols_size * sizeof(bool));

	// This function could be optimized from O(n^2) to O(n) with a hash table
	for (size_t i = 0; i < symbols_size; i++) {
		char *symbol = symbols[i];

		size_t parent_index;
		size_t ending_index;
		for (parent_index = 0; parent_index < symbols_size; parent_index++) {
			if (i != parent_index) {
				ending_index = get_ending_index(symbols[parent_index], symbol);
				if (ending_index != (size_t)-1) {
					break;
				}
			}
		}

		// If symbol wasn't at the end of another symbol
		bool is_substr = parent_index != symbols_size;

		if (is_substr) {
			parent_indices[i] = parent_index;
			substr_offsets[i] = ending_index;
		} else {
			symbol_name_dynstr_offsets[i] = offset;
			offset += strlen(symbol) + 1;
		}

		is_substrs[i] = is_substr;
	}

	// Now that all the parents have been given final offsets in .dynstr,
	// it is clear what index their substring symbols have
	for (size_t i = 0; i < symbols_size; i++) {
		size_t parent_index = parent_indices[i];
		if (parent_index != (size_t)-1) {
			size_t parent_offset = symbol_name_dynstr_offsets[parent_index];
			symbol_name_dynstr_offsets[i] = parent_offset + substr_offsets[i];
		}
	}
}

static void push_symbol(char *symbol) {
	grug_assert(symbols_size < MAX_SYMBOLS, "There are more than %d symbols, exceeding MAX_SYMBOLS", MAX_SYMBOLS);

	symbols[symbols_size++] = symbol;
}

static void init_section_header_indices(void) {
	size_t shindex = 1;

	shindex_hash = shindex++;
	shindex_dynsym = shindex++;
	shindex_dynstr = shindex++;
	if (on_fns_size > 0) {
		shindex_rela_dyn = shindex++;
	}
	shindex_rela_plt = shindex++;
	shindex_plt = shindex++;
	shindex_text = shindex++;
	shindex_eh_frame = shindex++;
	shindex_dynamic = shindex++;
	shindex_got_plt = shindex++;
	shindex_data = shindex++;
	shindex_symtab = shindex++;
	shindex_strtab = shindex++;
	shindex_shstrtab = shindex++;
}

static void generate_shared_object(char *grug_path, char *dll_path) {
	text_size = codes_size;

	reset_generate_shared_object();

	init_section_header_indices();

	push_symbol("define_type");
	data_symbols_size++;

	push_symbol("globals_size");
	data_symbols_size++;

	if (grug_define_entity->on_function_count > 0) {
		push_symbol("on_fns");
		data_symbols_size++;
	}

	if (data_strings_size > 0) {
		push_symbol("strings");
		data_symbols_size++;
	}

	first_used_game_fn_symbol_index = data_symbols_size;
	for (size_t i = 0; i < used_game_fns_size; i++) {
		push_symbol(used_game_fns[i]);
	}

	push_symbol("define");
	push_symbol("init_globals");

	on_fns_symbol_offset = symbols_size;
	for (size_t i = 0; i < on_fns_size; i++) {
		push_symbol(on_fns[i].fn_name);
	}

	for (size_t i = 0; i < helper_fns_size; i++) {
		push_symbol(helper_fns[i].fn_name);
	}

	init_symbol_name_dynstr_offsets();

	generate_shuffled_symbols();

	init_symbol_name_strtab_offsets();

	init_data_offsets();

	hash_on_fns();

	push_bytes(grug_path);

	patch_bytes();

	FILE *f = fopen(dll_path, "w");
	grug_assert(f, "fopen: %s", strerror(errno));
	fwrite(bytes, sizeof(u8), bytes_size, f);
	fclose(f);
}

//// HOT RELOADING

struct grug_mod_dir grug_mods;

struct grug_modified *grug_reloads;
size_t grug_reloads_size;
static size_t reloads_capacity;

static void regenerate_dll(char *grug_path, char *dll_path) {
	grug_log("# Regenerating %s\n", dll_path);

	static bool parsed_mod_api_json = false;
	if (!parsed_mod_api_json) {
		parse_mod_api_json();
		parsed_mod_api_json = true;
	}

	reset_utils();

	char *grug_text = read_file(grug_path);
	grug_log("\n# Read text\n%s", grug_text);

	tokenize(grug_text);
	grug_log("\n# Tokens\n");
#ifdef LOGGING
	print_tokens();
#else
	(void)print_tokens;
#endif

	verify_and_trim_spaces();
	grug_log("\n# Tokens after verify_and_trim_spaces()\n");
#ifdef LOGGING
	print_tokens();
#endif

	parse();
	grug_log("\n# AST (throw this into a JSON formatter)\n");
#ifdef LOGGING
	print_ast();
#else
	(void)print_ast;
#endif

	compile();

	grug_log("\n# Section offsets\n");
	generate_shared_object(grug_path, dll_path);
}

// Returns whether an error occurred
bool grug_test_regenerate_dll(char *grug_path, char *dll_path) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}
	regenerate_dll(grug_path, dll_path);
	return false;
}

static void try_create_parent_dirs(char *file_path) {
	char parent_dir_path[STUPID_MAX_PATH];
	size_t i = 0;

	errno = 0;
	while (*file_path) {
		parent_dir_path[i] = *file_path;
		parent_dir_path[i + 1] = '\0';

		if (*file_path == '/' || *file_path == '\\') {
			grug_assert(mkdir(parent_dir_path, 0777) != -1 || errno == EEXIST, "mkdir: %s", strerror(errno));
		}

		file_path++;
		i++;
	}
}

static void fill_as_path_with_dll_extension(char *dll_path, char *grug_path) {
	dll_path[0] = '\0';
	strncat(dll_path, grug_path, STUPID_MAX_PATH - 1);
	char *ext = get_file_extension(dll_path);
	assert(*ext);
	ext[1] = '\0';
	strncat(ext + 1, "so", STUPID_MAX_PATH - 1 - strlen(dll_path));
}

static void print_dlerror(char *function_name) {
	char *err = dlerror();
	grug_assert(err, "dlerror was asked to find an error string, but it couldn't find one");
	grug_error("%s: %s", function_name, err);
}

static void free_file(struct grug_file file) {
	free(file.name);

	if (file.dll && dlclose(file.dll)) {
		print_dlerror("dlclose");
	}
}

static void free_dir(struct grug_mod_dir dir) {
	free(dir.name);

	for (size_t i = 0; i < dir.dirs_size; i++) {
		free_dir(dir.dirs[i]);
	}
	free(dir.dirs);

	for (size_t i = 0; i < dir.files_size; i++) {
		free_file(dir.files[i]);
	}
	free(dir.files);
}

void grug_free_mods(void) {
	free_dir(grug_mods);
	memset(&grug_mods, 0, sizeof(grug_mods));
}

static void *grug_get(void *dll, char *symbol_name) {
	return dlsym(dll, symbol_name);
}

static void push_reload(struct grug_modified modified) {
	if (grug_reloads_size >= reloads_capacity) {
		reloads_capacity = reloads_capacity == 0 ? 1 : reloads_capacity * 2;
		grug_reloads = realloc(grug_reloads, reloads_capacity * sizeof(*grug_reloads));
		grug_assert(grug_reloads, "realloc: %s", strerror(errno));
	}
	grug_reloads[grug_reloads_size++] = modified;
}

static void push_file(struct grug_mod_dir *dir, struct grug_file file) {
	if (dir->files_size >= dir->files_capacity) {
		dir->files_capacity = dir->files_capacity == 0 ? 1 : dir->files_capacity * 2;
		dir->files = realloc(dir->files, dir->files_capacity * sizeof(*dir->files));
		grug_assert(dir->files, "realloc: %s", strerror(errno));
	}
	dir->files[dir->files_size++] = file;
}

static void push_subdir(struct grug_mod_dir *dir, struct grug_mod_dir subdir) {
	if (dir->dirs_size >= dir->dirs_capacity) {
		dir->dirs_capacity = dir->dirs_capacity == 0 ? 1 : dir->dirs_capacity * 2;
		dir->dirs = realloc(dir->dirs, dir->dirs_capacity * sizeof(*dir->dirs));
		grug_assert(dir->dirs, "realloc: %s", strerror(errno));
	}
	dir->dirs[dir->dirs_size++] = subdir;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static struct grug_file *get_file(struct grug_mod_dir *dir, char *name) {
	for (size_t i = 0; i < dir->files_size; i++) {
		if (streq(dir->files[i].name, name)) {
			return dir->files + i;
		}
	}
	return NULL;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static struct grug_mod_dir *get_subdir(struct grug_mod_dir *dir, char *name) {
	for (size_t i = 0; i < dir->dirs_size; i++) {
		if (streq(dir->dirs[i].name, name)) {
			return dir->dirs + i;
		}
	}
	return NULL;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static bool has_been_seen(char *name, char **seen_names, size_t seen_names_size) {
	for (size_t i = 0; i < seen_names_size; i++) {
		if (streq(seen_names[i], name)) {
			return true;
		}
	}
	return false;
}

static void reload_modified_mods(char *mods_dir_path, char *dll_dir_path, struct grug_mod_dir *dir) {
	DIR *dirp = opendir(mods_dir_path);
	grug_assert(dirp, "opendir: %s", strerror(errno));

	char **seen_dir_names = NULL;
	size_t seen_dir_names_size = 0;
	size_t seen_dir_names_capacity = 0;

	char **seen_file_names = NULL;
	size_t seen_file_names_size = 0;
	size_t seen_file_names_capacity = 0;

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		if (streq(dp->d_name, ".") || streq(dp->d_name, "..")) {
			continue;
		}

		char entry_path[STUPID_MAX_PATH];
		snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_dir_path, dp->d_name);

		char dll_entry_path[STUPID_MAX_PATH];
		snprintf(dll_entry_path, sizeof(dll_entry_path), "%s/%s", dll_dir_path, dp->d_name);

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s", strerror(errno));

		if (S_ISDIR(entry_stat.st_mode)) {
			if (seen_dir_names_size >= seen_dir_names_capacity) {
				seen_dir_names_capacity = seen_dir_names_capacity == 0 ? 1 : seen_dir_names_capacity * 2;
				seen_dir_names = realloc(seen_dir_names, seen_dir_names_capacity * sizeof(*seen_dir_names));
				grug_assert(seen_dir_names, "realloc: %s", strerror(errno));
			}
			seen_dir_names[seen_dir_names_size++] = strdup(dp->d_name);

			struct grug_mod_dir *subdir = get_subdir(dir, dp->d_name);
			if (!subdir) {
				struct grug_mod_dir inserted_subdir = {.name = strdup(dp->d_name)};
				grug_assert(inserted_subdir.name, "strdup: %s", strerror(errno));
				push_subdir(dir, inserted_subdir);
				subdir = dir->dirs + dir->dirs_size - 1;
			}
			reload_modified_mods(entry_path, dll_entry_path, subdir);
		} else if (S_ISREG(entry_stat.st_mode) && streq(get_file_extension(dp->d_name), ".grug")) {
			if (seen_file_names_size >= seen_file_names_capacity) {
				seen_file_names_capacity = seen_file_names_capacity == 0 ? 1 : seen_file_names_capacity * 2;
				seen_file_names = realloc(seen_file_names, seen_file_names_capacity * sizeof(*seen_file_names));
				grug_assert(seen_file_names, "realloc: %s", strerror(errno));
			}
			seen_file_names[seen_file_names_size++] = strdup(dp->d_name);

			char dll_path[STUPID_MAX_PATH];
			fill_as_path_with_dll_extension(dll_path, dll_entry_path);

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
			bool needs_regeneration = !dll_exists || entry_stat.st_mtime > dll_stat.st_mtime;

			struct grug_file *old_file = get_file(dir, dp->d_name);

			if (needs_regeneration || !old_file) {
				struct grug_modified modified = {0};

				if (old_file) {
					modified.old_dll = old_file->dll;
					if (dlclose(old_file->dll)) {
						print_dlerror("dlclose");
					}
				}

				if (needs_regeneration) {
					regenerate_dll(entry_path, dll_path);
				}

				struct grug_file file = {0};
				if (old_file) {
					file.name = old_file->name;
				} else {
					file.name = strdup(dp->d_name);
					grug_assert(file.name, "strdup: %s", strerror(errno));
				}

				file.dll = dlopen(dll_path, RTLD_NOW);
				if (!file.dll) {
					print_dlerror("dlopen");
				}

				#pragma GCC diagnostic push
				#pragma GCC diagnostic ignored "-Wpedantic"
				file.define_fn = grug_get(file.dll, "define");
				#pragma GCC diagnostic pop
				grug_assert(file.define_fn, "Retrieving the define() function with grug_get() failed for %s", dll_path);

				size_t *globals_size_ptr = grug_get(file.dll, "globals_size");
				grug_assert(globals_size_ptr, "Retrieving the globals_size variable with grug_get() failed for %s", dll_path);
				file.globals_size = *globals_size_ptr;

				#pragma GCC diagnostic push
				#pragma GCC diagnostic ignored "-Wpedantic"
				file.init_globals_fn = grug_get(file.dll, "init_globals");
				#pragma GCC diagnostic pop
				grug_assert(file.init_globals_fn, "Retrieving the init_globals() function with grug_get() failed for %s", dll_path);

				file.define_type = grug_get(file.dll, "define_type");
				grug_assert(file.define_type, "Retrieving the define_type string with grug_get() failed for %s", dll_path);

				// on_fns is optional, so don't check for NULL
				file.on_fns = grug_get(file.dll, "on_fns");

				if (old_file) {
					old_file->dll = file.dll;
					old_file->define_fn = file.define_fn;
					old_file->globals_size = file.globals_size;
					old_file->init_globals_fn = file.init_globals_fn;
					old_file->define_type = file.define_type;
					old_file->on_fns = file.on_fns;
				} else {
					push_file(dir, file);
				}

				if (needs_regeneration) {
					modified.new_dll = file.dll;
					modified.define_fn = file.define_fn;
					modified.globals_size = file.globals_size;
					modified.init_globals_fn = file.init_globals_fn;
					modified.define_type = file.define_type;
					modified.on_fns = file.on_fns;
					push_reload(modified);
				}
			}
		}
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);

	// If the directory used to contain a subdirectory or file
	// that doesn't exist anymore, free it
	//
	// TODO: This can be made O(n) rather than O(n*m) by letting every directory contain a "seen" boolean,
	// so that we can iterate over all directories and files once here
	for (size_t i = 0; i < dir->dirs_size; i++) {
		if (!has_been_seen(dir->dirs[i].name, seen_dir_names, seen_dir_names_size)) {
			free_dir(dir->dirs[i]);
			dir->dirs[i] = dir->dirs[--dir->dirs_size]; // Swap-remove
		}
	}
	for (size_t i = 0; i < dir->files_size; i++) {
		if (!has_been_seen(dir->files[i].name, seen_file_names, seen_file_names_size)) {
			free_file(dir->files[i]);
			dir->files[i] = dir->files[--dir->files_size]; // Swap-remove
		}
	}

	for (size_t i = 0; i < seen_dir_names_size; i++) {
		free(seen_dir_names[i]);
	}
	free(seen_dir_names);
	for (size_t i = 0; i < seen_file_names_size; i++) {
		free(seen_file_names[i]);
	}
	free(seen_file_names);
}

// Cases:
// 1. "" => ""
// 2. "/" => ""
// 3. "/a" => "a"
// 4. "/a/" => ""
// 5. "/a/b" => "b"
static char *get_basename(char *path) {
	char *base = strrchr(path, '/');
	return base ? base + 1 : path;
}

// Returns whether an error occurred
bool grug_regenerate_modified_mods(void) {
	assert(!strchr(MODS_DIR_PATH, '\\') && "MODS_DIR_PATH can't contain backslashes, so replace them with '/'");
	assert(MODS_DIR_PATH[strlen(MODS_DIR_PATH) - 1] != '/' && "MODS_DIR_PATH can't have a trailing '/'");

	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	static bool opened_resources = false;
	if (!opened_resources) {
		open_resources();
		opened_resources = true;
	}

	grug_reloads_size = 0;

	if (!grug_mods.name) {
		grug_mods.name = strdup(get_basename(MODS_DIR_PATH));
		grug_assert(grug_mods.name, "strdup: %s", strerror(errno));
	}

	reload_modified_mods(MODS_DIR_PATH, DLL_DIR_PATH, &grug_mods);
	return false;
}
