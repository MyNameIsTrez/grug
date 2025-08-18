#include "10_dumping_ast.c"

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
