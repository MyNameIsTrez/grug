#include "05_json.c"

//// PARSING MOD API JSON

#define MAX_GRUG_ENTITIES 420420
#define MAX_GRUG_ON_FUNCTIONS 420420
#define MAX_GRUG_GAME_FUNCTIONS 420420
#define MAX_GRUG_ARGUMENTS 420420

static struct grug_entity grug_entities[MAX_GRUG_ENTITIES];
static size_t grug_entities_size;

static struct grug_on_function grug_on_functions[MAX_GRUG_ON_FUNCTIONS];
static size_t grug_on_functions_size;

static struct grug_game_function grug_game_functions[MAX_GRUG_GAME_FUNCTIONS];
static size_t grug_game_functions_size;
static u32 buckets_game_fns[MAX_GRUG_GAME_FUNCTIONS];
static u32 chains_game_fns[MAX_GRUG_GAME_FUNCTIONS];

static struct argument grug_arguments[MAX_GRUG_ARGUMENTS];
static size_t grug_arguments_size;

static char mod_api_strings[JSON_MAX_STRINGS_CHARACTERS];
static size_t mod_api_strings_size;

static void push_grug_entity(struct grug_entity fn) {
	grug_assert(grug_entities_size < MAX_GRUG_ENTITIES, "There are more than %d entities in mod_api.json, exceeding MAX_GRUG_ENTITIES", MAX_GRUG_ENTITIES);
	grug_entities[grug_entities_size++] = fn;
}

static void push_grug_on_function(struct grug_on_function fn) {
	grug_assert(grug_on_functions_size < MAX_GRUG_ON_FUNCTIONS, "There are more than %d on_ functions in mod_api.json, exceeding MAX_GRUG_ON_FUNCTIONS", MAX_GRUG_ON_FUNCTIONS);
	grug_on_functions[grug_on_functions_size++] = fn;
}

USED_BY_PROGRAMS struct grug_game_function *get_grug_game_fn(const char *name) {
	if (grug_game_functions_size == 0) {
		return NULL;
	}

	u32 i = buckets_game_fns[elf_hash(name) % grug_game_functions_size];

	while (true) {
		if (i == UINT32_MAX) {
			return NULL;
		}

		if (streq(name, grug_game_functions[i].name)) {
			break;
		}

		i = chains_game_fns[i];
	}

	return grug_game_functions + i;
}

static void hash_game_fns(void) {
	memset(buckets_game_fns, 0xff, grug_game_functions_size * sizeof(u32));

	for (size_t i = 0; i < grug_game_functions_size; i++) {
		const char *name = grug_game_functions[i].name;

		u32 bucket_index = elf_hash(name) % grug_game_functions_size;

		chains_game_fns[i] = buckets_game_fns[bucket_index];

		buckets_game_fns[bucket_index] = i;
	}
}

static void push_grug_game_function(struct grug_game_function fn) {
	grug_assert(grug_game_functions_size < MAX_GRUG_GAME_FUNCTIONS, "There are more than %d game functions in mod_api.json, exceeding MAX_GRUG_GAME_FUNCTIONS", MAX_GRUG_GAME_FUNCTIONS);
	grug_game_functions[grug_game_functions_size++] = fn;
}

static void push_grug_argument(struct argument argument) {
	grug_assert(grug_arguments_size < MAX_GRUG_ARGUMENTS, "There are more than %d grug arguments, exceeding MAX_GRUG_ARGUMENTS", MAX_GRUG_ARGUMENTS);
	grug_arguments[grug_arguments_size++] = argument;
}

static void check_custom_id_is_pascal(const char *type_name) {
	// The first character must always be uppercase.
	grug_assert(isupper(type_name[0]), "'%s' seems like a custom ID type, but isn't in PascalCase", type_name);

	// Custom IDs only consist of uppercase and lowercase characters.
	for (const char *p = type_name; *p; p++) {
		char c = *p;
		grug_assert(isupper(c) || islower(c) || isdigit(c), "'%s' seems like a custom ID type, but it contains '%c', which isn't uppercase/lowercase/a digit", type_name, c);
	}
}

static void check_custom_id_type_capitalization(const char *type_name) {
	// If it is not a custom ID, return.
	if (streq(type_name, "bool")
	 || streq(type_name, "i32")
	 || streq(type_name, "f32")
	 || streq(type_name, "string")
	 || streq(type_name, "resource")
	 || streq(type_name, "entity")
	 || streq(type_name, "id")) {
		return;
	}

	check_custom_id_is_pascal(type_name);
}

static const char *push_mod_api_string(const char *old_str) {
	size_t length = strlen(old_str);

	grug_assert(mod_api_strings_size + length < JSON_MAX_STRINGS_CHARACTERS, "There are more than %d characters in the mod_api_strings array, exceeding JSON_MAX_STRINGS_CHARACTERS", JSON_MAX_STRINGS_CHARACTERS);

	const char *new_str = mod_api_strings + mod_api_strings_size;

	memcpy(mod_api_strings + mod_api_strings_size, old_str, length + 1);
	mod_api_strings_size += length + 1;

	return new_str;
}

