//// GRUG DOCUMENTATION
//
// See the bottom of this file for the MIT license
//
// See [my blog post](https://mynameistrez.github.io/2024/02/29/creating-the-perfect-modding-language.html) for an introduction to the grug modding language.
//
// You can find its test suite [here](https://github.com/MyNameIsTrez/grug-tests).
//
// ## Sections
//
// This file is composed of sections, which you can jump between by searching for `////` in the file:
//
// 1. GRUG DOCUMENTATION
// 2. INCLUDES AND DEFINES
// 3. UTILS
// 4. MACOS TIMER IMPLEMENTATION
// 5. RUNTIME ERROR HANDLING
// 6. JSON
// 7. PARSING MOD API JSON
// 8. READING
// 9. TOKENIZATION
// 10. VERIFY AND TRIM SPACES
// 11. PARSING
// 12. PRINTING AST
// 13. FILLING RESULT TYPES
// 14. COMPILING
// 15. LINKING
// 16. HOT RELOADING
//
// ## Small example programs
//
// - [terminal fighting game](https://github.com/MyNameIsTrez/grug-terminal-fighting-game)
// - [grug benchmarks](https://github.com/MyNameIsTrez/grug-benchmarks)
//
// ## Options
//
// Search for `#define` in this file (with Ctrl+F). All the defines are configurable.
//
// If you want to allow your compiler to optimize this file extra hard, add the compiler flag `-DCRASH_ON_UNREACHABLE`.

//// INCLUDES AND DEFINES

#define _XOPEN_SOURCE 700 // This is just so VS Code can find SIGSTKSZ

#include "grug.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
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
#define DLL_DIR_PATH "mod_dlls"
#define MOD_API_JSON_PATH "mod_api.json"
#define GRUG_ON_FN_TIME_LIMIT_MS 1000

// The only reason this define is surrounded by an ifndef,
// is so that tests.c can override it with "-Dtests" at compile time
// If you're a game developer however, you should instead just manually change the below "mods" string
#ifndef MODS_DIR_PATH
#define MODS_DIR_PATH "mods"
#endif

// "The problem is that you can't meaningfully define a constant like this
// in a header file. The maximum path size is actually to be something
// like a filesystem limitation, or at the very least a kernel parameter.
// This means that it's a dynamic value, not something preordained."
// https://eklitzke.org/path-max-is-tricky
#define STUPID_MAX_PATH 4096

static bool streq(char *a, char *b);

#define grug_error(...) {\
	if (snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__) < 0) {\
		abort();\
	}\
	\
	grug_error.line_number = 0; /* TODO: Change this to the .grug file's line number */\
	grug_error.grug_c_line_number = __LINE__;\
	\
	grug_error.has_changed =\
		!streq(grug_error.msg, previous_grug_error.msg)\
	 || !streq(grug_error.path, previous_grug_error.path)\
	 || grug_error.line_number != previous_grug_error.line_number;\
	\
	strncpy(previous_grug_error.msg, grug_error.msg, sizeof(previous_grug_error.msg));\
	strncpy(previous_grug_error.path, grug_error.path, sizeof(previous_grug_error.path));\
	previous_grug_error.line_number = grug_error.line_number;\
	\
	longjmp(error_jmp_buffer, 1);\
}

#define grug_assert(condition, ...) {\
	if (!(condition)) {\
		grug_error(__VA_ARGS__);\
	}\
}

#ifdef CRASH_ON_UNREACHABLE
#define grug_unreachable() {\
	assert(false && "This line of code is supposed to be unreachable. Please report this bug to the grug developers!");\
}
#else
#define grug_unreachable() {\
	grug_error("This line of code is supposed to be unreachable. Please report this bug to the grug developers!");\
}
#endif

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
typedef float f32;

struct grug_error grug_error;
struct grug_error previous_grug_error;
static jmp_buf error_jmp_buffer;

//// UTILS

#define MAX_TEMP_STRINGS_CHARACTERS 420420
#define BFD_HASH_BUCKET_SIZE 4051 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l345

static char temp_strings[MAX_TEMP_STRINGS_CHARACTERS];
static size_t temp_strings_size;

static void reset_utils(void) {
	temp_strings_size = 0;
}

static char *push_temp_string(char *slice_start, size_t length) {
	grug_assert(temp_strings_size + length < MAX_TEMP_STRINGS_CHARACTERS, "There are more than %d characters in the temp_strings array, exceeding MAX_TEMP_STRINGS_CHARACTERS", MAX_TEMP_STRINGS_CHARACTERS);

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

// "a" is the haystack, "b" is the needle
static bool starts_with(char *a, char *b) {
	return strncmp(a, b, strlen(b)) == 0;
}

// "a" is the haystack, "b" is the needle
static bool ends_with(char *a, char *b) {
	size_t len_a = strlen(a);
	size_t len_b = strlen(b);
	if (len_a < len_b) {
		return false;
	}
	return strncmp(a + len_a - len_b, b, len_b) == 0;
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

static void print_dlerror(char *function_name) {
	char *err = dlerror();
	grug_assert(err, "dlerror() was asked to find an error string, but it couldn't find one");
	grug_error("%s: %s", function_name, err);
}

static void *get_dll_symbol(void *dll, char *symbol_name) {
	return dlsym(dll, symbol_name);
}

//// MACOS TIMER IMPLEMENTATION

#ifdef __APPLE__

// Source: https://stackoverflow.com/a/68468895/13279557

// #include <sys/stdtypes.h>
#include <stdbool.h>
#include <mach/boolean.h>
#include <sys/errno.h>
#include <stdlib.h>

#include <dispatch/dispatch.h>

// #if !defined(MAC_OS_X_VERSION_10_12) || (MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12)
// typedef int clockid_t;
// #endif

struct itimerspec {
	struct timespec it_interval;	/* timer period */
	struct timespec it_value;		/* timer expiration */
};

struct sigevent;

/* If used a lot, queue should probably be outside of this struct */
struct macos_timer {
	dispatch_queue_t tim_queue;
	dispatch_source_t tim_timer;
	void (*tim_func)(union sigval);
	void *tim_arg;
};

typedef struct macos_timer *timer_t;

static inline void
_timer_cancel(void *arg)
{
	struct macos_timer *tim = (struct macos_timer *)arg;
	dispatch_release(tim->tim_timer);
	dispatch_release(tim->tim_queue);
	tim->tim_timer = NULL;
	tim->tim_queue = NULL;
	free(tim);
}

static inline void
_timer_handler(void *arg)
{
	struct macos_timer *tim = (struct macos_timer *)arg;
	union sigval sv;

	sv.sival_ptr = tim->tim_arg;

	if (tim->tim_func != NULL)
		tim->tim_func(sv);
}

static inline int
timer_create(clockid_t clockid, struct sigevent *sevp,
    timer_t *timerid)
{
	struct macos_timer *tim;

	*timerid = NULL;

	switch (clockid) {
		case CLOCK_REALTIME:

			/* What is implemented so far */
			if (sevp->sigev_notify != SIGEV_THREAD) {
				errno = ENOTSUP;
				return (-1);
			}

			tim = (struct macos_timer *)
			    malloc(sizeof (struct macos_timer));
			if (tim == NULL) {
				errno = ENOMEM;
				return (-1);
			}

			tim->tim_queue =
			    dispatch_queue_create("org.openzfsonosx.timerqueue",
			    0);
			tim->tim_timer =
			    dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
			    0, 0, tim->tim_queue);

			tim->tim_func = sevp->sigev_notify_function;
			tim->tim_arg = sevp->sigev_value.sival_ptr;
			*timerid = tim;

			/* Opting to use pure C instead of Block versions */
			dispatch_set_context(tim->tim_timer, tim);
			dispatch_source_set_event_handler_f(tim->tim_timer,
			    _timer_handler);
			dispatch_source_set_cancel_handler_f(tim->tim_timer,
			    _timer_cancel);

			return (0);
		default:
			break;
	}

	errno = EINVAL;
	return (-1);
}

static inline int
timer_settime(timer_t tim, int flags,
    const struct itimerspec *its, struct itimerspec *remainvalue)
{
	(void)flags;
	(void)remainvalue;

	if (tim != NULL) {

		/* Both zero, is disarm */
		if (its->it_value.tv_sec == 0 &&
		    its->it_value.tv_nsec == 0) {
		/* There's a comment about suspend count in Apple docs */
			dispatch_suspend(tim->tim_timer);
			return (0);
		}

		dispatch_time_t start;
		start = dispatch_time(DISPATCH_TIME_NOW,
		    NSEC_PER_SEC * its->it_value.tv_sec +
		    its->it_value.tv_nsec);
		dispatch_source_set_timer(tim->tim_timer, start,
		    NSEC_PER_SEC * its->it_value.tv_sec +
		    its->it_value.tv_nsec,
		    0);
		dispatch_resume(tim->tim_timer);
	}
	return (0);
}

// static inline int
// timer_delete(timer_t tim)
// {
// 	/* Calls _timer_cancel() */
// 	if (tim != NULL)
// 		dispatch_source_cancel(tim->tim_timer);

// 	return (0);
// }

#endif

//// RUNTIME ERROR HANDLING

volatile sig_atomic_t grug_runtime_error;
jmp_buf grug_runtime_error_jmp_buffer;

static struct sigaction previous_segv_sa;
static struct sigaction previous_alrm_sa;
static struct sigaction previous_fpe_sa;

static timer_t on_fn_timeout_timer_id;

sigset_t grug_block_mask;

void grug_disable_on_fn_runtime_error_handling(void) {
	// Disable the SIGALRM timeout timer
	static struct itimerspec new = {0};
	static struct itimerspec old;
	// Can't use grug_assert() here, since its snprintf() isn't in the async-signal-safe function list:
	// https://stackoverflow.com/a/67840070/13279557
	// if (timer_settime(on_fn_timeout_timer_id, 0, &new, NULL) == -1) {
	if (timer_settime(on_fn_timeout_timer_id, 0, &new, &old) == -1) {
		abort();
	}

	if (sigaction(SIGSEGV, &previous_segv_sa, NULL) == -1) {
		abort();
	}
	if (sigaction(SIGALRM, &previous_alrm_sa, NULL) == -1) {
		abort();
	}
	if (sigaction(SIGFPE, &previous_fpe_sa, NULL) == -1) {
		abort();
	}
}

static void grug_error_signal_handler_segv(int sig) {
	(void)sig; // TODO: REMOVE

	// write(STDERR_FILENO, "segv\n", 5); // TODO: REMOVE

	// It is important that we cancel on fn timeout alarms asap,
	// cause if there was a stack overflow, then the on fn didn't get the chance
	// to deactivate its own alarm
	//
	// The reason having an alarm be raised outside of the on fn is bad,
	// is because this function always longjmps out of itself,
	// meaning that if the alarm happens in the middle of the game's code,
	// then only a slice of the game's code would be executed, causing havoc
	grug_disable_on_fn_runtime_error_handling();

	grug_runtime_error = GRUG_ON_FN_STACK_OVERFLOW;

	siglongjmp(grug_runtime_error_jmp_buffer, 1);
}

static void grug_error_signal_handler_alrm(int sig) {
	(void)sig; // TODO: REMOVE

	// write(STDERR_FILENO, "alrm\n", 5); // TODO: REMOVE

	grug_disable_on_fn_runtime_error_handling();

	grug_runtime_error = GRUG_ON_FN_TIME_LIMIT_EXCEEDED;

	siglongjmp(grug_runtime_error_jmp_buffer, 1);
}

static void grug_error_signal_handler_fpe(int sig) {
	(void)sig; // TODO: REMOVE

	// write(STDERR_FILENO, "fpe\n", 4); // TODO: REMOVE

	grug_disable_on_fn_runtime_error_handling();

	grug_runtime_error = GRUG_ON_FN_ARITHMETIC_ERROR;

	siglongjmp(grug_runtime_error_jmp_buffer, 1);
}

void grug_enable_on_fn_runtime_error_handling(void) {
	// TODO: Try removing SA_RESETHAND and SA_NODEFER from these
	static struct sigaction sigsegv_sa = {
		.sa_handler = grug_error_signal_handler_segv,
		.sa_flags = SA_ONSTACK, // SA_ONSTACK gives SIGSEGV its own stack
	};
	static struct sigaction alrm_sa = {
		.sa_handler = grug_error_signal_handler_alrm,
	};
	static struct sigaction fpe_sa = {
		.sa_handler = grug_error_signal_handler_fpe,
	};

	static bool initialized = false;
	if (!initialized) {
		grug_assert(sigemptyset(&grug_block_mask) != -1, "sigemptyset: %s", strerror(errno));
		grug_assert(sigaddset(&grug_block_mask, SIGALRM) != -1, "sigaddset: %s", strerror(errno));

		// Handle stack overflow, from https://stackoverflow.com/a/7342398/13279557
		static char stack[SIGSTKSZ];
		stack_t ss = {
			.ss_size = SIGSTKSZ,
			.ss_sp = stack,
		};

		// Set up SIGSEGV's stack
		grug_assert(sigaltstack(&ss, NULL) != -1, "sigaltstack: %s", strerror(errno));

		// Create the SIGALRM timeout timer
		static struct sigevent sev = {
			.sigev_notify = SIGEV_SIGNAL,
			.sigev_signo = SIGALRM,
			// .sigev_value.sival_ptr = &on_fn_timeout_timer_id, // TODO: Remove?
		};
		grug_assert(timer_create(CLOCK_MONOTONIC, &sev, &on_fn_timeout_timer_id) != -1, "timer_create: %s", strerror(errno));

		grug_assert(sigfillset(&sigsegv_sa.sa_mask) != -1, "sigfillset: %s", strerror(errno));
		grug_assert(sigfillset(&alrm_sa.sa_mask) != -1, "sigfillset: %s", strerror(errno));
		grug_assert(sigfillset(&fpe_sa.sa_mask) != -1, "sigfillset: %s", strerror(errno));

		initialized = true;
	}

	// Register our handlers
	grug_assert(sigaction(SIGSEGV, &sigsegv_sa, &previous_segv_sa) != -1, "sigaction: %s", strerror(errno));
	grug_assert(sigaction(SIGALRM, &alrm_sa, &previous_alrm_sa) != -1, "sigaction: %s", strerror(errno));
	grug_assert(sigaction(SIGFPE, &fpe_sa, &previous_fpe_sa) != -1, "sigaction: %s", strerror(errno));

	// Set SIGALRM timeout
	static struct itimerspec its = {
		.it_value.tv_sec = GRUG_ON_FN_TIME_LIMIT_MS / 1000,
		.it_value.tv_nsec = (GRUG_ON_FN_TIME_LIMIT_MS % 1000) * 1000000,
	};
	grug_assert(timer_settime(on_fn_timeout_timer_id, 0, &its, NULL) != -1, "timer_settime: %s", strerror(errno));
}

char *grug_get_runtime_error_reason(void) {
	static char runtime_error_reason[420];

	switch (grug_runtime_error) {
		case GRUG_ON_FN_TIME_LIMIT_EXCEEDED:
			snprintf(runtime_error_reason, sizeof(runtime_error_reason), "An on_ function took longer than %d millisecond%s to run", GRUG_ON_FN_TIME_LIMIT_MS, GRUG_ON_FN_TIME_LIMIT_MS > 1 ? "s" : "");
			break;
		case GRUG_ON_FN_STACK_OVERFLOW:
			snprintf(runtime_error_reason, sizeof(runtime_error_reason), "An on_ function caused a stack overflow, so check for accidental infinite recursion");
			break;
		case GRUG_ON_FN_ARITHMETIC_ERROR:
			snprintf(runtime_error_reason, sizeof(runtime_error_reason), "An on_ function divided an i32 by 0");
			break;
		default:
			grug_unreachable();
	}

	return runtime_error_reason;
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
	type_bool,
	type_i32,
	type_f32,
	type_string,
	type_resource,
	type_entity,
};
static char *type_names[] = {
	[type_bool] = "bool",
	[type_i32] = "i32",
	[type_f32] = "f32",
	[type_string] = "string",
	[type_resource] = "resource",
	[type_entity] = "entity",
};
static size_t type_sizes[] = {
	[type_bool] = sizeof(bool),
	[type_i32] = sizeof(int32_t),
	[type_f32] = sizeof(float),
	[type_string] = sizeof(char *),
	[type_resource] = sizeof(char *),
	[type_entity] = sizeof(char *),
};

struct grug_on_function {
	char *name;
	struct argument *arguments;
	size_t argument_count;
};

struct grug_entity {
	char *name;
	struct argument *fields;
	size_t field_count;
	struct grug_on_function *on_functions;
	size_t on_function_count;
};

struct grug_game_function {
	char *name;
	enum type return_type;
	struct argument *arguments;
	size_t argument_count;
};

struct argument {
	char *name;
	enum type type;
	union {
		char *resource_extension; // This is optional
		char *entity_type; // This is optional
	};
};

struct grug_on_function grug_on_functions[MAX_GRUG_FUNCTIONS];
static size_t grug_on_functions_size;

struct grug_entity grug_define_functions[MAX_GRUG_FUNCTIONS];
static size_t grug_define_functions_size;

struct grug_game_function grug_game_functions[MAX_GRUG_FUNCTIONS];
static size_t grug_game_functions_size;
static u32 buckets_game_fns[MAX_GRUG_FUNCTIONS];
static u32 chains_game_fns[MAX_GRUG_FUNCTIONS];

struct argument grug_arguments[MAX_GRUG_ARGUMENTS];
static size_t grug_arguments_size;

static void push_grug_on_function(struct grug_on_function fn) {
	grug_assert(grug_on_functions_size < MAX_GRUG_FUNCTIONS, "There are more than %d on_ functions in mod_api.json, exceeding MAX_GRUG_FUNCTIONS", MAX_GRUG_FUNCTIONS);
	grug_on_functions[grug_on_functions_size++] = fn;
}

static void push_grug_entity(struct grug_entity fn) {
	grug_assert(grug_define_functions_size < MAX_GRUG_FUNCTIONS, "There are more than %d define_ functions in mod_api.json, exceeding MAX_GRUG_FUNCTIONS", MAX_GRUG_FUNCTIONS);
	grug_define_functions[grug_define_functions_size++] = fn;
}

static struct grug_game_function *get_grug_game_fn(char *name) {
	if (grug_game_functions_size == 0) {
		return NULL;
	}

	u32 i = buckets_game_fns[elf_hash(name) % grug_game_functions_size];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, grug_game_functions[i].name)) {
			break;
		}

		i = chains_game_fns[i];
	}

	return grug_game_functions + i;
}

static void hash_game_fns(void) {
	memset(buckets_game_fns, 0xff, grug_game_functions_size * sizeof(u32));

	for (size_t i = 0; i < grug_game_functions_size; i++) {
		char *name = grug_game_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_game_functions_size;

		chains_game_fns[i] = buckets_game_fns[bucket_index];

		buckets_game_fns[bucket_index] = i;
	}
}

static void push_grug_game_function(struct grug_game_function fn) {
	grug_assert(grug_game_functions_size < MAX_GRUG_FUNCTIONS, "There are more than %d game functions in mod_api.json, exceeding MAX_GRUG_FUNCTIONS", MAX_GRUG_FUNCTIONS);
	grug_game_functions[grug_game_functions_size++] = fn;
}

static void push_grug_argument(struct argument argument) {
	grug_assert(grug_arguments_size < MAX_GRUG_ARGUMENTS, "There are more than %d grug arguments, exceeding MAX_GRUG_ARGUMENTS", MAX_GRUG_ARGUMENTS);
	grug_arguments[grug_arguments_size++] = argument;
}

static enum type parse_type(char *type) {
	if (streq(type, "bool")) {
		return type_bool;
	}
	if (streq(type, "i32")) {
		return type_i32;
	}
	if (streq(type, "f32")) {
		return type_f32;
	}
	if (streq(type, "string")) {
		return type_string;
	}
	if (streq(type, "resource")) {
		return type_resource;
	}
	if (streq(type, "entity")) {
		return type_entity;
	}

