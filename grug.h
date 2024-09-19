// See the bottom of grug.c for the MIT license, which also applies to this file

#pragma once

#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define MAX_RELOADS 6969
#define MAX_RESOURCE_RELOADS 6969

#define grug_mod_had_runtime_error() sigsetjmp(grug_runtime_error_jmp_buffer, 1)

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
	int64_t *resource_mtimes;
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
	char path[4096];
	void *old_dll;
	struct grug_file *file;
};

struct grug_modified_resource {
	char path[4096];
};

struct grug_error {
	char path[4096];
	char msg[420];
	bool has_changed;
	int line_number;
	int grug_c_line_number;
};

enum grug_runtime_error {
	GRUG_ON_FN_TIME_LIMIT_EXCEEDED,
	GRUG_ON_FN_STACK_OVERFLOW,
	GRUG_ON_FN_ARITHMETIC_ERROR,
};

extern struct grug_mod_dir grug_mods;

extern struct grug_modified grug_reloads[MAX_RELOADS];
extern size_t grug_reloads_size;

extern struct grug_modified_resource grug_resource_reloads[MAX_RESOURCE_RELOADS];
extern size_t grug_resource_reloads_size;

extern struct grug_error grug_error;
extern char *grug_on_fn_name;
extern char *grug_on_fn_path;

extern volatile sig_atomic_t grug_runtime_error;
extern jmp_buf grug_runtime_error_jmp_buffer;

bool grug_regenerate_modified_mods(void);
void grug_free_mods(void);
char *grug_get_runtime_error_reason(void);

// Do NOT store the returned pointer!
// It has a chance to dangle after the next grug_regenerate_modified_mods() call
struct grug_file *grug_get_entity_file(char *entity_name);

// For the grug-tests repository
bool grug_test_regenerate_dll(char *grug_file_path, char *dll_path, char *mod);
