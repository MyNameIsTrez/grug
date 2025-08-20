#include "08_tokenization.c"

//// PARSING

#define MAX_EXPRS 420420
#define MAX_STATEMENTS 420420
#define MAX_GLOBAL_STATEMENTS 420420
#define MAX_ARGUMENTS 420420
#define MAX_HELPER_FNS 420420
#define MAX_CALLED_HELPER_FN_NAMES 420420
#define MAX_CALL_ARGUMENTS_PER_STACK_FRAME 69
#define MAX_STATEMENTS_PER_SCOPE 1337
#define MAX_PARSING_DEPTH 100

#define INCREASE_PARSING_DEPTH() parsing_depth++; grug_assert(parsing_depth < MAX_PARSING_DEPTH, "There is a function that contains more than %d levels of nested expressions", MAX_PARSING_DEPTH)
#define DECREASE_PARSING_DEPTH() assert(parsing_depth > 0); parsing_depth--
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

static struct on_fn on_fns[MAX_ON_FNS];
static size_t on_fns_size;

static struct helper_fn helper_fns[MAX_HELPER_FNS];
static size_t helper_fns_size;
static u32 buckets_helper_fns[MAX_HELPER_FNS];
static u32 chains_helper_fns[MAX_HELPER_FNS];

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

USED_BY_PROGRAMS struct helper_fn *get_helper_fn(const char *name) {
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