	// Make sure to add any new types to this error message
	grug_error("The type '%s' must be changed to one of bool/i32/f32/string/resource/entity", type);
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
				grug_assert(grug_fn.return_type != type_resource, "\"game_functions\" its function return types must not be 'resource'");
				grug_assert(grug_fn.return_type != type_entity, "\"game_functions\" its function return types must not be 'entity'");
				seen_return_type = true;
				field++;
			} else {
				grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second field was something other than \"return_type\" and \"arguments\"");
			}
		} else {
			grug_fn.return_type = type_void;
		}

		if ((!seen_return_type && fn.field_count > 1) || fn.field_count > 2) {
			grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second or third field was something other than \"arguments\"");

			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"game_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->data.array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->data.array.value_count;
			grug_assert(grug_fn.argument_count > 0, "\"game_functions\" its \"arguments\" array must not be empty (just remove the \"arguments\" key entirely)");

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct argument grug_arg = {0};

				grug_assert(value->type == JSON_NODE_OBJECT, "\"game_functions\" its function arguments must only contain objects");
				grug_assert(value->data.object.field_count >= 2, "\"game_functions\" must have the function argument fields \"name\" and \"type\"");
				grug_assert(value->data.object.field_count <= 3, "\"game_functions\" its function arguments can't have more than 3 fields");
				struct json_field *argument_field = value->data.object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"game_functions\" its function arguments must always have \"name\" as their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.name = argument_field->value->data.string;
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"game_functions\" its function arguments must always have \"type\" as their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->data.string);
				argument_field++;

				if (grug_arg.type == type_resource) {
					grug_assert(value->data.object.field_count == 3 && streq(argument_field->key, "resource_extension"), "\"game_functions\" its function arguments has a \"type\" field with the value \"resource\", which means a \"resource_extension\" field is required");
					grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function argument fields must always have string values");
					grug_arg.resource_extension = argument_field->value->data.string;
				} else if (grug_arg.type == type_entity) {
					grug_assert(value->data.object.field_count == 3 && streq(argument_field->key, "entity_type"), "\"game_functions\" its function arguments has a \"type\" field with the value \"entity\", which means an \"entity_type\" field is required");
					grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function argument fields must always have string values");
					grug_arg.entity_type = argument_field->value->data.string;
				} else {
					grug_assert(value->data.object.field_count == 2, "\"game_functions\" its function argument fields had an unexpected 3rd \"%s\" field", argument_field->key);
				}

				push_grug_argument(grug_arg);
				value++;
			}
		}

		push_grug_game_function(grug_fn);
	}

	hash_game_fns();
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
				struct argument grug_arg = {0};

				grug_assert(value->type == JSON_NODE_OBJECT, "\"on_functions\" its function arguments must only contain objects");
				grug_assert(value->data.object.field_count == 2, "\"on_functions\" its function arguments must only contain a name and type field");
				struct json_field *argument_field = value->data.object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"on_functions\" its function arguments must always have \"name\" as their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.name = argument_field->value->data.string;
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"on_functions\" its function arguments must always have \"type\" as their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->data.string);
				grug_assert(grug_arg.type != type_resource, "\"on_functions\" its function argument types must not be 'resource'");
				grug_assert(grug_arg.type != type_entity, "\"on_functions\" its function argument types must not be 'entity'");
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
				entity.fields = grug_arguments + grug_arguments_size;
				entity.field_count = field->value->data.array.value_count;

				for (size_t field_index = 0; field_index < entity.field_count; field_index++) {
					struct argument grug_field = {0};

					grug_assert(value->type == JSON_NODE_OBJECT, "\"entities\" its fields must only contain objects");
					grug_assert(value->data.object.field_count >= 2, "\"entities\" must have the fields \"name\" and \"type\"");
					grug_assert(value->data.object.field_count <= 3, "\"entities\" can't have more than 3 fields");
					struct json_field *json_field = value->data.object.fields;

					grug_assert(streq(json_field->key, "name"), "\"entities\" its fields must always have \"name\" as their first field");
					grug_assert(json_field->value->type == JSON_NODE_STRING, "\"entities\" its fields must always have string values");
					grug_field.name = json_field->value->data.string;
					json_field++;

					grug_assert(streq(json_field->key, "type"), "\"entities\" its fields must always have \"type\" as their second field");
					grug_assert(json_field->value->type == JSON_NODE_STRING, "\"entities\" its fields must always have string values");
					grug_field.type = parse_type(json_field->value->data.string);
					json_field++;

					if (grug_field.type == type_resource) {
						grug_assert(value->data.object.field_count == 3 && streq(json_field->key, "resource_extension"), "\"entities\" has a \"type\" field with the value \"resource\", which means a \"resource_extension\" field is required");
						grug_assert(json_field->value->type == JSON_NODE_STRING, "\"entities\" its fields must always have string values");
						grug_field.resource_extension = json_field->value->data.string;
					} else if (grug_field.type == type_entity) {
						grug_assert(value->data.object.field_count == 3 && streq(json_field->key, "entity_type"), "\"entities\" has a \"type\" field with the value \"entity\", which means an \"entity_type\" field is required");
						grug_assert(json_field->value->type == JSON_NODE_STRING, "\"entities\" its fields must always have string values");
						grug_field.entity_type = json_field->value->data.string;
					} else {
						grug_assert(value->data.object.field_count == 2, "\"entities\" had an unexpected 3rd \"%s\" field", json_field->key);
					}

					push_grug_argument(grug_field);
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
	WHILE_TOKEN,
	BREAK_TOKEN,
	RETURN_TOKEN,
	CONTINUE_TOKEN,
	SPACES_TOKEN,
	NEWLINES_TOKEN,
	STRING_TOKEN,
	WORD_TOKEN,
	I32_TOKEN,
	F32_TOKEN,
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
	[WHILE_TOKEN] = "WHILE_TOKEN",
	[BREAK_TOKEN] = "BREAK_TOKEN",
	[RETURN_TOKEN] = "RETURN_TOKEN",
	[CONTINUE_TOKEN] = "CONTINUE_TOKEN",
	[SPACES_TOKEN] = "SPACES_TOKEN",
	[NEWLINES_TOKEN] = "NEWLINES_TOKEN",
	[STRING_TOKEN] = "STRING_TOKEN",
	[WORD_TOKEN] = "WORD_TOKEN",
	[I32_TOKEN] = "I32_TOKEN",
	[F32_TOKEN] = "F32_TOKEN",
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

			i++;
			while (isdigit(grug_text[i]) || grug_text[i] == '.') {
				if (grug_text[i] == '.') {
					grug_assert(!seen_period, "Encountered two '.' periods in a number at character %zu of the grug text file", i);
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
						case OPEN_PARENTHESIS_TOKEN: // For example the inner open parenthesis in "foo(1, (2 + 3))"
						case MINUS_TOKEN:
						case TRUE_TOKEN:
						case FALSE_TOKEN:
						case STRING_TOKEN:
						case WORD_TOKEN:
						case I32_TOKEN:
						case F32_TOKEN:
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
			case WHILE_TOKEN:
			case BREAK_TOKEN:
			case RETURN_TOKEN:
			case CONTINUE_TOKEN:
				break;
			case SPACES_TOKEN: {
				grug_assert(i + 1 < tokens_size, "Expected another token after the space at token index %zu", i);

				struct token next_token = peek_token(i + 1);
				switch (next_token.type) {
					case OPEN_BRACE_TOKEN:
						depth++;
						assert_spaces(i, 1);
						break;
					case PLUS_TOKEN:
					case MULTIPLICATION_TOKEN:
					case DIVISION_TOKEN:
					case REMAINDER_TOKEN:
					case COMMA_TOKEN:
					case ELSE_TOKEN: // Skip the space in "} else"
						assert_spaces(i, 1);
						break;
					case IF_TOKEN:
					case WHILE_TOKEN:
					case BREAK_TOKEN:
					case RETURN_TOKEN:
					case CONTINUE_TOKEN:
					case PERIOD_TOKEN:
						assert_spaces(i, depth * SPACES_PER_INDENT);
						break;
					case SPACES_TOKEN:
						grug_unreachable();
					case NEWLINES_TOKEN:
						grug_error("Unexpected trailing whitespace '%s' at token index %zu", token.str, i);
					case COMMENT_TOKEN:
						// TODO: Ideally we'd assert there only ever being 1 space,
						// but the problem is that a standalone comment is allowed to have indentation
						// assert_spaces(i, 1);

						grug_assert(strlen(next_token.str) >= 2 && next_token.str[1] == ' ', "Expected a single space between the '#' in '%s' and the rest of the comment at token index %zu", next_token.str, i + 1);

						grug_assert(!isspace(next_token.str[strlen(next_token.str) - 1]), "Unexpected trailing whitespace in the comment token '%s' at token index %zu", next_token.str, i + 1);

						break;
					case OPEN_PARENTHESIS_TOKEN:
					case CLOSE_PARENTHESIS_TOKEN:
					case CLOSE_BRACE_TOKEN:
					case MINUS_TOKEN:
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
					case STRING_TOKEN:
					case WORD_TOKEN:
					case I32_TOKEN:
					case F32_TOKEN:
						break;
				}
				break;
			}
			case NEWLINES_TOKEN:
			case STRING_TOKEN:
			case PERIOD_TOKEN:
			case WORD_TOKEN:
			case I32_TOKEN:
			case F32_TOKEN:
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
		i32 i32;
		f32 f32;
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
	} type;
	enum type result_type;
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
	enum type type;
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

struct statement {
	enum {
		VARIABLE_STATEMENT,
		CALL_STATEMENT,
		IF_STATEMENT,
		RETURN_STATEMENT,
		WHILE_STATEMENT,
		BREAK_STATEMENT,
		CONTINUE_STATEMENT,
	} type;
	union {
		struct variable_statement variable_statement;
		struct call_statement call_statement;
		struct if_statement if_statement;
		struct return_statement return_statement;
		struct while_statement while_statement;
	};
};
static char *get_statement_type_str[] = {
	[VARIABLE_STATEMENT] = "VARIABLE_STATEMENT",
	[CALL_STATEMENT] = "CALL_STATEMENT",
	[IF_STATEMENT] = "IF_STATEMENT",
	[RETURN_STATEMENT] = "RETURN_STATEMENT",
	[WHILE_STATEMENT] = "WHILE_STATEMENT",
	[BREAK_STATEMENT] = "BREAK_STATEMENT",
	[CONTINUE_STATEMENT] = "CONTINUE_STATEMENT",
};
static struct statement statements[MAX_STATEMENTS_IN_FILE];
static size_t statements_size;

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
	enum type return_type;
	struct statement *body_statements;
	size_t body_statement_count;
};
static struct helper_fn helper_fns[MAX_HELPER_FNS_IN_FILE];
static size_t helper_fns_size;
static u32 buckets_helper_fns[MAX_HELPER_FNS_IN_FILE];
static u32 chains_helper_fns[MAX_HELPER_FNS_IN_FILE];

struct global_variable_statement {
	char *name;
	enum type type;
	struct expr assignment_expr;
};
static struct global_variable_statement global_variable_statements[MAX_GLOBAL_VARIABLES_IN_FILE];
static size_t global_variable_statements_size;

static void reset_parsing(void) {
	exprs_size = 0;
	fields_size = 0;
	statements_size = 0;
	arguments_size = 0;
	on_fns_size = 0;
	helper_fns_size = 0;
	global_variable_statements_size = 0;
}

static struct helper_fn *get_helper_fn(char *name) {
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
		char *name = helper_fns[i].fn_name;

		u32 bucket_index = elf_hash(name) % helper_fns_size;

		chains_helper_fns[i] = buckets_helper_fns[bucket_index];

		buckets_helper_fns[bucket_index] = i;
	}
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

// TODO: Either use or remove this function
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

static f32 str_to_f32(char *str) {
	char *end;
	errno = 0;
	float f = strtof(str, &end);

	if (errno == ERANGE) {
		if (f == HUGE_VALF) {
			grug_error("The float '%s' is too big to fit in an f32", str);
		} else if (f == 0) {
			grug_error("The float '%s' is too small to fit in an f32", str);
		}
		// No need to check `f == -HUGE_VALF`, since the token can't ever start with a minus sign
		grug_unreachable();
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
		case I32_TOKEN:
			(*i)++;
			expr.type = I32_EXPR;
			expr.literal.i32 = str_to_i32(token.str);
			return expr;
		case F32_TOKEN:
			(*i)++;
			expr.type = F32_EXPR;
			expr.literal.f32 = str_to_f32(token.str);
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
				(void)push_expr(local_call_arguments[argument_index]);
			}
		}
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

	return parse_call(i);
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

static struct statement parse_while_statement(size_t *i) {
	struct statement statement = {0};
	statement.type = WHILE_STATEMENT;
	statement.while_statement.condition = parse_expression(i);

	statement.while_statement.body_statements = parse_statements(i, &statement.while_statement.body_statement_count);

	return statement;
}

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

	size_t name_token_index = *i;
	struct token name_token = consume_token(i);
	variable_statement.name = name_token.str;

	struct token token = peek_token(*i);
	if (token.type == COLON_TOKEN) {
		(*i)++;

		struct token type_token = consume_token(i);
		grug_assert(type_token.type == WORD_TOKEN, "Expected a word token after the colon at token index %zu", name_token_index);

		variable_statement.has_type = true;
		variable_statement.type = parse_type(type_token.str);
		grug_assert(variable_statement.type != type_resource, "The local variable '%s' can't have 'resource' as its type", variable_statement.name);
		grug_assert(variable_statement.type != type_entity, "The local variable '%s' can't have 'entity' as its type", variable_statement.name);
	}

	token = peek_token(*i);
	grug_assert(token.type == ASSIGNMENT_TOKEN, "The variable '%s' was not assigned a value at token index %zu", variable_statement.name, name_token_index);

	(*i)++;
	variable_statement.assignment_expr = push_expr(parse_expression(i));

	return variable_statement;
}

static void push_global_variable(struct global_variable_statement global_variable) {
	grug_assert(global_variable_statements_size < MAX_GLOBAL_VARIABLES_IN_FILE, "There are more than %d global variables in the grug file, exceeding MAX_GLOBAL_VARIABLES_IN_FILE", MAX_GLOBAL_VARIABLES_IN_FILE);
	global_variable_statements[global_variable_statements_size++] = global_variable;
}

static void parse_global_variable(size_t *i) {
	struct global_variable_statement global_variable = {0};

	struct token name_token = consume_token(i);
	global_variable.name = name_token.str;

	assert_token_type(*i, COLON_TOKEN);
	consume_token(i);

	assert_token_type(*i, WORD_TOKEN);
	struct token type_token = consume_token(i);
	global_variable.type = parse_type(type_token.str);
	grug_assert(global_variable.type != type_resource, "The global variable '%s' can't have 'resource' as its type", global_variable.name);
	grug_assert(global_variable.type != type_entity, "The global variable '%s' can't have 'entity' as its type", global_variable.name);

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

	argument.type = parse_type(token.str);
	grug_assert(argument.type != type_resource, "The argument '%s' can't have 'resource' as its type", argument.name);
	grug_assert(argument.type != type_entity, "The argument '%s' can't have 'entity' as its type", argument.name);
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
		argument.type = parse_type(token.str);
		grug_assert(argument.type != type_resource, "The argument '%s' can't have 'resource' as its type", argument.name);
		grug_assert(argument.type != type_entity, "The argument '%s' can't have 'entity' as its type", argument.name);
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
		fn.return_type = parse_type(token.str);
		grug_assert(fn.return_type != type_resource, "The helper function '%s' can't have 'resource' as its return type", fn.fn_name);
		grug_assert(fn.return_type != type_entity, "The helper function '%s' can't have 'entity' as its return type", fn.fn_name);
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

static void parse(void) {
	reset_parsing();

	bool seen_define_fn = false;

	size_t i = 0;
	while (i < tokens_size) {
		struct token token = peek_token(i);
		int type = token.type;

		if (       type == WORD_TOKEN && streq(token.str, "define") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(!seen_define_fn, "There can't be more than one define_ function in a grug file");
			parse_define_fn(&i);
			seen_define_fn = true;
		} else if (type == WORD_TOKEN && peek_token(i + 1).type == COLON_TOKEN) {
			grug_assert(seen_define_fn, "Move the global variable '%s' below the define_ function", token.str);
			parse_global_variable(&i);
		} else if (type == WORD_TOKEN && starts_with(token.str, "on_") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			grug_assert(seen_define_fn, "Move the on_ function '%s' below the define_ function", token.str);
			parse_on_fn(&i);
		} else if (type == WORD_TOKEN && starts_with(token.str, "helper_") && peek_token(i + 1).type == OPEN_PARENTHESIS_TOKEN) {
			parse_helper_fn(&i);
		} else if (type == COMMENT_TOKEN) {
			i++;
		} else if (type == NEWLINES_TOKEN) {
			i++;
		} else {
			grug_error("Unexpected token '%s' at token index %zu in parse()", token.str, i);
		}
	}

	grug_assert(seen_define_fn, "Every grug file requires exactly one define_ function");

	hash_helper_fns();
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
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
		case IDENTIFIER_EXPR:
			grug_log(",");
			grug_log("\"str\":\"%s\"", expr.literal.string);
			break;
		case I32_EXPR:
			grug_log(",");
			grug_log("\"value\":%d", expr.literal.i32);
			break;
		case F32_EXPR:
			grug_log(",");
			grug_log("\"value\":%f", expr.literal.f32);
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
					grug_log("\"variable_type\":\"%s\",", type_names[statement.variable_statement.type]);
				}

				grug_log("\"assignment\":{");
				print_expr(*statement.variable_statement.assignment_expr);
				grug_log("}");

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
					grug_log(",");
					grug_log("\"expr\":{");
					print_expr(*statement.return_statement.value);
					grug_log("}");
				}
				break;
			case WHILE_STATEMENT:
				grug_log(",");
				grug_log("\"condition\":{");
				print_expr(statement.while_statement.condition);
				grug_log("},");

				grug_log("\"statements\":[");
				print_statements(statement.while_statement.body_statements, statement.while_statement.body_statement_count);
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
		grug_log("\"type\":\"%s\"", type_names[arg.type]);

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
			grug_log("\"return_type\":\"%s\",", type_names[fn.return_type]);
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

	for (size_t global_variable_index = 0; global_variable_index < global_variable_statements_size; global_variable_index++) {
		if (global_variable_index > 0) {
			grug_log(",");
		}

		struct global_variable_statement global_variable = global_variable_statements[global_variable_index];

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

//// FILLING RESULT TYPES

#define MAX_VARIABLES_PER_FUNCTION 420420
#define MAX_ENTITY_DEPENDENCY_NAME_LENGTH 420
#define MAX_ENTITY_DEPENDENCIES 420420
#define MAX_DATA_STRINGS 420420

#define GLOBAL_OFFSET_TABLE_POINTER_SIZE sizeof(void *)
#define GLOBAL_VARIABLES_POINTER_SIZE sizeof(void *)
#define GRUG_ON_FN_NAME_OFFSET sizeof(char *)
#define GRUG_ON_FN_PATH_OFFSET (GRUG_ON_FN_NAME_OFFSET + sizeof(char *))
#define GRUG_BLOCK_MASK_OFFSET (GRUG_ON_FN_PATH_OFFSET + sizeof(void *))

struct variable {
	char *name;
	enum type type;
	size_t offset;
};
static struct variable variables[MAX_VARIABLES_PER_FUNCTION];
static size_t variables_size;
static u32 buckets_variables[MAX_VARIABLES_PER_FUNCTION];
static u32 chains_variables[MAX_VARIABLES_PER_FUNCTION];

static struct variable global_variables[MAX_GLOBAL_VARIABLES_IN_FILE];
static size_t global_variables_size;
static size_t globals_bytes;
static u32 buckets_global_variables[MAX_GLOBAL_VARIABLES_IN_FILE];
static u32 chains_global_variables[MAX_GLOBAL_VARIABLES_IN_FILE];

static size_t stack_frame_bytes;

static enum type fn_return_type;
static char *filled_fn_name;

static struct grug_entity *grug_define_entity;

static u32 buckets_define_on_fns[MAX_ON_FNS_IN_FILE];
static u32 chains_define_on_fns[MAX_ON_FNS_IN_FILE];

static char *mod;

static u32 entity_types[MAX_ENTITY_DEPENDENCIES];
static size_t entity_types_size;

static char *data_strings[MAX_DATA_STRINGS];
static size_t data_strings_size;

static u32 buckets_data_strings[MAX_DATA_STRINGS];
static u32 chains_data_strings[MAX_DATA_STRINGS];

static void reset_filling(void) {
	global_variables_size = 0;
	globals_bytes = 0;
	memset(buckets_global_variables, 0xff, MAX_GLOBAL_VARIABLES_IN_FILE * sizeof(u32));
	entity_types_size = 0;
	data_strings_size = 0;
	memset(buckets_data_strings, 0xff, sizeof(buckets_data_strings));
}

static void push_data_string(char *string) {
	grug_assert(data_strings_size < MAX_DATA_STRINGS, "There are more than %d data strings, exceeding MAX_DATA_STRINGS", MAX_DATA_STRINGS);

	data_strings[data_strings_size++] = string;
}

static u32 get_data_string_index(char *string) {
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

static void add_data_string(char *string) {
	if (get_data_string_index(string) == UINT32_MAX) {
		u32 bucket_index = elf_hash(string) % MAX_DATA_STRINGS;

		chains_data_strings[data_strings_size] = buckets_data_strings[bucket_index];

		buckets_data_strings[bucket_index] = data_strings_size;

		push_data_string(string);
	}
}

static void push_entity_type(char *entity_type) {
	add_data_string(entity_type);

	grug_assert(entity_types_size < MAX_ENTITY_DEPENDENCIES, "There are more than %d entity types, exceeding MAX_ENTITY_DEPENDENCIES", MAX_ENTITY_DEPENDENCIES);

	entity_types[entity_types_size++] = get_data_string_index(entity_type);
}

static void fill_expr(struct expr *expr);

static void validate_entity_string(char *string) {
	grug_assert(string[0] != '\0', "Entities can't be empty strings");

	char *mod_name = mod;
	char *entity_name = string;

	char *colon = strchr(string, ':');
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

static void validate_resource_string(char *string, char *resource_extension) {
	grug_assert(string[0] != '\0', "Resources can't be empty strings");

	grug_assert(string[0] != '/', "Remove the leading slash from the resource \"%s\"", string);

	size_t string_len = strlen(string);

	grug_assert(string[string_len - 1] != '/', "Remove the trailing slash from the resource \"%s\"", string);

	grug_assert(!strchr(string, '\\'), "Replace the '\\' with '/' in the resource \"%s\"", string);

	grug_assert(!strstr(string, "//"), "Replace the '//' with '/' in the resource \"%s\"", string);

	char *dot = strchr(string, '.');
	if (dot) {
		if (dot == string) {
			grug_assert(string_len != 1 && string[1] != '/', "Remove the '.' from the resource \"%s\"", string);
		} else if (dot[-1] == '/') {
			grug_assert(dot[1] != '/' && dot[1] != '\0', "Remove the '.' from the resource \"%s\"", string);
		}
	}

	char *dotdot = strstr(string, "..");
	if (dotdot) {
		if (dotdot == string) {
			grug_assert(string_len != 2 && string[2] != '/', "Remove the '..' from the resource \"%s\"", string);
		} else if (dotdot[-1] == '/') {
			grug_assert(dotdot[2] != '/' && dotdot[2] != '\0', "Remove the '..' from the resource \"%s\"", string);
		}
	}

	grug_assert(ends_with(string, resource_extension), "The resource '%s' was supposed to have the extension '%s'", string, resource_extension);
}

static void check_arguments(struct argument *params, size_t param_count, struct call_expr call_expr) {
	char *name = call_expr.fn_name;

	grug_assert(call_expr.argument_count >= param_count, "Function call '%s' expected the argument '%s' with type %s", name, params[call_expr.argument_count].name, type_names[params[call_expr.argument_count].type]);

	grug_assert(call_expr.argument_count <= param_count, "Function call '%s' got an unexpected extra argument with type %s", name, type_names[call_expr.arguments[param_count].result_type]);

	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		struct expr *arg = &call_expr.arguments[argument_index];
		struct argument param = params[argument_index];

		if (arg->type == STRING_EXPR && param.type == type_resource) {
			arg->result_type = type_resource;
			arg->type = RESOURCE_EXPR;
			validate_resource_string(arg->literal.string, param.resource_extension);
		} else if (arg->type == STRING_EXPR && param.type == type_entity) {
			arg->result_type = type_entity;
			arg->type = ENTITY_EXPR;
			validate_entity_string(arg->literal.string);
			push_entity_type(param.entity_type);
		}

		grug_assert(arg->result_type == param.type, "Function call '%s' expected the type %s for argument '%s', but got %s", name, type_names[param.type], param.name, type_names[arg->result_type]);
	}
}

static void fill_call_expr(struct expr *expr) {
	struct call_expr call_expr = expr->call;

	for (size_t argument_index = 0; argument_index < call_expr.argument_count; argument_index++) {
		fill_expr(&call_expr.arguments[argument_index]);
	}

	char *name = expr->call.fn_name;

	struct helper_fn *helper_fn = get_helper_fn(name);
	if (helper_fn) {
		expr->result_type = helper_fn->return_type;

		check_arguments(helper_fn->arguments, helper_fn->argument_count, call_expr);

		return;
	}

	struct grug_game_function *game_fn = get_grug_game_fn(name);
	if (game_fn) {
		expr->result_type = game_fn->return_type;

		check_arguments(game_fn->arguments, game_fn->argument_count, call_expr);

		return;
	}

	if (starts_with(name, "helper_")) {
		grug_error("The function '%s' does not exist", name);
	} else {
		grug_error("The game function '%s' does not exist", name);
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

	grug_assert(binary_expr.left_expr->result_type == binary_expr.right_expr->result_type, "The left and right operand of a binary expression ('%s') must have the same type, but got %s and %s", get_token_type_str[binary_expr.operator], type_names[binary_expr.left_expr->result_type], type_names[binary_expr.right_expr->result_type]);

	switch (binary_expr.operator) {
		case EQUALS_TOKEN:
		case NOT_EQUALS_TOKEN:
			expr->result_type = type_bool;
			break;

		case GREATER_OR_EQUAL_TOKEN:
		case GREATER_TOKEN:
		case LESS_OR_EQUAL_TOKEN:
		case LESS_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_i32 || binary_expr.left_expr->result_type == type_f32, "'%s' operator expects i32 or f32", get_token_type_str[binary_expr.operator]);
			expr->result_type = type_bool;
			break;

		case AND_TOKEN:
		case OR_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_bool, "'%s' operator expects bool", get_token_type_str[binary_expr.operator]);
			expr->result_type = type_bool;
			break;

		case PLUS_TOKEN:
		case MINUS_TOKEN:
		case MULTIPLICATION_TOKEN:
		case DIVISION_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_i32 || binary_expr.left_expr->result_type == type_f32, "'%s' operator expects i32 or f32", get_token_type_str[binary_expr.operator]);
			expr->result_type = binary_expr.left_expr->result_type;
			break;

		case REMAINDER_TOKEN:
			grug_assert(binary_expr.left_expr->result_type == type_i32, "'%%' operator expects i32");
			expr->result_type = type_i32;
			break;

		case OPEN_PARENTHESIS_TOKEN:
		case CLOSE_PARENTHESIS_TOKEN:
		case OPEN_BRACE_TOKEN:
		case CLOSE_BRACE_TOKEN:
		case COMMA_TOKEN:
		case COLON_TOKEN:
		case PERIOD_TOKEN:
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
		case SPACES_TOKEN:
		case NEWLINES_TOKEN:
		case STRING_TOKEN:
		case WORD_TOKEN:
		case I32_TOKEN:
		case F32_TOKEN:
		case COMMENT_TOKEN:
			grug_unreachable();
	}
}

