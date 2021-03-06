/**

	File:		pplua.c

	Project:	DCPU-16 Toolchain
	Component:	LibDCPU-PP

	Authors:	James Rhodes

	Description:	Defines the public API for the preprocessor to load
			custom Lua modules.

**/

#include <assert.h>
#include <dirent.portable.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <bstring.h>
#include <simclist.h>
#include <osutil.h>
#include <ppexprlua.h>
#include <debug.h>
#include <stdlib.h>
#include "pplua.h"
#include "dcpu.h"

#define HANDLER_TABLE_NAME "__handlers"
#define HANDLER_FIELD_FUNCTION_NAME "function"

bool modules_online;
list_t modules;

struct lua_preproc
{
	lua_State* state;
};

size_t lua_preproc_meter(const void* el)
{
	return sizeof(struct lua_preproc);
}

int pp_lua_add_preprocessor_directive(lua_State* L)
{
	int handlers, entry;
	bstring lowered;
	lowered = bfromcstr(luaL_checkstring(L, 1));
	btolower(lowered);
	lua_getglobal(L, HANDLER_TABLE_NAME);
	handlers = lua_gettop(L);
	lua_newtable(L);
	entry = lua_gettop(L);
	lua_pushvalue(L, 2);
	lua_setfield(L, entry, HANDLER_FIELD_FUNCTION_NAME);
	// entry is now at the top of the stack.
	lua_setfield(L, handlers, lowered->data);
	printd(LEVEL_DEBUG, "registered preprocessor directive '%s' with custom Lua handler.\n", lowered->data);
	lua_pop(L, 1);
	bdestroy(lowered);
	return 0;
}

struct lua_preproc* pp_lua_load(bstring name)
{
	bstring path, modtype;
	struct lua_preproc* pp;
	int module;

	// Calculate full path to file.
	path = osutil_getmodulepath();
	bconchar(path, '/');
	bconcat(path, name);

	// Create the new lua preprocessor structure.
	pp = malloc(sizeof(struct lua_preproc));
	pp->state = lua_open();
	assert(pp->state != NULL);
	luaL_openlibs(pp->state);
	luaX_loadexpressionlib(pp->state);

	// Execute the code in the new Lua context.
	if (luaL_dofile(pp->state, path->data) != 0)
	{
		printd(LEVEL_ERROR, "lua error was %s.\n", lua_tostring(pp->state, -1));

		// Return NULL.
		lua_close(pp->state);
		free(pp);
		bdestroy(path);
		return NULL;
	}

	// Load tables.
	lua_getglobal(pp->state, "MODULE");
	module = lua_gettop(pp->state);

	// Ensure module table was provided.
	if (lua_isnoneornil(pp->state, module))
	{
		printd(LEVEL_ERROR, "failed to load preprocessor module from %s.\n", path->data);

		// Return NULL.
		lua_close(pp->state);
		free(pp);
		bdestroy(path);
		return NULL;
	}

	// Check to see whether the module is
	// a preprocessor module.
	lua_getfield(pp->state, module, "Type");
	modtype = bfromcstr(lua_tostring(pp->state, -1));
	if (!biseqcstrcaseless(modtype, "Preprocessor"))
	{
		// Return NULL.
		lua_pop(pp->state, 1);
		lua_close(pp->state);
		free(pp);
		bdestroy(modtype);
		bdestroy(path);
		return NULL;
	}
	lua_pop(pp->state, 1);
	bdestroy(modtype);

	// Create the handler table.
	lua_newtable(pp->state);
	lua_setglobal(pp->state, HANDLER_TABLE_NAME);

	// Set the global add_preprocessor_directive function.
	lua_pushcfunction(pp->state, &pp_lua_add_preprocessor_directive);
	lua_setglobal(pp->state, "add_preprocessor_directive");

	// Run the setup function.
	lua_getglobal(pp->state, "setup");
	if (lua_pcall(pp->state, 0, 0, 0) != 0)
	{
		printd(LEVEL_ERROR, "failed to run setup() in preprocessor module from %s.\n", path->data);
	}

	// Unset the add_preprocessor_directive function.
	lua_pushnil(pp->state);
	lua_setglobal(pp->state, "add_preprocessor_directive");

	// Pop tables from stack.
	lua_pop(pp->state, 2);

	// Return new preprocessor module.
	return pp;
}

