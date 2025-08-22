//// INCLUDES AND DEFINES

#define _XOPEN_SOURCE 700 // This is just so VS Code can find CLOCK_PROCESS_CPUTIME_ID

#include "grug.h"

// TODO: Remove unused includes
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

#define grug_error(...) {\
	if (snprintf(grug_error.msg, sizeof(grug_error.msg), __VA_ARGS__) < 0) {\
		abort();\
	}\
    grug_error_impl(__LINE__);\
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