static struct variable *get_global_variable(char *name) {
	u32 i = buckets_global_variables[elf_hash(name) % MAX_GLOBAL_VARIABLES_IN_FILE];

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

static void add_global_variable(char *name, enum type type) {
	// TODO: Print the exact grug file path, function and line number
	grug_assert(global_variables_size < MAX_GLOBAL_VARIABLES_IN_FILE, "There are more than %d global variables in a grug file, exceeding MAX_GLOBAL_VARIABLES_IN_FILE", MAX_GLOBAL_VARIABLES_IN_FILE);

	grug_assert(!get_global_variable(name), "The global variable '%s' shadows an earlier global variable with the same name, so change the name of either of them", name);

	global_variables[global_variables_size] = (struct variable){
		.name = name,
		.type = type,
		.offset = globals_bytes,
	};

	globals_bytes += type_sizes[type];

	u32 bucket_index = elf_hash(name) % MAX_GLOBAL_VARIABLES_IN_FILE;

	chains_global_variables[global_variables_size] = buckets_global_variables[bucket_index];

	buckets_global_variables[bucket_index] = global_variables_size++;
}

static struct variable *get_local_variable(char *name) {
	if (variables_size == 0) {
		return NULL;
	}

	u32 i = buckets_variables[elf_hash(name) % MAX_VARIABLES_PER_FUNCTION];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, variables[i].name)) {
			break;
		}

		i = chains_variables[i];
	}

	return variables + i;
}

static struct variable *get_variable(char *name) {
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
			break;
		case STRING_EXPR:
			expr->result_type = type_string;
			break;
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
			grug_unreachable();
		case IDENTIFIER_EXPR: {
			struct variable *var = get_variable(expr->literal.string);
			if (var) {
				expr->result_type = var->type;
				return;
			}

			grug_error("The variable '%s' does not exist", expr->literal.string);

			break;
		}
		case I32_EXPR:
			expr->result_type = type_i32;
			break;
		case F32_EXPR:
			expr->result_type = type_f32;
			break;
		case UNARY_EXPR:
			fill_expr(expr->unary.expr);
			expr->result_type = expr->unary.expr->result_type;

			if (expr->unary.operator == NOT_TOKEN) {
				grug_assert(expr->result_type == type_bool, "Found 'not' before %s, but it can only be put before a bool", type_names[expr->result_type]);
			} else if (expr->unary.operator == MINUS_TOKEN) {
				grug_assert(expr->result_type == type_i32 || expr->result_type == type_f32, "Found '-' before %s, but it can only be put before an i32 or f32", type_names[expr->result_type]);
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
			break;
	}
}

static void add_local_variable(char *name, enum type type) {
	// TODO: Print the exact grug file path, function and line number
	grug_assert(variables_size < MAX_VARIABLES_PER_FUNCTION, "There are more than %d variables in a function, exceeding MAX_VARIABLES_PER_FUNCTION", MAX_VARIABLES_PER_FUNCTION);

	grug_assert(!get_local_variable(name), "The local variable '%s' shadows an earlier local variable with the same name, so change the name of either of them", name);
	grug_assert(!get_global_variable(name), "The local variable '%s' shadows an earlier global variable with the same name, so change the name of either of them", name);

	stack_frame_bytes += type_sizes[type];

	variables[variables_size] = (struct variable){
		.name = name,
		.type = type,
		.offset = stack_frame_bytes, // This field is unused by the section "FILLING RESULT TYPES", but used by the section "COMPILING"
	};

	u32 bucket_index = elf_hash(name) % MAX_VARIABLES_PER_FUNCTION;

	chains_variables[variables_size] = buckets_variables[bucket_index];

	buckets_variables[bucket_index] = variables_size++;
}

static void fill_statements(struct statement *statements_offset, size_t statement_count) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		struct statement statement = statements_offset[statement_index];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				// This has to happen before the add_local_variable() we do below,
				// because `a: i32 = a` should throw
				fill_expr(statement.variable_statement.assignment_expr);

				struct variable *var = get_variable(statement.variable_statement.name);

				if (statement.variable_statement.has_type) {
					grug_assert(!var, "The variable '%s' already exists", statement.variable_statement.name);

					grug_assert(statement.variable_statement.type == statement.variable_statement.assignment_expr->result_type, "Can't assign %s to '%s', which has type %s", type_names[statement.variable_statement.assignment_expr->result_type], statement.variable_statement.name, type_names[statement.variable_statement.type]);

					add_local_variable(statement.variable_statement.name, statement.variable_statement.type);
				} else if (var) {
					grug_assert(var->type == statement.variable_statement.assignment_expr->result_type, "Can't assign %s to '%s', which has type %s", type_names[statement.variable_statement.assignment_expr->result_type], var->name, type_names[var->type]);
				} else {
					grug_error("Can't assign to the variable '%s', since it does not exist", statement.variable_statement.name);
				}

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
					fill_expr(statement.return_statement.value);

					grug_assert(fn_return_type != type_void, "Function '%s' wasn't supposed to return any value", filled_fn_name);
					grug_assert(statement.return_statement.value->result_type == fn_return_type, "Function '%s' was supposed to return %s", filled_fn_name, type_names[fn_return_type]);
				} else {
					grug_assert(fn_return_type == type_void, "Function '%s' was supposed to return a value of type %s", filled_fn_name, type_names[fn_return_type]);
				}
				break;
			case WHILE_STATEMENT:
				fill_expr(&statement.while_statement.condition);

				fill_statements(statement.while_statement.body_statements, statement.while_statement.body_statement_count);

				break;
			case BREAK_STATEMENT:
				break;
			case CONTINUE_STATEMENT:
				break;
		}

		grug_log("}");
	}
}

static void init_argument_variables(struct argument *fn_arguments, size_t argument_count) {
	variables_size = 0;
	memset(buckets_variables, 0xff, MAX_VARIABLES_PER_FUNCTION * sizeof(u32));

	stack_frame_bytes = GLOBAL_OFFSET_TABLE_POINTER_SIZE + GLOBAL_VARIABLES_POINTER_SIZE;

	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		struct argument arg = fn_arguments[argument_index];
		add_local_variable(arg.name, arg.type);
	}
}

static void fill_helper_fns(void) {
	for (size_t fn_index = 0; fn_index < helper_fns_size; fn_index++) {
		struct helper_fn fn = helper_fns[fn_index];

		fn_return_type = fn.return_type;
		filled_fn_name = fn.fn_name;

		init_argument_variables(fn.arguments, fn.argument_count);

		fill_statements(fn.body_statements, fn.body_statement_count);

		if (fn_return_type == type_void) {
			if (fn.body_statement_count > 0) {
				struct statement last_statement = fn.body_statements[fn.body_statement_count - 1];

				if (last_statement.type == RETURN_STATEMENT) {
					grug_assert(!last_statement.return_statement.has_value, "Function '%s' wasn't supposed to return anything", filled_fn_name);
				}
			}
		} else {
			grug_assert(fn.body_statement_count > 0, "Function '%s' was supposed to return %s", filled_fn_name, type_names[fn_return_type]);

			struct statement last_statement = fn.body_statements[fn.body_statement_count - 1];

			grug_assert(last_statement.type == RETURN_STATEMENT && last_statement.return_statement.has_value && last_statement.return_statement.value->result_type == fn_return_type, "Function '%s' was supposed to return %s", filled_fn_name, type_names[fn_return_type]);
		}
	}
}

static struct grug_on_function *get_define_on_fn(char *name) {
	if (grug_define_entity->on_function_count == 0) {
		return NULL;
	}

	u32 i = buckets_define_on_fns[elf_hash(name) % grug_define_entity->on_function_count];

	while (true) {
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
	memset(buckets_define_on_fns, 0xff, grug_define_entity->on_function_count * sizeof(u32));

	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		char *name = grug_define_entity->on_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_define_entity->on_function_count;

		chains_define_on_fns[i] = buckets_define_on_fns[bucket_index];

		buckets_define_on_fns[bucket_index] = i;
	}
}

static void fill_on_fns(void) {
	fn_return_type = type_void;

	for (size_t fn_index = 0; fn_index < on_fns_size; fn_index++) {
		struct on_fn fn = on_fns[fn_index];

		char *name = fn.fn_name;
		filled_fn_name = name;

		struct grug_on_function *define_on_fn = get_define_on_fn(on_fns[fn_index].fn_name);

		grug_assert(define_on_fn, "The function '%s' was not was not declared by entity '%s' in mod_api.json", on_fns[fn_index].fn_name, define_fn.return_type);

		struct argument *args = fn.arguments;
		size_t arg_count = fn.argument_count;

		struct argument *params = define_on_fn->arguments;
		size_t param_count = define_on_fn->argument_count;

		grug_assert(arg_count >= param_count, "Function '%s' expected the parameter '%s' with type %s", name, params[arg_count].name, type_names[params[arg_count].type]);

		grug_assert(arg_count <= param_count, "Function '%s' got an unexpected extra parameter '%s' with type %s", name, args[param_count].name, type_names[args[param_count].type]);

		for (size_t argument_index = 0; argument_index < arg_count; argument_index++) {
			enum type arg_type = args[argument_index].type;
			struct argument param = params[argument_index];

			grug_assert(arg_type == param.type, "Function '%s' its '%s' parameter was supposed to have the type %s, but was %s", name, param.name, type_names[param.type], type_names[arg_type]);
		}

		init_argument_variables(args, arg_count);

		fill_statements(fn.body_statements, fn.body_statement_count);
	}
}

static void check_global_expr(struct expr *expr, char *global_name) {
	switch (expr->type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
		case STRING_EXPR:
		case I32_EXPR:
		case F32_EXPR:
			break;
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
			grug_unreachable();
		case IDENTIFIER_EXPR:
			grug_error("The global variable '%s' is using a global variable, which isn't allowed", global_name);
			break;
		case UNARY_EXPR:
			check_global_expr(expr->unary.expr, global_name);
			break;
		case BINARY_EXPR:
		case LOGICAL_EXPR:
			check_global_expr(expr->binary.left_expr, global_name);
			check_global_expr(expr->binary.right_expr, global_name);
			break;
		case CALL_EXPR:
			grug_error("The global variable '%s' is calling a function, which isn't allowed", global_name);
			break;
		case PARENTHESIZED_EXPR:
			check_global_expr(expr->parenthesized, global_name);
			break;
	}
}

static void fill_global_variables(void) {
	for (size_t i = 0; i < global_variable_statements_size; i++) {
		struct global_variable_statement *global = &global_variable_statements[i];

		check_global_expr(&global->assignment_expr, global->name);

		fill_expr(&global->assignment_expr);

		grug_assert(global->type == global->assignment_expr.result_type, "Can't assign %s to '%s', which has type %s", type_names[global->assignment_expr.result_type], global->name, type_names[global->type]);

		add_global_variable(global->name, global->type);
	}
}

static void check_define_fn_field_doesnt_contain_call_nor_identifier(struct expr *expr) {
	switch (expr->type) {
		case TRUE_EXPR:
		case FALSE_EXPR:
		case STRING_EXPR:
		case RESOURCE_EXPR:
		case ENTITY_EXPR:
		case I32_EXPR:
		case F32_EXPR:
			break;
		case IDENTIFIER_EXPR:
			grug_error("The define function isn't allowed to use global variables");
			break;
		case UNARY_EXPR:
			check_define_fn_field_doesnt_contain_call_nor_identifier(expr->unary.expr);
			break;
		case BINARY_EXPR:
		case LOGICAL_EXPR:
			check_define_fn_field_doesnt_contain_call_nor_identifier(expr->binary.left_expr);
			check_define_fn_field_doesnt_contain_call_nor_identifier(expr->binary.right_expr);
			break;
		case CALL_EXPR:
			grug_error("The define function isn't allowed to call a function");
			break;
		case PARENTHESIZED_EXPR:
			check_define_fn_field_doesnt_contain_call_nor_identifier(expr->parenthesized);
			break;
	}
}

static void fill_define_fn(void) {
	size_t field_count = define_fn.returned_compound_literal.field_count;

	grug_assert(grug_define_entity->field_count == define_fn.returned_compound_literal.field_count, "The entity '%s' expects %zu fields, but got %zu", grug_define_entity->name, grug_define_entity->field_count, define_fn.returned_compound_literal.field_count);

	for (size_t i = 0; i < field_count; i++) {
		struct expr *field = &define_fn.returned_compound_literal.fields[i].expr_value;

		check_define_fn_field_doesnt_contain_call_nor_identifier(field);

		fill_expr(field);

		struct argument json_field = grug_define_entity->fields[i];

		if (field->type == STRING_EXPR && json_field.type == type_resource) {
			field->result_type = type_resource;
			field->type = RESOURCE_EXPR;
			validate_resource_string(field->literal.string, json_field.resource_extension);
		} else if (field->type == STRING_EXPR && json_field.type == type_entity) {
			field->result_type = type_entity;
			field->type = ENTITY_EXPR;
			validate_entity_string(field->literal.string);
			push_entity_type(json_field.entity_type);
		}

		grug_assert(field->result_type == json_field.type, "The define function its '%s' parameter was supposed to have the type %s, but was %s", json_field.name, type_names[json_field.type], type_names[field->result_type]);
	}
}

// TODO: This could be turned O(1) with a hash map
static struct grug_entity *get_grug_define_entity(char *return_type) {
	for (size_t i = 0; i < grug_define_functions_size; i++) {
		if (streq(return_type, grug_define_functions[i].name)) {
			return grug_define_functions + i;
		}
	}
	return NULL;
}

static void fill_result_types(void) {
	reset_filling();

	grug_define_entity = get_grug_define_entity(define_fn.return_type);

	grug_assert(grug_define_entity, "The entity '%s' was not declared by mod_api.json", define_fn.return_type);

	hash_define_on_fns();

	fill_define_fn();
	fill_global_variables();
	fill_on_fns();
	fill_helper_fns();
}

//// COMPILING

#define GAME_FN_PREFIX "game_fn_"

#define MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS 420420
#define MAX_SYMBOLS 420420
#define MAX_CODES 420420
#define MAX_RESOURCE_STRINGS_CHARACTERS 420420
#define MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS 420420
#define MAX_DATA_STRING_CODES 420420
#define MAX_GAME_FN_CALLS 420420
#define MAX_HELPER_FN_CALLS 420420
#define MAX_USED_GAME_FNS 420
#define MAX_HELPER_FN_OFFSETS 420420
#define MAX_STACK_SIZE 420420
#define MAX_RESOURCES 420420
#define MAX_LOOP_DEPTH 420
#define MAX_BREAK_STATEMENTS_PER_LOOP 420
#define MAX_GOT_ACCESSES (MAX_ON_FNS_IN_FILE + MAX_HELPER_FNS_IN_FILE)
#define NEXT_INSTRUCTION_OFFSET sizeof(u32)

// 0xDEADBEEF in little-endian
#define PLACEHOLDER_16 0xADDE
#define PLACEHOLDER_32 0xEFBEADDE
#define PLACEHOLDER_64 0xEFBEADDEEFBEADDE

// Start of code enums

#define CALL 0xe8 // call a function
#define RET 0xc3 // ret
#define MOV_EAX_TO_DEREF_RDI_8_BIT_OFFSET 0x4789 // mov rdi[n], eax
#define MOV_RAX_TO_DEREF_RDI_8_BIT_OFFSET 0x478948 // mov rdi[n], rax
#define MOV_EAX_TO_DEREF_RDI_32_BIT_OFFSET 0x8789 // mov rdi[n], eax
#define MOV_RAX_TO_DEREF_RDI_32_BIT_OFFSET 0x878948 // mov rdi[n], rax

#define PUSH_RAX 0x50 // push rax
#define PUSH_RBP 0x55 // push rbp
#define PUSH_32_BITS 0x68 // push n

#define MOV_RSP_TO_RBP 0xe58948 // mov rbp, rsp
#define SUB_RSP_8_BITS 0xec8348 // sub rsp, n
#define SUB_RSP_32_BITS 0xec8148 // sub rsp, n
#define ADD_RSP_8_BITS 0xc48348 // add rsp, n

#define MOV_ESI_TO_DEREF_RBP 0x7589 // mov rbp[n], esi
#define MOV_EDX_TO_DEREF_RBP 0x5589 // mov rbp[n], edx
#define MOV_ECX_TO_DEREF_RBP 0x4d89 // mov rbp[n], ecx
#define MOV_R8D_TO_DEREF_RBP 0x458944 // mov rbp[n], r8d
#define MOV_R9D_TO_DEREF_RBP 0x4d8944 // mov rbp[n], r9d

#define MOV_XMM0_TO_DEREF_RBP 0x45110ff3 // movss rbp[n], xmm0
#define MOV_XMM1_TO_DEREF_RBP 0x4d110ff3 // movss rbp[n], xmm1
#define MOV_XMM2_TO_DEREF_RBP 0x55110ff3 // movss rbp[n], xmm2
#define MOV_XMM3_TO_DEREF_RBP 0x5d110ff3 // movss rbp[n], xmm3
#define MOV_XMM4_TO_DEREF_RBP 0x65110ff3 // movss rbp[n], xmm4
#define MOV_XMM5_TO_DEREF_RBP 0x6d110ff3 // movss rbp[n], xmm5
#define MOV_XMM6_TO_DEREF_RBP 0x75110ff3 // movss rbp[n], xmm6
#define MOV_XMM7_TO_DEREF_RBP 0x7d110ff3 // movss rbp[n], xmm7

