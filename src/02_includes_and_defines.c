//// INCLUDES AND DEFINES

#define _XOPEN_SOURCE 700 // This is just so VS Code can find CLOCK_PROCESS_CPUTIME_ID

#include "grug.h"

#include <assert.h>
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

// "The problem is that you can't meaningfully define a constant like this
// in a header file. The maximum path size is actually to be something
// like a filesystem limitation, or at the very least a kernel parameter.
// This means that it's a dynamic value, not something preordained."
// https://eklitzke.org/path-max-is-tricky
#define STUPID_MAX_PATH 4096

static bool streq(const char *a, const char *b);

#define grug_error(...) {\
	if (snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__) < 0) {\
		abort();\
	}\
	\
	grug_error.grug_c_line_number = __LINE__;\
	\
	grug_error.has_changed =\
		!streq(grug_error.msg, previous_grug_error.msg)\
	 || !streq(grug_error.path, previous_grug_error.path)\
	 || grug_error.grug_c_line_number != previous_grug_error.grug_c_line_number;\
	\
	memcpy(previous_grug_error.msg, grug_error.msg, sizeof(grug_error.msg));\
	memcpy(previous_grug_error.path, grug_error.path, sizeof(grug_error.path));\
	previous_grug_error.grug_c_line_number = grug_error.grug_c_line_number;\
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
	grug_error("This line of code in grug.c:%d is supposed to be unreachable. Please report this bug to the grug developers!", __LINE__);\
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

#define USED_BY_MODS
#define USED_BY_PROGRAMS

#define BFD_HASH_BUCKET_SIZE 4051 // From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/hash.c#l345
