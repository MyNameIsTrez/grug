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