static enum type parse_type(const char *type) {
	if (streq(type, "bool")) {
		return type_bool;
	}
	if (streq(type, "i32")) {
		return type_i32;
	}
	if (streq(type, "f32")) {
		return type_f32;
	}
	if (streq(type, "string")) {
		return type_string;
	}
	if (streq(type, "resource")) {
		return type_resource;
	}
	if (streq(type, "entity")) {
		return type_entity;
	}
	return type_id;
}

static void init_game_fns(struct json_object fns) {
	for (size_t fn_index = 0; fn_index < fns.field_count; fn_index++) {
		struct grug_game_function grug_fn = {0};

		grug_fn.name = push_mod_api_string(fns.fields[fn_index].key);
		grug_assert(!streq(grug_fn.name, ""), "\"game_functions\" its function names must not be an empty string");
		grug_assert(!starts_with(grug_fn.name, "on_"), "\"game_functions\" its function names must not start with 'on_'");

		grug_assert(fns.fields[fn_index].value->type == JSON_NODE_OBJECT, "\"game_functions\" its array must only contain objects");
		struct json_object fn = fns.fields[fn_index].value->object;
		grug_assert(fn.field_count >= 1, "\"game_functions\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 3, "\"game_functions\" its objects must not have more than 3 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"game_functions\" its functions must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"game_functions\" its function descriptions must be strings");
		const char *description = push_mod_api_string(field->value->string);
		grug_assert(!streq(description, ""), "\"game_functions\" its function descriptions must not be an empty string");

		bool seen_return_type = false;

		if (fn.field_count > 1) {
			field++;

			if (streq(field->key, "return_type")) {
				grug_assert(field->value->type == JSON_NODE_STRING, "\"game_functions\" its function return types must be strings");
				grug_fn.return_type = parse_type(field->value->string);
				grug_fn.return_type_name = push_mod_api_string(field->value->string);
				check_custom_id_type_capitalization(grug_fn.return_type_name);
				grug_assert(grug_fn.return_type != type_resource, "\"game_functions\" its function return types must not be 'resource'");
				grug_assert(grug_fn.return_type != type_entity, "\"game_functions\" its function return types must not be 'entity'");
				seen_return_type = true;
				field++;
			} else {
				grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second field was something other than \"return_type\" and \"arguments\"");
			}
		} else {
			grug_fn.return_type = type_void;
		}

		if ((!seen_return_type && fn.field_count > 1) || fn.field_count > 2) {
			grug_assert(streq(field->key, "arguments"), "\"game_functions\" its second or third field was something other than \"arguments\"");

			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"game_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->array.value_count;
			grug_assert(grug_fn.argument_count > 0, "\"game_functions\" its \"arguments\" array must not be empty (just remove the \"arguments\" key entirely)");

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct argument grug_arg = {0};

				grug_assert(value->type == JSON_NODE_OBJECT, "\"game_functions\" its function arguments must only contain objects");
				grug_assert(value->object.field_count >= 2, "\"game_functions\" must have the function argument fields \"name\" and \"type\"");
				grug_assert(value->object.field_count <= 3, "\"game_functions\" its function arguments can't have more than 3 fields");
				struct json_field *argument_field = value->object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"game_functions\" its function arguments must always have \"name\" as their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.name = push_mod_api_string(argument_field->value->string);
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"game_functions\" its function arguments must always have \"type\" as their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->string);
				grug_arg.type_name = push_mod_api_string(argument_field->value->string);
				check_custom_id_type_capitalization(grug_arg.type_name);
				argument_field++;

				if (grug_arg.type == type_resource) {
					grug_assert(value->object.field_count == 3 && streq(argument_field->key, "resource_extension"), "\"game_functions\" its function arguments has a \"type\" field with the value \"resource\", which means a \"resource_extension\" field is required");
					grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function argument fields must always have string values");
					grug_arg.resource_extension = push_mod_api_string(argument_field->value->string);
				} else if (grug_arg.type == type_entity) {
					grug_assert(value->object.field_count == 3 && streq(argument_field->key, "entity_type"), "\"game_functions\" its function arguments has a \"type\" field with the value \"entity\", which means an \"entity_type\" field is required");
					grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"game_functions\" its function argument fields must always have string values");
					grug_arg.entity_type = push_mod_api_string(argument_field->value->string);
				} else {
					grug_assert(value->object.field_count == 2, "\"game_functions\" its function argument fields had an unexpected 3rd \"%s\" field", argument_field->key);
				}

				push_grug_argument(grug_arg);
				value++;
			}
		}

		push_grug_game_function(grug_fn);
	}

	hash_game_fns();
}

