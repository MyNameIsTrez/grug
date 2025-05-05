// See the bottom of grug.c for the MIT license, which also applies to this file

#pragma once

//// Includes

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//// Enums

enum grug_runtime_error_type {
	GRUG_ON_FN_DIVISION_BY_ZERO,
	GRUG_ON_FN_STACK_OVERFLOW,
	GRUG_ON_FN_TIME_LIMIT_EXCEEDED,
	GRUG_ON_FN_OVERFLOW,
	GRUG_ON_FN_GAME_FN_ERROR,
};

//// Function typedefs

typedef void (*grug_runtime_error_handler_t)(char *reason, enum grug_runtime_error_type type, char *on_fn_name, char *on_fn_path);

typedef void (*grug_init_globals_fn_t)(void *globals, uint64_t id);

//// Functions

// Returns whether an error occurred
bool grug_init(grug_runtime_error_handler_t handler, char *mod_api_json_path, char *mods_dir_path, char *dll_dir_path, size_t on_fn_time_limit_ms) __attribute__((warn_unused_result));

// Returns whether an error occurred
bool grug_regenerate_modified_mods(void) __attribute__((warn_unused_result));

// Do NOT store the returned pointer, as it has a chance to dangle
// after the next grug_regenerate_modified_mods() call!
struct grug_file *grug_get_entity_file(char *entity);

// You aren't expected to call this normally
void grug_free_mods(void);

// Calling this during a game function will cause grug
// to immediately return a runtime error, from the current on_ function call
void grug_game_function_error_happened(char *message);

// These functions are meant to be used together, to write and read the AST of the mods/ directory
// One use case is that this allows you to for example write a Python program that reads the mods/ AST,
// and modifies it to apply mod API changes the game had,
// like renaming game function calls, or doubling jump strength values if the game's gravity was doubled
//
// There's nothing stopping a game from doing this itself with these functions,
// but an external tool has the advantage that it isn't tied to the game's release cycle
//
// Returns whether an error occurred
bool grug_dump_file_to_json(char *input_grug_path, char *output_json_path) __attribute__((warn_unused_result));
bool grug_dump_mods_to_json(char *input_mods_path, char *output_json_path) __attribute__((warn_unused_result));
bool grug_generate_file_from_json(char *input_json_path, char *output_grug_path) __attribute__((warn_unused_result));
bool grug_generate_mods_from_json(char *input_json_path, char *output_mods_path) __attribute__((warn_unused_result));

// Safe mode is the default
// Safe mode is significantly slower than fast mode, but guarantees the program can't crash
// from grug mod runtime errors (division by 0/stack overflow/functions taking too long)
void grug_set_on_fns_to_safe_mode(void);
void grug_set_on_fns_to_fast_mode(void);
bool grug_are_on_fns_in_safe_mode(void);
void grug_toggle_on_fns_mode(void);

//// Defines

#define MAX_RELOADS 6969
#define MAX_RESOURCE_RELOADS 6969

//// Structs

struct grug_file {
	char *name;
	char *entity;
	char *entity_type;

	void *dll;

	size_t globals_size;
	grug_init_globals_fn_t init_globals_fn;

	void *on_fns;

	int64_t *_resource_mtimes;

	bool _seen;
};

struct grug_mod_dir {
	char *name;

	struct grug_mod_dir *dirs;
	size_t dirs_size;
	size_t _dirs_capacity;

	struct grug_file *files;
	size_t files_size;
	size_t _files_capacity;

	bool _seen;
};

struct grug_modified {
	char path[4096];
	void *old_dll;
	struct grug_file file;
};

struct grug_modified_resource {
	char path[4096];
};

struct grug_error {
	char msg[420];
	char path[4096];
	int grug_c_line_number;
	bool has_changed;
};

//// Globals

extern struct grug_mod_dir grug_mods;

extern struct grug_modified grug_reloads[MAX_RELOADS];
extern size_t grug_reloads_size;

extern struct grug_modified_resource grug_resource_reloads[MAX_RESOURCE_RELOADS];
extern size_t grug_resource_reloads_size;

extern struct grug_error grug_error;
extern bool grug_loading_error_in_grug_file;

// These help game functions print helpful errors
extern char *grug_fn_name;
extern char *grug_fn_path;
