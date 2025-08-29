#pragma once

// TODO: Remove unused includes
#include <assert.h>
#include <stdbool.h>
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

#define MAX_ENTITY_DEPENDENCY_NAME_LENGTH 420
#define MAX_HELPER_FN_MODE_NAMES_CHARACTERS 420420
#define MAX_VARIABLES_PER_FUNCTION 420420
#define MAX_GLOBAL_VARIABLES 420420
#define MAX_ON_FNS 420420

// These only exist to clarify who will be accessing
// the handful of globals that grug.c exposes.
#define USED_BY_MODS
#define USED_BY_PROGRAMS

typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
typedef float f32;

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

struct argument {
	const char *name;
	enum type type;
	const char *type_name;
	union {
		const char *resource_extension; // This is optional
		const char *entity_type; // This is optional
	};
};

struct grug_game_function {
	const char *name;
	enum type return_type;
	const char *return_type_name;
	struct argument *arguments;
	size_t argument_count;
};

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

struct on_fn {
	const char *fn_name;
	struct argument *arguments;
	size_t argument_count;
	struct statement *body_statements;
	size_t body_statement_count;
	bool calls_helper_fn;
	bool contains_while_loop;
};

struct helper_fn {
	const char *fn_name;
	struct argument *arguments;
	size_t argument_count;
	enum type return_type;
	const char *return_type_name;
	struct statement *body_statements;
	size_t body_statement_count;
};

struct global_variable_statement {
	const char *name;
	enum type type;
	const char *type_name;
	struct expr assignment_expr;
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

struct variable {
	const char *name;
	enum type type;
	const char *type_name;
	size_t offset;
};

struct grug_ast {
	const char *grug_file_path;

	const char *mod;
	const char *mods_root_dir_path;

	struct grug_entity *grug_entity;

	struct variable *global_variables; // TODO: Move into 13_grug_backend_linux.c?
	size_t global_variables_size; // TODO: Move into 13_grug_backend_linux.c?
	size_t globals_bytes; // TODO: Move into 13_grug_backend_linux.c?

	struct global_variable_statement *global_variable_statements;
	size_t global_variable_statements_size;

	struct on_fn *on_fns;
	size_t on_fns_size;

	struct helper_fn *helper_fns;
	size_t helper_fns_size;
};

struct grug_backend {
	// The backend implementation should let this handler return `true`
	// when an error occurred.
	bool (*load)(struct grug_ast *ast);

	void (*unload)(void);

	void (*run)(struct grug_backend_file backend_file, const char *on_fn_name);
};

void grug_error_impl(int line);
u32 elf_hash(const char *namearg);
bool streq(const char *a, const char *b);
struct helper_fn *get_helper_fn(const char *name);
struct grug_game_function *get_grug_game_fn(const char *name);
struct variable *get_global_variable(const char *name);
