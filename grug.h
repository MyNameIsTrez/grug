// See the bottom of grug.c for the MIT license, which also applies to this file

#pragma once

#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

#define GRUG_ON_FN_TIME_LIMIT_SECONDS 1

#define grug_mod_had_runtime_error() grug_init_signal_handlers(), sigsetjmp(grug_runtime_error_jmp_buffer, 1)

typedef void (*grug_define_fn_t)(void);
typedef void (*grug_init_globals_fn_t)(void *globals);

struct grug_file {
	char *name;
	void *dll;
	grug_define_fn_t define_fn;
	size_t globals_size;
	grug_init_globals_fn_t init_globals_fn;
	char *define_type;
	void *on_fns;
};

struct grug_mod_dir {
	char *name;

	struct grug_mod_dir *dirs;
	size_t dirs_size;
	size_t dirs_capacity;

	struct grug_file *files;
	size_t files_size;
	size_t files_capacity;
};

struct grug_modified {
	void *old_dll;
	void *new_dll;
	grug_define_fn_t define_fn;
	size_t globals_size;
	grug_init_globals_fn_t init_globals_fn;
	char *define_type;
	void *on_fns;
	char path[4096];
};

struct grug_error {
	bool has_changed;
	char msg[420];
	char path[4096];
	int line_number;
	int grug_c_line_number;
};

enum grug_runtime_error {
	GRUG_ON_FN_TIME_LIMIT_EXCEEDED,
	GRUG_ON_FN_STACK_OVERFLOW,
};

extern struct grug_mod_dir grug_mods;

extern struct grug_modified *grug_reloads;
extern size_t grug_reloads_size;

extern struct grug_error grug_error;

extern volatile sig_atomic_t grug_runtime_error;
extern jmp_buf grug_runtime_error_jmp_buffer;

bool grug_regenerate_modified_mods(void);
void grug_free_mods(void);

// Don't call this; this is just for grug_mod_had_runtime_error()
void grug_init_signal_handlers(void);

// For the grug-tests repository
bool grug_test_regenerate_dll(char *grug_file_path, char *dll_path);
