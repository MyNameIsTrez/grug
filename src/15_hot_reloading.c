#include "14_linking.c"

//// HOT RELOADING

#define MAX_ENTITIES 420420
#define MAX_ENTITY_STRINGS_CHARACTERS 420420
#define MAX_ENTITY_NAME_LENGTH 420
#define MAX_DIRECTORY_DEPTH 42

USED_BY_PROGRAMS struct grug_mod_dir grug_mods;

USED_BY_PROGRAMS struct grug_modified grug_reloads[MAX_RELOADS];
USED_BY_PROGRAMS size_t grug_reloads_size;

static const char *entities[MAX_ENTITIES];
static char entity_strings[MAX_ENTITY_STRINGS_CHARACTERS];
static size_t entity_strings_size;
static u32 buckets_entities[MAX_ENTITIES];
static u32 chains_entities[MAX_ENTITIES];
static struct grug_file entity_files[MAX_ENTITIES];
static size_t entities_size;

USED_BY_PROGRAMS struct grug_modified_resource grug_resource_reloads[MAX_RESOURCE_RELOADS];
USED_BY_PROGRAMS size_t grug_resource_reloads_size;

USED_BY_MODS const char *grug_fn_name;
USED_BY_MODS const char *grug_fn_path;

static bool is_grug_initialized = false;

static size_t directory_depth;

static void reset_regenerate_modified_mods(void) {
	grug_reloads_size = 0;
	entity_strings_size = 0;
	memset(buckets_entities, 0xff, sizeof(buckets_entities));
	entities_size = 0;
	grug_resource_reloads_size = 0;
	grug_fn_name = "OPTIMIZED OUT FUNCTION NAME";
	grug_fn_path = "OPTIMIZED OUT FUNCTION PATH";
	directory_depth = 0;
}

