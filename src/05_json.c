#include "04_runtime_error_handling.c"

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
