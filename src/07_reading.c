#include "06_parsing_mod_api_json.c"

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
