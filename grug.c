#include "grug.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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
#define UNREACHABLE_STR "This line of code is supposed to be unreachable. Please report this bug to the grug developers!"

// "The problem is that you can't meaningfully define a constant like this
// in a header file. The maximum path size is actually to be something
// like a filesystem limitation, or at the very least a kernel parameter.
// This means that it's a dynamic value, not something preordained."
// https://eklitzke.org/path-max-is-tricky
#define STUPID_MAX_PATH 4096

#define GRUG_ERROR(...) {\
	int ret = snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__);\
    (void)ret;\
    grug_error.filename = __FILE__;\
	grug_error.line_number = __LINE__;\
	longjmp(error_jmp_buffer, 1);\
}

#ifdef LOGGING
#define grug_log(...) printf(__VA_ARGS__)
#else
#define grug_log(...){ _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wunused-value\"") __VA_ARGS__; _Pragma("GCC diagnostic pop") } while (0)
#endif

grug_error_t grug_error;
static jmp_buf error_jmp_buffer;

//// READING

static char *read_file(char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
        GRUG_ERROR("fopen: %s", strerror(errno));
	}

	if (fseek(f, 0, SEEK_END)) {
        GRUG_ERROR("fseek: %s", strerror(errno));
	}

	long count = ftell(f);
	if (count == -1) {
        GRUG_ERROR("ftell: %s", strerror(errno));
	}

	rewind(f);

    if (count + 1 > MAX_CHARACTERS_IN_FILE) {
        GRUG_ERROR("There are more than %d characters in the grug file, exceeding MAX_CHARACTERS_IN_FILE", MAX_CHARACTERS_IN_FILE);
    }

    static char text[MAX_CHARACTERS_IN_FILE];

	ssize_t bytes_read = fread(text, sizeof(char), count, f);
	if (bytes_read != count) {
        GRUG_ERROR("fread: %s", strerror(errno));
	}

	text[count] = '\0';

    if (fclose(f)) {
        GRUG_ERROR("fclose: %s", strerror(errno));
    }

	return text;
}

//// TOKENIZATION

typedef struct token token_t;

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
	size_t len;
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
static token_t tokens[MAX_TOKENS_IN_FILE];
static size_t tokens_size;

static size_t max_size_t(size_t a, size_t b) {
	if (a > b) {
		return a;
	}
	return b;
}

static token_t peek_token(size_t token_index) {
	if (token_index >= tokens_size) {
		GRUG_ERROR("token_index %zu was out of bounds in peek_token()", token_index);
	}
	return tokens[token_index];
}

static token_t consume_token(size_t *token_index_ptr) {
	return peek_token((*token_index_ptr)++);
}