#define MOV_RBX_TO_DEREF_RBP 0x5d8948 // mov rbp[n], rbx
#define MOV_RDI_TO_DEREF_RBP 0x7d8948 // mov rbp[n], rdi
#define MOV_RSI_TO_DEREF_RBP 0x758948 // mov rbp[n], rsi
#define MOV_RDX_TO_DEREF_RBP 0x558948 // mov rbp[n], rdx
#define MOV_RCX_TO_DEREF_RBP 0x4d8948 // mov rbp[n], rcx
#define MOV_R8_TO_DEREF_RBP 0x45894c // mov rbp[n], r8
#define MOV_R9_TO_DEREF_RBP 0x4d894c // mov rbp[n], r9

#define DEREF_RBP_TO_EAX 0x458b // mov eax, rbp[n]
#define DEREF_RBP_TO_RAX 0x458b48 // mov rax, rbp[n]
#define DEREF_RBP_TO_RBX 0x5d8b48 // mov rbx, rbp[n]
#define DEREF_RBP_TO_R11 0x5d8b4c // mov r11, rbp[n]

#define DEREF_RBX_TO_RSI 0xb38b48 // mov rsi, rbx[n]
#define DEREF_RBX_TO_R11 0x9b8b4c // mov r11, rbx[n]

#define MOV_EAX_TO_DEREF_RBP 0x4589 // mov rbp[n], eax
#define MOV_RAX_TO_DEREF_RBP 0x458948 // mov rbp[n], rax

#define DEREF_RAX_TO_EAX 0x408b // mov eax, rax[n]
#define DEREF_RAX_TO_RAX 0x408b48 // mov rax, rax[n]

#define MOV_EAX_TO_DEREF_R11 0x438941 // mov r11[n], eax
#define MOV_RAX_TO_DEREF_R11 0x438949 // mov r11[n], rax
#define MOV_RAX_TO_DEREF_R11_OFFSETLESS 0x38949 // mov [r11], rax

#define MOV_RBP_TO_RSP 0xec8948 // mov rsp, rbp
#define POP_RBP 0x5d // pop rbp

#define ADD_R11_TO_RAX 0xd8014c // add rax, r11
#define SUB_R11_FROM_RAX 0xd8294c // sub rax, r11
#define MUL_RAX_BY_R11 0xebf749 // imul r11

#define CQO_CLEAR_BEFORE_DIVISION 0x9948 // cqo
#define DIV_RAX_BY_R11 0xfbf749 // idiv r11
#define MOV_RDX_TO_RAX 0xd08948 // mov rax, rdx

#define CMP_RAX_WITH_R11 0xd8394c // cmp rax, r11

// See this for an explanation of "ordered" vs. "unordered":
// https://stackoverflow.com/a/8627368/13279557
#define ORDERED_CMP_XMM0_WITH_XMM1 0xc12f0f // comiss xmm0, xmm1

#define NEGATE_RAX 0xd8f748 // neg rax

#define TEST_EAX_IS_ZERO 0xc085 // test eax, eax

#define JE_8_BIT_OFFSET 0x74 // je $+n
#define JE_32_BIT_OFFSET 0x840f // je strict $+n
#define JMP_32_BIT_OFFSET 0xe9 // jmp $+n

#define SETE_AL 0xc0940f // sete al
#define SETNE_AL 0xc0950f // setne al
#define SETGT_AL 0xc09f0f // setg al
#define SETGE_AL 0xc09d0f // setge al
#define SETLT_AL 0xc09c0f // setl al
#define SETLE_AL 0xc09e0f // setle al

#define SETA_AL 0xc0970f // seta al (set if above)
#define SETAE_AL 0xc0930f // setae al (set if above or equal)
#define SETB_AL 0xc0920f // setb al (set if below)
#define SETBE_AL 0xc0960f // setbe al (set if below or equal)

#define POP_RAX 0x58 // pop rax
#define POP_R11 0x5b41 // pop r11

#define POP_RDI 0x5f // pop rdi
#define POP_RSI 0x5e // pop rsi
#define POP_RDX 0x5a // pop rdx
#define POP_RCX 0x59 // pop rcx
#define POP_R8 0x5841 // pop r8
#define POP_R9 0x5941 // pop r9

#define XOR_EAX_BY_N 0x35 // xor eax, n
#define XOR_CLEAR_EAX 0xc031 // xor eax, eax
#define XOR_CLEAR_EDI 0xff31 // xor edi, edi
#define XOR_CLEAR_EDX 0xd231 // xor edx, edx
#define LEA_STRINGS_TO_RAX 0x58d48 // lea rax, strings[rel n]

#define LEA_RIP_TO_RBX 0x1d8d48 // lea rbx, [rel $$]

#define MOV_EAX_TO_XMM0 0xc06e0f66 // movd xmm0, eax
#define MOV_EAX_TO_XMM1 0xc86e0f66 // movd xmm1, eax
#define MOV_EAX_TO_XMM2 0xd06e0f66 // movd xmm2, eax
#define MOV_EAX_TO_XMM3 0xd86e0f66 // movd xmm3, eax
#define MOV_EAX_TO_XMM4 0xe06e0f66 // movd xmm4, eax
#define MOV_EAX_TO_XMM5 0xe86e0f66 // movd xmm5, eax
#define MOV_EAX_TO_XMM6 0xf06e0f66 // movd xmm6, eax
#define MOV_EAX_TO_XMM7 0xf86e0f66 // movd xmm7, eax

#define MOV_RAX_TO_RDI 0xc78948 // mov rdi, rax
#define MOV_RAX_TO_RSI 0xc68948 // mov rsi, rax
#define MOV_RAX_TO_RDX 0xc28948 // mov rdx, rax
#define MOV_RAX_TO_RCX 0xc18948 // mov rcx, rax
#define MOV_RAX_TO_R8 0xc08949 // mov r8, rax
#define MOV_RAX_TO_R9 0xc18949 // mov r9, rax

#define MOV_R11D_TO_XMM1 0xcb6e0f4166 // movd xmm1, r11d

#define MOV_R11_TO_RSI 0xde894c // mov rsi, r11

#define ADD_XMM1_TO_XMM0 0xc1580ff3 // addss xmm0, xmm1
#define SUB_XMM1_FROM_XMM0 0xc15c0ff3 // subss xmm0, xmm1
#define MUL_XMM0_WITH_XMM1 0xc1590ff3 // mulss xmm0, xmm1
#define DIV_XMM0_BY_XMM1 0xc15e0ff3 // divss xmm0, xmm1

#define MOV_XMM0_TO_EAX 0xc07e0f66 // movd eax, xmm0

#define MOV_TO_EAX 0xb8 // mov eax, n
#define MOV_TO_EDI 0xbf // mov edi, n

#define ADD_TO_RBX 0xc38148 // add rbx, n

#define NOP_8_BITS 0x90 // nop
#define NOP_32_BITS 0x401f0f // There isn't a nasm equivalent

#define PUSH_REL 0x35ff // Not quite push qword [$+n]
#define JMP_REL 0x25ff // Not quite jmp [$+n]

// End of code enums

struct data_string_code {
	char *string;
	size_t code_offset;
};

static size_t text_offsets[MAX_SYMBOLS];

static u8 codes[MAX_CODES];
static size_t codes_size;

static char *define_fn_name;

static char resource_strings[MAX_RESOURCE_STRINGS_CHARACTERS];
static size_t resource_strings_size;

static char entity_dependency_strings[MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS];
static size_t entity_dependency_strings_size;

static struct data_string_code data_string_codes[MAX_DATA_STRING_CODES];
static size_t data_string_codes_size;

struct fn_call {
	char *fn_name;
	size_t codes_offset;
};
static struct fn_call extern_fn_calls[MAX_GAME_FN_CALLS];
static size_t extern_fn_calls_size;
static struct fn_call helper_fn_calls[MAX_HELPER_FN_CALLS];
static size_t helper_fn_calls_size;

static char *used_extern_fns[MAX_USED_GAME_FNS];
static size_t extern_fns_size;
static u32 buckets_used_extern_fns[BFD_HASH_BUCKET_SIZE];
static u32 chains_used_extern_fns[MAX_USED_GAME_FNS];

static char used_extern_fn_symbols[MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS];
static size_t used_extern_fn_symbols_size;

struct fn_offset {
	char *fn_name;
	size_t offset;
};

static struct fn_offset helper_fn_offsets[MAX_HELPER_FN_OFFSETS];
static size_t helper_fn_offsets_size;
static u32 buckets_helper_fn_offsets[MAX_HELPER_FN_OFFSETS];
static u32 chains_helper_fn_offsets[MAX_HELPER_FN_OFFSETS];

static size_t stack_size;

static size_t start_of_loop_jump_offsets[MAX_LOOP_DEPTH];
static size_t start_of_loop_jump_offsets_size;

struct loop_break_statements {
	size_t break_statements[MAX_BREAK_STATEMENTS_PER_LOOP];
	size_t break_statements_size;
};
static struct loop_break_statements loop_break_statements_stack[MAX_LOOP_DEPTH];
static size_t loop_break_statements_stack_size;

static size_t got_accesses[MAX_GOT_ACCESSES];
static size_t got_accesses_size;

static bool in_on_fn;

static bool calling_game_fn;

static u32 resources[MAX_RESOURCES];
static size_t resources_size;

static u32 entity_dependencies[MAX_ENTITY_DEPENDENCIES];
static size_t entity_dependencies_size;

static void reset_compiling(void) {
	codes_size = 0;
	resource_strings_size = 0;
	entity_dependency_strings_size = 0;
	data_string_codes_size = 0;
	extern_fn_calls_size = 0;
	helper_fn_calls_size = 0;
	extern_fns_size = 0;
	used_extern_fn_symbols_size = 0;
	helper_fn_offsets_size = 0;
	stack_size = 0;
	start_of_loop_jump_offsets_size = 0;
	loop_break_statements_stack_size = 0;
	got_accesses_size = 0;
	in_on_fn = false;
	calling_game_fn = false;
	resources_size = 0;
	entity_dependencies_size = 0;
}

static size_t get_helper_fn_offset(char *name) {
	u32 i = buckets_helper_fn_offsets[elf_hash(name) % helper_fn_offsets_size];

	while (true) {
		assert(i != UINT32_MAX && "get_helper_fn_offset() is supposed to never fail");

		if (streq(name, helper_fn_offsets[i].fn_name)) {
			break;
		}

		i = chains_helper_fn_offsets[i];
	}

	return helper_fn_offsets[i].offset;
}

static void hash_helper_fn_offsets(void) {
	memset(buckets_helper_fn_offsets, 0xff, helper_fn_offsets_size * sizeof(u32));

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

static bool has_used_extern_fn(char *name) {
	u32 i = buckets_used_extern_fns[bfd_hash(name) % BFD_HASH_BUCKET_SIZE];

	while (true) {
		if (i == UINT32_MAX) {
			return false;
		}

		if (streq(name, used_extern_fns[i])) {
			break;
		}

		i = chains_used_extern_fns[i];
	}

	return true;
}

static void hash_used_extern_fns(void) {
	memset(buckets_used_extern_fns, 0xff, BFD_HASH_BUCKET_SIZE * sizeof(u32));

	for (size_t i = 0; i < extern_fn_calls_size; i++) {
		char *name = extern_fn_calls[i].fn_name;

		if (has_used_extern_fn(name)) {
			continue;
		}

		used_extern_fns[extern_fns_size] = name;

		u32 bucket_index = bfd_hash(name) % BFD_HASH_BUCKET_SIZE;

		chains_used_extern_fns[extern_fns_size] = buckets_used_extern_fns[bucket_index];

		buckets_used_extern_fns[bucket_index] = extern_fns_size++;
	}
}

static void push_helper_fn_call(char *fn_name, size_t codes_offset) {
	grug_assert(helper_fn_calls_size < MAX_HELPER_FN_CALLS, "There are more than %d helper function calls, exceeding MAX_HELPER_FN_CALLS", MAX_HELPER_FN_CALLS);

	helper_fn_calls[helper_fn_calls_size++] = (struct fn_call){
		.fn_name = fn_name,
		.codes_offset = codes_offset,
	};
}

static char *push_used_extern_fn_symbol(char *name, bool is_game_fn) {
	size_t length = strlen(name);
	size_t fn_prefix_length = is_game_fn ? sizeof(GAME_FN_PREFIX) - 1 : 0;

	grug_assert(used_extern_fn_symbols_size + fn_prefix_length + length < MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS, "There are more than %d characters in the used_extern_fn_symbols array, exceeding MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS", MAX_USED_EXTERN_FN_SYMBOLS_CHARACTERS);

	char *symbol = used_extern_fn_symbols + used_extern_fn_symbols_size;

	if (is_game_fn) {
		memcpy(symbol, GAME_FN_PREFIX, fn_prefix_length);
		used_extern_fn_symbols_size += fn_prefix_length;
	}

	for (size_t i = 0; i < length; i++) {
		used_extern_fn_symbols[used_extern_fn_symbols_size++] = name[i];
	}
	used_extern_fn_symbols[used_extern_fn_symbols_size++] = '\0';

	return symbol;
}

static void push_extern_fn_call(char *fn_name, size_t codes_offset, bool is_game_fn) {
	grug_assert(extern_fn_calls_size < MAX_GAME_FN_CALLS, "There are more than %d game function calls, exceeding MAX_GAME_FN_CALLS", MAX_GAME_FN_CALLS);

	extern_fn_calls[extern_fn_calls_size++] = (struct fn_call){
		.fn_name = push_used_extern_fn_symbol(fn_name, is_game_fn),
		.codes_offset = codes_offset,
	};
}

static void push_game_fn_call(char *fn_name, size_t codes_offset) {
	push_extern_fn_call(fn_name, codes_offset, true);
}

static void push_system_fn_call(char *fn_name, size_t codes_offset) {
	push_extern_fn_call(fn_name, codes_offset, false);
}

static void push_data_string_code(char *string, size_t code_offset) {
	grug_assert(data_string_codes_size < MAX_DATA_STRING_CODES, "There are more than %d data string code bytes, exceeding MAX_DATA_STRING_CODES", MAX_DATA_STRING_CODES);

	data_string_codes[data_string_codes_size++] = (struct data_string_code){
		.string = string,
		.code_offset = code_offset,
	};
}

static void compile_byte(u8 byte) {
	grug_assert(codes_size < MAX_CODES, "There are more than %d code bytes, exceeding MAX_CODES", MAX_CODES);

	codes[codes_size++] = byte;
}

static void compile_padded_number(u64 n, size_t byte_count) {
	while (byte_count-- > 0) {
		compile_byte(n & 0xff); // Little-endian
		n >>= 8;
	}
}

static void compile_32(u32 n) {
	compile_padded_number(n, sizeof(u32));
}

static void compile_unpadded(u64 n) {
	while (n > 0) {
		compile_byte(n & 0xff); // Little-endian
		n >>= 8;
	}
}

static void stack_pop_arguments(struct expr *fn_arguments, size_t argument_count, bool gets_global_variables_pointer) {
	if (!gets_global_variables_pointer && argument_count == 0) {
		return;
	}

	// `integer` here refers to the classification type:
	// "integer types and pointers which use the general purpose registers"
	// See https://stackoverflow.com/a/57861992/13279557
	size_t integer_argument_count = 0;
	if (gets_global_variables_pointer) {
		integer_argument_count++;
	}

	size_t float_argument_count = 0;

	for (size_t i = 0; i < argument_count; i++) {
		struct expr argument = fn_arguments[i];

		if (argument.result_type == type_f32) {
			float_argument_count++;
		} else {
			integer_argument_count++;
		}
	}

	// TODO: This should be `<= 5` for helper fns
	grug_assert(integer_argument_count <= 6, "Currently grug only supports up to six bool/i32/string arguments");
	grug_assert(float_argument_count <= 8, "Currently grug only supports up to eight f32 arguments");

	size_t argument_count_including_globals_ptr = argument_count;
	if (gets_global_variables_pointer) {
		argument_count_including_globals_ptr++;
	}

	assert(stack_size >= argument_count_including_globals_ptr);
	stack_size -= argument_count_including_globals_ptr;

	// u64 is the size of the RAX register that gets pushed for every argument
	stack_frame_bytes -= sizeof(u64) * argument_count_including_globals_ptr;

	for (size_t i = argument_count; i > 0;) {
		struct expr argument = fn_arguments[--i];

		if (argument.result_type == type_f32) {
			compile_byte(POP_RAX);

			static u32 movs[] = {
				MOV_EAX_TO_XMM0,
				MOV_EAX_TO_XMM1,
				MOV_EAX_TO_XMM2,
				MOV_EAX_TO_XMM3,
				MOV_EAX_TO_XMM4,
				MOV_EAX_TO_XMM5,
				MOV_EAX_TO_XMM6,
				MOV_EAX_TO_XMM7,
			};

			compile_unpadded(movs[--float_argument_count]);
		} else {
			static u16 pops[] = {
				POP_RDI,
				POP_RSI,
				POP_RDX,
				POP_RCX,
				POP_R8,
				POP_R9,
			};

			compile_unpadded(pops[--integer_argument_count]);
		}
	}

	if (gets_global_variables_pointer) {
		// Pop the secret global variables pointer argument
		compile_byte(POP_RDI);
	}
}

static void overwrite_jmp_address(size_t jump_address, size_t size) {
	size_t byte_count = 4;
	for (u32 n = size - (jump_address + byte_count); byte_count > 0; n >>= 8, byte_count--) {
		codes[jump_address++] = n & 0xff; // Little-endian
	}
}

static void stack_pop_r11(void) {
	assert(stack_size > 0);
	--stack_size;

	compile_unpadded(POP_R11);
	stack_frame_bytes -= sizeof(u64);
}

static void stack_push_rax(void) {
	grug_assert(stack_size < MAX_STACK_SIZE, "There are more than %d stack values, exceeding MAX_STACK_SIZE", MAX_STACK_SIZE);
	stack_size++;

	compile_byte(PUSH_RAX);
	stack_frame_bytes += sizeof(u64);
}

static void compile_expr(struct expr expr);
static void compile_statements(struct statement *statements_offset, size_t statement_count);

static void push_break_statement_jump_address_offset(size_t offset) {
	grug_assert(loop_break_statements_stack_size > 0, "One of the break statements isn't inside of a while() loop");

	struct loop_break_statements *loop_break_statements = &loop_break_statements_stack[loop_break_statements_stack_size - 1];

	grug_assert(loop_break_statements->break_statements_size < MAX_BREAK_STATEMENTS_PER_LOOP, "There are more than %d break statements in one of the while() loops, exceeding MAX_BREAK_STATEMENTS_PER_LOOP", MAX_BREAK_STATEMENTS_PER_LOOP);

	loop_break_statements->break_statements[loop_break_statements->break_statements_size++] = offset;
}

static void push_loop_break_statements(void) {
	grug_assert(loop_break_statements_stack_size < MAX_LOOP_DEPTH, "There are more than %d loops nested inside each other, exceeding MAX_LOOP_DEPTH", MAX_LOOP_DEPTH);

	loop_break_statements_stack[loop_break_statements_stack_size++].break_statements_size = 0;
}

static void push_start_of_loop_jump_offset(size_t offset) {
	grug_assert(start_of_loop_jump_offsets_size < MAX_LOOP_DEPTH, "There are more than %d offsets in start_of_loop_jump_offsets[], exceeding MAX_LOOP_DEPTH", MAX_LOOP_DEPTH);

	start_of_loop_jump_offsets[start_of_loop_jump_offsets_size++] = offset;
}

static void compile_while_statement(struct while_statement while_statement) {
	size_t start_of_loop_jump_offset = codes_size;

	push_start_of_loop_jump_offset(start_of_loop_jump_offset);
	push_loop_break_statements();

	compile_expr(while_statement.condition);
	compile_unpadded(TEST_EAX_IS_ZERO);
	compile_unpadded(JE_32_BIT_OFFSET);
	size_t end_jump_offset = codes_size;
	compile_unpadded(PLACEHOLDER_32);

	compile_statements(while_statement.body_statements, while_statement.body_statement_count);

	compile_unpadded(JMP_32_BIT_OFFSET);
	compile_32(start_of_loop_jump_offset - (codes_size + NEXT_INSTRUCTION_OFFSET));

	overwrite_jmp_address(end_jump_offset, codes_size);

	struct loop_break_statements *loop_break_statements = &loop_break_statements_stack[loop_break_statements_stack_size - 1];

	for (size_t i = 0; i < loop_break_statements->break_statements_size; i++) {
		size_t break_statement_codes_offset = loop_break_statements->break_statements[i];

		overwrite_jmp_address(break_statement_codes_offset, codes_size);
	}

	start_of_loop_jump_offsets_size--;
	loop_break_statements_stack_size--;
}

static void compile_if_statement(struct if_statement if_statement) {
	compile_expr(if_statement.condition);
	compile_unpadded(TEST_EAX_IS_ZERO);
	compile_unpadded(JE_32_BIT_OFFSET);
	size_t else_or_end_jump_offset = codes_size;
	compile_unpadded(PLACEHOLDER_32);
	compile_statements(if_statement.if_body_statements, if_statement.if_body_statement_count);

	if (if_statement.else_body_statement_count > 0) {
		compile_unpadded(JMP_32_BIT_OFFSET);
		size_t skip_else_jump_offset = codes_size;
		compile_unpadded(PLACEHOLDER_32);

		overwrite_jmp_address(else_or_end_jump_offset, codes_size);

		compile_statements(if_statement.else_body_statements, if_statement.else_body_statement_count);

		overwrite_jmp_address(skip_else_jump_offset, codes_size);
	} else {
		overwrite_jmp_address(else_or_end_jump_offset, codes_size);
	}
}

// 0 0000 => 0 0000
// 0 0001 => 0 1111 (NOT 1 1111, due to the `& 0xf`)
// 0 1111 => 0 0001
// 1 0000 => 0 0000
// 1 0001 => 0 1111
static size_t get_padding(void) {
	return -stack_frame_bytes & 0xf;
}

static void compile_call_expr(struct call_expr call_expr) {
	char *fn_name = call_expr.fn_name;
	struct grug_game_function *game_fn = get_grug_game_fn(fn_name);

	if (game_fn) {
		calling_game_fn = true;

		// Compile sigprocmask(SIG_BLOCK, &grug_block_mask, NULL);
		compile_unpadded(XOR_CLEAR_EDX);
		compile_unpadded(DEREF_RBX_TO_RSI);
		compile_32(-(u8)GRUG_BLOCK_MASK_OFFSET);
		compile_unpadded(XOR_CLEAR_EDI);
		compile_byte(CALL);
		push_system_fn_call("sigprocmask", codes_size);
		compile_unpadded(PLACEHOLDER_32);
	}

	bool gets_global_variables_pointer = false;
	if (get_helper_fn(call_expr.fn_name)) {
		// Push the secret global variables pointer argument
		compile_unpadded(DEREF_RBP_TO_RAX);
		compile_byte(-(u8)(GLOBAL_OFFSET_TABLE_POINTER_SIZE + GLOBAL_VARIABLES_POINTER_SIZE));
		stack_push_rax();

		// The secret global variables pointer argument will need to get popped
		gets_global_variables_pointer = true;
	}

	for (size_t i = 0; i < call_expr.argument_count; i++) {
		struct expr argument = call_expr.arguments[i];

		// TODO: Verify that the argument has the same type as the one in grug_define_entity
		// TODO: This should be done when the AST gets created, not during compilation!

		compile_expr(argument);
		stack_push_rax();
	}

	stack_pop_arguments(call_expr.arguments, call_expr.argument_count, gets_global_variables_pointer);

	// Ensures the call will be 16-byte aligned
	size_t padding = get_padding();
	if (padding > 0) {
		compile_unpadded(SUB_RSP_8_BITS);
		compile_byte(padding);
		stack_frame_bytes += padding;
	}

	compile_byte(CALL);

	bool returns_float = false;
	if (game_fn) {
		push_game_fn_call(fn_name, codes_size);
		returns_float = game_fn->return_type == type_f32;
	} else {
		struct helper_fn *helper_fn = get_helper_fn(fn_name);
		if (helper_fn) {
			push_helper_fn_call(fn_name, codes_size);
			returns_float = helper_fn->return_type == type_f32;
		} else {
			grug_unreachable();
		}
	}
	compile_unpadded(PLACEHOLDER_32);

	// Ensures the top of the stack is where it was before the alignment,
	// which is important during nested expressions, since they expect
	// the top of the stack to hold their intermediate values
	if (padding > 0) {
		compile_unpadded(ADD_RSP_8_BITS);
		compile_byte(padding);
		stack_frame_bytes += padding;
	}

	if (returns_float) {
		compile_unpadded(MOV_XMM0_TO_EAX);
	}

	if (game_fn) {
		compile_unpadded(PUSH_RAX);

		// Compile sigprocmask(SIG_UNBLOCK, &grug_block_mask, NULL);
		compile_unpadded(XOR_CLEAR_EDX);
		compile_unpadded(DEREF_RBX_TO_RSI);
		compile_32(-(u8)GRUG_BLOCK_MASK_OFFSET);
		compile_unpadded(MOV_TO_EDI);
		compile_32(1);
		compile_unpadded(SUB_RSP_8_BITS);
		compile_byte(0x8);
		compile_byte(CALL);
		push_system_fn_call("sigprocmask", codes_size);
		compile_unpadded(PLACEHOLDER_32);
		compile_unpadded(ADD_RSP_8_BITS);
		compile_byte(0x8);

		compile_unpadded(POP_RAX);
	}
}

static void compile_logical_expr(struct binary_expr logical_expr) {
	switch (logical_expr.operator) {
		case AND_TOKEN: {
			compile_expr(*logical_expr.left_expr);
			compile_unpadded(TEST_EAX_IS_ZERO);
			compile_unpadded(JE_32_BIT_OFFSET);
			size_t end_jump_offset = codes_size;
			compile_unpadded(PLACEHOLDER_32);
			compile_expr(*logical_expr.right_expr);
			compile_unpadded(TEST_EAX_IS_ZERO);
			compile_unpadded(MOV_TO_EAX);
			compile_32(0);
			compile_unpadded(SETNE_AL);
			overwrite_jmp_address(end_jump_offset, codes_size);
			break;
		}
		case OR_TOKEN: {
			compile_expr(*logical_expr.left_expr);
			compile_unpadded(TEST_EAX_IS_ZERO);
			compile_byte(JE_8_BIT_OFFSET);
			compile_byte(10); // Jump 10 bytes forward
			compile_byte(MOV_TO_EAX);
			compile_32(1);
			compile_unpadded(JMP_32_BIT_OFFSET);
			size_t end_jump_offset = codes_size;
			compile_unpadded(PLACEHOLDER_32);
			compile_expr(*logical_expr.right_expr);
			compile_unpadded(TEST_EAX_IS_ZERO);
			compile_unpadded(MOV_TO_EAX);
			compile_32(0);
			compile_unpadded(SETNE_AL);
			overwrite_jmp_address(end_jump_offset, codes_size);
			break;
		}
		default:
			grug_unreachable();
	}
}

static void compile_binary_expr(struct expr expr) {
	assert(expr.type == BINARY_EXPR);
	struct binary_expr binary_expr = expr.binary;

	compile_expr(*binary_expr.right_expr);
	stack_push_rax();
	compile_expr(*binary_expr.left_expr);
	stack_pop_r11();

	switch (binary_expr.operator) {
		case PLUS_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(ADD_R11_TO_RAX);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(ADD_XMM1_TO_XMM0);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case MINUS_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(SUB_R11_FROM_RAX);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(SUB_XMM1_FROM_XMM0);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case MULTIPLICATION_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(MUL_RAX_BY_R11);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(MUL_XMM0_WITH_XMM1);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case DIVISION_TOKEN:
			if (expr.result_type == type_i32) {
				compile_unpadded(CQO_CLEAR_BEFORE_DIVISION);
				compile_unpadded(DIV_RAX_BY_R11);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(DIV_XMM0_BY_XMM1);
				compile_unpadded(MOV_XMM0_TO_EAX);
			}
			break;
		case REMAINDER_TOKEN:
			compile_unpadded(CQO_CLEAR_BEFORE_DIVISION);
			compile_unpadded(DIV_RAX_BY_R11);
			compile_unpadded(MOV_RDX_TO_RAX);
			break;
		case EQUALS_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETE_AL);
			} else if (binary_expr.left_expr->result_type == type_f32) {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETE_AL);
			} else {
				compile_unpadded(MOV_R11_TO_RSI);
				compile_unpadded(MOV_RAX_TO_RDI);
				compile_byte(CALL);
				push_system_fn_call("strcmp", codes_size);
				compile_unpadded(PLACEHOLDER_32);
				compile_unpadded(TEST_EAX_IS_ZERO);
				compile_unpadded(SETE_AL);
			}
			break;
		case NOT_EQUALS_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETNE_AL);
			} else if (binary_expr.left_expr->result_type == type_f32) {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETNE_AL);
			} else {
				compile_unpadded(MOV_R11_TO_RSI);
				compile_unpadded(MOV_RAX_TO_RDI);
				compile_byte(CALL);
				push_system_fn_call("strcmp", codes_size);
				compile_unpadded(PLACEHOLDER_32);
				compile_unpadded(TEST_EAX_IS_ZERO);
				compile_unpadded(SETNE_AL);
			}
			break;
		case GREATER_OR_EQUAL_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETGE_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETAE_AL);
			}
			break;
		case GREATER_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETGT_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETA_AL);
			}
			break;
		case LESS_OR_EQUAL_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETLE_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETBE_AL);
			}
			break;
		case LESS_TOKEN:
			if (binary_expr.left_expr->result_type == type_bool || binary_expr.left_expr->result_type == type_i32) {
				compile_unpadded(CMP_RAX_WITH_R11);
				compile_unpadded(MOV_TO_EAX);
				compile_32(0);
				compile_unpadded(SETLT_AL);
			} else {
				compile_unpadded(MOV_EAX_TO_XMM0);
				compile_unpadded(MOV_R11D_TO_XMM1);
				compile_unpadded(XOR_CLEAR_EAX);
				compile_unpadded(ORDERED_CMP_XMM0_WITH_XMM1);
				compile_unpadded(SETB_AL);
			}
			break;
		default:
			grug_unreachable();
	}
}

