#include "03_utils.c"

//// RUNTIME ERROR HANDLING

#define NS_PER_MS 1000000
#define MS_PER_SEC 1000
#define NS_PER_SEC 1000000000

static char runtime_error_reason[420];

static uint64_t on_fn_time_limit_ms;
static size_t on_fn_time_limit_sec;
static size_t on_fn_time_limit_ns;

static thread_local u64 grug_max_rsp;
static thread_local struct timespec grug_current_time;
static thread_local struct timespec grug_max_time;

USED_BY_MODS grug_runtime_error_handler_t grug_runtime_error_handler = NULL;

static const char *grug_get_runtime_error_reason(enum grug_runtime_error_type type) {
	switch (type) {
		case GRUG_ON_FN_DIVISION_BY_ZERO:
			return "Division of an i32 by 0";
		case GRUG_ON_FN_STACK_OVERFLOW:
			return "Stack overflow, so check for accidental infinite recursion";
		case GRUG_ON_FN_TIME_LIMIT_EXCEEDED: {
			snprintf(runtime_error_reason, sizeof(runtime_error_reason), "Took longer than %" PRIu64 " milliseconds to run", on_fn_time_limit_ms);
			return runtime_error_reason;
		}
		case GRUG_ON_FN_OVERFLOW:
			return "i32 overflow";
		case GRUG_ON_FN_GAME_FN_ERROR:
			return runtime_error_reason;
	}
	grug_unreachable();
}

USED_BY_MODS void grug_call_runtime_error_handler(enum grug_runtime_error_type type);
void grug_call_runtime_error_handler(enum grug_runtime_error_type type) {
	const char *reason = grug_get_runtime_error_reason(type);

	grug_runtime_error_handler(reason, type, grug_fn_name, grug_fn_path);
}

USED_BY_PROGRAMS void grug_error_impl(int line) {
	grug_error.grug_c_line_number = line;

	grug_error.has_changed =
		!streq(grug_error.msg, previous_grug_error.msg)
	 || !streq(grug_error.path, previous_grug_error.path)
	 || grug_error.grug_c_line_number != previous_grug_error.grug_c_line_number;

	memcpy(previous_grug_error.msg, grug_error.msg, sizeof(grug_error.msg));
	memcpy(previous_grug_error.path, grug_error.path, sizeof(grug_error.path));

	previous_grug_error.grug_c_line_number = grug_error.grug_c_line_number;
}

USED_BY_MODS bool grug_is_time_limit_exceeded(void);
USED_BY_MODS bool grug_is_time_limit_exceeded(void) {
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &grug_current_time);

	if (grug_current_time.tv_sec < grug_max_time.tv_sec) {
		return false;
	}

	if (grug_current_time.tv_sec > grug_max_time.tv_sec) {
		return true;
	}

	return grug_current_time.tv_nsec > grug_max_time.tv_nsec;
}

USED_BY_MODS void grug_set_time_limit(void);
USED_BY_MODS void grug_set_time_limit(void) {
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &grug_max_time);

	grug_max_time.tv_sec += on_fn_time_limit_sec;

	grug_max_time.tv_nsec += on_fn_time_limit_ns;

	if (grug_max_time.tv_nsec >= NS_PER_SEC) {
		grug_max_time.tv_nsec -= NS_PER_SEC;
		grug_max_time.tv_sec++;
	}
}

USED_BY_MODS u64* grug_get_max_rsp_addr(void);
USED_BY_MODS u64* grug_get_max_rsp_addr(void) {
    return &grug_max_rsp;
}

USED_BY_MODS u64 grug_get_max_rsp(void);
USED_BY_MODS u64 grug_get_max_rsp(void) {
    return grug_max_rsp;
}
