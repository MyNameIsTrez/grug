#include "07_reading.c"

//// TOKENIZATION

#define MAX_TOKENS 420420
#define MAX_TOKEN_STRINGS_CHARACTERS 420420
#define SPACES_PER_INDENT 4

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