static void compile_unary_expr(struct unary_expr unary_expr) {
	switch (unary_expr.operator) {
		case MINUS_TOKEN:
			compile_expr(*unary_expr.expr);
			if (unary_expr.expr->result_type == type_i32) {
				compile_unpadded(NEGATE_RAX);
			} else {
				compile_unpadded(XOR_EAX_BY_N);
				compile_32(0x80000000);
			}
			break;
		case NOT_TOKEN:
			compile_expr(*unary_expr.expr);
			compile_unpadded(TEST_EAX_IS_ZERO);
			compile_unpadded(MOV_TO_EAX);
			compile_32(0);
			compile_unpadded(SETE_AL);
			break;
		default:
			grug_unreachable();
	}
}

static void push_entity_dependency(u32 string_index) {
	grug_assert(entity_dependencies_size < MAX_ENTITY_DEPENDENCIES, "There are more than %d entity dependencies, exceeding MAX_ENTITY_DEPENDENCIES", MAX_ENTITY_DEPENDENCIES);

	entity_dependencies[entity_dependencies_size++] = string_index;
}

static void push_resource(u32 string_index) {
	grug_assert(resources_size < MAX_RESOURCES, "There are more than %d resources, exceeding MAX_RESOURCES", MAX_RESOURCES);

	resources[resources_size++] = string_index;
}

static char *push_entity_dependency_string(char *string) {
	static char entity[MAX_ENTITY_DEPENDENCY_NAME_LENGTH];

	if (strchr(string, ':')) {
		strcpy(entity, string);
	} else {
		snprintf(entity, sizeof(entity), "%s:%s", mod, string);
	}

	size_t length = strlen(entity);

	grug_assert(entity_dependency_strings_size + length < MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS, "There are more than %d characters in the entity_dependency_strings array, exceeding MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS", MAX_ENTITY_DEPENDENCIES_STRINGS_CHARACTERS);

	char *entity_str = entity_dependency_strings + entity_dependency_strings_size;

	for (size_t i = 0; i < length; i++) {
		entity_dependency_strings[entity_dependency_strings_size++] = entity[i];
	}
	entity_dependency_strings[entity_dependency_strings_size++] = '\0';

	return entity_str;
}

static char *push_resource_string(char *string) {
	static char resource[STUPID_MAX_PATH];
	snprintf(resource, sizeof(resource), MODS_DIR_PATH"/%s/%s", mod, string);

	size_t length = strlen(resource);

	grug_assert(resource_strings_size + length < MAX_RESOURCE_STRINGS_CHARACTERS, "There are more than %d characters in the resource_strings array, exceeding MAX_RESOURCE_STRINGS_CHARACTERS", MAX_RESOURCE_STRINGS_CHARACTERS);

	char *resource_str = resource_strings + resource_strings_size;

	for (size_t i = 0; i < length; i++) {
		resource_strings[resource_strings_size++] = resource[i];
	}
	resource_strings[resource_strings_size++] = '\0';

	return resource_str;
}

static void compile_expr(struct expr expr) {
	switch (expr.type) {
		case TRUE_EXPR:
			compile_byte(MOV_TO_EAX);
			compile_32(1);
			break;
		case FALSE_EXPR:
			compile_unpadded(XOR_CLEAR_EAX);
			break;
		case STRING_EXPR: {
			char *string = expr.literal.string;

			add_data_string(string);

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case RESOURCE_EXPR: {
			char *string = expr.literal.string;

			string = push_resource_string(string);

			bool had_string = get_data_string_index(string) != UINT32_MAX;

			add_data_string(string);

			if (!had_string) {
				push_resource(get_data_string_index(string));
			}

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case ENTITY_EXPR: {
			char *string = expr.literal.string;

			string = push_entity_dependency_string(string);

			add_data_string(string);

			// We can't do the same thing as with RESOURCE_EXPR,
			// where we only call `push_entity_dependency()` when `!had_string`,
			// because the same entity dependency strings
			// can have with different "entity_type" values in mod_api.json
			// (namely, game fn 1 might have "car", and game fn 2 the empty string "")
			push_entity_dependency(get_data_string_index(string));

			compile_unpadded(LEA_STRINGS_TO_RAX);

			// RIP-relative address of data string
			push_data_string_code(string, codes_size);
			compile_unpadded(PLACEHOLDER_32);

			break;
		}
		case IDENTIFIER_EXPR: {
			struct variable *var = get_local_variable(expr.literal.string);
			if (var) {
				// TODO: Support any 32 bit offset, instead of only 8 bits
				switch (var->type) {
					case type_void:
					case type_resource:
					case type_entity:
						grug_unreachable();
					case type_bool:
					case type_i32:
					case type_f32:
						compile_unpadded(DEREF_RBP_TO_EAX);
						compile_byte(-var->offset);
						break;
					case type_string:
						compile_unpadded(DEREF_RBP_TO_RAX);
						compile_byte(-var->offset);
						break;
				}
				return;
			}

			compile_unpadded(DEREF_RBP_TO_RAX);
			compile_byte(-(u8)(GLOBAL_OFFSET_TABLE_POINTER_SIZE + GLOBAL_VARIABLES_POINTER_SIZE));

			// TODO: Support any 32 bit offset, instead of only 8 bits
			var = get_global_variable(expr.literal.string);
			switch (var->type) {
				case type_void:
				case type_resource:
				case type_entity:
					grug_unreachable();
				case type_bool:
				case type_i32:
				case type_f32:
					compile_unpadded(DEREF_RAX_TO_EAX);
					compile_byte(var->offset);
					break;
				case type_string:
					compile_unpadded(DEREF_RAX_TO_RAX);
					compile_byte(var->offset);
					break;
			}

			break;
		}
		case I32_EXPR: {
			i32 n = expr.literal.i32;
			if (n == 0) {
				compile_unpadded(XOR_CLEAR_EAX);
			} else if (n == 1) {
				compile_byte(MOV_TO_EAX);
				compile_32(1);
			} else {
				compile_unpadded(MOV_TO_EAX);
				compile_32(n);
			}
			break;
		}
		case F32_EXPR:
			compile_unpadded(MOV_TO_EAX);
			unsigned char *bytes = (unsigned char *)&expr.literal.f32;
			for (size_t i = 0; i < sizeof(float); i++) {
				compile_byte(*bytes); // Little-endian
				bytes++;
			}
			break;
		case UNARY_EXPR:
			compile_unary_expr(expr.unary);
			break;
		case BINARY_EXPR:
			compile_binary_expr(expr);
			break;
		case LOGICAL_EXPR:
			compile_logical_expr(expr.binary);
			break;
		case CALL_EXPR:
			compile_call_expr(expr.call);
			break;
		case PARENTHESIZED_EXPR:
			compile_expr(*expr.parenthesized);
			break;
	}
}

static void compile_variable_statement(struct variable_statement variable_statement) {
	compile_expr(*variable_statement.assignment_expr);

	struct variable *var = get_local_variable(variable_statement.name);
	if (var) {
		// TODO: Support any 32 bit offset, instead of only 8 bits
		switch (var->type) {
			case type_void:
			case type_resource:
			case type_entity:
				grug_unreachable();
			case type_bool:
			case type_i32:
			case type_f32:
				compile_unpadded(MOV_EAX_TO_DEREF_RBP);
				compile_byte(-var->offset);
				break;
			case type_string:
				compile_unpadded(MOV_RAX_TO_DEREF_RBP);
				compile_byte(-var->offset);
				break;
		}
		return;
	}

	compile_unpadded(DEREF_RBP_TO_R11);
	compile_byte(-(u8)(GLOBAL_OFFSET_TABLE_POINTER_SIZE + GLOBAL_VARIABLES_POINTER_SIZE));

	// TODO: Support any 32 bit offset, instead of only 8 bits
	var = get_global_variable(variable_statement.name);
	switch (var->type) {
		case type_void:
		case type_resource:
		case type_entity:
			grug_unreachable();
		case type_bool:
		case type_i32:
		case type_f32:
			compile_unpadded(MOV_EAX_TO_DEREF_R11);
			compile_byte(var->offset);
			break;
		case type_string:
			compile_unpadded(MOV_RAX_TO_DEREF_R11);
			compile_byte(var->offset);
			break;
	}
}

static void compile_statements(struct statement *statements_offset, size_t statement_count) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		struct statement statement = statements_offset[statement_index];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				compile_variable_statement(statement.variable_statement);
				break;
			case CALL_STATEMENT:
				compile_call_expr(statement.call_statement.expr->call);
				break;
			case IF_STATEMENT:
				compile_if_statement(statement.if_statement);
				break;
			case RETURN_STATEMENT:
				if (statement.return_statement.has_value) {
					compile_expr(*statement.return_statement.value);
				}

				if (in_on_fn) {
					compile_byte(PUSH_RAX);

					compile_byte(CALL);
					push_system_fn_call("grug_disable_on_fn_runtime_error_handling", codes_size);
					compile_unpadded(PLACEHOLDER_32);

					compile_byte(POP_RAX);
				}

				compile_unpadded(DEREF_RBP_TO_RBX);
				compile_byte(-(u8)GLOBAL_OFFSET_TABLE_POINTER_SIZE);

				// Function epilogue
				compile_unpadded(MOV_RBP_TO_RSP);
				compile_byte(POP_RBP);

				compile_byte(RET);

				break;
			case WHILE_STATEMENT:
				compile_while_statement(statement.while_statement);
				break;
			case BREAK_STATEMENT:
				compile_unpadded(JMP_32_BIT_OFFSET);
				push_break_statement_jump_address_offset(codes_size);
				compile_unpadded(PLACEHOLDER_32);
				break;
			case CONTINUE_STATEMENT:
				compile_unpadded(JMP_32_BIT_OFFSET);
				size_t start_of_loop_jump_offset = start_of_loop_jump_offsets[start_of_loop_jump_offsets_size - 1];
				compile_32(start_of_loop_jump_offset - (codes_size + NEXT_INSTRUCTION_OFFSET));
				break;
		}
	}
}

static void add_variables_in_statements(struct statement *statements_offset, size_t statement_count) {
	for (size_t statement_index = 0; statement_index < statement_count; statement_index++) {
		struct statement statement = statements_offset[statement_index];

		switch (statement.type) {
			case VARIABLE_STATEMENT:
				if (statement.variable_statement.has_type) {
					add_local_variable(statement.variable_statement.name, statement.variable_statement.type);
				}
				break;
			case CALL_STATEMENT:
				break;
			case IF_STATEMENT:
				add_variables_in_statements(statement.if_statement.if_body_statements, statement.if_statement.if_body_statement_count);

				if (statement.if_statement.else_body_statement_count > 0) {
					add_variables_in_statements(statement.if_statement.else_body_statements, statement.if_statement.else_body_statement_count);
				}

				break;
			case RETURN_STATEMENT:
				break;
			case WHILE_STATEMENT:
				add_variables_in_statements(statement.while_statement.body_statements, statement.while_statement.body_statement_count);
				break;
			case BREAK_STATEMENT:
				break;
			case CONTINUE_STATEMENT:
				break;
		}
	}
}

static void push_got_access(size_t codes_offset) {
	grug_assert(got_accesses_size < MAX_GOT_ACCESSES, "There are more than %d global offset table accesses, exceeding MAX_GOT_ACCESSES", MAX_GOT_ACCESSES);

	got_accesses[got_accesses_size++] = codes_offset;
}

// From https://stackoverflow.com/a/9194117/13279557
static size_t round_to_power_of_2(size_t n, size_t multiple) {
	// Assert that `multiple` is a power of 2
	assert(multiple && ((multiple & (multiple - 1)) == 0));

	return (n + multiple - 1) & -multiple;
}

