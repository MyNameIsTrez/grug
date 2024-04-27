#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <typedefs.h>

typedef void (*grug_error_handler_fn)(char *error_msg, char *filename, int line_number);
typedef void (*init_globals_struct_fn_type)(void *globals_struct);

typedef struct grug_file grug_file;
typedef struct mod_directory mod_directory;

typedef struct reload reload;

struct grug_file {
	char *name;
	void *dll;
	size_t globals_struct_size;
	init_globals_struct_fn_type init_globals_struct_fn;
};

struct mod_directory {
	char *name;

	mod_directory *dirs;
	size_t dirs_size;
	size_t dirs_capacity;

	grug_file *files;
	size_t files_size;
	size_t files_capacity;
};

struct reload {
	void *old_dll;
	void *new_dll;
	size_t globals_struct_size;
	init_globals_struct_fn_type init_globals_struct_fn;
};

extern mod_directory mods;

extern reload *reloads;
extern size_t reloads_size;

void grug_init(grug_error_handler_fn grug_error_handler);
void grug_reload_modified_mods(void);
void grug_print_mods(void);
void *grug_get_fn(void *dll, char *fn_name);
void grug_free_mods(void);
