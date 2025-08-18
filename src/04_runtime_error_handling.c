#include "03_utils.c"

//// RUNTIME ERROR HANDLING

static char runtime_error_reason[420];

static uint64_t on_fn_time_limit_ms;
static size_t on_fn_time_limit_sec;
static size_t on_fn_time_limit_ns;

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