static void init_on_fns(struct json_object fns) {
	for (size_t fn_index = 0; fn_index < fns.field_count; fn_index++) {
		struct grug_on_function grug_fn = {0};

		grug_fn.name = push_mod_api_string(fns.fields[fn_index].key);
		grug_assert(!streq(grug_fn.name, ""), "\"on_functions\" its function names must not be an empty string");
		grug_assert(starts_with(grug_fn.name, "on_"), "\"on_functions\" its function names must start with 'on_'");

		grug_assert(fns.fields[fn_index].value->type == JSON_NODE_OBJECT, "\"on_functions\" its array must only contain objects");
		struct json_object fn = fns.fields[fn_index].value->object;
		grug_assert(fn.field_count >= 1, "\"on_functions\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 2, "\"on_functions\" its objects must not have more than 2 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"on_functions\" its functions must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"on_functions\" its function descriptions must be strings");
		const char *description = push_mod_api_string(field->value->string);
		grug_assert(!streq(description, ""), "\"on_functions\" its function descriptions must not be an empty string");

		if (fn.field_count > 1) {
			field++;

			grug_assert(streq(field->key, "arguments"), "\"on_functions\" its functions must have \"arguments\" as the second field");
			grug_assert(field->value->type == JSON_NODE_ARRAY, "\"on_functions\" its function arguments must be arrays");
			struct json_node *value = field->value->array.values;

			grug_fn.arguments = grug_arguments + grug_arguments_size;
			grug_fn.argument_count = field->value->array.value_count;

			for (size_t argument_index = 0; argument_index < grug_fn.argument_count; argument_index++) {
				struct argument grug_arg = {0};

				grug_assert(value->type == JSON_NODE_OBJECT, "\"on_functions\" its function arguments must only contain objects");
				grug_assert(value->object.field_count == 2, "\"on_functions\" its function arguments must only contain a name and type field");
				struct json_field *argument_field = value->object.fields;

				grug_assert(streq(argument_field->key, "name"), "\"on_functions\" its function arguments must always have \"name\" as their first field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.name = push_mod_api_string(argument_field->value->string);
				argument_field++;

				grug_assert(streq(argument_field->key, "type"), "\"on_functions\" its function arguments must always have \"type\" as their second field");
				grug_assert(argument_field->value->type == JSON_NODE_STRING, "\"on_functions\" its function arguments must always have string values");
				grug_arg.type = parse_type(argument_field->value->string);
				grug_arg.type_name = push_mod_api_string(argument_field->value->string);
				check_custom_id_type_capitalization(grug_arg.type_name);
				grug_assert(grug_arg.type != type_resource, "\"on_functions\" its function argument types must not be 'resource'");
				grug_assert(grug_arg.type != type_entity, "\"on_functions\" its function argument types must not be 'entity'");
				argument_field++;

				push_grug_argument(grug_arg);
				value++;
			}
		}

		push_grug_on_function(grug_fn);
	}
}

static void init_entities(struct json_object entities) {
	for (size_t entity_field_index = 0; entity_field_index < entities.field_count; entity_field_index++) {
		struct grug_entity entity = {0};

		entity.name = push_mod_api_string(entities.fields[entity_field_index].key);
		grug_assert(!streq(entity.name, ""), "\"entities\" its names must not be an empty string");
		check_custom_id_type_capitalization(entity.name);

		grug_assert(entities.fields[entity_field_index].value->type == JSON_NODE_OBJECT, "\"entities\" must only contain object values");
		struct json_object fn = entities.fields[entity_field_index].value->object;
		grug_assert(fn.field_count >= 1, "\"entities\" its objects must have at least a \"description\" field");
		grug_assert(fn.field_count <= 2, "\"entities\" its objects must not have more than 2 fields");

		struct json_field *field = fn.fields;

		grug_assert(streq(field->key, "description"), "\"entities\" must have \"description\" as the first field");
		grug_assert(field->value->type == JSON_NODE_STRING, "\"entities\" its descriptions must be strings");
		const char *description = push_mod_api_string(field->value->string);
		grug_assert(!streq(description, ""), "\"entities\" its descriptions must not be an empty string");

		if (fn.field_count > 1) {
			field++;
			grug_assert(streq(field->key, "on_functions"), "\"entities\" its second field was something other than \"on_functions\"");
			grug_assert(field->value->type == JSON_NODE_OBJECT, "\"entities\" its \"on_functions\" field must have an object as its value");
			entity.on_functions = grug_on_functions + grug_on_functions_size;
			entity.on_function_count = field->value->object.field_count;
			init_on_fns(field->value->object);
		}

		push_grug_entity(entity);
	}
}

static void parse_mod_api_json(const char *mod_api_json_path) {
	struct json_node node;
	json(mod_api_json_path, &node);

	grug_assert(node.type == JSON_NODE_OBJECT, "mod_api.json its root must be an object");
	struct json_object root_object = node.object;

	grug_assert(root_object.field_count == 2, "mod_api.json must only have these 2 fields, in this order: \"entities\", \"game_functions\"");

	struct json_field *field = root_object.fields;

	grug_assert(streq(field->key, "entities"), "mod_api.json its root object must have \"entities\" as its first field");
	grug_assert(field->value->type == JSON_NODE_OBJECT, "mod_api.json its \"entities\" field must have an object as its value");
	init_entities(field->value->object);
	field++;

	grug_assert(streq(field->key, "game_functions"), "mod_api.json its root object must have \"game_functions\" as its third field");
	grug_assert(field->value->type == JSON_NODE_OBJECT, "mod_api.json its \"game_functions\" field must have an object as its value");
	init_game_fns(field->value->object);
}