static void compile_on_or_helper_fn(char *fn_name, struct argument *fn_arguments, size_t argument_count, struct statement *body_statements, size_t body_statement_count, bool is_on_fn, char *grug_path) {
	init_argument_variables(fn_arguments, argument_count);

	add_variables_in_statements(body_statements, body_statement_count);

	// Function prologue
	compile_byte(PUSH_RBP);

	// Deliberately leaving this out, so we don't have to worry about adding 8
	// after turning stack_frame_bytes into a multiple of 16
	// stack_frame_bytes += sizeof(u64);

	compile_unpadded(MOV_RSP_TO_RBP);

	// Make space in the stack for the arguments and variables
	// The System V ABI requires 16-byte stack alignment: https://stackoverflow.com/q/49391001/13279557
	stack_frame_bytes = round_to_power_of_2(stack_frame_bytes, 0x10);
	if (stack_frame_bytes < 0xff) {
		compile_unpadded(SUB_RSP_8_BITS);
		compile_byte(stack_frame_bytes);
	} else {
		compile_unpadded(SUB_RSP_32_BITS);
		compile_32(stack_frame_bytes);
	}

	compile_unpadded(MOV_RBX_TO_DEREF_RBP);
	compile_byte(-(u8)GLOBAL_OFFSET_TABLE_POINTER_SIZE);

	// We need to push the secret global variables pointer to the function call's stack frame,
	// because the RDI register will get clobbered when this function calls another function:
	// https://stackoverflow.com/a/55387707/13279557
	compile_unpadded(MOV_RDI_TO_DEREF_RBP);
	compile_byte(-(u8)(GLOBAL_OFFSET_TABLE_POINTER_SIZE + GLOBAL_VARIABLES_POINTER_SIZE));

	size_t integer_argument_index = 0;
	size_t float_argument_index = 0;

	// TODO: Add err and ok test for max i32 and f32 arguments

	// Move the rest of the arguments
	for (size_t argument_index = 0; argument_index < argument_count; argument_index++) {
		struct argument arg = fn_arguments[argument_index];

		// We skip EDI/RDI, since that is reserved by the secret global variables pointer
		switch (arg.type) {
			case type_void:
			case type_resource:
			case type_entity:
				grug_unreachable();
			case type_bool:
			case type_i32:
				compile_unpadded((u32[]){
					MOV_ESI_TO_DEREF_RBP,
					MOV_EDX_TO_DEREF_RBP,
					MOV_ECX_TO_DEREF_RBP,
					MOV_R8D_TO_DEREF_RBP,
					MOV_R9D_TO_DEREF_RBP,
				}[integer_argument_index]);
				break;
			case type_f32:
				compile_unpadded((u32[]){
					MOV_XMM0_TO_DEREF_RBP,
					MOV_XMM1_TO_DEREF_RBP,
					MOV_XMM2_TO_DEREF_RBP,
					MOV_XMM3_TO_DEREF_RBP,
					MOV_XMM4_TO_DEREF_RBP,
					MOV_XMM5_TO_DEREF_RBP,
					MOV_XMM6_TO_DEREF_RBP,
					MOV_XMM7_TO_DEREF_RBP,
				}[float_argument_index]);
				break;
			case type_string:
				compile_unpadded((u32[]){
					MOV_RSI_TO_DEREF_RBP,
					MOV_RDX_TO_DEREF_RBP,
					MOV_RCX_TO_DEREF_RBP,
					MOV_R8_TO_DEREF_RBP,
					MOV_R9_TO_DEREF_RBP,
				}[integer_argument_index]);
				break;
		}

		size_t offset = get_local_variable(arg.name)->offset;
		// TODO: Support offset >= 256 bytes, and add a test for it
		grug_assert(offset < 256, "Currently grug doesn't allow function arguments to use more than 256 bytes in the function's stack frame, so use fewer arguments for the time being");
		compile_byte(-offset);

		if (arg.type == type_f32) {
			float_argument_index++;
		} else {
			integer_argument_index++;
		}
	}

	// Let RBX contain the address of the global offset table
	// lea rbx, [rel $$]:
	compile_unpadded(LEA_RIP_TO_RBX);
	compile_32(-(codes_size + NEXT_INSTRUCTION_OFFSET));
	// add rbx, _GLOBAL_OFFSET_TABLE_ wrt ..gotpc:
	compile_unpadded(ADD_TO_RBX);
	push_got_access(codes_size);
	compile_unpadded(PLACEHOLDER_32);

	if (is_on_fn) {
		// lea rax, strings[rel n]:
		add_data_string(grug_path);
		compile_unpadded(LEA_STRINGS_TO_RAX);
		push_data_string_code(grug_path, codes_size);
		compile_unpadded(PLACEHOLDER_32);
		// mov r11, rbx[grug_on_fn_path wrt ..got]:
		compile_unpadded(DEREF_RBX_TO_R11);
		compile_32(-(u8)GRUG_ON_FN_PATH_OFFSET);
		// mov [r11], rax:
		compile_unpadded(MOV_RAX_TO_DEREF_R11_OFFSETLESS);

		// lea rax, strings[rel n]:
		add_data_string(fn_name);
		compile_unpadded(LEA_STRINGS_TO_RAX);
		push_data_string_code(fn_name, codes_size);
		compile_unpadded(PLACEHOLDER_32);
		// mov r11, rbx[grug_on_fn_name wrt ..got]:
		compile_unpadded(DEREF_RBX_TO_R11);
		compile_32(-(u8)GRUG_ON_FN_NAME_OFFSET);
		// mov [r11], rax:
		compile_unpadded(MOV_RAX_TO_DEREF_R11_OFFSETLESS);

		compile_byte(CALL);
		push_system_fn_call("grug_enable_on_fn_runtime_error_handling", codes_size);
		compile_unpadded(PLACEHOLDER_32);

		in_on_fn = true;
	}

	compile_statements(body_statements, body_statement_count);

	if (is_on_fn) {
		in_on_fn = false;

		compile_byte(CALL);
		push_system_fn_call("grug_disable_on_fn_runtime_error_handling", codes_size);
		compile_unpadded(PLACEHOLDER_32);
	}

	compile_unpadded(DEREF_RBP_TO_RBX);
	compile_byte(-(u8)GLOBAL_OFFSET_TABLE_POINTER_SIZE);

	// Function epilogue
	compile_unpadded(MOV_RBP_TO_RSP);
	compile_byte(POP_RBP);

	compile_byte(RET);
}

static void compile_on_fn(struct on_fn fn, char *grug_path) {
	compile_on_or_helper_fn(fn.fn_name, fn.arguments, fn.argument_count, fn.body_statements, fn.body_statement_count, true, grug_path);
}

static void compile_helper_fn(struct helper_fn fn) {
	compile_on_or_helper_fn(fn.fn_name, fn.arguments, fn.argument_count, fn.body_statements, fn.body_statement_count, false, NULL);
}

static void compile_init_globals_fn(void) {
	size_t ptr_offset = 0;

	for (size_t global_variable_index = 0; global_variable_index < global_variable_statements_size; global_variable_index++) {
		struct global_variable_statement global_variable = global_variable_statements[global_variable_index];

		compile_expr(global_variable.assignment_expr);

		if (ptr_offset < 0x80) { // an i8 with the value 0x80 is negative in two's complement
			if (global_variable.assignment_expr.result_type == type_string) {
				compile_unpadded(MOV_RAX_TO_DEREF_RDI_8_BIT_OFFSET);
			} else {
				compile_unpadded(MOV_EAX_TO_DEREF_RDI_8_BIT_OFFSET);
			}
			compile_byte(ptr_offset);
		} else {
			if (global_variable.assignment_expr.result_type == type_string) {
				compile_unpadded(MOV_RAX_TO_DEREF_RDI_32_BIT_OFFSET);
			} else {
				compile_unpadded(MOV_EAX_TO_DEREF_RDI_32_BIT_OFFSET);
			}
			compile_32(ptr_offset);
		}

		if (global_variable.type == type_string) {
			ptr_offset += sizeof(char *);
		} else {
			ptr_offset += sizeof(u32);
		}
	}

	compile_byte(RET);
}

static void compile_define_fn_returned_fields(void) {
	size_t field_count = define_fn.returned_compound_literal.field_count;

	// `integer` here refers to the classification type:
	// "integer types and pointers which use the general purpose registers"
	// See https://stackoverflow.com/a/57861992/13279557
	size_t integer_field_count = 0;

	size_t float_field_count = 0;

	for (size_t i = 0; i < field_count; i++) {
		struct expr field = define_fn.returned_compound_literal.fields[i].expr_value;

		if (field.result_type == type_f32) {
			float_field_count++;
		} else {
			integer_field_count++;
		}
	}

	// TODO: Add tests for these
	grug_assert(integer_field_count <= 6, "Currently grug only supports returning up to six bool/i32/string fields from the define function");
	grug_assert(float_field_count <= 8, "Currently grug only supports returning up to eight f32 fields from the define function");

	// TODO: Does padding have to be added before and subtracted after the call?

	for (size_t i = field_count; i > 0;) {
		struct expr field = define_fn.returned_compound_literal.fields[--i].expr_value;

		compile_expr(field);

		if (field.result_type == type_f32) {
			static u32 movs[] = {
				MOV_EAX_TO_XMM0,
				MOV_EAX_TO_XMM1,
				MOV_EAX_TO_XMM2,
				MOV_EAX_TO_XMM3,
				MOV_EAX_TO_XMM4,
				MOV_EAX_TO_XMM5,
				MOV_EAX_TO_XMM6,
				MOV_EAX_TO_XMM7,
			};

			compile_unpadded(movs[--float_field_count]);
		} else {
			static u32 movs[] = {
				MOV_RAX_TO_RDI,
				MOV_RAX_TO_RSI,
				MOV_RAX_TO_RDX,
				MOV_RAX_TO_RCX,
				MOV_RAX_TO_R8,
				MOV_RAX_TO_R9,
			};

			compile_unpadded(movs[--integer_field_count]);
		}
	}
}

static void compile_define_fn(void) {
	size_t field_count = define_fn.returned_compound_literal.field_count;

	for (size_t field_index = 0; field_index < field_count; field_index++) {
		struct field field = define_fn.returned_compound_literal.fields[field_index];

		grug_assert(streq(field.key, grug_define_entity->fields[field_index].name), "Field %zu named '%s' that you're returning from your define function must be renamed to '%s', according to the entity '%s' in mod_api.json", field_index + 1, field.key, grug_define_entity->fields[field_index].name, grug_define_entity->name);

		// TODO: Verify that the argument has the same type as the one in grug_define_entity
	}

	compile_define_fn_returned_fields();
	compile_byte(CALL);
	push_game_fn_call(define_fn_name, codes_size);
	compile_unpadded(PLACEHOLDER_32);
	compile_byte(RET);
}

static void init_define_fn_name(char *name) {
	grug_assert(temp_strings_size + sizeof("define_") - 1 + strlen(name) < MAX_TEMP_STRINGS_CHARACTERS, "There are more than %d characters in the strings array, exceeding MAX_TEMP_STRINGS_CHARACTERS", MAX_TEMP_STRINGS_CHARACTERS);

	define_fn_name = temp_strings + temp_strings_size;

	memcpy(define_fn_name, "define_", sizeof("define_") - 1);
	temp_strings_size += sizeof("define_") - 1;

	size_t name_length = strlen(name);

	for (size_t i = 0; i < name_length; i++) {
		temp_strings[temp_strings_size++] = name[i];
	}
	temp_strings[temp_strings_size++] = '\0';
}

static void compile(char *grug_path) {
	reset_compiling();

	init_define_fn_name(grug_define_entity->name);

	size_t text_offset_index = 0;
	size_t text_offset = 0;

	compile_define_fn();
	text_offsets[text_offset_index++] = text_offset;
	text_offset = codes_size;

	size_t start_codes_size = codes_size;
	compile_init_globals_fn();
	text_offsets[text_offset_index++] = text_offset;
	text_offset += codes_size - start_codes_size;

	for (size_t on_fn_index = 0; on_fn_index < on_fns_size; on_fn_index++) {
		start_codes_size = codes_size;

		struct on_fn fn = on_fns[on_fn_index];

		compile_on_fn(fn, grug_path);

		text_offsets[text_offset_index++] = text_offset;
		text_offset += codes_size - start_codes_size;
	}

	for (size_t helper_fn_index = 0; helper_fn_index < helper_fns_size; helper_fn_index++) {
		start_codes_size = codes_size;

		struct helper_fn fn = helper_fns[helper_fn_index];

		push_helper_fn_offset(fn.fn_name, codes_size);

		compile_helper_fn(fn);

		text_offsets[text_offset_index++] = text_offset;
		text_offset += codes_size - start_codes_size;
	}

	hash_used_extern_fns();
	hash_helper_fn_offsets();
}

//// LINKING

#define MAX_BYTES 420420
#define MAX_GAME_FN_OFFSETS 420420
#define MAX_HASH_BUCKETS 32771 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elflink.c;h=6db6a9c0b4702c66d73edba87294e2a59ffafcf5;hb=refs/heads/master#l6560

// The first three addresses pushed by push_got_plt() are special:
// A recent update of the "ld" linker causes the first three .got.plt addresses to always be placed
// 0x18 bytes before the start of a new page, so at 0x2fe8/0x3fe8, etc.
// The grug tester compares the grug output against ld, so that's why we mimic ld here
#define GOT_PLT_INTRO_SIZE 0x18

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

static size_t shindex_hash;
static size_t shindex_dynsym;
static size_t shindex_dynstr;
static size_t shindex_rela_dyn;
static size_t shindex_rela_plt;
static size_t shindex_plt;
static size_t shindex_text;
static size_t shindex_eh_frame;
static size_t shindex_dynamic;
static size_t shindex_got;
static size_t shindex_got_plt;
static size_t shindex_data;
static size_t shindex_symtab;
static size_t shindex_strtab;
static size_t shindex_shstrtab;

static char *symbols[MAX_SYMBOLS];
static size_t symbols_size;

static size_t on_fns_symbol_offset;

static size_t data_symbols_size;
static size_t extern_data_symbols_size;

static size_t symbol_name_dynstr_offsets[MAX_SYMBOLS];
static size_t symbol_name_strtab_offsets[MAX_SYMBOLS];

static u32 buckets_on_fns[MAX_ON_FNS_IN_FILE];
static u32 chains_on_fns[MAX_ON_FNS_IN_FILE];

static char *shuffled_symbols[MAX_SYMBOLS];
static size_t shuffled_symbols_size;

static size_t shuffled_symbol_index_to_symbol_index[MAX_SYMBOLS];
static size_t symbol_index_to_shuffled_symbol_index[MAX_SYMBOLS];

static size_t first_extern_data_symbol_index;
static size_t first_used_extern_fn_symbol_index;

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
static size_t eh_frame_offset;
static size_t dynamic_offset;
static size_t dynamic_size;
static size_t got_offset;
static size_t got_size;
static size_t got_plt_offset;
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
static size_t got_shstrtab_offset;
static size_t got_plt_shstrtab_offset;
static size_t data_shstrtab_offset;
static size_t symtab_shstrtab_offset;
static size_t strtab_shstrtab_offset;
static size_t shstrtab_shstrtab_offset;

static struct fn_offset game_fn_offsets[MAX_GAME_FN_OFFSETS];
static size_t game_fn_offsets_size;
static u32 buckets_game_fn_offsets[MAX_GAME_FN_OFFSETS];
static u32 chains_game_fn_offsets[MAX_GAME_FN_OFFSETS];

static size_t strings_offset;
static size_t resources_offset;
static size_t entities_offset;
static size_t entity_types_offset;

static void reset_generate_shared_object(void) {
	symbols_size = 0;
	data_symbols_size = 0;
	extern_data_symbols_size = 0;
	shuffled_symbols_size = 0;
	bytes_size = 0;
	game_fn_offsets_size = 0;
}

static void overwrite(u64 n, size_t bytes_offset, size_t overwrite_count) {
	for (size_t i = 0; i < overwrite_count; i++) {
		bytes[bytes_offset++] = n & 0xff; // Little-endian
		n >>= 8;
	}
}

static void overwrite_16(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, sizeof(u16));
}

static void overwrite_32(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, sizeof(u32));
}

static void overwrite_64(u64 n, size_t bytes_offset) {
	overwrite(n, bytes_offset, sizeof(u64));
}

static struct on_fn *get_on_fn(char *name) {
	if (on_fns_size == 0) {
		return NULL;
	}

	u32 i = buckets_on_fns[elf_hash(name) % on_fns_size];

	while (true) {
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
	memset(buckets_on_fns, 0xff, on_fns_size * sizeof(u32));

	for (size_t i = 0; i < on_fns_size; i++) {
		char *name = on_fns[i].fn_name;

		grug_assert(!get_on_fn(name), "The function '%s' was defined several times in the same file", name);

		u32 bucket_index = elf_hash(name) % on_fns_size;

		chains_on_fns[i] = buckets_on_fns[bucket_index];

		buckets_on_fns[bucket_index] = i;
	}
}

static void patch_plt(void) {
	size_t overwritten_address = plt_offset;

	size_t address_size = sizeof(u32);

	overwritten_address += sizeof(u16);
	overwrite_32(got_plt_offset - overwritten_address - address_size + 0x8, overwritten_address);

	overwritten_address += address_size + sizeof(u16);
	overwrite_32(got_plt_offset - overwritten_address - address_size + 0x10, overwritten_address);

	size_t got_plt_fn_address = got_plt_offset + GOT_PLT_INTRO_SIZE;

	overwritten_address += 2 * sizeof(u32) + sizeof(u16);

	for (size_t i = 0; i < BFD_HASH_BUCKET_SIZE; i++) {
		u32 chain_index = buckets_used_extern_fns[i];
		if (chain_index == UINT32_MAX) {
			continue;
		}

		while (true) {
			overwrite_32(got_plt_fn_address - overwritten_address - NEXT_INSTRUCTION_OFFSET, overwritten_address);

			got_plt_fn_address += sizeof(u64);

			overwritten_address += sizeof(u32) + sizeof(u8) + sizeof(u32) + sizeof(u8) + sizeof(u32) + sizeof(u16);

			chain_index = chains_used_extern_fns[chain_index];
			if (chain_index == UINT32_MAX) {
				break;
			}
		}
	}
}

static void patch_rela_plt(void) {
	size_t value_offset = got_plt_offset + GOT_PLT_INTRO_SIZE;

	size_t address_offset = rela_plt_offset;

	for (size_t shuffled_symbol_index = 0; shuffled_symbol_index < symbols_size; shuffled_symbol_index++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[shuffled_symbol_index];

		if (symbol_index < first_used_extern_fn_symbol_index || symbol_index >= first_used_extern_fn_symbol_index + extern_fns_size) {
			continue;
		}

		overwrite_64(value_offset, address_offset);
		value_offset += sizeof(u64);
		size_t entry_size = 3;
		address_offset += entry_size * sizeof(u64);
	}
}

static void patch_rela_dyn(void) {
	size_t return_type_data_size = strlen(define_fn.return_type) + 1;
	size_t globals_size_data_size = sizeof(u64);
	size_t on_fn_data_offset = return_type_data_size + globals_size_data_size;

	size_t excess = on_fn_data_offset % sizeof(u64); // Alignment
	if (excess > 0) {
		on_fn_data_offset += sizeof(u64) - excess;
	}

	size_t bytes_offset = rela_dyn_offset;
	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(grug_define_entity->on_functions[i].name);
		if (on_fn) {
			size_t on_fn_index = on_fn - on_fns;

			overwrite_64(got_plt_offset + got_plt_size + on_fn_data_offset, bytes_offset);
			bytes_offset += 2 * sizeof(u64);

			size_t fns_before_on_fns = 2; // define() and init_globals()
			overwrite_64(text_offset + text_offsets[on_fn_index + fns_before_on_fns], bytes_offset);
			bytes_offset += sizeof(u64);
		}
		on_fn_data_offset += sizeof(size_t);
	}