static void print_tokens(void) {
	size_t longest_token_type_len = 0;
	for (size_t i = 0; i < tokens_size; i++) {
		token_t token = peek_token(i);
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
		token_t token = peek_token(i);

		grug_log("| %*zu ", (int)longest_index, i);

		char *token_type_str = get_token_type_str[token.type];
		grug_log("| %*s ", (int)longest_token_type_len, token_type_str);

		if (token.type == NEWLINES_TOKEN) {
			grug_log("| '");
			for (size_t i = 0; i < token.len; i++) {
				grug_log("\\n");
			}
			grug_log("'\n");
		} else {
			grug_log("| '%.*s'\n", (int)token.len, token.str);
		}
	}

	grug_log("\n");
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

static void push_token(token_t token) {
	if (tokens_size + 1 > MAX_TOKENS_IN_FILE) {
		GRUG_ERROR("There are more than %d tokens in the grug file, exceeding MAX_TOKENS_IN_FILE", MAX_TOKENS_IN_FILE);
	}
	tokens[tokens_size++] = token;
}

static void tokenize(char *grug_text) {
	size_t i = 0;
	while (grug_text[i]) {
		if (       grug_text[i] == '(') {
			push_token((token_t){.type=OPEN_PARENTHESIS_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == ')') {
			push_token((token_t){.type=CLOSE_PARENTHESIS_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '{') {
			push_token((token_t){.type=OPEN_BRACE_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '}') {
			push_token((token_t){.type=CLOSE_BRACE_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '+') {
			push_token((token_t){.type=PLUS_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '-') {
			push_token((token_t){.type=MINUS_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '*') {
			push_token((token_t){.type=MULTIPLICATION_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '/') {
			push_token((token_t){.type=DIVISION_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '%') {
			push_token((token_t){.type=REMAINDER_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == ',') {
			push_token((token_t){.type=COMMA_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == ':') {
			push_token((token_t){.type=COLON_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '.') {
			push_token((token_t){.type=PERIOD_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '=' && grug_text[i + 1] == '=') {
			push_token((token_t){.type=EQUALS_TOKEN, .str=grug_text+i, .len=2});
			i += 2;
		} else if (grug_text[i] == '!' && grug_text[i + 1] == '=') {
			push_token((token_t){.type=NOT_EQUALS_TOKEN, .str=grug_text+i, .len=2});
			i += 2;
		} else if (grug_text[i] == '=') {
			push_token((token_t){.type=ASSIGNMENT_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '>' && grug_text[i + 1] == '=') {
			push_token((token_t){.type=GREATER_OR_EQUAL_TOKEN, .str=grug_text+i, .len=2});
			i += 2;
		} else if (grug_text[i] == '>') {
			push_token((token_t){.type=GREATER_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i] == '<' && grug_text[i + 1] == '=') {
			push_token((token_t){.type=LESS_OR_EQUAL_TOKEN, .str=grug_text+i, .len=2});
			i += 2;
		} else if (grug_text[i] == '<') {
			push_token((token_t){.type=LESS_TOKEN, .str=grug_text+i, .len=1});
			i += 1;
		} else if (grug_text[i + 0] == 'n' && grug_text[i + 1] == 'o' && grug_text[i + 2] == 't' && grug_text[i + 3] == ' ') {
			push_token((token_t){.type=NOT_TOKEN, .str=grug_text+i, .len=3});
			i += 3;
		} else if (grug_text[i + 0] == 't' && grug_text[i + 1] == 'r' && grug_text[i + 2] == 'u' && grug_text[i + 3] == 'e' && grug_text[i + 4] == ' ') {
			push_token((token_t){.type=TRUE_TOKEN, .str=grug_text+i, .len=4});
			i += 4;
		} else if (grug_text[i + 0] == 'f' && grug_text[i + 1] == 'a' && grug_text[i + 2] == 'l' && grug_text[i + 3] == 's' && grug_text[i + 4] == 'e' && grug_text[i + 5] == ' ') {
			push_token((token_t){.type=FALSE_TOKEN, .str=grug_text+i, .len=5});
			i += 5;
		} else if (grug_text[i + 0] == 'i' && grug_text[i + 1] == 'f' && grug_text[i + 2] == ' ') {
			push_token((token_t){.type=IF_TOKEN, .str=grug_text+i, .len=2});
			i += 2;
		} else if (grug_text[i + 0] == 'e' && grug_text[i + 1] == 'l' && grug_text[i + 2] == 's' && grug_text[i + 3] == 'e' && grug_text[i + 4] == ' ') {
			push_token((token_t){.type=ELSE_TOKEN, .str=grug_text+i, .len=4});
			i += 4;
		} else if (grug_text[i + 0] == 'l' && grug_text[i + 1] == 'o' && grug_text[i + 2] == 'o' && grug_text[i + 3] == 'p' && grug_text[i + 4] == ' ') {
			push_token((token_t){.type=LOOP_TOKEN, .str=grug_text+i, .len=4});
			i += 4;
		} else if (grug_text[i + 0] == 'b' && grug_text[i + 1] == 'r' && grug_text[i + 2] == 'e' && grug_text[i + 3] == 'a' && grug_text[i + 4] == 'k' && (grug_text[i + 5] == ' ' || grug_text[i + 5] == '\n')) {
			push_token((token_t){.type=BREAK_TOKEN, .str=grug_text+i, .len=5});
			i += 5;
		} else if (grug_text[i + 0] == 'r' && grug_text[i + 1] == 'e' && grug_text[i + 2] == 't' && grug_text[i + 3] == 'u' && grug_text[i + 4] == 'r' && grug_text[i + 5] == 'n' && (grug_text[i + 6] == ' ' || grug_text[i + 6] == '\n')) {
			push_token((token_t){.type=RETURN_TOKEN, .str=grug_text+i, .len=6});
			i += 6;
		} else if (grug_text[i + 0] == 'c' && grug_text[i + 1] == 'o' && grug_text[i + 2] == 'n' && grug_text[i + 3] == 't' && grug_text[i + 4] == 'i' && grug_text[i + 5] == 'n' && grug_text[i + 6] == 'u' && grug_text[i + 7] == 'e' && (grug_text[i + 8] == ' ' || grug_text[i + 8] == '\n')) {
			push_token((token_t){.type=CONTINUE_TOKEN, .str=grug_text+i, .len=8});
			i += 8;
		} else if (grug_text[i] == ' ') {
			token_t token = {.type=SPACES_TOKEN, .str=grug_text+i};

			do {
				i++;
			} while (grug_text[i] == ' ');

			token.len = i - (token.str - grug_text);
			push_token(token);
		} else if (grug_text[i] == '\n') {
			token_t token = {.type=NEWLINES_TOKEN, .str=grug_text+i};

			do {
				i++;
			} while (grug_text[i] == '\n');

			token.len = i - (token.str - grug_text);
			push_token(token);
		} else if (grug_text[i] == '\"') {
			token_t token = {.type=STRING_TOKEN, .str=grug_text+i};

			do {
				i++;
			} while (grug_text[i] != '\"' && grug_text[i] != '\0');

			if (grug_text[i] == '\"') {
				i++;
			}

			token.len = i - (token.str - grug_text);
			push_token(token);
		} else if (isalpha(grug_text[i]) || grug_text[i] == '_') {
			token_t token = {.type=WORD_TOKEN, .str=grug_text+i};

			do {
				i++;
			} while (isalnum(grug_text[i]) || grug_text[i] == '_');

			token.len = i - (token.str - grug_text);
			push_token(token);
		} else if (isdigit(grug_text[i])) {
			token_t token = {.type=NUMBER_TOKEN, .str=grug_text+i};

			bool seen_period = false;

			do {
				i++;

				if (grug_text[i] == '.') {
					if (seen_period) {
						GRUG_ERROR("Encountered two '.' periods in a number at character %zu of the grug text file", i);
					}
					seen_period = true;
				}
			} while (isdigit(grug_text[i]));

			token.len = i - (token.str - grug_text);
			push_token(token);
		} else if (grug_text[i] == ';') {
			token_t token = {.type=COMMENT_TOKEN, .str=grug_text+i};

			while (true) {
				i++;
				if (!isprint(grug_text[i])) {
					if (grug_text[i] == '\n' || grug_text[i] == '\0') {
						break;
					}

					GRUG_ERROR("Unexpected unprintable character '%.*s' at character %zu of the grug text file", is_escaped_char(grug_text[i]) ? 2 : 1, get_escaped_char(&grug_text[i]), i + 1);
				}
			}

			token.len = i - (token.str - grug_text);
			push_token(token);
		} else {
			GRUG_ERROR("Unrecognized character '%.*s' at character %zu of the grug text file", is_escaped_char(grug_text[i]) ? 2 : 1, get_escaped_char(&grug_text[i]), i + 1);
		}
	}
}

//// PARSING

typedef struct literal_expr literal_expr_t;
typedef struct unary_expr unary_expr_t;
typedef struct binary_expr binary_expr_t;
typedef struct call_expr call_expr_t;
typedef struct field field_t;
typedef struct compound_literal compound_literal_t;
typedef struct parenthesized_expr parenthesized_expr_t;
typedef struct expr expr_t;
typedef struct variable_statement variable_statement_t;
typedef struct call_statement call_statement_t;
typedef struct if_statement if_statement_t;
typedef struct return_statement return_statement_t;
typedef struct loop_statement loop_statement_t;
typedef struct statement statement_t;
typedef struct argument argument_t;
typedef struct define_fn define_fn_t;
typedef struct on_fn on_fn_t;
typedef struct helper_fn helper_fn_t;
typedef struct global_variable global_variable_t;

struct literal_expr {
	char *str;
	size_t len;
};

struct unary_expr {
	enum token_type operator;
	size_t expr_index;
};

struct binary_expr {
	size_t left_expr_index;
	enum token_type operator;
	size_t right_expr_index;
};

struct call_expr {
	char *fn_name;
	size_t fn_name_len;
	size_t arguments_exprs_offset;
	size_t argument_count;
};

struct field {
	char *key;
	size_t key_len;
	char *value;
	size_t value_len;
};
static field_t fields[MAX_FIELDS_IN_FILE];
static size_t fields_size;

struct compound_literal {
	size_t fields_offset;
	size_t field_count;
};

struct parenthesized_expr {
	size_t expr_index;
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
		CALL_EXPR,
		PARENTHESIZED_EXPR,
	} type;
	union {
		literal_expr_t literal_expr;
		unary_expr_t unary_expr;
		binary_expr_t binary_expr;
		call_expr_t call_expr;
		parenthesized_expr_t parenthesized_expr;
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
	[CALL_EXPR] = "CALL_EXPR",
	[PARENTHESIZED_EXPR] = "PARENTHESIZED_EXPR",
};
static expr_t exprs[MAX_EXPRS_IN_FILE];
static size_t exprs_size;

struct variable_statement {
	char *name;
	size_t name_len;
	char *type;
	size_t type_len;
	bool has_type;
	size_t assignment_expr_index;
	bool has_assignment;
};

struct call_statement {
	size_t expr_index;
};

struct if_statement {
	expr_t condition;
	size_t if_body_statements_offset;
	size_t if_body_statement_count;
	size_t else_body_statements_offset;
	size_t else_body_statement_count;
};

struct return_statement {
	size_t value_expr_index;
	bool has_value;
};

struct loop_statement {
	size_t body_statements_offset;
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
		variable_statement_t variable_statement;
		call_statement_t call_statement;
		if_statement_t if_statement;
		return_statement_t return_statement;
		loop_statement_t loop_statement;
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
static statement_t statements[MAX_STATEMENTS_IN_FILE];
static size_t statements_size;

struct argument {
	char *type;
	size_t type_len;
	char *name;
	size_t name_len;
};
static argument_t arguments[MAX_ARGUMENTS_IN_FILE];
static size_t arguments_size;

struct define_fn {
	char *return_type;
	size_t return_type_len;
	compound_literal_t returned_compound_literal;
};
static define_fn_t define_fn;

struct on_fn {
	char *fn_name;
	size_t fn_name_len;
	size_t arguments_offset;
	size_t argument_count;
	size_t body_statements_offset;
	size_t body_statement_count;
};
static on_fn_t on_fns[MAX_ON_FNS_IN_FILE];
static size_t on_fns_size;

struct helper_fn {
	char *fn_name;
	size_t fn_name_len;
	size_t arguments_offset;
	size_t argument_count;
	char *return_type;
	size_t return_type_len;
	size_t body_statements_offset;
	size_t body_statement_count;
};
static helper_fn_t helper_fns[MAX_HELPER_FNS_IN_FILE];
static size_t helper_fns_size;

struct global_variable {
	char *name;
	size_t name_len;
	char *type;
	size_t type_len;
	expr_t assignment_expr;
};
static global_variable_t global_variables[MAX_GLOBAL_VARIABLES_IN_FILE];
static size_t global_variables_size;

static void print_expr(expr_t expr);

static void print_parenthesized_expr(parenthesized_expr_t parenthesized_expr) {
	grug_log("\"expr\": {\n");
	print_expr(exprs[parenthesized_expr.expr_index]);
	grug_log("},\n");
}

static void print_call_expr(call_expr_t call_expr) {
	grug_log("\"fn_name\": \"%.*s\",\n", (int)call_expr.fn_name_len, call_expr.fn_name);
	// { _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wunused-value\"") "\"fn_name\": \"%.*s\",\n", (int)call_expr.fn_name_len, call_expr.fn_name; _Pragma("GCC diagnostic pop") } while (0)

	grug_log("\"arguments\": [\n");
	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		grug_log("{\n");
		print_expr(exprs[call_expr.arguments_exprs_offset + argument_index]);
		grug_log("},\n");
	}
	grug_log("],\n");
}

static void print_binary_expr(binary_expr_t binary_expr) {
	grug_log("\"left_expr\": {\n");
	print_expr(exprs[binary_expr.left_expr_index]);
	grug_log("},\n");
	grug_log("\"operator\": \"%s\",\n", get_token_type_str[binary_expr.operator]);
	grug_log("\"right_expr\": {\n");
	print_expr(exprs[binary_expr.right_expr_index]);
	grug_log("},\n");
}

static void print_expr(expr_t expr) {
	grug_log("\"type\": \"%s\",\n", get_expr_type_str[expr.type]);

	switch (expr.type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
			break;
		case STRING_EXPR:
		case IDENTIFIER_EXPR:
		case NUMBER_EXPR:
			grug_log("\"str\": \"%.*s\",\n", (int)expr.literal_expr.len, expr.literal_expr.str);
			break;
		case UNARY_EXPR:
			grug_log("\"operator\": \"%s\",\n", get_token_type_str[expr.unary_expr.operator]);
			grug_log("\"expr\": {\n");
			print_expr(exprs[expr.unary_expr.expr_index]);
			grug_log("},\n");
			break;
		case BINARY_EXPR:
			print_binary_expr(expr.binary_expr);
			break;
		case CALL_EXPR:
			print_call_expr(expr.call_expr);
			break;
		case PARENTHESIZED_EXPR:
			print_parenthesized_expr(expr.parenthesized_expr);
			break;
	}
}

static void print_statements(size_t statements_offset, size_t statement_count) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		grug_log("{\n");

		statement_t statement = statements[statements_offset + statement_index];

		grug_log("\"type\": \"%s\",\n", get_statement_type_str[statement.type]);

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				grug_log("\"variable_name\": \"%.*s\",\n", (int)statement.variable_statement.name_len, statement.variable_statement.name);

				if (statement.variable_statement.has_type) {
					grug_log("\"variable_type\": \"%.*s\",\n", (int)statement.variable_statement.type_len, statement.variable_statement.type);
				}

				if (statement.variable_statement.has_assignment) {
					grug_log("\"assignment\": {\n");
					print_expr(exprs[statement.variable_statement.assignment_expr_index]);
					grug_log("},\n");
				}

				break;
			case CALL_STATEMENT:
				print_call_expr(exprs[statement.call_statement.expr_index].call_expr);
				break;
			case IF_STATEMENT:
				grug_log("\"condition\": {\n");
				print_expr(statement.if_statement.condition);
				grug_log("},\n");

				grug_log("\"if_statements\": [\n");
				print_statements(statement.if_statement.if_body_statements_offset, statement.if_statement.if_body_statement_count);
				grug_log("],\n");

				if (statement.if_statement.else_body_statement_count > 0) {
					grug_log("\"else_statements\": [\n");
					print_statements(statement.if_statement.else_body_statements_offset, statement.if_statement.else_body_statement_count);
					grug_log("],\n");
				}

				break;
			case RETURN_STATEMENT:
				if (statement.return_statement.has_value) {
					expr_t return_expr = exprs[statement.return_statement.value_expr_index];
					grug_log("\"expr\": {\n");
					print_expr(return_expr);
					grug_log("},\n");
				}
				break;
			case LOOP_STATEMENT:
				grug_log("\"statements\": [\n");
				print_statements(statement.loop_statement.body_statements_offset, statement.loop_statement.body_statement_count);
				grug_log("],\n");
				break;
			case BREAK_STATEMENT:
				break;
			case CONTINUE_STATEMENT:
				break;
		}

		grug_log("},\n");
	}
}

static void print_arguments(size_t arguments_offset, size_t argument_count) {
	grug_log("\"arguments\": [\n");

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		grug_log("{\n");

		argument_t arg = arguments[arguments_offset + argument_index];

		grug_log("\"name\": \"%.*s\",\n", (int)arg.name_len, arg.name);
		grug_log("\"type\": \"%.*s\",\n", (int)arg.type_len, arg.type);

		grug_log("},\n");
	}

	grug_log("],\n");
}

static void print_helper_fns(void) {
	grug_log("\"helper_fns\": [\n");

	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
		grug_log("{\n");

		helper_fn_t fn = helper_fns[fn_index];

		grug_log("\"fn_name\": \"%.*s\",\n", (int)fn.fn_name_len, fn.fn_name);

		print_arguments(fn.arguments_offset, fn.argument_count);

		if (fn.return_type_len > 0) {
			grug_log("\"return_type\": \"%.*s\",\n", (int)fn.return_type_len, fn.return_type);
		}

		grug_log("\"statements\": [\n");
		print_statements(fn.body_statements_offset, fn.body_statement_count);
		grug_log("],\n");

		grug_log("},\n");
	}

	grug_log("],\n");
}

static void print_on_fns(void) {
	grug_log("\"on_fns\": [\n");

	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
		grug_log("{\n");

		on_fn_t fn = on_fns[fn_index];

		grug_log("\"fn_name\": \"%.*s\",\n", (int)fn.fn_name_len, fn.fn_name);

		print_arguments(fn.arguments_offset, fn.argument_count);

		grug_log("\"statements\": [\n");
		print_statements(fn.body_statements_offset, fn.body_statement_count);
		grug_log("],\n");

		grug_log("},\n");
	}

	grug_log("],\n");
}

static void print_global_variables(void) {
	grug_log("\"global_variables\": [\n");

	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
		grug_log("{\n");

		global_variable_t global_variable = global_variables[global_variable_index];

		grug_log("\"variable_name\": \"%.*s\",\n", (int)global_variable.name_len, global_variable.name);

		grug_log("\"variable_type\": \"%.*s\",\n", (int)global_variable.type_len, global_variable.type);

		grug_log("\"assignment\": {\n");
		print_expr(global_variable.assignment_expr);
		grug_log("},\n");

		grug_log("},\n");
	}

	grug_log("],\n");
}

static void print_compound_literal(compound_literal_t compound_literal) {
	grug_log("\"returned_compound_literal\": [\n");

	for (size_t field_index = 0; field_index < compound_literal.field_count; field_index++) {
		grug_log("{\n");

		field_t field = fields[compound_literal.fields_offset + field_index];

		grug_log("\"key\": \"%.*s\",\n", (int)field.key_len, field.key);
		grug_log("\"value\": %.*s,\n", (int)field.value_len, field.value);

		grug_log("},\n");
	}

	grug_log("]\n");
}

static void print_define_fn(void) {
	grug_log("\"define_fn\": {\n");

	grug_log("\"return_type\": \"%.*s\",\n", (int)define_fn.return_type_len, define_fn.return_type);

	print_compound_literal(define_fn.returned_compound_literal);

	grug_log("},\n");
}

static void print_fns(void) {
	grug_log("{\n");

	print_define_fn();
	print_global_variables();
	print_on_fns();
	print_helper_fns();

	grug_log("}\n");
}

static void push_helper_fn(helper_fn_t helper_fn) {
	if (helper_fns_size + 1 > MAX_HELPER_FNS_IN_FILE) {
		GRUG_ERROR("There are more than %d helper_fns in the grug file, exceeding MAX_HELPER_FNS_IN_FILE", MAX_HELPER_FNS_IN_FILE);
	}
	helper_fns[helper_fns_size++] = helper_fn;
}

static void push_on_fn(on_fn_t on_fn) {
	if (on_fns_size + 1 > MAX_ON_FNS_IN_FILE) {
		GRUG_ERROR("There are more than %d on_fns in the grug file, exceeding MAX_ON_FNS_IN_FILE", MAX_ON_FNS_IN_FILE);
	}
	on_fns[on_fns_size++] = on_fn;
}

static size_t push_statement(statement_t statement) {
	if (statements_size + 1 > MAX_STATEMENTS_IN_FILE) {
		GRUG_ERROR("There are more than %d statements in the grug file, exceeding MAX_STATEMENTS_IN_FILE", MAX_STATEMENTS_IN_FILE);
	}
	statements[statements_size] = statement;
	return statements_size++;
}

static size_t push_expr(expr_t expr) {
	if (exprs_size + 1 > MAX_EXPRS_IN_FILE) {
		GRUG_ERROR("There are more than %d exprs in the grug file, exceeding MAX_EXPRS_IN_FILE", MAX_EXPRS_IN_FILE);
	}
	exprs[exprs_size] = expr;
	return exprs_size++;
}

static void potentially_skip_comment(size_t *i) {
	token_t token = peek_token(*i);
	if (token.type == COMMENT_TOKEN) {
		(*i)++;
	}
}

static void assert_token_type(size_t token_index, unsigned int expected_type) {
	token_t token = peek_token(token_index);
	if (token.type != expected_type) {
		GRUG_ERROR("Expected token type %s, but got %s at token index %zu", get_token_type_str[expected_type], get_token_type_str[token.type], token_index);
	}
}

static void consume_token_type(size_t *token_index_ptr, unsigned int expected_type) {
	assert_token_type((*token_index_ptr)++, expected_type);
}

static void consume_1_newline(size_t *token_index_ptr) {
	assert_token_type(*token_index_ptr, NEWLINES_TOKEN);

	token_t token = peek_token(*token_index_ptr);
	if (token.len != 1) {
		GRUG_ERROR("Expected 1 newline, but got %zu at token index %zu", token.len, *token_index_ptr);
	}

	(*token_index_ptr)++;
}

static expr_t parse_expression(size_t *i);

static expr_t parse_primary(size_t *i) {
	token_t token = peek_token(*i);

	expr_t expr = {0};

	switch (token.type) {
		case OPEN_PARENTHESIS_TOKEN:
			(*i)++;
			expr.type = PARENTHESIZED_EXPR;
			expr.parenthesized_expr.expr_index = push_expr(parse_expression(i));
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
			expr.literal_expr.str = token.str;
			expr.literal_expr.len = token.len;
			return expr;
		case WORD_TOKEN:
			(*i)++;
			expr.type = IDENTIFIER_EXPR;
			expr.literal_expr.str = token.str;
			expr.literal_expr.len = token.len;
			return expr;
		case NUMBER_TOKEN:
			(*i)++;
			expr.type = NUMBER_EXPR;
			expr.literal_expr.str = token.str;
			expr.literal_expr.len = token.len;
			return expr;
		default:
			GRUG_ERROR("Expected a primary expression token, but got token type %s at token index %zu", get_token_type_str[token.type], *i);
	}
}

static expr_t parse_call(size_t *i) {
	expr_t expr = parse_primary(i);

	token_t token = peek_token(*i);
	if (token.type == OPEN_PARENTHESIS_TOKEN) {
		(*i)++;

		if (expr.type != IDENTIFIER_EXPR) {
			GRUG_ERROR("Unexpected open parenthesis after non-identifier expression type %s at token index %zu", get_expr_type_str[expr.type], *i - 2);
		}
		expr.type = CALL_EXPR;

		expr.call_expr.fn_name = expr.literal_expr.str;
		expr.call_expr.fn_name_len = expr.literal_expr.len;

		expr.call_expr.argument_count = 0;

		token = peek_token(*i);
		if (token.type == CLOSE_PARENTHESIS_TOKEN) {
			(*i)++;
		} else {
			expr_t local_call_arguments[MAX_CALL_ARGUMENTS_PER_STACK_FRAME];

			while (true) {
				expr_t call_argument = parse_expression(i);

				if (expr.call_expr.argument_count + 1 > MAX_CALL_ARGUMENTS_PER_STACK_FRAME) {
					GRUG_ERROR("There are more than %d arguments to a function call in one of the grug file's stack frames, exceeding MAX_CALL_ARGUMENTS_PER_STACK_FRAME", MAX_CALL_ARGUMENTS_PER_STACK_FRAME);
				}
				local_call_arguments[expr.call_expr.argument_count++] = call_argument;

				token = peek_token(*i);
				if (token.type != COMMA_TOKEN) {
					assert_token_type(*i, CLOSE_PARENTHESIS_TOKEN);
					(*i)++;
					break;
				}
				(*i)++;
			}

			expr.call_expr.arguments_exprs_offset = exprs_size;
			for (size_t i = 0; i < expr.call_expr.argument_count; i++) {
				push_expr(local_call_arguments[i]);
			}
		}
	}

	return expr;
}

static expr_t parse_member(size_t *i) {
	expr_t expr = parse_call(i);

	while (true) {
		token_t token = peek_token(*i);
		if (token.type != PERIOD_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary_expr.left_expr_index = push_expr(expr);
		expr.binary_expr.operator = PERIOD_TOKEN;
		expr.binary_expr.right_expr_index = push_expr(parse_call(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static expr_t parse_unary(size_t *i) {
	token_t token = peek_token(*i);
	if (token.type == MINUS_TOKEN
	 || token.type == NOT_TOKEN) {
		(*i)++;
		expr_t expr = {0};

		expr.unary_expr.operator = token.type;
		expr.unary_expr.expr_index = push_expr(parse_unary(i));
		expr.type = UNARY_EXPR;
		
		return expr;
	}

	return parse_member(i);
}

static expr_t parse_factor(size_t *i) {
	expr_t expr = parse_unary(i);

	while (true) {
		token_t token = peek_token(*i);
		if (token.type != MULTIPLICATION_TOKEN
		 && token.type != DIVISION_TOKEN
		 && token.type != REMAINDER_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary_expr.left_expr_index = push_expr(expr);
		expr.binary_expr.operator = token.type;
		expr.binary_expr.right_expr_index = push_expr(parse_unary(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static expr_t parse_term(size_t *i) {
	expr_t expr = parse_factor(i);

	while (true) {
		token_t token = peek_token(*i);
		if (token.type != PLUS_TOKEN
		 && token.type != MINUS_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary_expr.left_expr_index = push_expr(expr);
		expr.binary_expr.operator = token.type;
		expr.binary_expr.right_expr_index = push_expr(parse_factor(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static expr_t parse_comparison(size_t *i) {
	expr_t expr = parse_term(i);

	while (true) {
		token_t token = peek_token(*i);
		if (token.type != GREATER_OR_EQUAL_TOKEN
		 && token.type != GREATER_TOKEN
		 && token.type != LESS_OR_EQUAL_TOKEN
		 && token.type != LESS_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary_expr.left_expr_index = push_expr(expr);
		expr.binary_expr.operator = token.type;
		expr.binary_expr.right_expr_index = push_expr(parse_term(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

static expr_t parse_equality(size_t *i) {
	expr_t expr = parse_comparison(i);

	while (true) {
		token_t token = peek_token(*i);
		if (token.type != EQUALS_TOKEN
		 && token.type != NOT_EQUALS_TOKEN) {
			break;
		}
		(*i)++;
		expr.binary_expr.left_expr_index = push_expr(expr);
		expr.binary_expr.operator = token.type;
		expr.binary_expr.right_expr_index = push_expr(parse_comparison(i));
		expr.type = BINARY_EXPR;
	}

	return expr;
}

// Recursive descent parsing inspired by the book Crafting Interpreters:
// https://craftinginterpreters.com/parsing-expressions.html#recursive-descent-parsing
static expr_t parse_expression(size_t *i) {
	return parse_equality(i);
}

static void parse_statements(size_t *i, size_t *body_statements_offset, size_t *body_statement_count);

static statement_t parse_if_statement(size_t *i) {
	statement_t statement = {0};
	statement.type = IF_STATEMENT;
	statement.if_statement.condition = parse_expression(i);

	parse_statements(i, &statement.if_statement.if_body_statements_offset, &statement.if_statement.if_body_statement_count);

	if (peek_token(*i).type == ELSE_TOKEN) {
		(*i)++;

		if (peek_token(*i).type == IF_TOKEN) {
			(*i)++;

			statement.if_statement.else_body_statement_count = 1;

			statement_t else_if_statement = parse_if_statement(i);
			statement.if_statement.else_body_statements_offset = push_statement(else_if_statement);
		} else {
			parse_statements(i, &statement.if_statement.else_body_statements_offset, &statement.if_statement.else_body_statement_count);
		}
	}

	return statement;
}

static variable_statement_t parse_variable_statement(size_t *i) {
	variable_statement_t variable_statement = {0};

	token_t name_token = consume_token(i);
	variable_statement.name = name_token.str;
	variable_statement.name_len = name_token.len;

	token_t token = peek_token(*i);
	if (token.type == COLON_TOKEN) {
		(*i)++;

		token_t type_token = consume_token(i);
		if (type_token.type == WORD_TOKEN) {
			variable_statement.has_type = true;
			variable_statement.type = type_token.str;
			variable_statement.type_len = type_token.len;
		} else {
			GRUG_ERROR("Expected a word token after the colon at token index %zu", *i - 3);
		}
	}

	token = peek_token(*i);
	if (token.type == ASSIGNMENT_TOKEN) {
		(*i)++;
		variable_statement.has_assignment = true;
		variable_statement.assignment_expr_index = push_expr(parse_expression(i));
	}

	return variable_statement;
}

static void push_global_variable(global_variable_t global_variable) {
	if (global_variables_size + 1 > MAX_GLOBAL_VARIABLES_IN_FILE) {
		GRUG_ERROR("There are more than %d global variables in the grug file, exceeding MAX_GLOBAL_VARIABLES_IN_FILE", MAX_GLOBAL_VARIABLES_IN_FILE);
	}
	global_variables[global_variables_size++] = global_variable;
}

static void parse_global_variable(size_t *i) {
	global_variable_t global_variable = {0};

	token_t name_token = consume_token(i);
	global_variable.name = name_token.str;
	global_variable.name_len = name_token.len;

	assert_token_type(*i, COLON_TOKEN);
	consume_token(i);

	assert_token_type(*i, WORD_TOKEN);
	token_t type_token = consume_token(i);
	global_variable.type = type_token.str;
	global_variable.type_len = type_token.len;

	assert_token_type(*i, ASSIGNMENT_TOKEN);
	consume_token(i);

	global_variable.assignment_expr = parse_expression(i);

	push_global_variable(global_variable);
}

static statement_t parse_statement(size_t *i) {
	token_t switch_token = peek_token(*i);

	statement_t statement = {0};
	switch (switch_token.type) {
		case WORD_TOKEN: {
			token_t token = peek_token(*i + 1);
			if (token.type == OPEN_PARENTHESIS_TOKEN) {
				statement.type = CALL_STATEMENT;
				expr_t expr = parse_call(i);
				statement.call_statement.expr_index = push_expr(expr);
			} else if (token.type == COLON_TOKEN || token.type == ASSIGNMENT_TOKEN) {
				statement.type = VARIABLE_STATEMENT;
				statement.variable_statement = parse_variable_statement(i);
			} else {
				GRUG_ERROR("Expected '(' or ':' or ' =' after the word '%.*s' at token index %zu", (int)switch_token.len, switch_token.str, *i);
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

			token_t token = peek_token(*i);
			if (token.type == NEWLINES_TOKEN) {
				statement.return_statement.has_value = false;
			} else {
				statement.return_statement.has_value = true;
				statement.return_statement.value_expr_index = push_expr(parse_expression(i));
            }

			break;
		}
		case LOOP_TOKEN:
			(*i)++;
			statement.type = LOOP_STATEMENT;
			parse_statements(i, &statement.loop_statement.body_statements_offset, &statement.loop_statement.body_statement_count);
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
			GRUG_ERROR("Expected a statement token, but got token type %s at token index %zu", get_token_type_str[switch_token.type], *i - 1);
	}

	return statement;
}

static void parse_statements(size_t *i, size_t *body_statements_offset, size_t *body_statement_count) {
	consume_token_type(i, OPEN_BRACE_TOKEN);
	potentially_skip_comment(i);

	consume_1_newline(i);

	// This local array is necessary, cause an IF or LOOP substatement can contain its own statements
	statement_t local_statements[MAX_STATEMENTS_PER_STACK_FRAME];
	*body_statement_count = 0;

	while (true) {
		token_t token = peek_token(*i);
		if (token.type == CLOSE_BRACE_TOKEN) {
			break;
		}

		if (token.type != COMMENT_TOKEN) {
			statement_t statement = parse_statement(i);

			if (*body_statement_count + 1 > MAX_STATEMENTS_PER_STACK_FRAME) {
				GRUG_ERROR("There are more than %d statements in one of the grug file's stack frames, exceeding MAX_STATEMENTS_PER_STACK_FRAME", MAX_STATEMENTS_PER_STACK_FRAME);
			}
			local_statements[(*body_statement_count)++] = statement;
		}
		potentially_skip_comment(i);

		consume_token_type(i, NEWLINES_TOKEN);
	}

	*body_statements_offset = statements_size;
	for (size_t i = 0; i < *body_statement_count; i++) {
		push_statement(local_statements[i]);
	}

	consume_token_type(i, CLOSE_BRACE_TOKEN);

	if (peek_token(*i).type != ELSE_TOKEN) {
		potentially_skip_comment(i);
	}
}

static size_t push_argument(argument_t argument) {
	if (arguments_size + 1 > MAX_ARGUMENTS_IN_FILE) {
		GRUG_ERROR("There are more than %d arguments in the grug file, exceeding MAX_ARGUMENTS_IN_FILE", MAX_ARGUMENTS_IN_FILE);
	}
	arguments[arguments_size] = argument;
	return arguments_size++;
}

static void parse_arguments(size_t *i, size_t *arguments_offset, size_t *argument_count) {
	token_t token = consume_token(i);
	argument_t argument = {.name = token.str, .name_len = token.len};

	consume_token_type(i, COLON_TOKEN);

	assert_token_type(*i, WORD_TOKEN);
	token = consume_token(i);

	argument.type = token.str;
	argument.type_len = token.len;
	*arguments_offset = push_argument(argument);
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
		argument_t argument = {.name = token.str, .name_len = token.len};

		consume_token_type(i, COLON_TOKEN);

		assert_token_type(*i, WORD_TOKEN);
		token = consume_token(i);
		argument.type = token.str;
		argument.type_len = token.len;
		push_argument(argument);
		(*argument_count)++;
	}
}

static void parse_helper_fn(size_t *i) {
	helper_fn_t fn = {0};

	token_t token = consume_token(i);
	fn.fn_name = token.str;
	fn.fn_name_len = token.len;

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		parse_arguments(i, &fn.arguments_offset, &fn.argument_count);
	}

	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		(*i)++;
		fn.return_type = token.str;
		fn.return_type_len = token.len;
	}

	parse_statements(i, &fn.body_statements_offset, &fn.body_statement_count);

	push_helper_fn(fn);
}

static void parse_on_fn(size_t *i) {
	on_fn_t fn = {0};

	token_t token = consume_token(i);
	fn.fn_name = token.str;
	fn.fn_name_len = token.len;

	consume_token_type(i, OPEN_PARENTHESIS_TOKEN);

	token = peek_token(*i);
	if (token.type == WORD_TOKEN) {
		parse_arguments(i, &fn.arguments_offset, &fn.argument_count);
	}

	consume_token_type(i, CLOSE_PARENTHESIS_TOKEN);

	parse_statements(i, &fn.body_statements_offset, &fn.body_statement_count);

	push_on_fn(fn);
}

static void push_field(field_t field) {
	if (fields_size + 1 > MAX_FIELDS_IN_FILE) {
		GRUG_ERROR("There are more than %d fields in the grug file, exceeding MAX_FIELDS_IN_FILE", MAX_FIELDS_IN_FILE);
	}
	fields[fields_size++] = field;
}

static compound_literal_t parse_compound_literal(size_t *i) {
	(*i)++;
	potentially_skip_comment(i);

	compound_literal_t compound_literal = {.fields_offset = fields_size};

	consume_1_newline(i);

	while (true) {
		token_t token = peek_token(*i);
		if (token.type == CLOSE_BRACE_TOKEN) {
			break;
		}

		consume_token_type(i, PERIOD_TOKEN);

		assert_token_type(*i, WORD_TOKEN);
		token = peek_token(*i);
		field_t field = {.key = token.str, .key_len = token.len};
		(*i)++;

		consume_token_type(i, ASSIGNMENT_TOKEN);

		token = peek_token(*i);
		if (token.type != STRING_TOKEN && token.type != NUMBER_TOKEN) {
			GRUG_ERROR("Expected token type STRING_TOKEN or NUMBER_TOKEN, but got %s at token index %zu", get_token_type_str[token.type], *i);
		}
		field.value = token.str;
		field.value_len = token.len;
		push_field(field);
		compound_literal.field_count++;
		(*i)++;

		consume_token_type(i, COMMA_TOKEN);
		potentially_skip_comment(i);

		consume_1_newline(i);
	}

	if (compound_literal.field_count == 0) {
		GRUG_ERROR("Expected at least one field in the compound literal near token index %zu", *i);
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
	token_t token = consume_token(i);
	define_fn.return_type = token.str;
	define_fn.return_type_len = token.len;

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

static bool starts_with(char *a, char *b) {
	return strncmp(a, b, strlen(b)) == 0;
}

static void parse(void) {
	bool seen_define_fn = false;

	size_t i = 0;
	while (i < tokens_size) {
		token_t token = peek_token(i);
		int type = token.type;

		if (       type == WORD_TOKEN && strncmp(token.str, "define", token.len) == 0 && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			if (seen_define_fn) {
				GRUG_ERROR("There can't be more than one define_ function in a grug file");
			}
			parse_define_fn(&i);
			seen_define_fn = true;
		} else if (type == WORD_TOKEN && starts_with(token.str, "on_") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			parse_on_fn(&i);
		} else if (type == WORD_TOKEN && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			parse_helper_fn(&i);
		} else if (type == WORD_TOKEN && peek_token(i + 1).type == COLON_TOKEN) {
			parse_global_variable(&i);
		} else if (type == COMMENT_TOKEN) {
			i++;
		} else if (type == NEWLINES_TOKEN) {
			i++;
		} else {
			GRUG_ERROR("Unexpected token '%.*s' at token index %zu in parse()", (int)token.len, token.str, i);
		}
	}

	if (!seen_define_fn) {
		GRUG_ERROR("Every grug file requires exactly one define_ function");
	}
}

static void assert_spaces(size_t token_index, size_t expected_spaces) {
	assert_token_type(token_index, SPACES_TOKEN);

	token_t token = peek_token(token_index);
	if (token.len != expected_spaces) {
		GRUG_ERROR("Expected %zu space%s, but got %zu at token index %zu", expected_spaces, expected_spaces > 1 ? "s" : "", token.len, token_index);
	}
}

// Trims whitespace tokens after verifying that the formatting is correct.
// 1. The whitespace indentation follows the block scope nesting, like in Python.
// 2. There aren't any leading/trailing/missing/extra spaces.
static void verify_and_trim_spaces(void) {
	size_t i = 0;
	size_t new_index = 0;
	int depth = 0;

	while (i < tokens_size) {
		token_t token = peek_token(i);

		switch (token.type) {
			case OPEN_PARENTHESIS_TOKEN:
			case CLOSE_PARENTHESIS_TOKEN:
			case OPEN_BRACE_TOKEN:
				break;
			case CLOSE_BRACE_TOKEN: {
				depth--;
				if (depth < 0) {
					GRUG_ERROR("Expected a '{' to match the '}' at token index %zu", i + 1);
				}
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
				if (i + 1 >= tokens_size) {
					GRUG_ERROR("Expected something after the comma at token index %zu", i);
				}

				token_t next_token = peek_token(i + 1);
				if (next_token.type != NEWLINES_TOKEN && next_token.type != SPACES_TOKEN) {
					GRUG_ERROR("Expected a single newline or space after the comma, but got token type %s at token index %zu", get_token_type_str[next_token.type], i + 1);
				}
				if (next_token.len != 1) {
					GRUG_ERROR("Expected one newline or space, but got several after the comma at token index %zu", i + 1);
				}

				if (next_token.type == SPACES_TOKEN) {
					if (i + 2 >= tokens_size) {
						GRUG_ERROR("Expected text after the comma and space at token index %zu", i);
					}

					next_token = peek_token(i + 2);
					switch (next_token.type) {
						case OPEN_PARENTHESIS_TOKEN:
						case MINUS_TOKEN:
						case STRING_TOKEN:
						case WORD_TOKEN:
						case NUMBER_TOKEN:
							break;
						default:
							GRUG_ERROR("Unexpected token type %s after the comma and space, at token index %zu", get_token_type_str[next_token.type], i + 2);
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
				if (i + 1 >= tokens_size) {
					GRUG_ERROR("Expected another token after the space at token index %zu", i);
				}

				token_t next_token = peek_token(i + 1);
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
						GRUG_ERROR(UNREACHABLE_STR);
					case NEWLINES_TOKEN:
						GRUG_ERROR("Unexpected trailing whitespace '%.*s' at token index %zu", (int)token.len, token.str, i);
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

						if (next_token.len < 2 || next_token.str[1] != ' ') {
							GRUG_ERROR("Expected the comment token '%.*s' to start with a space character at token index %zu", (int)next_token.len, next_token.str, i + 1);
						}

						if (next_token.len < 3 || isspace(next_token.str[2])) {
							GRUG_ERROR("Expected the comment token '%.*s' to have a text character directly after the space at token index %zu", (int)next_token.len, next_token.str, i + 1);
						}

						if (isspace(next_token.str[next_token.len - 1])) {
							GRUG_ERROR("Unexpected trailing whitespace in the comment token '%.*s' at token index %zu", (int)next_token.len, next_token.str, i + 1);
						}

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

	if (depth > 0) {
		GRUG_ERROR("There were more '{' than '}'");
	}

	tokens_size = new_index;
}

//// SERIALIZING TO C

static char serialized[MAX_SERIALIZED_TO_C_CHARS + 1];
static size_t serialized_size;

static void serialize_append_slice(char *str, size_t len) {
	if (serialized_size + len > MAX_SERIALIZED_TO_C_CHARS) {
		GRUG_ERROR("There are more than %d characters in the output C file, exceeding MAX_SERIALIZED_TO_C_CHARS", MAX_SERIALIZED_TO_C_CHARS);
	}
	memcpy(serialized + serialized_size, str, len);
	serialized_size += len;
}

static void serialize_append(char *str) {
	serialize_append_slice(str, strlen(str));
}

static void serialize_append_indents(size_t depth) {
	for (size_t i = 0; i < depth * SPACES_PER_INDENT; i++) {
		serialize_append(" ");
	}
}

static void serialize_expr(expr_t expr);

static void serialize_parenthesized_expr(parenthesized_expr_t parenthesized_expr) {
	serialize_append("(");
	serialize_expr(exprs[parenthesized_expr.expr_index]);
	serialize_append(")");
}

static bool is_helper_function(char *name, size_t len) {
	for (size_t i = 0; i < helper_fns_size; i++) {
		helper_fn_t fn = helper_fns[i];
		if (fn.fn_name_len == len && memcmp(fn.fn_name, name, len) == 0) {
			return true;
		}
	}
	return false;
}

static void serialize_call_expr(call_expr_t call_expr) {
	serialize_append_slice(call_expr.fn_name, call_expr.fn_name_len);

	serialize_append("(");
	if (is_helper_function(call_expr.fn_name, call_expr.fn_name_len)) {
		serialize_append("globals_void");
		if (call_expr.argument_count > 0) {
			serialize_append(", ");
		}
	}
	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		if (argument_index > 0) {
			serialize_append(", ");
		}

		serialize_expr(exprs[call_expr.arguments_exprs_offset + argument_index]);
	}
	serialize_append(")");
}

static void serialize_operator(enum token_type operator);

static void serialize_binary_expr(binary_expr_t binary_expr) {
	serialize_expr(exprs[binary_expr.left_expr_index]);

	if (binary_expr.operator != PERIOD_TOKEN) {
		serialize_append(" ");
	}

	serialize_operator(binary_expr.operator);

	if (binary_expr.operator != PERIOD_TOKEN) {
		serialize_append(" ");
	}

	serialize_expr(exprs[binary_expr.right_expr_index]);
}

static void serialize_operator(enum token_type operator) {
	switch (operator) {
		case PLUS_TOKEN:
			serialize_append("+");
			return;
		case MINUS_TOKEN:
			serialize_append("-");
			return;
		case MULTIPLICATION_TOKEN:
			serialize_append("*");
			return;
		case DIVISION_TOKEN:
			serialize_append("/");
			return;
		case REMAINDER_TOKEN:
			serialize_append("%");
			return;
		case PERIOD_TOKEN:
			serialize_append(".");
			return;
		case EQUALS_TOKEN:
			serialize_append("==");
			return;
		case NOT_EQUALS_TOKEN:
			serialize_append("!=");
			return;
		case GREATER_OR_EQUAL_TOKEN:
			serialize_append(">=");
			return;
		case GREATER_TOKEN:
			serialize_append(">");
			return;
		case LESS_OR_EQUAL_TOKEN:
			serialize_append("<=");
			return;
		case LESS_TOKEN:
			serialize_append("<");
			return;
		case NOT_TOKEN:
			serialize_append("not");
			return;
		default:
			GRUG_ERROR(UNREACHABLE_STR);
	}
}

static bool is_identifier_global(char *name, size_t len) {
	for (size_t i = 0; i < global_variables_size; i++) {
		global_variable_t global = global_variables[i];
		if (global.name_len == len && memcmp(global.name, name, len) == 0) {
			return true;
		}
	}
	return false;
}

static void serialize_expr(expr_t expr) {
	switch (expr.type) {
		case TRUE_EXPR:
			serialize_append("true");
			break;
		case FALSE_EXPR:
			serialize_append("false");
			break;
		case STRING_EXPR:
			serialize_append_slice(expr.literal_expr.str, expr.literal_expr.len);
			break;
		case IDENTIFIER_EXPR:
			if (is_identifier_global(expr.literal_expr.str, expr.literal_expr.len)) {
				serialize_append("globals->");
			}
			serialize_append_slice(expr.literal_expr.str, expr.literal_expr.len);
			break;
		case NUMBER_EXPR:
			serialize_append_slice(expr.literal_expr.str, expr.literal_expr.len);
			break;
		case UNARY_EXPR:
			serialize_operator(expr.unary_expr.operator);
			serialize_expr(exprs[expr.unary_expr.expr_index]);
			break;
		case BINARY_EXPR:
			serialize_binary_expr(expr.binary_expr);
			break;
		case CALL_EXPR:
			serialize_call_expr(expr.call_expr);
			break;
		case PARENTHESIZED_EXPR:
			serialize_parenthesized_expr(expr.parenthesized_expr);
			break;
	}
}

static void serialize_statements(size_t statements_offset, size_t statement_count, size_t depth) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		statement_t statement = statements[statements_offset + statement_index];

		serialize_append_indents(depth);

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				if (statement.variable_statement.has_type) {
					serialize_append_slice(statement.variable_statement.type, statement.variable_statement.type_len);
					serialize_append(" ");
				}

				if (is_identifier_global(statement.variable_statement.name, statement.variable_statement.name_len)) {
					serialize_append("globals->");
				}
				serialize_append_slice(statement.variable_statement.name, statement.variable_statement.name_len);

				if (statement.variable_statement.has_assignment) {
					serialize_append(" = ");
					serialize_expr(exprs[statement.variable_statement.assignment_expr_index]);
				}

				serialize_append(";");

				break;
			case CALL_STATEMENT:
				serialize_call_expr(exprs[statement.call_statement.expr_index].call_expr);
				serialize_append(";");
				break;
			case IF_STATEMENT:
				serialize_append("if (");
				serialize_expr(statement.if_statement.condition);
				serialize_append(") {\n");
				serialize_statements(statement.if_statement.if_body_statements_offset, statement.if_statement.if_body_statement_count, depth + 1);
				
				if (statement.if_statement.else_body_statement_count > 0) {
					serialize_append_indents(depth);
					serialize_append("} else {\n");
					serialize_statements(statement.if_statement.else_body_statements_offset, statement.if_statement.else_body_statement_count, depth + 1);
				}

				serialize_append_indents(depth);
				serialize_append("}");

				break;
			case RETURN_STATEMENT:
				serialize_append("return");
				if (statement.return_statement.has_value) {
					serialize_append(" ");
					expr_t return_expr = exprs[statement.return_statement.value_expr_index];
					serialize_expr(return_expr);
				}
				serialize_append(";");
				break;
			case LOOP_STATEMENT:
				serialize_append("while (true) {\n");
				serialize_statements(statement.loop_statement.body_statements_offset, statement.loop_statement.body_statement_count, depth + 1);
				serialize_append_indents(depth);
				serialize_append("}");
				break;
			case BREAK_STATEMENT:
				serialize_append("break;");
				break;
			case CONTINUE_STATEMENT:
				serialize_append("continue;");
				break;
		}

		serialize_append("\n");
	}
}

static void serialize_arguments(size_t arguments_offset, size_t argument_count) {
	if (argument_count == 0) {
		return;
	}

	argument_t arg = arguments[arguments_offset];

	serialize_append_slice(arg.type, arg.type_len);
	serialize_append(" ");
	serialize_append_slice(arg.name, arg.name_len);

	for (size_t argument_index = 1; argument_index < argument_count; argument_index++) {
		arg = arguments[arguments_offset + argument_index];

		serialize_append(", ");
		serialize_append_slice(arg.type, arg.type_len);
		serialize_append(" ");
		serialize_append_slice(arg.name, arg.name_len);
	}
}

static void serialize_helper_fns(void) {
	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
		helper_fn_t fn = helper_fns[fn_index];

		serialize_append("\n");

		if (fn.return_type_len > 0) {
			serialize_append_slice(fn.return_type, fn.return_type_len);
		} else {
			serialize_append("void");
		}
		
		serialize_append(" ");
		serialize_append_slice(fn.fn_name, fn.fn_name_len);

		serialize_append("(");
		serialize_append("void *globals_void");
		if (fn.argument_count > 0) {
			serialize_append(", ");
		}
		serialize_arguments(fn.arguments_offset, fn.argument_count);
		serialize_append(") {\n");

		serialize_append_indents(1);
		serialize_append("struct globals *globals = globals_void;\n");

		serialize_append("\n");
		serialize_statements(fn.body_statements_offset, fn.body_statement_count, 1);

		serialize_append("}\n");
	}
}

static void serialize_exported_on_fns(void) {
    serialize_append("struct ");
    serialize_append_slice(define_fn.return_type, define_fn.return_type_len);
    serialize_append("_on_fns on_fns = {\n");

    for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
        on_fn_t fn = on_fns[fn_index];

        serialize_append_indents(1);
        serialize_append(".");

        // Skip the "on_"
        serialize_append_slice(fn.fn_name + 3, fn.fn_name_len - 3);

        serialize_append(" = ");
        serialize_append_slice(fn.fn_name, fn.fn_name_len);
        serialize_append(",\n");
    }

    serialize_append("};\n");
}

static void serialize_on_fns(void) {
	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
		on_fn_t fn = on_fns[fn_index];

		serialize_append("\n");

		serialize_append("static void ");
		serialize_append_slice(fn.fn_name, fn.fn_name_len);

		serialize_append("(");
		serialize_append("void *globals_void");
		if (fn.argument_count > 0) {
			serialize_append(", ");
		}
		serialize_arguments(fn.arguments_offset, fn.argument_count);
		serialize_append(") {\n");

		serialize_append_indents(1);
		serialize_append("struct globals *globals = globals_void;\n");

		serialize_append("\n");
		serialize_statements(fn.body_statements_offset, fn.body_statement_count, 1);

		serialize_append("}\n");
	}
}

static void serialize_forward_declare_helper_fns(void) {
	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
		helper_fn_t fn = helper_fns[fn_index];

		if (fn.return_type_len > 0) {
			serialize_append_slice(fn.return_type, fn.return_type_len);
		} else {
			serialize_append("void");
		}

		serialize_append(" ");
		serialize_append_slice(fn.fn_name, fn.fn_name_len);

		serialize_append("(");
		serialize_append("void *globals_void");
		if (fn.argument_count > 0) {
			serialize_append(", ");
		}
		serialize_arguments(fn.arguments_offset, fn.argument_count);
		serialize_append(");\n");
	}
}

static void serialize_init_globals_struct(void) {
	serialize_append("void init_globals_struct(void *globals_struct) {\n");

	serialize_append_indents(1);
	serialize_append("memcpy(globals_struct, &(struct globals){\n");

	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
		global_variable_t global_variable = global_variables[global_variable_index];

		serialize_append_indents(2);

		serialize_append(".");
		serialize_append_slice(global_variable.name, global_variable.name_len);

		serialize_append(" = ");

		serialize_expr(global_variable.assignment_expr);

		serialize_append(",\n");
	}

	serialize_append_indents(1);
	serialize_append("}, sizeof(struct globals));\n");

	serialize_append("}\n");
}

static void serialize_get_globals_struct_size(void) {
	serialize_append("size_t get_globals_struct_size(void) {\n");
	serialize_append_indents(1);
	serialize_append("return sizeof(struct globals);\n");
	serialize_append("}\n");
}

static void serialize_global_variables(void) {
	serialize_append("struct globals {\n");

	for (size_t global_variable_index = 0; global_variable_index < global_variables_size; global_variable_index++) {
		global_variable_t global_variable = global_variables[global_variable_index];

		serialize_append_indents(1);

		serialize_append_slice(global_variable.type, global_variable.type_len);
		serialize_append(" ");
		serialize_append_slice(global_variable.name, global_variable.name_len);

		serialize_append(";\n");
	}

	serialize_append("};\n");
}

static void serialize_define_type(void) {
    serialize_append("char *define_type = \"");
    serialize_append_slice(define_fn.return_type, define_fn.return_type_len);
    serialize_append("\";\n");
}

static void serialize_define_struct(void) {
	serialize_append("struct ");
	serialize_append_slice(define_fn.return_type, define_fn.return_type_len);
	serialize_append(" define = {\n");

	compound_literal_t compound_literal = define_fn.returned_compound_literal;

	for (size_t field_index = 0; field_index < compound_literal.field_count; field_index++) {
		field_t field = fields[compound_literal.fields_offset + field_index];

		serialize_append_indents(1);
		serialize_append(".");
		serialize_append_slice(field.key, field.key_len);
		serialize_append(" = ");
		serialize_append_slice(field.value, field.value_len);
		serialize_append(",\n");
	}

	serialize_append("};\n");
}

static void serialize_to_c(void) {
	serialize_append("#include \"mod.h\"\n\n");

	serialize_define_struct();
    
    serialize_append("\n");
    serialize_define_type();

	serialize_append("\n");
	serialize_global_variables();

	serialize_append("\n");
	serialize_get_globals_struct_size();

	serialize_append("\n");
	serialize_init_globals_struct();

	if (helper_fns_size > 0) {
		serialize_append("\n");
		serialize_forward_declare_helper_fns();
	}

	if (on_fns_size > 0) {
		serialize_on_fns();
        serialize_append("\n");
        serialize_exported_on_fns();
    }

	if (helper_fns_size > 0) {
		serialize_helper_fns();
	}

	serialized[serialized_size] = '\0';
}

//// COMPILING

// TODO: Write

//// MACHINING

// TODO: Write

//// LINKING

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BYTES 420420
#define MAX_SYMBOLS 420420

#define MAX_HASH_BUCKETS 32771 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elflink.c;h=6db6a9c0b4702c66d73edba87294e2a59ffafcf5;hb=refs/heads/master#l6560

// TODO: These need to be able to grow
#define TEXT_OFFSET 0x1000
#define EH_FRAME_OFFSET 0x2000
#define DYNAMIC_OFFSET 0x2f50
#define DATA_OFFSET 0x3000

#define SYMTAB_ENTRY_SIZE 24

// The array element specifies the location and size of a segment
// which may be made read-only after relocations have been processed
// From https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/progheader.html
#define PT_GNU_RELRO 0x6474e552

#define EH_FRAME_SECTION_HEADER_INDEX 4
#define SYMTAB_SECTION_HEADER_INDEX 7
#define STRTAB_SECTION_HEADER_INDEX 9

// From "st_info" its description here:
// https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-79797/index.html
#define ELF32_ST_INFO(bind, type) (((bind)<<4)+((type)&0xf))

enum d_type {
    DT_NULL = 0, // Marks the end of the _DYNAMIC array
    DT_HASH = 4, // The address of the symbol hash table. This table refers to the symbol table indicated by the DT_SYMTAB element
    DT_STRTAB = 5, // The address of the string table
    DT_SYMTAB = 6, // The address of the symbol table
    DT_STRSZ = 10, // The total size, in bytes, of the DT_STRTAB string table
    DT_SYMENT = 11, // The size, in bytes, of the DT_SYMTAB symbol entry
};

enum p_type {
    PT_LOAD = 1, // Loadable segment
    PT_DYNAMIC = 2, // Dynamic linking information
};

enum p_flags {
    PF_X = 1, // Executable segment
    PF_W = 2, // Writable segment
    PF_R = 4, // Readable segment
};

enum sh_type {
    SHT_PROGBITS = 0x1, // Program data
    SHT_SYMTAB = 0x2, // Symbol table
    SHT_STRTAB = 0x3, // String table
    SHT_HASH = 0x5, // Symbol hash table
    SHT_DYNAMIC = 0x6, // Dynamic linking information
    SHT_DYNSYM = 0xb, // Dynamic linker symbol table
};

enum sh_flags {
    SHF_WRITE = 1, // Writable
    SHF_ALLOC = 2, // Occupies memory during execution
    SHF_EXECINSTR = 4, // Executable machine instructions
};

enum e_type {
    ET_DYN = 3, // Shared object
};

enum st_binding {
    STB_LOCAL = 0, // Local symbol
    STB_GLOBAL = 1, // Global symbol
};

enum st_type {
    STT_NOTYPE = 0, // The symbol type is not specified
    STT_OBJECT = 1, // This symbol is associated with a data object
    STT_FILE = 4, // This symbol is associated with a file
};

enum sh_index {
    SHN_UNDEF = 0, // An undefined section reference
    SHN_ABS = 0xfff1, // Absolute values for the corresponding reference
};

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

static char *symbols[MAX_SYMBOLS];
static size_t symbols_size;

static bool is_substrs[MAX_SYMBOLS];

static size_t symbol_name_dynstr_offsets[MAX_SYMBOLS];
static size_t symbol_name_strtab_offsets[MAX_SYMBOLS];

static u32 buckets[MAX_HASH_BUCKETS];

static u32 chains[MAX_SYMBOLS];
static size_t chains_size;

static char *shuffled_symbols[MAX_SYMBOLS];
static size_t shuffled_symbols_size;

static size_t shuffled_symbol_index_to_symbol_index[MAX_SYMBOLS];

static size_t data_offsets[MAX_SYMBOLS];
static size_t text_offsets[MAX_SYMBOLS];

static u8 bytes[MAX_BYTES];
static size_t bytes_size;

static size_t text_size;
static size_t data_size;
static size_t hash_offset;
static size_t hash_size;
static size_t dynsym_offset;
static size_t dynsym_size;
static size_t dynstr_offset;
static size_t dynstr_size;
static size_t segment_0_size;
static size_t symtab_offset;
static size_t symtab_size;
static size_t strtab_offset;
static size_t strtab_size;
static size_t shstrtab_offset;
static size_t shstrtab_size;
static size_t section_headers_offset;

static void overwrite_address(u64 n, size_t bytes_offset) {
    for (size_t i = 0; i < 8; i++) {
        // Little-endian requires the least significant byte first
        bytes[bytes_offset++] = n & 0xff;

        n >>= 8; // Shift right by one byte
    }
}

static void fix_bytes() {
    // ELF section header table offset
    overwrite_address(section_headers_offset, 0x28);

    // Segment 0 its file_size
    overwrite_address(segment_0_size, 0x60);

    // Segment 0 its mem_size
    overwrite_address(segment_0_size, 0x68);
}

static void push_byte(u8 byte) {
    if (bytes_size + 1 > MAX_BYTES) {
        fprintf(stderr, "error: MAX_BYTES of %d was exceeded\n", MAX_BYTES);
        exit(EXIT_FAILURE);
    }

    bytes[bytes_size++] = byte;
}

static void push_zeros(size_t count) {
    for (size_t i = 0; i < count; i++) {
        push_byte(0);
    }
}

static void push_alignment(size_t alignment) {
    size_t excess = bytes_size % alignment;
    if (excess > 0) {
        push_zeros(alignment - excess);
    }
}

static void push_string(char *str) {
    for (size_t i = 0; i < strlen(str); i++) {
        push_byte(str[i]);
    }
    push_byte('\0');
}

static void push_shstrtab(void) {
    shstrtab_offset = bytes_size;

    push_byte(0);
    push_string(".symtab");
    push_string(".strtab");
    push_string(".shstrtab");
    push_string(".hash");
    push_string(".dynsym");
    push_string(".dynstr");
    push_string(".text");
    push_string(".eh_frame");
    push_string(".dynamic");
    push_string(".data");

    shstrtab_size = bytes_size - shstrtab_offset;

    push_alignment(8);
}

static void push_strtab(void) {
    strtab_offset = bytes_size;

    push_byte(0);
    push_string("full.s");
    
    // Local symbols
    // TODO: Add loop

    push_string("_DYNAMIC");

    // Global symbols
    // TODO: Don't loop through local symbols
    for (size_t i = 0; i < symbols_size; i++) {
        size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

        if (!is_substrs[symbol_index]) {
            push_string(shuffled_symbols[i]);
        }
    }

    strtab_size = bytes_size - strtab_offset;
}

static void push_number(u64 n, size_t byte_count) {
    while (n > 0) {
        // Little-endian requires the least significant byte first
        push_byte(n & 0xff);
        byte_count--;

        n >>= 8; // Shift right by one byte
    }

    // Optional padding
    push_zeros(byte_count);
}

// See https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-79797/index.html
// See https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblj/index.html#chapter6-tbl-21
static void push_symbol_entry(u32 name, u16 info, u16 shndx, u32 offset) {
    push_number(name, 4); // Indexed into .strtab, because .symtab its "link" points to it
    push_number(info, 2);
    push_number(shndx, 2);
    push_number(offset, 4); // In executable and shared object files, st_value holds a virtual address

    // TODO: I'm confused by why we don't seem to need these
    // push_number(size, 4);
    // push_number(other, 4);

    push_zeros(SYMTAB_ENTRY_SIZE - 12);
}

static void push_symtab(void) {
    symtab_offset = bytes_size;

    // Null entry
    push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), SHN_UNDEF, 0);

    // "full.s" entry
    push_symbol_entry(1, ELF32_ST_INFO(STB_LOCAL, STT_FILE), SHN_ABS, 0);

    // TODO: ? entry
    push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_FILE), SHN_ABS, 0);

    // "_DYNAMIC" entry
    push_symbol_entry(8, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), 6, DYNAMIC_OFFSET);

    // The symbols are pushed in shuffled_symbols order
    for (size_t i = 0; i < symbols_size; i++) {
        size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

        bool is_data = symbol_index < 10; // TODO: Use the data symbol count from the AST
        u16 shndx = is_data ? SYMTAB_SECTION_HEADER_INDEX : EH_FRAME_SECTION_HEADER_INDEX;
        u32 offset = is_data ? DATA_OFFSET + data_offsets[symbol_index] : TEXT_OFFSET + text_offsets[symbol_index - 10]; // TODO: Use the data symbol count from the AST

        // The starting offset of 16 is from "full.s" + "_DYNAMIC"
        push_symbol_entry(16 + symbol_name_strtab_offsets[symbol_index], ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), shndx, offset);
    }

    symtab_size = bytes_size - symtab_offset;
}

static void push_data(void) {
    // TODO: Use the data from the AST

    // // "define" symbol
    // push_number(1337, 2);
    // push_byte(69);

    // "define_type" symbol
    push_string("entity");

    push_alignment(8);
}

// See https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-42444.html
static void push_dynamic_entry(u64 tag, u64 value) {
    push_number(tag, 8);
    push_number(value, 8);
}

static void push_dynamic() {
    push_dynamic_entry(DT_HASH, hash_offset);
    push_dynamic_entry(DT_STRTAB, dynstr_offset);
    push_dynamic_entry(DT_SYMTAB, dynsym_offset);
    push_dynamic_entry(DT_STRSZ, dynstr_size);
    push_dynamic_entry(DT_SYMENT, SYMTAB_ENTRY_SIZE);
    push_dynamic_entry(DT_NULL, 0);
    push_dynamic_entry(DT_NULL, 0);
    push_dynamic_entry(DT_NULL, 0);
    push_dynamic_entry(DT_NULL, 0);
    push_dynamic_entry(DT_NULL, 0);
    push_dynamic_entry(DT_NULL, 0);
}

static void push_text(void) {
    // TODO: Use the code from the AST
    push_byte(0xb8);
    push_byte(0x2a);
    push_byte(0);
    push_byte(0);
    push_byte(0);
    push_byte(0xc3);
    push_byte(0xb8);
    push_byte(0x2a);
    push_byte(0);
    push_byte(0);
    push_byte(0);
    push_byte(0xc3);

    push_alignment(8);
}

static void push_dynstr(void) {
    dynstr_offset = bytes_size;

    // .dynstr always starts with a '\0'
    dynstr_size = 1;

    push_byte(0);
    for (size_t i = 0; i < symbols_size; i++) {
        if (!is_substrs[i]) {
            push_string(symbols[i]);
            dynstr_size += strlen(symbols[i]) + 1;
        }
    }

    segment_0_size = bytes_size;

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

// From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elf.c#l193
static u32 elf_hash(const char *namearg) {
    u32 h = 0;

    for (const unsigned char *name = (const unsigned char *) namearg; *name; name++) {
        h = (h << 4) + *name;
        h ^= (h >> 24) & 0xf0;
    }

    return h & 0x0fffffff;
}

static void push_chain(u32 chain) {
    if (chains_size + 1 > MAX_SYMBOLS) {
        fprintf(stderr, "error: MAX_SYMBOLS of %d was exceeded\n", MAX_SYMBOLS);
        exit(EXIT_FAILURE);
    }

    chains[chains_size++] = chain;
}

// See https://flapenguin.me/elf-dt-hash
// See https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.dynamic.html#hash
//
// Example with 16 symbols "abcdefghijklmnop":
// 
// nbuckets: 3 (what get_nbucket() returns when there are 16 symbols)
// nchain: 17 (16 symbols + the SHT_UNDEF at index 0)
// 
// Bucket[i] always has the value of the last entry that has `hash % nbucket` equal to `i`
// 
//  i  bucket[i]  name of first symbol in chain
// --  ---------  -----------------------------
//  0  11         c
//  1  16         m
//  2  15         e
// 
// Two asterisks ** and parens () indicate the start of a chain, so it's easier to see.
// 
//        SYMBOL TABLE   |
//                       |
// 	   name =            | hash =          bucket_index =
//  i  symtab[i].st_name | elf_hash(name)  hash % nbucket
// --  ----------------- | --------------  --------------  
//  0  <STN_UNDEF>       |
//  1  b                 |  98             2                 /---> 0
//  2  p                 | 112             1                 | /-> 0
//  3  j                 | 106             1                 | \-- 2 <---\.
//  4  n                 | 110             2                 \---- 1 <---|-\.
//  5  f                 | 102             0                       0 <-\ | |
//  6  g                 | 103             1                 /---> 3 --|-/ |
//  7  o                 | 111             0                 | /-> 5 --/   |
//  8  l                 | 108             0                 | \-- 7 <-\   |
//  9  k                 | 107             2               /-|---> 4 --|---/
// 10  i                 | 105             0               | | /-> 8 --/
// 11  c                 |  99             0 **            | | \-(10)
// 12  d                 | 100             1               | \---- 6 <-\.
// 13  h                 | 104             2               \------ 9 <-|-\.
// 14  a                 |  97             1                  /-> 12 --/ |
// 15  e                 | 101             2 **               |  (13)----/
// 16  m                 | 109             1 **               \--(14)
static void push_hash(void) {
    hash_offset = bytes_size;

    u32 nbucket = get_nbucket();
    push_number(nbucket, 4);

    u32 nchain = 1 + symbols_size; // `1 + `, because index 0 is always STN_UNDEF (the value 0)
    push_number(nchain, 4);

    memset(buckets, 0, nbucket * sizeof(u32));

    chains_size = 0;

    push_chain(0); // The first entry in the chain is always STN_UNDEF

    for (size_t i = 0; i < symbols_size; i++) {
        u32 hash = elf_hash(shuffled_symbols[i]);
        u32 bucket_index = hash % nbucket;

        push_chain(buckets[bucket_index]);

        buckets[bucket_index] = i + 1;
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
    section_headers_offset = bytes_size;

    // Null section
    push_zeros(0x40);

    // .hash: Hash section
    push_section_header(0x1b, SHT_HASH, SHF_ALLOC, hash_offset, hash_offset, hash_size, 2, 0, 8, 4);

    // .dynsym: Dynamic linker symbol table section
    push_section_header(0x21, SHT_DYNSYM, SHF_ALLOC, dynsym_offset, dynsym_offset, dynsym_size, 3, 1, 8, 0x18);

    // .dynstr: String table section
    push_section_header(0x29, SHT_STRTAB, SHF_ALLOC, dynstr_offset, dynstr_offset, dynstr_size, 0, 0, 1, 0);

    // .text: Code section
    push_section_header(0x31, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, TEXT_OFFSET, TEXT_OFFSET, text_size, 0, 0, 16, 0);

    // .eh_frame: Exception stack unwinding section
    push_section_header(0x37, SHT_PROGBITS, SHF_ALLOC, EH_FRAME_OFFSET, EH_FRAME_OFFSET, 0, 0, 0, 8, 0);

    // .dynamic: Dynamic linking information section
    push_section_header(0x41, SHT_DYNAMIC, SHF_WRITE | SHF_ALLOC, DYNAMIC_OFFSET, DYNAMIC_OFFSET, 0xb0, 3, 0, 8, 0x10);

    // .data: Data section
    push_section_header(0x4a, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, DATA_OFFSET, DATA_OFFSET, data_size, 0, 0, 4, 0);

    // .symtab: Symbol table section
    // The "link" is the section header index of the associated string table
    // The "info" of 4 is the symbol table index of the first non-local symbol, which is the 5th entry in push_symtab(), the global "b" symbol
    push_section_header(0x1, SHT_SYMTAB, 0, 0, symtab_offset, symtab_size, STRTAB_SECTION_HEADER_INDEX, 4, 8, SYMTAB_ENTRY_SIZE);

    // .strtab: String table section
    push_section_header(0x09, SHT_PROGBITS | SHT_SYMTAB, 0, 0, strtab_offset, strtab_size, 0, 0, 1, 0);

    // .shstrtab: Section header string table section
    push_section_header(0x11, SHT_PROGBITS | SHT_SYMTAB, 0, 0, shstrtab_offset, shstrtab_size, 0, 0, 1, 0);
}

static void push_dynsym(void) {
    dynsym_offset = bytes_size;

    // Null entry
    push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), SHN_UNDEF, 0);

    // The symbols are pushed in shuffled_symbols order
    for (size_t i = 0; i < symbols_size; i++) {
        size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

        bool is_data = symbol_index < 10; // TODO: Use the data symbol count from the AST
        u16 shndx = is_data ? SYMTAB_SECTION_HEADER_INDEX : EH_FRAME_SECTION_HEADER_INDEX;
        u32 offset = is_data ? DATA_OFFSET + data_offsets[symbol_index] : TEXT_OFFSET + text_offsets[symbol_index - 10]; // TODO: Use the data symbol count from the AST

        push_symbol_entry(symbol_name_dynstr_offsets[symbol_index], ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), shndx, offset);
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
    // .hash, .dynsym, .dynstr segment
    // file_size and mem_size get overwritten later
    push_program_header(PT_LOAD, PF_R, 0, 0, 0, 0, 0, 0x1000);

    // TODO: Use the data from the AST
	// data_size += 3; // "define" symbol
    data_size += sizeof("entity"); // "define_type" symbol

    // .text segment
    push_program_header(PT_LOAD, PF_R | PF_X, TEXT_OFFSET, TEXT_OFFSET, TEXT_OFFSET, 12, 12, 0x1000);

    // .eh_frame segment
    push_program_header(PT_LOAD, PF_R, EH_FRAME_OFFSET, EH_FRAME_OFFSET, EH_FRAME_OFFSET, 0, 0, 0x1000);

    // .dynamic, .data
    push_program_header(PT_LOAD, PF_R | PF_W, 0x2f50, 0x2f50, 0x2f50, 0xb0 + data_size, 0xb0 + data_size, 0x1000);

    // .dynamic segment
    push_program_header(PT_DYNAMIC, PF_R | PF_W, 0x2f50, 0x2f50, 0x2f50, 0xb0, 0xb0, 8);

    // .dynamic segment
    push_program_header(PT_GNU_RELRO, PF_R, 0x2f50, 0x2f50, 0x2f50, 0xb0, 0xb0, 1);
}

static void push_elf_header(void) {
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

    // Section header table offset (this value gets overwritten later)
    // 0x28 to 0x30
    push_zeros(8);

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
    push_byte(11);
    push_byte(0);

    // Index of entry with section names
    // 0x3e to 0x40
    push_byte(10);
    push_byte(0);
}

static void push_bytes() {
    // 0x0 to 0x40
    push_elf_header();

    // 0x40 to 0x190
    push_program_headers();

    push_hash();

    push_dynsym();

    push_dynstr();

    push_zeros(TEXT_OFFSET - bytes_size);

    push_text();

    push_zeros(DYNAMIC_OFFSET - bytes_size);

    push_dynamic();

    push_data();

    push_symtab();

    push_strtab();

    push_shstrtab();

    push_section_headers();
}

static void init_text_offsets(void) {
    // TODO: Use the data from the AST
    for (size_t i = 0; i < 2; i++) {
        text_offsets[i] = i * 6; // fn1_c takes 6 bytes of instructions
    }
}

static void init_data_offsets(void) {
    // TODO: Use the data from the AST
    size_t i = 0;
    size_t offset = 0;

    // data_offsets[i++] = offset; // "define" symbol
    // offset += 3;

    data_offsets[i++] = offset; // "define_type" symbol
    offset += sizeof("entity");

    // for (size_t j = 0; j < 8; j++) {
    //     data_offsets[i++] = offset;
    //     offset += sizeof("a^");
    // }
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
    size_t offset = 1;

    static size_t parent_indices[MAX_SYMBOLS];
    static size_t substr_offsets[MAX_SYMBOLS];

    memset(parent_indices, -1, symbols_size * sizeof(size_t));

    // This function could be optimized from O(n^2) to O(n) with a hash map
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

        // If symbol wasn't in the end of another symbol
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
    if (shuffled_symbols_size + 1 > MAX_SYMBOLS) {
        fprintf(stderr, "error: MAX_SYMBOLS of %d was exceeded\n", MAX_SYMBOLS);
        exit(EXIT_FAILURE);
    }

    shuffled_symbols[shuffled_symbols_size++] = shuffled_symbol;
}

// This is solely here to put the symbols in the same weird order as ld does
// From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l508
static unsigned long bfd_hash_hash(const char *string) {
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

// See the documentation of push_hash() for how this function roughly works
//
// name | index
// "a"  | 3485
// "b"  |  245
// "c"  | 2224
// "d"  | 2763
// "e"  | 3574
// "f"  |  553
// "g"  |  872
// "h"  | 3042
// "i"  | 1868
// "j"  |  340
// "k"  | 1151
// "l"  | 1146
// "m"  | 3669
// "n"  |  429
// "o"  |  967
// "p"  |  256
//
// This gets shuffled by ld to this:
// (see https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l618)
// "b"
// "p"
// "j"
// "n"
// "f"
// "g"
// "o"
// "l"
// "k"
// "i"
// "c"
// "d"
// "h"
// "a"
// "e"
// "m"
static void generate_shuffled_symbols(void) {
    #define DEFAULT_SIZE 4051 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l345

    static u32 buckets[DEFAULT_SIZE];

    memset(buckets, 0, DEFAULT_SIZE * sizeof(u32));

    chains_size = 0;

    push_chain(0); // The first entry in the chain is always STN_UNDEF

    for (size_t i = 0; i < symbols_size; i++) {
        u32 hash = bfd_hash_hash(symbols[i]);
        u32 bucket_index = hash % DEFAULT_SIZE;

        push_chain(buckets[bucket_index]);

        buckets[bucket_index] = i + 1;
    }

    for (size_t i = 0; i < DEFAULT_SIZE; i++) {
        u32 chain_index = buckets[i];
        if (chain_index == 0) {
            continue;
        }

        char *symbol = symbols[chain_index - 1];

        shuffled_symbol_index_to_symbol_index[shuffled_symbols_size] = chain_index - 1;

        push_shuffled_symbol(symbol);

        while (true) {
            chain_index = chains[chain_index];
            if (chain_index == 0) {
                break;
            }

            symbol = symbols[chain_index - 1];

            shuffled_symbol_index_to_symbol_index[shuffled_symbols_size] = chain_index - 1;

            push_shuffled_symbol(symbol);
        }
    }
}

static void init_symbol_name_dynstr_offsets(void) {
    size_t offset = 1;

    static size_t parent_indices[MAX_SYMBOLS];
    static size_t substr_offsets[MAX_SYMBOLS];

    memset(parent_indices, -1, symbols_size * sizeof(size_t));

    memset(is_substrs, false, symbols_size * sizeof(bool));

    // This function could be optimized from O(n^2) to O(n) with a hash map
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

        // If symbol wasn't in the end of another symbol
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
    if (symbols_size + 1 > MAX_SYMBOLS) {
        fprintf(stderr, "error: MAX_SYMBOLS of %d was exceeded\n", MAX_SYMBOLS);
        exit(EXIT_FAILURE);
    }

    symbols[symbols_size++] = symbol;
}

static void reset_generate_simple_so(void) {
    symbols_size = 0;
    chains_size = 0;
    shuffled_symbols_size = 0;
    bytes_size = 0;
}

static void generate_simple_so(char *dll_path) {
    reset_generate_simple_so();

    // TODO: Use the symbols from the AST
    // push_symbol("define");
    push_symbol("define_type");
    // push_symbol("a");
    // push_symbol("fn1_c");
    // push_symbol("fn2_c");

    // TODO: Let this be gotten with push_text() calls
	text_size = 0;
    // text_size = 12;

    init_symbol_name_dynstr_offsets();

    generate_shuffled_symbols();

    init_symbol_name_strtab_offsets();

    init_data_offsets();
    init_text_offsets();

    push_bytes();

    fix_bytes();

    FILE *f = fopen(dll_path, "w");
    if (!f) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fwrite(bytes, sizeof(u8), bytes_size, f);
    fclose(f);
}

//// MISC

grug_mod_dir_t grug_mods;

grug_modified_t *grug_reloads;
size_t grug_reloads_size;
static size_t reloads_capacity;

typedef size_t (*get_globals_struct_size_fn)(void);

static void write_c(char *c_path) {
    FILE *f = fopen(c_path, "w");
	if (!f) {
        GRUG_ERROR("fopen: %s", strerror(errno));
	}

	ssize_t bytes_written = fwrite(serialized, sizeof(char), strlen(serialized), f);
	if (bytes_written < 0 || (size_t)bytes_written != strlen(serialized)) {
		GRUG_ERROR("fwrite: %s", strerror(errno));
	}

    if (fclose(f)) {
        GRUG_ERROR("fclose: %s", strerror(errno));
    }
}

static void reset_regenerate_dll(void) {
	tokens_size = 0;
	fields_size = 0;
	exprs_size = 0;
	statements_size = 0;
	arguments_size = 0;
	helper_fns_size = 0;
	on_fns_size = 0;
	global_variables_size = 0;
	serialized_size = 0;
}

static void regenerate_dll(char *grug_file_path, char *dll_path, char *c_path) {
	grug_log("Regenerating %s\n", dll_path);

	reset_regenerate_dll();

	char *grug_text = read_file(grug_file_path);
	grug_log("grug_text:\n%s\n", grug_text);

	tokenize(grug_text);
	grug_log("After tokenize():\n");
	print_tokens();

	verify_and_trim_spaces();
	grug_log("After verify_and_trim_spaces():\n");
	print_tokens();

	parse();
	grug_log("\nfns:\n");
	print_fns();

	serialize_to_c();
	grug_log("\nserialized:\n%s\n", serialized);

    write_c(c_path);

    generate_simple_so(dll_path);
}

// Returns whether an error occurred
bool grug_test_regenerate_dll(char *grug_file_path, char *dll_path, char *c_path) {
    if (setjmp(error_jmp_buffer)) {
        return true;
	}
    regenerate_dll(grug_file_path, dll_path, c_path);
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
			if (mkdir(parent_dir_path, 0777) && errno != EEXIST) {
				GRUG_ERROR("mkdir: %s", strerror(errno));
			}
		}

		file_path++;
		i++;
	}
}

static char *get_file_extension(char *filename) {
	char *ext = strrchr(filename, '.');
	if (ext) {
		return ext;
	}
	return "";
}

static void fill_as_path_with_c_extension(char *c_path, char *grug_file_path) {
	c_path[0] = '\0';
	strncat(c_path, grug_file_path, STUPID_MAX_PATH - 1);
	char *ext = get_file_extension(c_path);
	assert(*ext);
	ext[1] = '\0';
	strncat(ext + 1, "c", STUPID_MAX_PATH - 1 - strlen(c_path));
}

static void fill_as_path_with_dll_extension(char *dll_path, char *grug_file_path) {
	dll_path[0] = '\0';
	strncat(dll_path, grug_file_path, STUPID_MAX_PATH - 1);
	char *ext = get_file_extension(dll_path);
	assert(*ext);
	ext[1] = '\0';
	strncat(ext + 1, "so", STUPID_MAX_PATH - 1 - strlen(dll_path));
}

static void print_dlerror(char *function_name) {
	char *err = dlerror();
	if (!err) {
		GRUG_ERROR("dlerror was asked to find an error string, but it couldn't find one");
	}
	GRUG_ERROR("%s: %s", function_name, err);
}

static void free_file(grug_file_t file) {
    free(file.name);

    if (file.dll && dlclose(file.dll)) {
        print_dlerror("dlclose");
    }
}

static void free_dir(grug_mod_dir_t dir) {
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

static void push_reload(grug_modified_t modified) {
    if (grug_reloads_size + 1 > reloads_capacity) {
        reloads_capacity = reloads_capacity == 0 ? 1 : reloads_capacity * 2;
        grug_reloads = realloc(grug_reloads, reloads_capacity * sizeof(*grug_reloads));
        if (!grug_reloads) {
            GRUG_ERROR("realloc: %s", strerror(errno));
        }
    }
    grug_reloads[grug_reloads_size++] = modified;
}

static void push_file(grug_mod_dir_t *dir, grug_file_t file) {
    if (dir->files_size + 1 > dir->files_capacity) {
        dir->files_capacity = dir->files_capacity == 0 ? 1 : dir->files_capacity * 2;
        dir->files = realloc(dir->files, dir->files_capacity * sizeof(*dir->files));
        if (!dir->files) {
            GRUG_ERROR("realloc: %s", strerror(errno));
        }
    }
    dir->files[dir->files_size++] = file;
}

static void push_subdir(grug_mod_dir_t *dir, grug_mod_dir_t subdir) {
    if (dir->dirs_size + 1 > dir->dirs_capacity) {
        dir->dirs_capacity = dir->dirs_capacity == 0 ? 1 : dir->dirs_capacity * 2;
        dir->dirs = realloc(dir->dirs, dir->dirs_capacity * sizeof(*dir->dirs));
        if (!dir->dirs) {
            GRUG_ERROR("realloc: %s", strerror(errno));
        }
    }
    dir->dirs[dir->dirs_size++] = subdir;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hashmap is worth it
static grug_file_t *get_file(grug_mod_dir_t *dir, char *name) {
    for (size_t i = 0; i < dir->files_size; i++) {
        if (strcmp(dir->files[i].name, name) == 0) {
            return dir->files + i;
        }
    }
    return NULL;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hashmap is worth it
static grug_mod_dir_t *get_subdir(grug_mod_dir_t *dir, char *name) {
    for (size_t i = 0; i < dir->dirs_size; i++) {
        if (strcmp(dir->dirs[i].name, name) == 0) {
            return dir->dirs + i;
        }
    }
    return NULL;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hashmap is worth it
static bool has_been_seen(char *name, char **seen_names, size_t seen_names_size) {
    for (size_t i = 0; i < seen_names_size; i++) {
        if (strcmp(seen_names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static void reload_modified_mods(char *mods_dir_path, char *dll_dir_path, grug_mod_dir_t *dir) {
	DIR *dirp = opendir(mods_dir_path);
	if (!dirp) {
		GRUG_ERROR("opendir: %s", strerror(errno));
	}

    char **seen_dir_names = NULL;
    size_t seen_dir_names_size = 0;
    size_t seen_dir_names_capacity = 0;

    char **seen_file_names = NULL;
    size_t seen_file_names_size = 0;
    size_t seen_file_names_capacity = 0;

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
			continue;
		}

		char entry_path[STUPID_MAX_PATH];
		snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_dir_path, dp->d_name);

		char dll_entry_path[STUPID_MAX_PATH];
		snprintf(dll_entry_path, sizeof(dll_entry_path), "%s/%s", dll_dir_path, dp->d_name);

		struct stat entry_stat;
		if (stat(entry_path, &entry_stat) == -1) {
			GRUG_ERROR("stat: %s", strerror(errno));
		}

		if (S_ISDIR(entry_stat.st_mode)) {
            if (seen_dir_names_size + 1 > seen_dir_names_capacity) {
                seen_dir_names_capacity = seen_dir_names_capacity == 0 ? 1 : seen_dir_names_capacity * 2;
                seen_dir_names = realloc(seen_dir_names, seen_dir_names_capacity * sizeof(*seen_dir_names));
                if (!seen_dir_names) {
                    GRUG_ERROR("realloc: %s", strerror(errno));
                }
            }
            seen_dir_names[seen_dir_names_size++] = strdup(dp->d_name);

            grug_mod_dir_t *subdir = get_subdir(dir, dp->d_name);
            if (!subdir) {
                grug_mod_dir_t inserted_subdir = {.name = strdup(dp->d_name)};
                if (!inserted_subdir.name) {
                    GRUG_ERROR("strdup: %s", strerror(errno));
                }
                push_subdir(dir, inserted_subdir);
                subdir = dir->dirs + dir->dirs_size - 1;
            }
			reload_modified_mods(entry_path, dll_entry_path, subdir);
		} else if (S_ISREG(entry_stat.st_mode) && strcmp(get_file_extension(dp->d_name), ".grug") == 0) {
            if (seen_file_names_size + 1 > seen_file_names_capacity) {
                seen_file_names_capacity = seen_file_names_capacity == 0 ? 1 : seen_file_names_capacity * 2;
                seen_file_names = realloc(seen_file_names, seen_file_names_capacity * sizeof(*seen_file_names));
                if (!seen_file_names) {
                    GRUG_ERROR("realloc: %s", strerror(errno));
                }
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
				if (errno != 0 && errno != ENOENT) {
					GRUG_ERROR("access: %s", strerror(errno));
				}
			}

            // If the dll doesn't exist or is outdated
            bool needs_regeneration = !dll_exists || entry_stat.st_mtime > dll_stat.st_mtime;

            grug_file_t *old_file = get_file(dir, dp->d_name);

			if (needs_regeneration || !old_file) {
                grug_modified_t modified = {0};

                if (old_file) {
                    modified.old_dll = old_file->dll;
                    if (dlclose(old_file->dll)) {
                        print_dlerror("dlclose");
                    }
                }

                if (needs_regeneration) {
                    char c_path[STUPID_MAX_PATH];
                    fill_as_path_with_c_extension(c_path, dll_entry_path);

				    regenerate_dll(entry_path, dll_path, c_path);
                }

                grug_file_t file = {0};
                if (old_file) {
                    file.name = old_file->name;
                } else {
                    file.name = strdup(dp->d_name);
                    if (!file.name) {
                        GRUG_ERROR("strdup: %s", strerror(errno));
                    }
                }

                file.dll = dlopen(dll_path, RTLD_NOW);
                if (!file.dll) {
                    print_dlerror("dlopen");
                }

                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wpedantic"
                get_globals_struct_size_fn get_globals_struct_size_fn = grug_get(file.dll, "get_globals_struct_size");
                #pragma GCC diagnostic pop
                if (!get_globals_struct_size_fn) {
                    GRUG_ERROR("Retrieving the get_globals_struct_size() function with grug_get() failed for %s", dll_path);
                }
                file.globals_struct_size = get_globals_struct_size_fn();

                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wpedantic"
                file.init_globals_struct_fn = grug_get(file.dll, "init_globals_struct");
                #pragma GCC diagnostic pop
                if (!file.init_globals_struct_fn) {
                    GRUG_ERROR("Retrieving the init_globals_struct() function with grug_get() failed for %s", dll_path);
                }

                char **define_type_ptr = grug_get(file.dll, "define_type");
                if (!define_type_ptr) {
                    GRUG_ERROR("Retrieving the define_type string with grug_get() failed for %s", dll_path);
                }
                file.define_type = *define_type_ptr;

                file.define = grug_get(file.dll, "define");
                if (!file.define) {
                    GRUG_ERROR("Retrieving the define struct with grug_get() failed for %s", dll_path);
                }

                // on_fns is optional, so don't check for NULL
                file.on_fns = grug_get(file.dll, "on_fns");

                if (old_file) {
                    old_file->dll = file.dll;
                    old_file->globals_struct_size = file.globals_struct_size;
                    old_file->init_globals_struct_fn = file.init_globals_struct_fn;
                    old_file->define_type = file.define_type;
                    old_file->define = file.define;
                    old_file->on_fns = file.on_fns;
                } else {
                    push_file(dir, file);
                }

                if (needs_regeneration) {
                    modified.new_dll = file.dll;
                    modified.globals_struct_size = file.globals_struct_size;
                    modified.init_globals_struct_fn = file.init_globals_struct_fn;
                    modified.define_type = file.define_type;
                    modified.define = file.define;
                    modified.on_fns = file.on_fns;
                    push_reload(modified);
                }
			}
		}
	}
	if (errno != 0) {
		GRUG_ERROR("readdir: %s", strerror(errno));
	}

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

    grug_reloads_size = 0;

    if (!grug_mods.name) {
        grug_mods.name = strdup(get_basename(MODS_DIR_PATH));
        if (!grug_mods.name) {
            GRUG_ERROR("strdup: %s", strerror(errno));
        }
    }

	reload_modified_mods(MODS_DIR_PATH, DLL_DIR_PATH, &grug_mods);
    return false;
}

static void print_dir(grug_mod_dir_t dir) {
	static int depth;

	printf("%*s%s/\n", depth * 2, "", dir.name);

	depth++;
	for (size_t i = 0; i < dir.dirs_size; i++) {
		print_dir(dir.dirs[i]);
	}
	for (size_t i = 0; i < dir.files_size; i++) {
		printf("%*s%s\n", depth * 2, "", dir.files[i].name);
	}
	depth--;
}

void grug_print_mods(void) {
	print_dir(grug_mods);
}