void pp_lua_init()
{
	DIR* dir;
	struct dirent* entry;
	struct lua_preproc* ppm;
	bstring name = NULL;
	bstring modpath = NULL;
	bstring ext = bfromcstr(".lua");

	// Initialize lists.
	list_init(&modules);
	list_attributes_copy(&modules, &lua_preproc_meter, 1);

	// Get the module path.
	modpath = osutil_getmodulepath();
	if (modpath == NULL)
	{
		bdestroy(ext);
		return;
	}

	// Attempt to open the module directory.
	dir = opendir(modpath->data);
	if (dir == NULL)
	{
		// The directory does not exist, so we don't load
		// any custom preprocessor modules.
		modules_online = false;
		bdestroy(modpath);
		bdestroy(ext);
		return;
	}

	// Load each file from the hw directory.
	while ((entry = readdir(dir)) != NULL)
	{
		name = bfromcstr(&entry->d_name[0]);

		// Check to see whether it is a lua file.
		if (binstr(name, blength(name) - 4, ext) == BSTR_ERR)
		{
			// Not a Lua file, skip over and
			// then continue.
			bdestroy(name);
			continue;
		}

		// Check to see if it is a normal file.
#if defined(DT_REG)
		if (entry->d_type != DT_REG)
#elif defined(DT_DIR)
		if (entry->d_type == DT_DIR)
#elif defined(DT_UNKNOWN)
		if (entry->d_type == DT_UNKNOWN)
#else
#error Build system must support DT_REG, DT_DIR or DT_UNKNOWN in dirent.h.
#endif
		{
			// Not a file, skip over and then
			// continue.
			bdestroy(name);
			continue;
		}

		// Attempt to load the Lua file.
		printd(LEVEL_DEBUG, "loading custom preprocessor module from: %s\n", name->data);
		ppm = pp_lua_load(name);
		if (ppm != NULL)
			list_append(&modules, ppm);

		// Free data.
		bdestroy(name);
	}

	// Free resources.
	closedir(dir);
}

struct pp_state* pp_lua_extract_state(lua_State* L, int idx)
{
	struct pp_state* state = NULL;
	lua_getmetatable(L, idx);
	lua_rawgeti(L, lua_gettop(L), 0);
	if (!lua_isuserdata(L, -1))
	{
		lua_pushstring(L, "state must be first parameter to function, or use : operator.");
		lua_error(L);
	}
	state = (struct pp_state*)lua_touserdata(L, -1);
	lua_pop(L, 2);
	return state;
}

void* pp_lua_extract_state_ud(lua_State* L, int idx)
{
	void* ud = NULL;
	lua_getmetatable(L, idx);
	lua_rawgeti(L, lua_gettop(L), 1);
	if (!lua_isuserdata(L, -1))
	{
		lua_pushstring(L, "state must be first parameter to function, or use : operator.");
		lua_error(L);
	}
	ud = lua_touserdata(L, -1);
	lua_pop(L, 2);
	return ud;
}

int pp_lua_handle_print(lua_State* L)
{
	struct pp_state* state = pp_lua_extract_state(L, 1);
	void* ud = pp_lua_extract_state_ud(L, 1);
	state->pp_lua_print(luaL_checkstring(L, 2), ud);
	return 0;
}

int pp_lua_handle_print_line(lua_State* L)
{
	struct pp_state* state = pp_lua_extract_state(L, 1);
	void* ud = pp_lua_extract_state_ud(L, 1);
	state->pp_lua_print_line(luaL_checkstring(L, 2), ud);
	return 0;
}

int pp_lua_handle_scope_enter(lua_State* L)
{
	struct pp_state* state = pp_lua_extract_state(L, 1);
	void* ud = pp_lua_extract_state_ud(L, 1);
	state->pp_lua_scope_enter(lua_toboolean(L, 2), ud);
	return 0;
}

int pp_lua_handle_scope_exit(lua_State* L)
{
	struct pp_state* state = pp_lua_extract_state(L, 1);
	void* ud = pp_lua_extract_state_ud(L, 1);
	state->pp_lua_scope_exit(ud);
	return 0;
}

int pp_lua_handle_add_symbol(lua_State* L)
{
	struct pp_state* state = pp_lua_extract_state(L, 1);
	void* ud = pp_lua_extract_state_ud(L, 1);
	state->pp_lua_add_symbol(luaL_checkstring(L, 2), ud);
	return 0;
}

void pp_lua_push_state(struct lua_preproc* pp, struct pp_state* state, void* ud)
{
	int tbl, tbl_mt;

	// Create the new table and metatable.
	lua_newtable(pp->state);
	tbl = lua_gettop(pp->state);
	lua_newtable(pp->state);
	tbl_mt = lua_gettop(pp->state);

	// Push userdata into metatable.
	lua_pushlightuserdata(pp->state, state);
	lua_rawseti(pp->state, tbl_mt, 0);
	lua_pushlightuserdata(pp->state, ud);
	lua_rawseti(pp->state, tbl_mt, 1);

	// Push C functions into table.
	lua_pushcfunction(pp->state, &pp_lua_handle_print);
	lua_setfield(pp->state, tbl, "print");
	lua_pushcfunction(pp->state, &pp_lua_handle_print_line);
	lua_setfield(pp->state, tbl, "print_line");
	lua_pushcfunction(pp->state, &pp_lua_handle_scope_enter);
	lua_setfield(pp->state, tbl, "scope_enter");
	lua_pushcfunction(pp->state, &pp_lua_handle_scope_exit);
	lua_setfield(pp->state, tbl, "scope_exit");
	lua_pushcfunction(pp->state, &pp_lua_handle_add_symbol);
	lua_setfield(pp->state, tbl, "add_symbol");

	// Associate metatables.
	lua_pushvalue(pp->state, tbl_mt);
	lua_setmetatable(pp->state, tbl);

	// Clean up stack.
	lua_pop(pp->state, lua_gettop(pp->state) - tbl);

	// New table is now at the top of the stack.
}