	for (size_t i = 0; i < resources_size; i++) {
		overwrite_64(resources_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(data_offset + data_string_offsets[resources[i]], bytes_offset);
		bytes_offset += sizeof(u64);
	}

	for (size_t i = 0; i < entity_dependencies_size; i++) {
		overwrite_64(entities_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(data_offset + data_string_offsets[entity_dependencies[i]], bytes_offset);
		bytes_offset += sizeof(u64);
	}

	for (size_t i = 0; i < entity_dependencies_size; i++) {
		overwrite_64(entity_types_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(data_offset + data_string_offsets[entity_types[i]], bytes_offset);
		bytes_offset += sizeof(u64);
	}

	for (size_t i = 0; i < extern_data_symbols_size; i++) {
		overwrite_64(got_offset + i * sizeof(u64), bytes_offset);
		bytes_offset += 2 * sizeof(u64);
		overwrite_64(0, bytes_offset);
		bytes_offset += sizeof(u64);
	}
}

static u32 get_symbol_offset(size_t symbol_index) {
	bool is_data = symbol_index < data_symbols_size;
	if (is_data) {
		return data_offset + data_offsets[symbol_index];
	}

	bool is_extern_data = symbol_index < first_extern_data_symbol_index + extern_data_symbols_size;
	if (is_extern_data) {
		return 0;
	}

	bool is_extern = symbol_index < first_used_extern_fn_symbol_index + extern_fns_size;
	if (is_extern) {
		return 0;
	}

	return text_offset + text_offsets[symbol_index - data_symbols_size - extern_data_symbols_size - extern_fns_size];
}

static u16 get_symbol_shndx(size_t symbol_index) {
	bool is_data = symbol_index < data_symbols_size;
	if (is_data) {
		return shindex_data;
	}

	bool is_extern_data = symbol_index < first_extern_data_symbol_index + extern_data_symbols_size;
	if (is_extern_data) {
		return SHN_UNDEF;
	}

	bool is_extern = symbol_index < first_used_extern_fn_symbol_index + extern_fns_size;
	if (is_extern) {
		return SHN_UNDEF;
	}

	return shindex_text;
}

static void patch_dynsym(void) {
	// The symbols are pushed in shuffled_symbols order
	size_t bytes_offset = dynsym_placeholders_offset;
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		overwrite_32(symbol_name_dynstr_offsets[symbol_index], bytes_offset);
		bytes_offset += sizeof(u32);
		overwrite_16(ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), bytes_offset);
		bytes_offset += sizeof(u16);
		overwrite_16(get_symbol_shndx(symbol_index), bytes_offset);
		bytes_offset += sizeof(u16);
		overwrite_32(get_symbol_offset(symbol_index), bytes_offset);
		bytes_offset += sizeof(u32);

		bytes_offset += SYMTAB_ENTRY_SIZE - sizeof(u32) - sizeof(u16) - sizeof(u16) - sizeof(u32);
	}
}

static size_t get_game_fn_offset(char *name) {
	u32 i = buckets_game_fn_offsets[elf_hash(name) % game_fn_offsets_size];

	while (true) {
		assert(i != UINT32_MAX && "get_game_fn_offset() is supposed to never fail");

		if (streq(name, game_fn_offsets[i].fn_name)) {
			break;
		}

		i = chains_game_fn_offsets[i];
	}

	return game_fn_offsets[i].offset;
}

static void hash_game_fn_offsets(void) {
	memset(buckets_game_fn_offsets, 0xff, game_fn_offsets_size * sizeof(u32));

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

static void patch_dynamic(void) {
	overwrite_64(got_plt_offset, dynamic_offset + 0x58);
}

static void patch_text(void) {
	for (size_t i = 0; i < got_accesses_size; i++) {
		overwrite_32(got_plt_offset - text_offset, text_offset + got_accesses[i]);
	}

	for (size_t i = 0; i < extern_fn_calls_size; i++) {
		struct fn_call fn_call = extern_fn_calls[i];
		size_t offset = text_offset + fn_call.codes_offset;
		size_t address_after_call_instruction = offset + NEXT_INSTRUCTION_OFFSET;
		size_t game_fn_plt_offset = plt_offset + get_game_fn_offset(fn_call.fn_name);
		overwrite_32(game_fn_plt_offset - address_after_call_instruction, offset);
	}

	for (size_t i = 0; i < helper_fn_calls_size; i++) {
		struct fn_call fn_call = helper_fn_calls[i];
		size_t offset = text_offset + fn_call.codes_offset;
		size_t address_after_call_instruction = offset + NEXT_INSTRUCTION_OFFSET;
		size_t helper_fn_text_offset = text_offset + get_helper_fn_offset(fn_call.fn_name);
		overwrite_32(helper_fn_text_offset - address_after_call_instruction, offset);
	}

	for (size_t i = 0; i < data_string_codes_size; i++) {
		struct data_string_code dsc = data_string_codes[i];
		char *string = dsc.string;
		size_t code_offset = dsc.code_offset;

		size_t string_index = get_data_string_index(string);
		assert(string_index != UINT32_MAX);

		size_t string_address = data_offset + data_string_offsets[string_index];

		size_t next_instruction_address = text_offset + code_offset + NEXT_INSTRUCTION_OFFSET;

		// RIP-relative address of data string
		size_t string_offset = string_address - next_instruction_address;

		overwrite_32(string_offset, text_offset + code_offset);
	}
}

static bool has_got(void) {
	return on_fns_size > 0;
}

static void patch_program_headers(void) {
	// .hash, .dynsym, .dynstr, .rela.dyn, .rela.plt segment
	overwrite_64(segment_0_size, 0x60); // file_size
	overwrite_64(segment_0_size, 0x68); // mem_size

	// .plt, .text segment
	overwrite_64(plt_offset, 0x80); // offset
	overwrite_64(plt_offset, 0x88); // virtual_address
	overwrite_64(plt_offset, 0x90); // physical_address
	overwrite_64(plt_size + text_size, 0x98); // file_size
	overwrite_64(plt_size + text_size, 0xa0); // mem_size

	// .eh_frame segment
	overwrite_64(eh_frame_offset, 0xb8); // offset
	overwrite_64(eh_frame_offset, 0xc0); // virtual_address
	overwrite_64(eh_frame_offset, 0xc8); // physical_address

	// .dynamic, .got, .got.plt, .data segment
	overwrite_64(dynamic_offset, 0xf0); // offset
	overwrite_64(dynamic_offset, 0xf8); // virtual_address
	overwrite_64(dynamic_offset, 0x100); // physical_address
	size_t size = dynamic_size + got_plt_size + data_size;
	if (has_got()) {
		size += got_size;
	}
	overwrite_64(size, 0x108); // file_size
	overwrite_64(size, 0x110); // mem_size

	// .dynamic segment
	overwrite_64(dynamic_offset, 0x128); // offset
	overwrite_64(dynamic_offset, 0x130); // virtual_address
	overwrite_64(dynamic_offset, 0x138); // physical_address
	overwrite_64(dynamic_size, 0x140); // file_size
	overwrite_64(dynamic_size, 0x148); // mem_size

	// .dynamic, .got segment
	overwrite_64(dynamic_offset, 0x160); // offset
	overwrite_64(dynamic_offset, 0x168); // virtual_address
	overwrite_64(dynamic_offset, 0x170); // physical_address
	size_t segment_5_size = dynamic_size;
	if (has_got()) {
		segment_5_size += got_size;
	}
#ifndef OLD_LD
	segment_5_size += GOT_PLT_INTRO_SIZE;
#endif
	overwrite_64(segment_5_size, 0x178); // file_size
	overwrite_64(segment_5_size, 0x180); // mem_size
}

static bool has_rela_dyn(void) {
	return on_fns_size > 0 || resources_size > 0 || entity_dependencies_size > 0;
}

static void patch_bytes(void) {
	// ELF section header table offset
	overwrite_64(section_headers_offset, 0x28);

	patch_program_headers();

	patch_dynsym();
	if (has_rela_dyn()) {
		patch_rela_dyn();
	}
	patch_rela_plt();
	patch_plt();
	patch_text();
	patch_dynamic();
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
			// nasm aligns using the NOP instruction:
			// https://stackoverflow.com/a/18414187/13279557
			push_byte(NOP_8_BITS);
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

	if (has_rela_dyn()) {
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

	if (has_got()) {
		got_shstrtab_offset = offset;
		push_string_bytes(".got");
		offset += sizeof(".got");
	}

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

	push_string_bytes("_DYNAMIC");
	push_string_bytes("_GLOBAL_OFFSET_TABLE_");

	for (size_t i = 0; i < symbols_size; i++) {
		push_string_bytes(shuffled_symbols[i]);
	}

	strtab_size = bytes_size - strtab_offset;
}

static void push_number(u64 n, size_t byte_count) {
	for (; byte_count-- > 0; n >>= 8) {
		push_byte(n & 0xff); // Little-endian
	}
}

static void push_16(u16 n) {
	push_number(n, sizeof(u16));
}

static void push_32(u32 n) {
	push_number(n, sizeof(u32));
}

static void push_64(u64 n) {
	push_number(n, sizeof(u64));
}

// See https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-79797/index.html
// See https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblj/index.html#chapter6-tbl-21
static void push_symbol_entry(u32 name, u16 info, u16 shndx, u32 offset) {
	push_32(name); // Indexed into .strtab for .symtab, because .symtab its "link" points to it; .dynstr for .dynstr
	push_16(info);
	push_16(shndx);
	push_32(offset); // In executable and shared object files, st_value holds a virtual address

	push_zeros(SYMTAB_ENTRY_SIZE - sizeof(u32) - sizeof(u16) - sizeof(u16) - sizeof(u32));
}

static void push_symtab(char *grug_path) {
	grug_log_section(".symtab");

	symtab_offset = bytes_size;

	size_t pushed_symbol_entries = 0;

	// Null entry
	push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), SHN_UNDEF, 0);
	pushed_symbol_entries++;

	// "<some_path>.s" entry
	push_symbol_entry(1, ELF32_ST_INFO(STB_LOCAL, STT_FILE), SHN_ABS, 0);
	pushed_symbol_entries++;

	// TODO: ? entry
	push_symbol_entry(0, ELF32_ST_INFO(STB_LOCAL, STT_FILE), SHN_ABS, 0);
	pushed_symbol_entries++;

	// TODO: Let this use path of the .grug file, instead of the .s that's used purely for testing purposes
	// The `1 +` is to skip the 0 byte that .strtab always starts with
	size_t name_offset = 1 + strlen(grug_path) + 1;

	// "_DYNAMIC" entry
	push_symbol_entry(name_offset, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), shindex_dynamic, dynamic_offset);
	pushed_symbol_entries++;
	name_offset += sizeof("_DYNAMIC");

	// "_GLOBAL_OFFSET_TABLE_" entry
	push_symbol_entry(name_offset, ELF32_ST_INFO(STB_LOCAL, STT_OBJECT), shindex_got_plt, got_plt_offset);
	pushed_symbol_entries++;
	name_offset += sizeof("_GLOBAL_OFFSET_TABLE_");

	symtab_index_first_global = pushed_symbol_entries;

	// The symbols are pushed in shuffled_symbols order
	for (size_t i = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];

		push_symbol_entry(name_offset + symbol_name_strtab_offsets[symbol_index], ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), get_symbol_shndx(symbol_index), get_symbol_offset(symbol_index));
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
	push_64(globals_bytes);

	// "on_fns" function addresses
	size_t previous_on_fn_index = 0;
	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(grug_define_entity->on_functions[i].name);
		if (on_fn) {
			size_t on_fn_index = on_fn - on_fns;
			grug_assert(previous_on_fn_index <= on_fn_index, "The function '%s' was in the wrong order, according to the entity '%s' in mod_api.json", on_fn->fn_name, grug_define_entity->name);
			previous_on_fn_index = on_fn_index;

			size_t fns_before_on_fns = 2; // define() and init_globals()
			push_64(text_offset + text_offsets[on_fn_index + fns_before_on_fns]);
		} else {
			push_64(0x0);
		}
	}

	// "strings" symbol
	strings_offset = bytes_size;
	for (size_t i = 0; i < data_strings_size; i++) {
		char *string = data_strings[i];
		push_string_bytes(string);
	}

	// "resources_size" symbol
	push_nasm_alignment(8);
	push_64(resources_size);

	// "resources" symbol
	resources_offset = bytes_size;
	for (size_t i = 0; i < resources_size; i++) {
		push_64(data_offset + data_string_offsets[resources[i]]);
	}

	// "entities_size" symbol
	push_64(entity_dependencies_size);

	// "entities" symbol
	entities_offset = bytes_size;
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_64(data_offset + data_string_offsets[entity_dependencies[i]]);
	}

	// "entity_types" symbol
	entity_types_offset = bytes_size;
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_64(data_offset + data_string_offsets[entity_types[i]]);
	}

	push_alignment(8);
}

static void push_got_plt(void) {
	grug_log_section(".got.plt");

	got_plt_offset = bytes_size;

	push_64(dynamic_offset);
	push_zeros(8); // TODO: What is this for? I presume it's patched by the dynamic linker at runtime?
	push_zeros(8); // TODO: What is this for? I presume it's patched by the dynamic linker at runtime?

	// 0x6 is the offset every .plt entry has to their push instruction
	size_t entry_size = 0x10;
	size_t offset = plt_offset + entry_size + 0x6;

	for (size_t i = 0; i < extern_fns_size; i++) {
		push_64(offset); // text section address of push <i> instruction
		offset += entry_size;
	}

	got_plt_size = bytes_size - got_plt_offset;
}

static void push_got(void) {
	grug_log_section(".got");

	got_offset = bytes_size;

	// This is for extern globals
	push_zeros(8); // grug_on_fn_name
	push_zeros(8); // grug_on_fn_path
	if (calling_game_fn) {
		push_zeros(8); // grug_block_mask
	}

	got_size = bytes_size - got_offset;
}

// See https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-42444.html
static void push_dynamic_entry(u64 tag, u64 value) {
	push_64(tag);
	push_64(value);
}

static void push_dynamic(void) {
	grug_log_section(".dynamic");

	size_t entry_size = 0x10;
	dynamic_size = has_rela_dyn() ? 18 * entry_size : 15 * entry_size;

	size_t segment_2_to_3_offset = 0x1000;
	dynamic_offset = bytes_size + segment_2_to_3_offset - dynamic_size;
	if (has_got()) {
		// This subtracts the future got_size set by push_got()
		// TODO: Stop having these hardcoded here
		dynamic_offset -= 2 * sizeof(u64);
		if (calling_game_fn) {
			dynamic_offset -= sizeof(u64);
		}
	}
#ifndef OLD_LD
	dynamic_offset -= GOT_PLT_INTRO_SIZE;
#endif
	push_zeros(dynamic_offset - bytes_size);

	push_dynamic_entry(DT_HASH, hash_offset);
	push_dynamic_entry(DT_STRTAB, dynstr_offset);
	push_dynamic_entry(DT_SYMTAB, dynsym_offset);
	push_dynamic_entry(DT_STRSZ, dynstr_size);
	push_dynamic_entry(DT_SYMENT, SYMTAB_ENTRY_SIZE);
	push_dynamic_entry(DT_PLTGOT, PLACEHOLDER_64);
	push_dynamic_entry(DT_PLTRELSZ, PLT_ENTRY_SIZE * extern_fns_size);
	push_dynamic_entry(DT_PLTREL, DT_RELA);
	push_dynamic_entry(DT_JMPREL, rela_plt_offset);

	if (has_rela_dyn()) {
		push_dynamic_entry(DT_RELA, rela_dyn_offset);
		push_dynamic_entry(DT_RELASZ, (on_fns_size + extern_data_symbols_size + resources_size + 2 * entity_dependencies_size) * RELA_ENTRY_SIZE);
		push_dynamic_entry(DT_RELAENT, RELA_ENTRY_SIZE);
		push_dynamic_entry(DT_RELACOUNT, on_fns_size + resources_size + 2 * entity_dependencies_size);
	}

	// "Marks the end of the _DYNAMIC array."
	// From https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-42444.html
	push_dynamic_entry(DT_NULL, 0);

	// TODO: I have no clue where this 5 comes from
	size_t padding = 5 * entry_size;

	size_t count = 0;
	count += resources_size > 0;
	count += entity_dependencies_size > 0;
	count += on_fns_size > 0;

	if (count > 0) {
		padding -= entry_size;
	}

	push_zeros(padding);
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

	plt_offset = round_to_power_of_2(bytes_size, 0x1000);
	push_zeros(plt_offset - bytes_size);

	// See this for an explanation: https://stackoverflow.com/q/76987336/13279557
	push_16(PUSH_REL);
	push_32(PLACEHOLDER_32);
	push_16(JMP_REL);
	push_32(PLACEHOLDER_32);
	push_32(NOP_32_BITS); // See https://reverseengineering.stackexchange.com/a/11973

	size_t pushed_plt_entries = 0;

	size_t offset = 0x10;
	for (size_t i = 0; i < BFD_HASH_BUCKET_SIZE; i++) {
		u32 chain_index = buckets_used_extern_fns[i];
		if (chain_index == UINT32_MAX) {
			continue;
		}

		while (true) {
			char *name = used_extern_fns[chain_index];

			push_16(JMP_REL);
			push_32(PLACEHOLDER_32);
			push_byte(PUSH_32_BITS);
			push_32(pushed_plt_entries++);
			push_byte(JMP_32_BIT_OFFSET);
			push_game_fn_offset(name, offset);
			size_t offset_to_start_of_plt = -offset - 0x10;
			push_32(offset_to_start_of_plt);
			offset += 0x10;

			chain_index = chains_used_extern_fns[chain_index];
			if (chain_index == UINT32_MAX) {
				break;
			}
		}
	}

	hash_game_fn_offsets();

	plt_size = bytes_size - plt_offset;
}

static void push_rela(u64 offset, u64 info, u64 addend) {
	push_64(offset);
	push_64(info);
	push_64(addend);
}

// Source:
// https://docs.oracle.com/cd/E19683-01/816-1386/6m7qcoblk/index.html#chapter6-1235
// https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-54839.html
static void push_rela_plt(void) {
	grug_log_section(".rela.plt");

	rela_plt_offset = bytes_size;

	for (size_t shuffled_symbol_index = 0; shuffled_symbol_index < symbols_size; shuffled_symbol_index++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[shuffled_symbol_index];

		if (symbol_index < first_used_extern_fn_symbol_index || symbol_index >= first_used_extern_fn_symbol_index + extern_fns_size) {
			continue;
		}

		// `1 +` skips the first symbol, which is always undefined
		size_t dynsym_index = 1 + shuffled_symbol_index;

		push_rela(PLACEHOLDER_64, ELF64_R_INFO(dynsym_index, R_X86_64_JUMP_SLOT), 0);
	}

	segment_0_size = bytes_size;

	rela_plt_size = bytes_size - rela_plt_offset;
}

// Source: https://stevens.netmeister.org/631/elf.html
static void push_rela_dyn(void) {
	grug_log_section(".rela.dyn");

	rela_dyn_offset = bytes_size;

	for (size_t i = 0; i < grug_define_entity->on_function_count; i++) {
		struct on_fn *on_fn = get_on_fn(grug_define_entity->on_functions[i].name);
		if (on_fn) {
			push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
		}
	}

	for (size_t i = 0; i < resources_size; i++) {
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
	}

	// "entities" symbol
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
	}

	// "entity_types" symbol
	for (size_t i = 0; i < entity_dependencies_size; i++) {
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(0, R_X86_64_RELATIVE), PLACEHOLDER_64);
	}

	// Idk why, but nasm seems to always push the symbols in the reverse order
	// Maybe this should use symbol_index_to_shuffled_symbol_index?
	for (size_t i = extern_data_symbols_size; i > 0; i--) {
		// `1 +` skips the first symbol, which is always undefined
		push_rela(PLACEHOLDER_64, ELF64_R_INFO(1 + symbol_index_to_shuffled_symbol_index[first_extern_data_symbol_index + i - 1], R_X86_64_GLOB_DAT), PLACEHOLDER_64);
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
		char *symbol = symbols[i];

		push_string_bytes(symbol);
		dynstr_size += strlen(symbol) + 1;
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
	push_32(nbucket);

	u32 nchain = 1 + symbols_size; // `1 + `, because index 0 is always STN_UNDEF (the value 0)
	push_32(nchain);

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
		push_32(buckets[i]);
	}

	for (size_t i = 0; i < chains_size; i++) {
		push_32(chains[i]);
	}

	hash_size = bytes_size - hash_offset;

	push_alignment(8);
}

