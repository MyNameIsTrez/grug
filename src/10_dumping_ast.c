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