// Get a reference to the function declared in the parser.  We can't
// include "parser.h" because it conflicts with the Windows headers, which
// are included by dirent.portable.h.
extern void handle_pp_lua_print_line(const char* text, void* ud);

// Get a reference to the function that allows use to push an
// expression as a Lua object.
extern int luaX_pushexpression(lua_State* L, struct expr* expr);

void pp_lua_handle(struct pp_state* state, void* scanner, bstring name, list_t* parameters)
{
	struct lua_preproc* pp;
	struct customarg_entry* carg;
	char* cstr;
	bstring dot;
	unsigned int i;
	int paramtbl;
	
	// Convert the name to lowercase.
	btolower(name);
	cstr = bstr2cstr(name, '0');

	// Loop through all of the modules.
	list_iterator_start(&modules);
	while (list_iterator_hasnext(&modules))
	{
		pp = list_iterator_next(&modules);

		// Set stack top (I don't know why the top of the
		// stack is negative!)
		lua_settop(pp->state, 0);

		// Search handler table for entries.
		lua_getglobal(pp->state, HANDLER_TABLE_NAME);
		lua_getfield(pp->state, -1, cstr);
		if (!lua_istable(pp->state, -1))
		{
			// No such entry.
			lua_pop(pp->state, 2);
			continue;
		}

		// Call the handler function.
		lua_getfield(pp->state, -1, HANDLER_FIELD_FUNCTION_NAME);
		pp_lua_push_state(pp, state, scanner);
		lua_newtable(pp->state);
		paramtbl = lua_gettop(pp->state);
		for (i = 0; i < list_size(parameters); i++)
		{
			carg = list_get_at(parameters, i);

			lua_newtable(pp->state);
			if (carg->expr != NULL)
				lua_pushstring(pp->state, "EXPRESSION");
			else if (carg->string != NULL)
				lua_pushstring(pp->state, "STRING");
			else if (carg->word != NULL)
				lua_pushstring(pp->state, "WORD");
			else
				lua_pushstring(pp->state, "NUMBER");
			lua_setfield(pp->state, -2, "type");
			if (carg->expr != NULL)
				luaX_pushexpression(pp->state, carg->expr);
			else if (carg->string != NULL)
				lua_pushstring(pp->state, carg->string->data);
			else if (carg->word != NULL)
				lua_pushstring(pp->state, carg->word->data);
			else
				lua_pushnumber(pp->state, carg->number);
			lua_setfield(pp->state, -2, "value");
			lua_rawseti(pp->state, paramtbl, i + 1);
		}
		if (lua_pcall(pp->state, 2, 0, 0) != 0)
		{
			printd(LEVEL_ERROR, "error: unable to call preprocessor handler for '%s'.\n", name->data);
			printd(LEVEL_ERROR, "%s\n", lua_tostring(pp->state, -1));
			bdestroy(name);
			bcstrfree(cstr);
			lua_pop(pp->state, 2);
			list_iterator_stop(&modules);
			list_destroy(parameters);
			return;
		}
		
		bdestroy(name);
		bcstrfree(cstr);
		lua_pop(pp->state, 2);
		list_iterator_stop(&modules);
		list_destroy(parameters);
		return;
	}
	list_iterator_stop(&modules);

	// There is no custom preprocessor module that handles this directive, however
	// it could be a directive that is recognised by the underlying assembler / compiler,
	// so pass it through as output.
	dot = bfromcstr(".");
	bconcat(dot, name);
	btoupper(dot);
	bconchar(dot, ' ');
	list_iterator_start(parameters);
	while (list_iterator_hasnext(parameters))
	{
		carg = list_iterator_next(parameters);

		// Output the parameter based on the type.
		if (carg->word != NULL)
			bconcat(dot, carg->word);
		else if (carg->string != NULL)
		{
			bconchar(dot, '"');
			bescape(carg->string);
			bconcat(dot, carg->string);
			bconchar(dot, '"');
		}
		else
			bformata(dot, "%i", carg->number);
	}
	list_iterator_stop(parameters);
	handle_pp_lua_print_line(dot->data, scanner);

	// Clean up.
	list_destroy(parameters);
	bdestroy(name);
	bcstrfree(cstr);
}