static void push_section_header(u32 name_offset, u32 type, u64 flags, u64 address, u64 offset, u64 size, u32 link, u32 info, u64 alignment, u64 entry_size) {
	push_32(name_offset);
	push_32(type);
	push_64(flags);
	push_64(address);
	push_64(offset);
	push_64(size);
	push_32(link);
	push_32(info);
	push_64(alignment);
	push_64(entry_size);
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

	if (has_rela_dyn()) {
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
	push_section_header(eh_frame_shstrtab_offset, SHT_PROGBITS, SHF_ALLOC, eh_frame_offset, eh_frame_offset, 0, SHN_UNDEF, 0, 8, 0);

	// .dynamic: Dynamic linking information section
	push_section_header(dynamic_shstrtab_offset, SHT_DYNAMIC, SHF_WRITE | SHF_ALLOC, dynamic_offset, dynamic_offset, dynamic_size, shindex_dynstr, 0, 8, 16);

	if (has_got()) {
		// .got: Global offset table section
		push_section_header(got_shstrtab_offset, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, got_offset, got_offset, got_size, SHN_UNDEF, 0, 8, 8);
	}

	// .got.plt: Global offset table procedure linkage table section
	push_section_header(got_plt_shstrtab_offset, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, got_plt_offset, got_plt_offset, got_plt_size, SHN_UNDEF, 0, 8, 8);

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
	push_32(type);
	push_32(flags);
	push_64(offset);
	push_64(virtual_address);
	push_64(physical_address);
	push_64(file_size);
	push_64(mem_size);
	push_64(alignment);
}

static void push_program_headers(void) {
	grug_log_section("Program headers");

	// Segment 0
	// .hash, .dynsym, .dynstr, .rela.dyn, .rela.plt
	// 0x40 to 0x78
	push_program_header(PT_LOAD, PF_R, 0, 0, 0, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// Segment 1
	// .plt, .text
	// 0x78 to 0xb0
	push_program_header(PT_LOAD, PF_R | PF_X, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// Segment 2
	// .eh_frame
	// 0xb0 to 0xe8
	push_program_header(PT_LOAD, PF_R, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 0, 0, 0x1000);

	// Segment 3
	// .dynamic, .got, .got.plt, .data
	// 0xe8 to 0x120
	push_program_header(PT_LOAD, PF_R | PF_W, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 0x1000);

	// Segment 4
	// .dynamic
	// 0x120 to 0x158
	push_program_header(PT_DYNAMIC, PF_R | PF_W, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 8);

	// Segment 5
	// .dynamic, .got
	// 0x158 to 0x190
	push_program_header(PT_GNU_RELRO, PF_R, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, PLACEHOLDER_64, 1);
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
	push_64(PLACEHOLDER_64);

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
	push_byte(14 + has_got() + has_rela_dyn());
	push_byte(0);

	// Index of entry with section names
	// 0x3e to 0x40
	push_byte(13 + has_got() + has_rela_dyn());
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

	if (has_rela_dyn()) {
		push_rela_dyn();
	}

	push_rela_plt();

	push_plt();

	push_text();

	eh_frame_offset = round_to_power_of_2(bytes_size, 0x1000);
	push_zeros(eh_frame_offset - bytes_size);

	push_dynamic();

	if (has_got()) {
		push_got();
	}

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
	size_t excess = offset % sizeof(u64); // Alignment
	if (excess > 0) {
		offset += sizeof(u64) - excess;
	}
	data_offsets[i++] = offset;
	offset += sizeof(u64);

	// "on_fns" function address symbols
	if (grug_define_entity->on_function_count > 0) {
		data_offsets[i++] = offset;
		for (size_t on_fn_index = 0; on_fn_index < grug_define_entity->on_function_count; on_fn_index++) {
			offset += sizeof(size_t);
		}
	}

	// "strings" symbol
	data_offsets[i++] = offset;
	for (size_t string_index = 0; string_index < data_strings_size; string_index++) {
		data_string_offsets[string_index] = offset;
		char *string = data_strings[string_index];
		offset += strlen(string) + 1;
	}

	// "resources_size" symbol
	excess = offset % sizeof(u64); // Alignment
	if (excess > 0) {
		offset += sizeof(u64) - excess;
	}
	data_offsets[i++] = offset;
	offset += sizeof(u64);

	// "resources" symbol
	if (resources_size > 0) {
		data_offsets[i++] = offset;
		for (size_t resource_index = 0; resource_index < resources_size; resource_index++) {
			offset += sizeof(size_t);
		}
	}

	// "entities_size" symbol
	data_offsets[i++] = offset;
	offset += sizeof(u64);

	if (entity_dependencies_size > 0) {
		// "entities" symbol
		data_offsets[i++] = offset;
		for (size_t entity_dependency_index = 0; entity_dependency_index < entity_dependencies_size; entity_dependency_index++) {
			offset += sizeof(size_t);
		}

		// "entity_types" symbol
		data_offsets[i++] = offset;
		for (size_t entity_dependency_index = 0; entity_dependency_index < entity_dependencies_size; entity_dependency_index++) {
			offset += sizeof(size_t);
		}
	}

	data_size = offset;
}

static void init_symbol_name_strtab_offsets(void) {
	for (size_t i = 0, offset = 0; i < symbols_size; i++) {
		size_t symbol_index = shuffled_symbol_index_to_symbol_index[i];
		char *symbol = symbols[symbol_index];

		symbol_name_strtab_offsets[symbol_index] = offset;
		offset += strlen(symbol) + 1;
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
	for (size_t i = 0, offset = 1; i < symbols_size; i++) {
		char *symbol = symbols[i];

		symbol_name_dynstr_offsets[i] = offset;
		offset += strlen(symbol) + 1;
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
	if (has_rela_dyn()) {
		shindex_rela_dyn = shindex++;
	}
	shindex_rela_plt = shindex++;
	shindex_plt = shindex++;
	shindex_text = shindex++;
	shindex_eh_frame = shindex++;
	shindex_dynamic = shindex++;
	if (has_got()) {
		shindex_got = shindex++;
	}
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

	push_symbol("strings");
	data_symbols_size++;

	push_symbol("resources_size");
	data_symbols_size++;

	if (resources_size > 0) {
		push_symbol("resources");
		data_symbols_size++;
	}

	push_symbol("entities_size");
	data_symbols_size++;

	if (entity_dependencies_size != entity_types_size) {
		grug_unreachable();
	}

	if (entity_dependencies_size > 0) {
		push_symbol("entities");
		data_symbols_size++;

		push_symbol("entity_types");
		data_symbols_size++;
	}

	first_extern_data_symbol_index = data_symbols_size;
	if (on_fns_size > 0) {
		push_symbol("grug_on_fn_name");
		extern_data_symbols_size++;

		push_symbol("grug_on_fn_path");
		extern_data_symbols_size++;
	}
	if (calling_game_fn) {
		push_symbol("grug_block_mask");
		extern_data_symbols_size++;
	}

	first_used_extern_fn_symbol_index = first_extern_data_symbol_index + extern_data_symbols_size;
	for (size_t i = 0; i < extern_fns_size; i++) {
		push_symbol(used_extern_fns[i]);
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

#define MAX_ENTITIES 420420
#define MAX_ENTITY_STRINGS_CHARACTERS 420420
#define MAX_ENTITY_NAME_LENGTH 420

char *grug_on_fn_name;
char *grug_on_fn_path;

struct grug_mod_dir grug_mods;

struct grug_modified grug_reloads[MAX_RELOADS];
size_t grug_reloads_size;

static char *entities[MAX_ENTITIES];
static char entity_strings[MAX_ENTITY_STRINGS_CHARACTERS];
static size_t entity_strings_size;
static u32 buckets_entities[MAX_ENTITIES];
static u32 chains_entities[MAX_ENTITIES];
static struct grug_file entity_files[MAX_ENTITIES];
static size_t entities_size;

struct grug_modified_resource grug_resource_reloads[MAX_RESOURCE_RELOADS];
size_t grug_resource_reloads_size;

static void reset_regenerate_modified_mods(void) {
	grug_reloads_size = 0;
	entity_strings_size = 0;
	memset(buckets_entities, 0xff, sizeof(buckets_entities));
	entities_size = 0;
	grug_resource_reloads_size = 0;
}

static void reload_resources_from_dll(char *dll_path, i64 *resource_mtimes) {
	void *dll = dlopen(dll_path, RTLD_NOW);
	if (!dll) {
		print_dlerror("dlopen");
	}

	size_t *dll_resources_size_ptr = get_dll_symbol(dll, "resources_size");
	if (!dll_resources_size_ptr) {
		if (dlclose(dll)) {
			print_dlerror("dlclose");
		}
		grug_error("Retrieving resources_size with get_dll_symbol() failed for %s", dll_path);
	}

	if (*dll_resources_size_ptr == 0) {
		if (dlclose(dll)) {
			print_dlerror("dlclose");
		}
		return;
	}

	char **dll_resources = get_dll_symbol(dll, "resources");
	if (!dll_resources) {
		if (dlclose(dll)) {
			print_dlerror("dlclose");
		}
		grug_error("Retrieving resources with get_dll_symbol() failed for %s", dll_path);
	}

	for (size_t i = 0; i < *dll_resources_size_ptr; i++) {
		char *resource = dll_resources[i];

		struct stat resource_stat;
		if (stat(resource, &resource_stat) == -1) {
			if (dlclose(dll)) {
				print_dlerror("dlclose");
			}
			grug_error("%s: %s", resource, strerror(errno));
		}

		if (resource_stat.st_mtime > resource_mtimes[i]) {
			resource_mtimes[i] = resource_stat.st_mtime;

			struct grug_modified_resource modified = {0};

			strncpy(modified.path, resource, sizeof(modified.path));

			if (grug_resource_reloads_size >= MAX_RESOURCE_RELOADS) {
				if (dlclose(dll)) {
					print_dlerror("dlclose");
				}
				grug_error("There are more than %d modified resources, exceeding MAX_RESOURCE_RELOADS", MAX_RESOURCE_RELOADS);
			}

			grug_resource_reloads[grug_resource_reloads_size++] = modified;
		}
	}

	if (dlclose(dll)) {
		print_dlerror("dlclose");
	}
}

static void regenerate_dll(char *grug_path, char *dll_path) {
	grug_log("# Regenerating %s\n", dll_path);

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
	fill_result_types();
	grug_log("\n# AST (throw this into a JSON formatter)\n");
#ifdef LOGGING
	print_ast();
#else
	(void)print_ast;
#endif

	compile(grug_path);

	grug_log("\n# Section offsets\n");
	generate_shared_object(grug_path, dll_path);
}

// Resetting previous_grug_error is necessary for this edge case:
// 1. Add a typo to a mod, causing a compilation error
// 2. Remove the typo, causing it to compile again
// 3. Add the exact same typo to the same line; we want this to show the earlier error again
static void reset_previous_grug_error(void) {
	previous_grug_error.msg[0] = '\0';
	previous_grug_error.path[0] = '\0';
	previous_grug_error.line_number = 0;
}

// Returns whether an error occurred
bool grug_test_regenerate_dll(char *grug_path, char *dll_path, char *mod_name) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	mod = mod_name;

	strncpy(grug_error.path, grug_path, sizeof(grug_error.path) - 1);
	grug_error.path[sizeof(grug_error.path) - 1] = '\0';

	static bool parsed_mod_api_json = false;
	if (!parsed_mod_api_json) {
		parse_mod_api_json();
		parsed_mod_api_json = true;
	}

	regenerate_dll(grug_path, dll_path);

	reset_previous_grug_error();

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

static void free_file(struct grug_file file) {
	free(file.name);

	if (file.dll && dlclose(file.dll)) {
		print_dlerror("dlclose");
	}

	free(file.resource_mtimes);
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

static u32 get_entity_index(char *entity) {
	if (entities_size == 0) {
		return UINT32_MAX;
	}

	u32 i = buckets_entities[elf_hash(entity) % MAX_ENTITIES];

	while (true) {
		if (i == UINT32_MAX) {
			return UINT32_MAX;
		}

		if (streq(entity, entities[i])) {
			break;
		}

		i = chains_entities[i];
	}

	return i;
}

struct grug_file *grug_get_entity_file(char *entity_name) {
	u32 index = get_entity_index(entity_name);
	if (index == UINT32_MAX) {
		return NULL;
	}
	return &entity_files[index];
}

void check_that_every_entity_exists(struct grug_mod_dir dir) {
	for (size_t i = 0; i < dir.files_size; i++) {
		struct grug_file file = dir.files[i];

		size_t *entities_size_ptr = get_dll_symbol(file.dll, "entities_size");

		if (*entities_size_ptr > 0) {
			char **dll_entities = get_dll_symbol(file.dll, "entities");

			char **dll_entity_types = get_dll_symbol(file.dll, "entity_types");

			for (size_t dll_entity_index = 0; dll_entity_index < *entities_size_ptr; dll_entity_index++) {
				char *entity = dll_entities[dll_entity_index];

				u32 entity_index = get_entity_index(entity);

				grug_assert(entity_index != UINT32_MAX, "The entity '%s' does not exist", entity);

				char *json_entity_type = dll_entity_types[dll_entity_index];

				struct grug_file other_file = entity_files[entity_index];

				grug_assert(*json_entity_type == '\0' || streq(other_file.define_type, json_entity_type), "The entity '%s' has the type '%s', whereas the expected type from mod_api.json is '%s'", entity, other_file.define_type, json_entity_type);
			}
		}
	}

	for (size_t i = 0; i < dir.dirs_size; i++) {
		check_that_every_entity_exists(dir.dirs[i]);
	}
}

static void push_reload(struct grug_modified modified) {
	grug_assert(grug_reloads_size < MAX_RELOADS, "There are more than %d modified grug files, exceeding MAX_RELOADS", MAX_RELOADS);
	grug_reloads[grug_reloads_size++] = modified;
}

// Returns `mod + ':' + grug_filename - ".grug"`
static char *form_entity(char *grug_filename) {
	static char grug_basename[MAX_ENTITY_NAME_LENGTH];

	char *period = strrchr(grug_filename, '.');
	if (period == NULL) {
		grug_unreachable();
	}

	size_t basename_length = period - grug_filename;

	grug_assert(basename_length < MAX_ENTITY_NAME_LENGTH, "There are more than %d characters in the grug filename '%s', exceeding MAX_ENTITY_NAME_LENGTH", MAX_ENTITY_NAME_LENGTH, grug_filename);
	memcpy(grug_basename, grug_filename, basename_length);
	grug_basename[basename_length] = '\0';

	static char entity[MAX_ENTITY_NAME_LENGTH];
	grug_assert(snprintf(entity, sizeof(entity), "%s:%s", mod, grug_basename) >= 0, "Filling the variable 'entity' failed");

	size_t entity_length = strlen(entity);

	grug_assert(entity_strings_size + entity_length < MAX_ENTITY_STRINGS_CHARACTERS, "There are more than %d characters in the entity_strings array, exceeding MAX_ENTITY_STRINGS_CHARACTERS", MAX_ENTITY_STRINGS_CHARACTERS);

	char *entity_str = entity_strings + entity_strings_size;

	memcpy(entity_str, entity, entity_length);
	entity_strings_size += entity_length;
	entity_strings[entity_strings_size++] = '\0';

	return entity_str;
}

static void add_entity(char *grug_filename, struct grug_file *file) {
	grug_assert(entities_size < MAX_ENTITIES, "There are more than %d entities, exceeding MAX_ENTITIES", MAX_ENTITIES);

	char *entity = form_entity(grug_filename);

	grug_assert(get_entity_index(entity) == UINT32_MAX, "The entity '%s' already exists, because there are two grug files called '%s' in the mod '%s'", entity, grug_filename, mod);

	u32 bucket_index = elf_hash(entity) % MAX_ENTITIES;

	chains_entities[entities_size] = buckets_entities[bucket_index];

	buckets_entities[bucket_index] = entities_size;

	// entity_files[] needs to take ownership of `file`,
	// since reload_modified_mod() can swap-remove the file
	entity_files[entities_size] = *file;

	entities[entities_size++] = entity;
}

static struct grug_file *push_file(struct grug_mod_dir *dir, struct grug_file file) {
	if (dir->files_size >= dir->files_capacity) {
		dir->files_capacity = dir->files_capacity == 0 ? 1 : dir->files_capacity * 2;
		dir->files = realloc(dir->files, dir->files_capacity * sizeof(*dir->files));
		grug_assert(dir->files, "realloc: %s", strerror(errno));
	}
	dir->files[dir->files_size] = file;
	return &dir->files[dir->files_size++];
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
static bool seen_entry(char *name, char **seen_names, size_t seen_names_size) {
	for (size_t i = 0; i < seen_names_size; i++) {
		if (streq(seen_names[i], name)) {
			return true;
		}
	}
	return false;
}

static struct grug_file *regenerate_dll_and_file(struct grug_file *file, char entry_path[STUPID_MAX_PATH], bool needs_regeneration, char dll_path[STUPID_MAX_PATH], char *grug_filename, struct grug_mod_dir *dir) {
	struct grug_modified modified = {0};

	strncpy(grug_error.path, entry_path, sizeof(grug_error.path));

	if (file && file->dll) {
		modified.old_dll = file->dll;
		if (dlclose(file->dll)) {
			print_dlerror("dlclose");
		}
		file->dll = NULL;
	}

	if (needs_regeneration) {
		regenerate_dll(entry_path, dll_path);
	}

	struct grug_file new_file = {0};

	if (file) {
		new_file.name = file->name;
	} else {
		new_file.name = strdup(grug_filename);
		grug_assert(new_file.name, "strdup: %s", strerror(errno));
	}

	new_file.dll = dlopen(dll_path, RTLD_NOW);
	if (!new_file.dll) {
		print_dlerror("dlopen");
	}

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"
	new_file.define_fn = get_dll_symbol(new_file.dll, "define");
	#pragma GCC diagnostic pop
	grug_assert(new_file.define_fn, "Retrieving the define() function with get_dll_symbol() failed for %s", dll_path);

	size_t *globals_size_ptr = get_dll_symbol(new_file.dll, "globals_size");
	grug_assert(globals_size_ptr, "Retrieving the globals_size variable with get_dll_symbol() failed for %s", dll_path);
	new_file.globals_size = *globals_size_ptr;

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"
	new_file.init_globals_fn = get_dll_symbol(new_file.dll, "init_globals");
	#pragma GCC diagnostic pop
	grug_assert(new_file.init_globals_fn, "Retrieving the init_globals() function with get_dll_symbol() failed for %s", dll_path);

	new_file.define_type = get_dll_symbol(new_file.dll, "define_type");
	grug_assert(new_file.define_type, "Retrieving the define_type string with get_dll_symbol() failed for %s", dll_path);

	// on_fns is optional, so don't check for NULL
	// Note that if an entity in mod_api.json specifies that it has on_fns that the modder can use,
	// on_fns is guaranteed NOT to be NULL!
	new_file.on_fns = get_dll_symbol(new_file.dll, "on_fns");

	size_t *resources_size_ptr = get_dll_symbol(new_file.dll, "resources_size");
	size_t dll_resources_size = *resources_size_ptr;

	if (file) {
		file->dll = new_file.dll;
		file->define_fn = new_file.define_fn;
		file->globals_size = new_file.globals_size;
		file->init_globals_fn = new_file.init_globals_fn;
		file->define_type = new_file.define_type;
		file->on_fns = new_file.on_fns;

		if (dll_resources_size > 0) {
			file->resource_mtimes = realloc(file->resource_mtimes, dll_resources_size * sizeof(i64));
			grug_assert(file->resource_mtimes, "realloc: %s", strerror(errno));
		} else {
			// We can't use realloc() to do this
			// See https://stackoverflow.com/a/16760080/13279557
			free(file->resource_mtimes);
			file->resource_mtimes = NULL;
		}
	} else {
		// We check dll_resources_size > 0, since whether malloc(0) returns NULL is implementation defined
		// See https://stackoverflow.com/a/1073175/13279557
		if (dll_resources_size > 0) {
			new_file.resource_mtimes = malloc(dll_resources_size * sizeof(i64));
			grug_assert(new_file.resource_mtimes, "malloc: %s", strerror(errno));
		}

		file = push_file(dir, new_file);
	}

	if (dll_resources_size > 0) {
		char **dll_resources = get_dll_symbol(file->dll, "resources");

		// Initialize file->resource_mtimes
		for (size_t i = 0; i < dll_resources_size; i++) {
			struct stat resource_stat;
			grug_assert(stat(dll_resources[i], &resource_stat) == 0, "%s: %s", dll_resources[i], strerror(errno));

			file->resource_mtimes[i] = resource_stat.st_mtime;
		}
	}

	// Let the game developer know that a grug file was recompiled
	if (needs_regeneration) {
		strncpy(modified.path, entry_path, sizeof(modified.path));
		modified.file = file;
		push_reload(modified);
	}

	return file;
}

static void reload_grug_file(char *dll_entry_path, i64 entry_mtime, char *grug_filename, struct grug_mod_dir *dir, char entry_path[STUPID_MAX_PATH]) {
	// Fill dll_path
	char dll_path[STUPID_MAX_PATH];
	memcpy(dll_path, dll_entry_path, STUPID_MAX_PATH);
	char *ext = get_file_extension(dll_path);
	assert(*ext);
	ext[1] = '\0';
	strncat(ext + 1, "so", STUPID_MAX_PATH - 1 - strlen(dll_path));

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
	bool needs_regeneration = !dll_exists || entry_mtime > dll_stat.st_mtime;

	struct grug_file *file = get_file(dir, grug_filename);

	if (needs_regeneration || !file) {
		file = regenerate_dll_and_file(file, entry_path, needs_regeneration, dll_path, grug_filename, dir);
	}

	// Needed for grug_get_entitity_file() and check_that_every_entity_exists()
	add_entity(grug_filename, file);

	// Let the game developer know when they need to reload a resource
	reload_resources_from_dll(dll_path, file->resource_mtimes);
}

static bool is_lowercase(char *str) {
	for (; *str; str++) {
        // `!islower()` doesn't work, since '-' isn't considered lowercase
		if (isupper(*str)) {
			return false;
		}
	}
	return true;
}

static void reload_modified_mod(char *mods_dir_path, char *dll_dir_path, struct grug_mod_dir *dir) {
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

		grug_assert(is_lowercase(dp->d_name), "Mod file and directory names must be lowercase, but \"%s\" in \"%s\" isn't", dp->d_name, entry_path);

		char dll_entry_path[STUPID_MAX_PATH];
		snprintf(dll_entry_path, sizeof(dll_entry_path), "%s/%s", dll_dir_path, dp->d_name);

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

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

			reload_modified_mod(entry_path, dll_entry_path, subdir);
		} else if (S_ISREG(entry_stat.st_mode) && streq(get_file_extension(dp->d_name), ".grug")) {
			if (seen_file_names_size >= seen_file_names_capacity) {
				seen_file_names_capacity = seen_file_names_capacity == 0 ? 1 : seen_file_names_capacity * 2;
				seen_file_names = realloc(seen_file_names, seen_file_names_capacity * sizeof(*seen_file_names));
				grug_assert(seen_file_names, "realloc: %s", strerror(errno));
			}
			seen_file_names[seen_file_names_size++] = strdup(dp->d_name);

			reload_grug_file(dll_entry_path, entry_stat.st_mtime, dp->d_name, dir, entry_path);
		}
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);

	// If the directory used to contain a subdirectory or file
	// that doesn't exist anymore, free it
	//
	// TODO: This can be made O(n) rather than O(n*m) by letting every directory contain a "seen" boolean,
	// so that we can iterate over all directories and files once here
	for (size_t i = dir->dirs_size; i > 0;) {
		i--;
		if (!seen_entry(dir->dirs[i].name, seen_dir_names, seen_dir_names_size)) {
			free_dir(dir->dirs[i]);
			dir->dirs[i] = dir->dirs[--dir->dirs_size]; // Swap-remove
		}
	}
	for (size_t i = dir->files_size; i > 0;) {
		i--;
		if (!seen_entry(dir->files[i].name, seen_file_names, seen_file_names_size)) {
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

static void reload_modified_mods(void) {
	struct grug_mod_dir *dir = &grug_mods;

	DIR *dirp = opendir(MODS_DIR_PATH);
	grug_assert(dirp, "opendir: %s", strerror(errno));

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		if (streq(dp->d_name, ".") || streq(dp->d_name, "..")) {
			continue;
		}

		char entry_path[STUPID_MAX_PATH];
		snprintf(entry_path, sizeof(entry_path), MODS_DIR_PATH"/%s", dp->d_name);

		grug_assert(is_lowercase(dp->d_name), "Mod file and directory names must be lowercase, but \"%s\" in \"%s\" isn't", dp->d_name, entry_path);

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

		if (S_ISDIR(entry_stat.st_mode)) {
			mod = dp->d_name;

			char about_json_path[STUPID_MAX_PATH];
			grug_assert(snprintf(about_json_path, sizeof(about_json_path), "%s/about.json", entry_path) >= 0, "Filling the variable 'about_json_path' failed");

			grug_assert(access(about_json_path, F_OK) == 0, "Every mod requires an 'about.json' file, but the mod '%s' doesn't have one", mod);

			char dll_entry_path[STUPID_MAX_PATH];
			snprintf(dll_entry_path, sizeof(dll_entry_path), DLL_DIR_PATH"/%s", dp->d_name);

			struct grug_mod_dir *subdir = get_subdir(dir, dp->d_name);
			if (!subdir) {
				struct grug_mod_dir inserted_subdir = {.name = strdup(dp->d_name)};
				grug_assert(inserted_subdir.name, "strdup: %s", strerror(errno));
				push_subdir(dir, inserted_subdir);
				subdir = dir->dirs + dir->dirs_size - 1;
			}

			reload_modified_mod(entry_path, dll_entry_path, subdir);
		}
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);
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

	reset_regenerate_modified_mods();

	static bool parsed_mod_api_json = false;
	if (!parsed_mod_api_json) {
		parse_mod_api_json();
		parsed_mod_api_json = true;
	}

	if (!grug_mods.name) {
		grug_mods.name = strdup(get_basename(MODS_DIR_PATH));
		grug_assert(grug_mods.name, "strdup: %s", strerror(errno));
	}

	reload_modified_mods();

	check_that_every_entity_exists(grug_mods);

	reset_previous_grug_error();

	return false;
}

// MIT License

// Copyright (c) 2024 MyNameIsTrez

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
