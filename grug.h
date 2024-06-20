#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct grug_file grug_file_t;
typedef struct mod_dir grug_mod_dir_t;

typedef struct modified grug_modified_t;

typedef struct grug_error grug_error_t;

typedef void (*init_globals_fn_t)(void *globals_struct);

struct grug_file {
	char *name;
	void *dll;
	size_t globals_struct_size;
	init_globals_fn_t init_globals_fn;
	char *define_type;
	void *define;
	void *on_fns;
};

struct mod_dir {
	char *name;

	grug_mod_dir_t *dirs;
	size_t dirs_size;
	size_t dirs_capacity;

	grug_file_t *files;
	size_t files_size;
	size_t files_capacity;
};

struct modified {
	void *old_dll;
	void *new_dll;
	size_t globals_struct_size;
	init_globals_fn_t init_globals_fn;
	char *define_type;
	void *define;
	void *on_fns;
};

struct grug_error {
	char msg[420];
	char *filename;
	int line_number;
};

extern grug_mod_dir_t grug_mods;

extern grug_modified_t *grug_reloads;
extern size_t grug_reloads_size;

extern grug_error_t grug_error;

bool grug_regenerate_modified_mods(void);
void grug_free_mods(void);

// For the grug-tests repository
bool grug_test_regenerate_dll(char *grug_file_path, char *dll_path, char *c_path);