static void reload_resources_from_dll(const char *dll_path, i64 *resource_mtimes, size_t dll_resources_size) {
	void *dll = dlopen(dll_path, RTLD_NOW);
	if (!dll) {
		print_dlerror("dlopen");

		// Needed for clang's --analyze, since it doesn't recognize
		// that print_dlerror() its longjmp guarantees that `dll`
		// will always be non-null when this if-statement has *not* been entered
		return;
	}

	const char **dll_resources = get_dll_symbol(dll, "resources");
	if (!dll_resources) {
		if (dlclose(dll)) {
			print_dlerror("dlclose");
		}
		grug_error("Retrieving resources with get_dll_symbol() failed for %s", dll_path);
	}

	for (size_t i = 0; i < dll_resources_size; i++) {
		const char *resource = dll_resources[i];

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

			grug_assert(strlen(resource) + 1 <= sizeof(modified.path), "The resource '%s' exceeds the maximum path length of %zu", resource, sizeof(modified.path));
			memcpy(modified.path, resource, strlen(resource) + 1);

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

static void regenerate_dll(const char *grug_path, const char *dll_path) {
	grug_log("# Regenerating %s\n", dll_path);

	grug_loading_error_in_grug_file = true;

	read_file(grug_path);
	grug_log("\n# Read text\n%s", grug_text);

	tokenize();
	grug_log("\n# Tokens\n");
#ifdef LOGGING
	print_tokens();
#else
	(void)print_tokens;
#endif

	parse();
	fill_result_types();

	compile(grug_path);

	grug_log("\n# Section offsets\n");
	generate_shared_object(dll_path);

	grug_loading_error_in_grug_file = false;
}

// Resetting previous_grug_error is necessary for this edge case:
// 1. Add a typo to a mod, causing a compilation error
// 2. Remove the typo, causing it to compile again
// 3. Add the exact same typo to the same line; we want this to show the earlier error again
static void reset_previous_grug_error(void) {
	previous_grug_error.msg[0] = '\0';
	previous_grug_error.path[0] = '\0';
	previous_grug_error.grug_c_line_number = 0;
}

static void initialize_file_entity_type(const char *grug_filename) {
	const char *dash = strchr(grug_filename, '-');

	grug_assert(dash && dash[1] != '\0', "'%s' is missing an entity type in its name; use a dash to specify it, like 'ak47-gun.grug'", grug_filename);

	const char *period = strchr(dash + 1, '.');
	grug_assert(period, "'%s' is missing a period in its filename", grug_filename);

	// "foo-.grug" has an entity_type_len of 0
	size_t entity_type_len = period - dash - 1;
	grug_assert(entity_type_len > 0, "'%s' is missing an entity type in its name; use a dash to specify it, like 'ak47-gun.grug'", grug_filename);

	grug_assert(entity_type_len < MAX_FILE_ENTITY_TYPE_LENGTH, "There are more than %d characters in the entity type of '%s', exceeding MAX_FILE_ENTITY_TYPE_LENGTH", MAX_FILE_ENTITY_TYPE_LENGTH, grug_filename);
	memcpy(file_entity_type, dash + 1, entity_type_len);
	file_entity_type[entity_type_len] = '\0';

	check_custom_id_is_pascal(file_entity_type);
}

static void set_grug_error_path(const char *grug_path) {
	// Since grug_error.path is the maximum path length of operating systems,
	// it shouldn't be possible for grug_path to exceed it
	assert(strlen(grug_path) + 1 <= sizeof(grug_error.path));

	memcpy(grug_error.path, grug_path, strlen(grug_path) + 1);
}

// This function just exists for the grug-tests repository
// It returns whether an error occurred
USED_BY_PROGRAMS bool grug_test_regenerate_dll(const char *grug_path, const char *dll_path, const char *mod_name);
bool grug_test_regenerate_dll(const char *grug_path, const char *dll_path, const char *mod_name) {
	assert(is_grug_initialized && "You forgot to call grug_init() once at program startup!");

	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	mod = mod_name;

	grug_loading_error_in_grug_file = false;

	set_grug_error_path(grug_path);

	const char *grug_filename = strrchr(grug_path, '/');
	grug_assert(grug_filename, "The grug file path '%s' does not contain a '/' character", grug_path);
	initialize_file_entity_type(grug_filename + 1);

	regenerate_dll(grug_path, dll_path);

	reset_previous_grug_error();

	return false;
}

static void try_create_parent_dirs(const char *file_path) {
	char parent_dir_path[STUPID_MAX_PATH];
	size_t i = 0;

	errno = 0;
	while (*file_path) {
		parent_dir_path[i] = *file_path;
		parent_dir_path[i + 1] = '\0';

		if (*file_path == '/' || *file_path == '\\') {
			grug_assert(mkdir(parent_dir_path, 0775) != -1 || errno == EEXIST, "mkdir: %s", strerror(errno));
		}

		file_path++;
		i++;
	}
}

static void free_file(struct grug_file file) {
	free((void *)file.name);
	free((void *)file.entity);
	free((void *)file.entity_type);

	if (file.dll && dlclose(file.dll)) {
		print_dlerror("dlclose");
	}

	free(file._resource_mtimes);
}

static void free_dir(struct grug_mod_dir dir) {
	free((void *)dir.name);

	for (size_t i = 0; i < dir.dirs_size; i++) {
		free_dir(dir.dirs[i]);
	}
	free(dir.dirs);

	for (size_t i = 0; i < dir.files_size; i++) {
		free_file(dir.files[i]);
	}
	free(dir.files);
}

static u32 get_entity_index(const char *entity) {
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

struct grug_file *grug_get_entity_file(const char *entity) {
	u32 index = get_entity_index(entity);
	if (index == UINT32_MAX) {
		return NULL;
	}
	return &entity_files[index];
}

static void check_that_every_entity_exists(struct grug_mod_dir dir) {
	for (size_t i = 0; i < dir.files_size; i++) {
		struct grug_file file = dir.files[i];

		size_t *entities_size_ptr = get_dll_symbol(file.dll, "entities_size");
		grug_assert(entities_size_ptr, "Retrieving the entities_size variable with get_dll_symbol() failed for '%s'", file.name);

		if (*entities_size_ptr > 0) {
			const char **dll_entities = get_dll_symbol(file.dll, "entities");
			grug_assert(entities_size_ptr, "Retrieving the dll_entities variable with get_dll_symbol() failed for '%s'", file.name);

			const char **dll_entity_types = get_dll_symbol(file.dll, "entity_types");
			grug_assert(entities_size_ptr, "Retrieving the dll_entity_types variable with get_dll_symbol() failed for '%s'", file.name);

			for (size_t dll_entity_index = 0; dll_entity_index < *entities_size_ptr; dll_entity_index++) {
				const char *entity = dll_entities[dll_entity_index];

				u32 entity_index = get_entity_index(entity);

				grug_assert(entity_index != UINT32_MAX, "The entity '%s' does not exist", entity);

				const char *json_entity_type = dll_entity_types[dll_entity_index];

				struct grug_file other_file = entity_files[entity_index];

				grug_assert(*json_entity_type == '\0' || streq(other_file.entity_type, json_entity_type), "The entity '%s' has the type '%s', whereas the expected type from mod_api.json is '%s'", entity, other_file.entity_type, json_entity_type);
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

// Returns `mod + ':' + grug_filename - "-<entity type>.grug"`
static const char *form_entity(const char *grug_filename) {
	static char entity_name[MAX_ENTITY_NAME_LENGTH];

	const char *dash = strrchr(grug_filename, '-');
	if (dash == NULL) {
		// The function initialize_file_entity_type() already checked for a missing dash
		grug_unreachable();
	}

	size_t entity_name_length = dash - grug_filename;

	grug_assert(entity_name_length < MAX_ENTITY_NAME_LENGTH, "There are more than %d entity name characters in the grug filename '%s', exceeding MAX_ENTITY_NAME_LENGTH", MAX_ENTITY_NAME_LENGTH, grug_filename);
	memcpy(entity_name, grug_filename, entity_name_length);
	entity_name[entity_name_length] = '\0';

	static char entity[MAX_ENTITY_NAME_LENGTH];
	grug_assert(snprintf(entity, sizeof(entity), "%s:%s", mod, entity_name) >= 0, "Filling the variable 'entity' failed");

	size_t entity_length = strlen(entity);

	grug_assert(entity_strings_size + entity_length < MAX_ENTITY_STRINGS_CHARACTERS, "There are more than %d characters in the entity_strings array, exceeding MAX_ENTITY_STRINGS_CHARACTERS", MAX_ENTITY_STRINGS_CHARACTERS);

	char *entity_str = entity_strings + entity_strings_size;

	memcpy(entity_str, entity, entity_length);
	entity_strings_size += entity_length;
	entity_strings[entity_strings_size++] = '\0';

	return entity_str;
}

static void add_entity(const char *grug_filename, struct grug_file *file) {
	grug_assert(entities_size < MAX_ENTITIES, "There are more than %d entities, exceeding MAX_ENTITIES", MAX_ENTITIES);

	const char *entity = form_entity(grug_filename);

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
	if (dir->files_size >= dir->_files_capacity) {
		dir->_files_capacity = dir->_files_capacity == 0 ? 1 : dir->_files_capacity * 2;
		dir->files = realloc(dir->files, dir->_files_capacity * sizeof(*dir->files));
		grug_assert(dir->files, "realloc: %s", strerror(errno));
	}
	dir->files[dir->files_size] = file;
	return &dir->files[dir->files_size++];
}

static struct grug_mod_dir *push_subdir(struct grug_mod_dir *dir, struct grug_mod_dir subdir) {
	if (dir->dirs_size >= dir->_dirs_capacity) {
		dir->_dirs_capacity = dir->_dirs_capacity == 0 ? 1 : dir->_dirs_capacity * 2;
		dir->dirs = realloc(dir->dirs, dir->_dirs_capacity * sizeof(*dir->dirs));
		grug_assert(dir->dirs, "realloc: %s", strerror(errno));
	}
	dir->dirs[dir->dirs_size] = subdir;
	return &dir->dirs[dir->dirs_size++];
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static struct grug_file *get_file(struct grug_mod_dir *dir, const char *name) {
	for (size_t i = 0; i < dir->files_size; i++) {
		if (streq(dir->files[i].name, name)) {
			return dir->files + i;
		}
	}
	return NULL;
}

// Profiling may indicate that rewriting this to use an O(1) technique like a hash table is worth it
static struct grug_mod_dir *get_subdir(struct grug_mod_dir *dir, const char *name) {
	for (size_t i = 0; i < dir->dirs_size; i++) {
		if (streq(dir->dirs[i].name, name)) {
			return dir->dirs + i;
		}
	}
	return NULL;
}

static struct grug_file *regenerate_file(struct grug_file *file, const char *dll_path, const char *grug_filename, struct grug_mod_dir *dir) {
	struct grug_file new_file = {0};

	new_file.dll = dlopen(dll_path, RTLD_NOW);
	if (!new_file.dll) {
		print_dlerror("dlopen");
	}

	size_t *globals_size_ptr = get_dll_symbol(new_file.dll, "globals_size");
	grug_assert(globals_size_ptr, "Retrieving the globals_size variable with get_dll_symbol() failed for %s", dll_path);
	new_file.globals_size = *globals_size_ptr;

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"
	new_file.init_globals_fn = get_dll_symbol(new_file.dll, "init_globals");
	#pragma GCC diagnostic pop
	grug_assert(new_file.init_globals_fn, "Retrieving the init_globals() function with get_dll_symbol() failed for %s", dll_path);

	// on_fns is optional, so don't check for NULL
	// Note that if an entity in mod_api.json specifies that it has on_fns that the modder can use,
	// on_fns is guaranteed NOT to be NULL!
	new_file.on_fns = get_dll_symbol(new_file.dll, "on_fns");

	size_t *resources_size_ptr = get_dll_symbol(new_file.dll, "resources_size");
	size_t dll_resources_size = *resources_size_ptr;

	if (file) {
		file->dll = new_file.dll;
		file->globals_size = new_file.globals_size;
		file->init_globals_fn = new_file.init_globals_fn;
		file->on_fns = new_file.on_fns;

		if (dll_resources_size > 0) {
			file->_resource_mtimes = realloc(file->_resource_mtimes, dll_resources_size * sizeof(i64));
			grug_assert(file->_resource_mtimes, "realloc: %s", strerror(errno));
		} else {
			// We can't use realloc() to do this
			// See https://stackoverflow.com/a/16760080/13279557
			free(file->_resource_mtimes);
			file->_resource_mtimes = NULL;
		}
	} else {
		new_file.name = strdup(grug_filename);
		grug_assert(new_file.name, "strdup: %s", strerror(errno));

		new_file.entity = strdup(form_entity(grug_filename));
		grug_assert(new_file.entity, "strdup: %s", strerror(errno));

		new_file.entity_type = strdup(file_entity_type);
		grug_assert(new_file.entity_type, "strdup: %s", strerror(errno));

		// We check dll_resources_size > 0, since whether malloc(0) returns NULL is implementation defined
		// See https://stackoverflow.com/a/1073175/13279557
		if (dll_resources_size > 0) {
			new_file._resource_mtimes = malloc(dll_resources_size * sizeof(i64));
			grug_assert(new_file._resource_mtimes, "malloc: %s", strerror(errno));
		}

		file = push_file(dir, new_file);
	}

	if (dll_resources_size > 0) {
		const char **dll_resources = get_dll_symbol(file->dll, "resources");

		// Initialize file->_resource_mtimes
		for (size_t i = 0; i < dll_resources_size; i++) {
			struct stat resource_stat;
			grug_assert(stat(dll_resources[i], &resource_stat) == 0, "%s: %s", dll_resources[i], strerror(errno));

			file->_resource_mtimes[i] = resource_stat.st_mtime;
		}
	}

	return file;
}

static void reload_grug_file(const char *dll_entry_path, i64 grug_file_mtime, const char *grug_filename, struct grug_mod_dir *dir, const char *grug_path) {
	initialize_file_entity_type(grug_filename);

	// Fill dll_path
	char dll_path[STUPID_MAX_PATH];
	grug_assert(strlen(dll_entry_path) + 1 <= STUPID_MAX_PATH, "There are more than %d characters in the dll_entry_path '%s', exceeding STUPID_MAX_PATH", STUPID_MAX_PATH, dll_entry_path);
	memcpy(dll_path, dll_entry_path, strlen(dll_entry_path) + 1);

	// Cast is safe because it indexes into stack-allocated memory
	char *extension = (char *)get_file_extension(dll_path);

	// The code that called this reload_grug_file() function has already checked
	// that the file ends with ".grug", so '.' will always be found here
	assert(extension[0] == '.');

	// We know that there's enough space, since ".so" is shorter than ".grug"
	memcpy(extension + 1, "so", sizeof("so"));

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
	bool needs_regeneration = !dll_exists || grug_file_mtime > dll_stat.st_mtime;

	struct grug_file *file = get_file(dir, grug_filename);

	if (needs_regeneration || !file) {
		struct grug_modified modified = {0};

		set_grug_error_path(grug_path);

		if (needs_regeneration) {
			regenerate_dll(grug_path, dll_path);
		}

		if (file && file->dll) {
			modified.old_dll = file->dll;

			// This dlclose() needs to happen after the regenerate_dll() call,
			// since even if regenerate_dll() throws when a typo is introduced to a mod,
			// we want to keep the pre-typo DLL version open so the game doesn't crash
			//
			// This dlclose() needs to happen before the upcoming dlopen() call,
			// since the DLL won't be reloaded otherwise
			if (dlclose(file->dll)) {
				print_dlerror("dlclose");
			}

			// Not necessary, but makes debugging less confusing
			file->dll = NULL;
		}

		file = regenerate_file(file, dll_path, grug_filename, dir);

		// Let the game developer know that a grug file was recompiled
		if (needs_regeneration) {
			// Since modified.path is the maximum path length of operating systems,
			// it shouldn't be possible for grug_path to exceed it
			assert(strlen(grug_path) + 1 <= sizeof(modified.path));

			memcpy(modified.path, grug_path, strlen(grug_path) + 1);

			modified.file = *file;
			push_reload(modified);
		}
	}

	file->_seen = true;

	// Needed for grug_get_entitity_file() and check_that_every_entity_exists()
	add_entity(grug_filename, file);

	// Let the game developer know when they need to reload a resource
	if (file->_resources_size > 0) {
		reload_resources_from_dll(dll_path, file->_resource_mtimes, file->_resources_size);
	}
}

static void reload_modified_mod(const char *mods_dir_path, const char *dll_dir_path, struct grug_mod_dir *dir);

static void reload_entry(const char *name, const char *mods_dir_path, const char *dll_dir_path, struct grug_mod_dir *dir) {
	if (streq(name, ".") || streq(name, "..")) {
		return;
	}

	char entry_path[STUPID_MAX_PATH];
	snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_dir_path, name);

	char dll_entry_path[STUPID_MAX_PATH];
	snprintf(dll_entry_path, sizeof(dll_entry_path), "%s/%s", dll_dir_path, name);

	struct stat entry_stat;
	grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

	if (S_ISDIR(entry_stat.st_mode)) {
		struct grug_mod_dir *subdir = get_subdir(dir, name);

		if (!subdir) {
			struct grug_mod_dir inserted_subdir = {.name = strdup(name)};
			grug_assert(inserted_subdir.name, "strdup: %s", strerror(errno));
			subdir = push_subdir(dir, inserted_subdir);
		}

		subdir->_seen = true;

		reload_modified_mod(entry_path, dll_entry_path, subdir);
	} else if (S_ISREG(entry_stat.st_mode) && streq(get_file_extension(name), ".grug")) {
		reload_grug_file(dll_entry_path, entry_stat.st_mtime, name, dir, entry_path);
	}
}

static void reload_modified_mod(const char *mods_dir_path, const char *dll_dir_path, struct grug_mod_dir *dir) {
	directory_depth++;
	grug_assert(directory_depth < MAX_DIRECTORY_DEPTH, "There is a mod that contains more than %d levels of nested directories", MAX_DIRECTORY_DEPTH);

	DIR *dirp = opendir(mods_dir_path);
	grug_assert(dirp, "opendir(\"%s\"): %s", mods_dir_path, strerror(errno));

	for (size_t i = 0; i < dir->dirs_size; i++) {
		dir->dirs[i]._seen = false;
	}
	for (size_t i = 0; i < dir->files_size; i++) {
		dir->files[i]._seen = false;
	}

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		reload_entry(dp->d_name, mods_dir_path, dll_dir_path, dir);
	}
	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);

	// If the directory used to contain a subdirectory or file
	// that doesn't exist anymore, free it
	for (size_t i = dir->dirs_size; i > 0;) {
		i--;
		if (!dir->dirs[i]._seen) {
			free_dir(dir->dirs[i]);
			dir->dirs[i] = dir->dirs[--dir->dirs_size]; // Swap-remove
		}
	}
	for (size_t i = dir->files_size; i > 0;) {
		i--;
		if (!dir->files[i]._seen) {
			free_file(dir->files[i]);
			dir->files[i] = dir->files[--dir->files_size]; // Swap-remove
		}
	}

	assert(directory_depth > 0);
	directory_depth--;
}

static bool validate_about_file(const char *about_json_path) {
  // returns false if the about file dosent exist, raises a grug error if the about.json is invalid
	if (access(about_json_path, F_OK)) {
  	errno = 0;
  	return false;
	}

	struct json_node node;
	json(about_json_path, &node);

	grug_assert(node.type == JSON_NODE_OBJECT, "%s its root must be an object", about_json_path);
	struct json_object root_object = node.object;

	grug_assert(root_object.field_count >= 4, "%s must have at least these 4 fields, in this order: \"name\", \"version\", \"game_version\", \"author\"", about_json_path);

	struct json_field *field = root_object.fields;

	grug_assert(streq(field->key, "name"), "%s its root object must have \"name\" as its first field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"name\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"name\" field value must not be an empty string", about_json_path);
	field++;

	grug_assert(streq(field->key, "version"), "%s its root object must have \"version\" as its second field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"version\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"version\" field value must not be an empty string", about_json_path);
	field++;

	grug_assert(streq(field->key, "game_version"), "%s its root object must have \"game_version\" as its third field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"game_version\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"game_version\" field value must not be an empty string", about_json_path);
	field++;

	grug_assert(streq(field->key, "author"), "%s its root object must have \"author\" as its fourth field", about_json_path);
	grug_assert(field->value->type == JSON_NODE_STRING, "%s its \"author\" field must have a string as its value", about_json_path);
	grug_assert(!streq(field->value->string, ""), "%s its \"author\" field value must not be an empty string", about_json_path);
	field++;

	for (size_t i = 4; i < root_object.field_count; i++) {
		grug_assert(!streq(field->key, ""), "%s its %zuth field key must not be an empty string", about_json_path, i + 1);
		field++;
	}

	return true;
}

// Cases:
// 1. "" => ""
// 2. "/" => ""
// 3. "/a" => "a"
// 4. "/a/" => ""
// 5. "/a/b" => "b"
static const char *get_basename(const char *path) {
	const char *base = strrchr(path, '/');
	return base ? base + 1 : path;
}

static char entry_path[STUPID_MAX_PATH];
static char dll_entry_path[STUPID_MAX_PATH];

static void reload_modified_mods_dir(char *mods_root_path, char *dll_root_path, struct grug_mod_dir *dir) {
  if (mods_root_path != entry_path) { // this is a pointer comparison, sets up entry_path if needed
    grug_assert(snprintf(entry_path, sizeof(entry_path), "%s", mods_root_path) >= 0, "Filling the variable 'entry_path' failed");
  }

  if (dll_root_path != dll_entry_path) { // this is a pointer comparison, sets up entry_path if needed
    grug_assert(snprintf(dll_entry_path, sizeof(dll_entry_path), "%s", dll_root_path) >= 0, "Filling the variable 'entry_path' failed");
  }

	DIR *dirp = opendir(mods_root_path);
	grug_assert(dirp, "opendir(\"%s\"): %s", mods_root_path, strerror(errno));

	for (size_t i = 0; i < dir->dirs_size; i++) {
		dir->dirs[i]._seen = false;
	}

	errno = 0;
	struct dirent *dp;
	while ((dp = readdir(dirp))) {
		const char *name = /dp->d_name;

		if (streq(name, ".") || streq(name, "..")) {
			continue;
		}

		// static char entry_path[STUPID_MAX_PATH];
		// grug_assert(snprintf(entry_path, sizeof(entry_path), "%s/%s", mods_root_path, name) >= 0, "Filling the variable 'entry_path' failed");

		int entry_start = strlen(entry_path);
		grug_assert(snprintf(&entry_path[entry_start], sizeof(entry_path) - entry_start, "/%s", name) >= 0, "Filling the variable 'entry_path' failed");

		struct stat entry_stat;
		grug_assert(stat(entry_path, &entry_stat) == 0, "stat: %s: %s", entry_path, strerror(errno));

		int dll_entry_start = strlen(dll_entry_path);
		grug_assert(snprintf(&dll_entry_path[dll_entry_start], sizeof(dll_entry_path) - dll_entry_start, "/%s", name) >= 0, "Filling the variable 'entry_path' failed");

		if (S_ISDIR(entry_stat.st_mode)) {
			static char about_json_path[STUPID_MAX_PATH];
			grug_assert(snprintf(about_json_path, sizeof(about_json_path), "%s/about.json", entry_path) >= 0, "Filling the variable 'about_json_path' failed");

 			// This always returns NULL during the first call of reload_modified_mods()
 			struct grug_mod_dir *subdir = get_subdir(dir, name);

 			if (!subdir) {
  			struct grug_mod_dir inserted_subdir = { .name = strdup(entry_path) };
  			grug_assert(inserted_subdir.name, "strdup: %s", strerror(errno));
  			subdir = push_subdir(dir, inserted_subdir);
 			}

			if ((subdir->is_mod = validate_about_file(about_json_path))) {
			  mod = name;

			  printf("%s\n", about_json_path);

   			subdir->_seen = true;
   			reload_modified_mod(entry_path, dll_entry_path, subdir);
			} else {
   			subdir->_seen = true;
			  reload_modified_mods_dir(entry_path, dll_entry_path, subdir);
			}

      if (!subdir->is_mod) {
        grug_assert(subdir->files_size == 0, "Grug files must be contained in a valid mod directory, however no parent of '%s' has an about.json", entry_path)
      }
		} else if (S_ISREG(entry_stat.st_mode)) {
		  grug_assert(!streq(get_file_extension(entry_path), ".grug"), "Grug files must be contained in a valid mod directory, however no parent of '%s' has an about.json", entry_path)
		}

		entry_path[entry_start] = 0;
		dll_entry_path[dll_entry_start] = 0;
	}

	grug_assert(errno == 0, "readdir: %s", strerror(errno));

	closedir(dirp);

	// If the directory used to contain a mod that doesn't exist anymore, free it
	for (size_t i = dir->dirs_size; i > 0;) {
		i--;
		if (!dir->dirs[i]._seen) {
			free_dir(dir->dirs[i]);
			dir->dirs[i] = dir->dirs[--dir->dirs_size]; // Swap-remove
		}
	}
}

static void reload_modified_mods(void) {
  entry_path[0] = 0;
	reload_modified_mods_dir(mods_root_dir_path, dll_root_dir_path, &grug_mods);
}

bool grug_init(grug_runtime_error_handler_t handler, const char *mod_api_json_path, const char *mods_dir_path, const char *dll_dir_path, uint64_t on_fn_time_limit_ms_) {
	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	assert(handler && "grug_init() its grug_runtime_error_handler can't be NULL");
	grug_runtime_error_handler = handler;

	assert(!is_grug_initialized && "grug_init() can't be called more than once");

	assert(!strchr(mods_dir_path, '\\') && "grug_init() its mods_dir_path can't contain backslashes, so replace them with '/'");
	assert(mods_dir_path[strlen(mods_dir_path) - 1] != '/' && "grug_init() its mods_dir_path can't have a trailing '/'");

	assert(!strchr(dll_dir_path, '\\') && "grug_init() its dll_dir_path can't contain backslashes, so replace them with '/'");
	assert(dll_dir_path[strlen(dll_dir_path) - 1] != '/' && "grug_init() its dll_dir_path can't have a trailing '/'");

	parse_mod_api_json(mod_api_json_path);

	assert(strlen(mods_dir_path) + 1 <= STUPID_MAX_PATH && "grug_init() its mods_dir_path exceeds the maximum path length");
	memcpy(mods_root_dir_path, mods_dir_path, strlen(mods_dir_path) + 1);

	assert(strlen(dll_dir_path) + 1 <= STUPID_MAX_PATH && "grug_init() its dll_dir_path exceeds the maximum path length");
	memcpy(dll_root_dir_path, dll_dir_path, strlen(dll_dir_path) + 1);

	on_fn_time_limit_ms = on_fn_time_limit_ms_;
	on_fn_time_limit_sec = on_fn_time_limit_ms / MS_PER_SEC;
	on_fn_time_limit_ns = (on_fn_time_limit_ms % MS_PER_SEC) * NS_PER_MS;

	is_grug_initialized = true;

	return false;
}

bool grug_regenerate_modified_mods(void) {
	assert(is_grug_initialized && "You forgot to call grug_init() once at program startup!");

	if (setjmp(error_jmp_buffer)) {
		return true;
	}

	reset_regenerate_modified_mods();

	grug_loading_error_in_grug_file = false;

	if (!grug_mods.name) {
		grug_mods.name = strdup(get_basename(mods_root_dir_path));
		grug_assert(grug_mods.name, "strdup: %s", strerror(errno));
	}

	reload_modified_mods();

	check_that_every_entity_exists(grug_mods);

	reset_previous_grug_error();

	return false;
}

USED_BY_MODS bool grug_has_runtime_error_happened = false;
void grug_game_function_error_happened(const char *message) {
	grug_has_runtime_error_happened = true;
	snprintf(runtime_error_reason, sizeof(runtime_error_reason), "%s", message);
}

USED_BY_MODS bool grug_on_fns_in_safe_mode = true;
void grug_set_on_fns_to_safe_mode(void) {
	grug_on_fns_in_safe_mode = true;
}
void grug_set_on_fns_to_fast_mode(void) {
	grug_on_fns_in_safe_mode = false;
}
bool grug_are_on_fns_in_safe_mode(void) {
	return grug_on_fns_in_safe_mode;
}
void grug_toggle_on_fns_mode(void) {
	grug_on_fns_in_safe_mode = !grug_on_fns_in_safe_mode;
}
