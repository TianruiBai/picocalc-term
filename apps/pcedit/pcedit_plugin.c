/****************************************************************************
 * apps/pcedit/pcedit_plugin.c
 *
 * Plugin ecosystem for the vi/vim editor.
 *
 * Supports:
 *   - Lua scripting engine for vimrc-style configuration
 *   - Plugin loading from /sdcard/pcedit/plugins/
 *   - Hook system: on_open, on_save, on_key, on_mode_change,
 *                  on_cursor_move, on_buf_change
 *   - API: buffer manipulation, cursor control, UI commands,
 *          syntax highlighting, keymappings, autocommands
 *
 * Plugin structure:
 *   /sdcard/pcedit/plugins/<name>/init.lua
 *   /sdcard/pcedit/init.lua   (user config, like .vimrc)
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PLUGIN_MAX_HOOKS    32
#define PLUGIN_MAX_MAPS     64
#define PLUGIN_MAX_AUTOCMDS 32
#define PLUGIN_MAX_PLUGINS  16
#define PLUGIN_DIR          "/sdcard/pcedit/plugins"
#define PLUGIN_INIT_FILE    "/sdcard/pcedit/init.lua"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Hook types that plugins can register */

typedef enum
{
  HOOK_ON_OPEN,           /* File opened */
  HOOK_ON_SAVE,           /* File saved */
  HOOK_ON_KEY,            /* Key pressed (before default handling) */
  HOOK_ON_MODE_CHANGE,    /* Vi mode changed */
  HOOK_ON_CURSOR_MOVE,    /* Cursor position changed */
  HOOK_ON_BUF_CHANGE,     /* Buffer content changed */
  HOOK_ON_INSERT_ENTER,   /* Entered insert mode */
  HOOK_ON_INSERT_LEAVE,   /* Left insert mode */
  HOOK_ON_CMD_LINE,       /* Command entered */
  HOOK_ON_SYNTAX,         /* Syntax highlight request */
  HOOK_MAX
} hook_type_t;

/* Key mapping entry */

typedef struct keymap_s
{
  char   mode;             /* 'n', 'i', 'v', 'c' */
  char   from[32];         /* Key sequence */
  char   to[64];           /* Mapped command/keys */
  bool   noremap;          /* Non-recursive mapping */
  bool   active;
} keymap_t;

/* Autocommand entry */

typedef struct autocmd_s
{
  char   event[32];        /* Event name (BufRead, BufWrite, etc.) */
  char   pattern[64];      /* File pattern (*.c, *.py, etc.) */
  char   command[128];     /* Command to execute */
  bool   active;
} autocmd_t;

/* Plugin info */

typedef struct plugin_info_s
{
  char   name[32];
  char   path[128];
  bool   loaded;
  bool   enabled;
} plugin_info_t;

/* Hook callback (C function pointer) */

typedef int (*hook_callback_t)(hook_type_t type, void *arg);

/* Hook entry */

typedef struct hook_entry_s
{
  hook_type_t     type;
  hook_callback_t callback;
  char            plugin_name[32];
  bool            active;
} hook_entry_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static hook_entry_t  g_hooks[PLUGIN_MAX_HOOKS];
static int           g_hook_count = 0;

static keymap_t      g_keymaps[PLUGIN_MAX_MAPS];
static int           g_keymap_count = 0;

static autocmd_t     g_autocmds[PLUGIN_MAX_AUTOCMDS];
static int           g_autocmd_count = 0;

static plugin_info_t g_plugins[PLUGIN_MAX_PLUGINS];
static int           g_plugin_count = 0;

/* Editor options (set via :set) */

typedef struct editor_options_s
{
  int    tabstop;          /* Tab width (default 4) */
  int    shiftwidth;       /* Indent width (default 4) */
  bool   expandtab;        /* Expand tabs to spaces */
  bool   number;           /* Show line numbers */
  bool   relativenumber;   /* Relative line numbers */
  bool   wrap;             /* Line wrapping */
  bool   ignorecase;       /* Search ignore case */
  bool   smartcase;        /* Smart case search */
  bool   hlsearch;         /* Highlight search */
  bool   incsearch;        /* Incremental search */
  bool   autoindent;       /* Auto indent */
  bool   smartindent;      /* Smart indent */
  bool   showmatch;        /* Show matching bracket */
  bool   cursorline;       /* Highlight cursor line */
  int    scrolloff;        /* Scroll offset lines */
  int    textwidth;        /* Text width for formatting */
  char   colorscheme[32];  /* Color scheme name */
  bool   syntax;           /* Syntax highlighting enabled */
  char   filetype[16];     /* File type detection */
} editor_options_t;

static editor_options_t g_options =
{
  .tabstop        = 4,
  .shiftwidth     = 4,
  .expandtab      = true,
  .number         = true,
  .relativenumber = false,
  .wrap           = true,
  .ignorecase     = false,
  .smartcase      = true,
  .hlsearch       = true,
  .incsearch      = true,
  .autoindent     = true,
  .smartindent    = true,
  .showmatch      = true,
  .cursorline     = true,
  .scrolloff      = 3,
  .textwidth      = 80,
  .colorscheme    = "default",
  .syntax         = true,
  .filetype       = "",
};

/****************************************************************************
 * Lua Scripting Engine
 ****************************************************************************/

#ifdef CONFIG_INTERPRETER_LUA

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static lua_State *g_lua = NULL;

/* === Lua API Functions === */

/**
 * vim.api.buf_get_lines(start, end)
 * Returns table of lines from the current buffer.
 */
static int lua_buf_get_lines(lua_State *L)
{
  /* Placeholder — actual buffer access provided by pcedit_main */

  lua_newtable(L);
  return 1;
}

/**
 * vim.api.buf_set_lines(start, end, replacement_lines)
 * Sets lines in the current buffer.
 */
static int lua_buf_set_lines(lua_State *L)
{
  /* Placeholder — handled by buffer module */

  return 0;
}

/**
 * vim.api.get_cursor()
 * Returns {line, col}.
 */
static int lua_get_cursor(lua_State *L)
{
  lua_newtable(L);
  lua_pushinteger(L, 1);  /* line placeholder */
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, 1);  /* col placeholder */
  lua_rawseti(L, -2, 2);
  return 1;
}

/**
 * vim.api.set_cursor(line, col)
 */
static int lua_set_cursor(lua_State *L)
{
  /* int line = luaL_checkinteger(L, 1); */
  /* int col  = luaL_checkinteger(L, 2); */
  /* Placeholder — caller handles cursor movement */

  return 0;
}

/**
 * vim.api.command(cmd)
 * Execute an ex command.
 */
static int lua_command(lua_State *L)
{
  const char *cmd = luaL_checkstring(L, 1);
  syslog(LOG_INFO, "PLUGIN: Execute command: %s\n", cmd);
  /* TODO: route to vi_parse_command */
  (void)cmd;
  return 0;
}

/**
 * vim.api.feedkeys(keys, mode)
 * Feed keys as if typed.
 */
static int lua_feedkeys(lua_State *L)
{
  const char *keys = luaL_checkstring(L, 1);
  syslog(LOG_INFO, "PLUGIN: Feedkeys: %s\n", keys);
  (void)keys;
  return 0;
}

/**
 * vim.keymap.set(mode, lhs, rhs, opts)
 * Create a key mapping.
 */
static int lua_keymap_set(lua_State *L)
{
  const char *mode = luaL_checkstring(L, 1);
  const char *lhs  = luaL_checkstring(L, 2);
  const char *rhs  = luaL_checkstring(L, 3);

  if (g_keymap_count < PLUGIN_MAX_MAPS)
    {
      keymap_t *km = &g_keymaps[g_keymap_count++];
      km->mode = mode[0];
      strncpy(km->from, lhs, sizeof(km->from) - 1);
      strncpy(km->to, rhs, sizeof(km->to) - 1);
      km->noremap = true;
      km->active = true;

      syslog(LOG_INFO, "PLUGIN: Map %c %s -> %s\n",
             km->mode, km->from, km->to);
    }

  return 0;
}

/**
 * vim.opt accessor for setting editor options.
 */
static int lua_set_option(lua_State *L)
{
  const char *name  = luaL_checkstring(L, 1);
  /* Value can be bool, int, or string */

  if (strcmp(name, "tabstop") == 0)
    {
      g_options.tabstop = luaL_checkinteger(L, 2);
    }
  else if (strcmp(name, "shiftwidth") == 0)
    {
      g_options.shiftwidth = luaL_checkinteger(L, 2);
    }
  else if (strcmp(name, "expandtab") == 0)
    {
      g_options.expandtab = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "number") == 0)
    {
      g_options.number = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "relativenumber") == 0)
    {
      g_options.relativenumber = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "wrap") == 0)
    {
      g_options.wrap = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "ignorecase") == 0)
    {
      g_options.ignorecase = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "smartcase") == 0)
    {
      g_options.smartcase = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "hlsearch") == 0)
    {
      g_options.hlsearch = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "autoindent") == 0)
    {
      g_options.autoindent = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "syntax") == 0)
    {
      g_options.syntax = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "cursorline") == 0)
    {
      g_options.cursorline = lua_toboolean(L, 2);
    }
  else if (strcmp(name, "scrolloff") == 0)
    {
      g_options.scrolloff = luaL_checkinteger(L, 2);
    }

  return 0;
}

/**
 * vim.fn.* namespace for calling vimscript functions.
 */
static int lua_fn_expand(lua_State *L)
{
  const char *expr = luaL_checkstring(L, 1);

  if (strcmp(expr, "%") == 0)
    {
      /* Current filename */
      lua_pushstring(L, "");  /* Placeholder */
    }
  else
    {
      lua_pushstring(L, expr);
    }

  return 1;
}

/**
 * Register autocmd from Lua.
 */
static int lua_autocmd(lua_State *L)
{
  const char *event   = luaL_checkstring(L, 1);
  const char *pattern = luaL_checkstring(L, 2);
  const char *command = luaL_checkstring(L, 3);

  if (g_autocmd_count < PLUGIN_MAX_AUTOCMDS)
    {
      autocmd_t *ac = &g_autocmds[g_autocmd_count++];
      strncpy(ac->event, event, sizeof(ac->event) - 1);
      strncpy(ac->pattern, pattern, sizeof(ac->pattern) - 1);
      strncpy(ac->command, command, sizeof(ac->command) - 1);
      ac->active = true;

      syslog(LOG_INFO, "PLUGIN: Autocmd %s %s -> %s\n",
             event, pattern, command);
    }

  return 0;
}

/**
 * Initialize the Lua scripting engine.
 */
static int lua_engine_init(void)
{
  g_lua = luaL_newstate();
  if (g_lua == NULL)
    {
      syslog(LOG_ERR, "PLUGIN: Failed to create Lua state\n");
      return -1;
    }

  /* Open standard libraries (limited set for embedded) */

  luaL_openlibs(g_lua);

  /* Create vim.api table */

  lua_newtable(g_lua);

  lua_pushcfunction(g_lua, lua_buf_get_lines);
  lua_setfield(g_lua, -2, "buf_get_lines");

  lua_pushcfunction(g_lua, lua_buf_set_lines);
  lua_setfield(g_lua, -2, "buf_set_lines");

  lua_pushcfunction(g_lua, lua_get_cursor);
  lua_setfield(g_lua, -2, "get_cursor");

  lua_pushcfunction(g_lua, lua_set_cursor);
  lua_setfield(g_lua, -2, "set_cursor");

  lua_pushcfunction(g_lua, lua_command);
  lua_setfield(g_lua, -2, "command");

  lua_pushcfunction(g_lua, lua_feedkeys);
  lua_setfield(g_lua, -2, "feedkeys");

  /* Create vim table and nest api inside */

  lua_newtable(g_lua);
  lua_pushvalue(g_lua, -2);
  lua_setfield(g_lua, -2, "api");

  /* vim.keymap */

  lua_newtable(g_lua);
  lua_pushcfunction(g_lua, lua_keymap_set);
  lua_setfield(g_lua, -2, "set");
  lua_setfield(g_lua, -2, "keymap");

  /* vim.opt - simplified set function */

  lua_pushcfunction(g_lua, lua_set_option);
  lua_setfield(g_lua, -2, "opt_set");

  /* vim.fn */

  lua_newtable(g_lua);
  lua_pushcfunction(g_lua, lua_fn_expand);
  lua_setfield(g_lua, -2, "expand");
  lua_setfield(g_lua, -2, "fn");

  /* vim.autocmd */

  lua_pushcfunction(g_lua, lua_autocmd);
  lua_setfield(g_lua, -2, "autocmd");

  lua_setglobal(g_lua, "vim");

  /* Pop the api table */

  lua_pop(g_lua, 1);

  syslog(LOG_INFO, "PLUGIN: Lua engine initialized\n");
  return 0;
}

/**
 * Execute a Lua script file.
 */
static int lua_exec_file(const char *path)
{
  if (g_lua == NULL) return -1;

  syslog(LOG_INFO, "PLUGIN: Executing %s\n", path);

  int ret = luaL_dofile(g_lua, path);
  if (ret != LUA_OK)
    {
      const char *err = lua_tostring(g_lua, -1);
      syslog(LOG_ERR, "PLUGIN: Lua error: %s\n", err ? err : "unknown");
      lua_pop(g_lua, 1);
      return -1;
    }

  return 0;
}

/**
 * Execute a Lua string.
 */
static int lua_exec_string(const char *code)
{
  if (g_lua == NULL) return -1;

  int ret = luaL_dostring(g_lua, code);
  if (ret != LUA_OK)
    {
      const char *err = lua_tostring(g_lua, -1);
      syslog(LOG_ERR, "PLUGIN: Lua error: %s\n", err ? err : "unknown");
      lua_pop(g_lua, 1);
      return -1;
    }

  return 0;
}

#else /* !CONFIG_INTERPRETER_LUA */

static int lua_engine_init(void)
{
  syslog(LOG_WARNING,
         "PLUGIN: Lua not available (CONFIG_INTERPRETER_LUA disabled)\n");
  return 0;
}

static int lua_exec_file(const char *path)
{
  (void)path;
  syslog(LOG_WARNING, "PLUGIN: Cannot execute Lua (not compiled)\n");
  return -1;
}

static int lua_exec_string(const char *code)
{
  (void)code;
  return -1;
}

#endif /* CONFIG_INTERPRETER_LUA */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcedit_plugin_init
 *
 * Description:
 *   Initialize the plugin system and Lua engine.
 *   Scans /sdcard/pcedit/plugins/ for available plugins.
 *   Loads /sdcard/pcedit/init.lua as user configuration.
 *
 ****************************************************************************/

int pcedit_plugin_init(void)
{
  int ret;

  syslog(LOG_INFO, "PLUGIN: Initializing plugin system\n");

  memset(g_hooks, 0, sizeof(g_hooks));
  memset(g_keymaps, 0, sizeof(g_keymaps));
  memset(g_autocmds, 0, sizeof(g_autocmds));
  memset(g_plugins, 0, sizeof(g_plugins));

  g_hook_count = 0;
  g_keymap_count = 0;
  g_autocmd_count = 0;
  g_plugin_count = 0;

  /* Initialize Lua engine */

  ret = lua_engine_init();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "PLUGIN: Lua init failed, "
             "plugins will be limited\n");
    }

  /* Scan plugin directory */

  DIR *dir = opendir(PLUGIN_DIR);
  if (dir != NULL)
    {
      struct dirent *entry;

      while ((entry = readdir(dir)) != NULL &&
             g_plugin_count < PLUGIN_MAX_PLUGINS)
        {
          if (entry->d_name[0] == '.') continue;

#ifdef HAVE_DIRENT_D_TYPE
          if (entry->d_type != DT_DIR) continue;
#endif

          plugin_info_t *p = &g_plugins[g_plugin_count];
          strncpy(p->name, entry->d_name, sizeof(p->name) - 1);
          snprintf(p->path, sizeof(p->path),
                   "%s/%s/init.lua", PLUGIN_DIR, entry->d_name);
          p->enabled = true;
          p->loaded = false;
          g_plugin_count++;

          syslog(LOG_INFO, "PLUGIN: Found plugin: %s\n", p->name);
        }

      closedir(dir);
    }

  /* Load user init.lua */

  FILE *f = fopen(PLUGIN_INIT_FILE, "r");
  if (f != NULL)
    {
      fclose(f);
      lua_exec_file(PLUGIN_INIT_FILE);
      syslog(LOG_INFO, "PLUGIN: Loaded user config: %s\n",
             PLUGIN_INIT_FILE);
    }

  /* Load enabled plugins */

  for (int i = 0; i < g_plugin_count; i++)
    {
      if (g_plugins[i].enabled)
        {
          f = fopen(g_plugins[i].path, "r");
          if (f != NULL)
            {
              fclose(f);
              if (lua_exec_file(g_plugins[i].path) == 0)
                {
                  g_plugins[i].loaded = true;
                  syslog(LOG_INFO, "PLUGIN: Loaded: %s\n",
                         g_plugins[i].name);
                }
            }
        }
    }

  syslog(LOG_INFO, "PLUGIN: %d plugins found, %d keymaps, %d autocmds\n",
         g_plugin_count, g_keymap_count, g_autocmd_count);

  return 0;
}

/****************************************************************************
 * Name: pcedit_plugin_register_hook
 ****************************************************************************/

int pcedit_plugin_register_hook(hook_type_t type,
                                hook_callback_t callback,
                                const char *plugin_name)
{
  if (g_hook_count >= PLUGIN_MAX_HOOKS) return -1;

  hook_entry_t *h = &g_hooks[g_hook_count++];
  h->type = type;
  h->callback = callback;
  h->active = true;

  if (plugin_name)
    {
      strncpy(h->plugin_name, plugin_name, sizeof(h->plugin_name) - 1);
    }

  return 0;
}

/****************************************************************************
 * Name: pcedit_plugin_fire_hook
 *
 * Description:
 *   Fire all registered hooks of the given type.
 *
 ****************************************************************************/

int pcedit_plugin_fire_hook(hook_type_t type, void *arg)
{
  int handled = 0;

  for (int i = 0; i < g_hook_count; i++)
    {
      if (g_hooks[i].active && g_hooks[i].type == type)
        {
          int ret = g_hooks[i].callback(type, arg);
          if (ret > 0) handled++;
        }
    }

  return handled;
}

/****************************************************************************
 * Name: pcedit_plugin_check_keymap
 *
 * Description:
 *   Check if a key sequence matches a registered mapping.
 *   Returns the mapped-to string, or NULL if no mapping.
 *
 ****************************************************************************/

const char *pcedit_plugin_check_keymap(char mode, const char *keys)
{
  for (int i = 0; i < g_keymap_count; i++)
    {
      if (g_keymaps[i].active &&
          g_keymaps[i].mode == mode &&
          strcmp(g_keymaps[i].from, keys) == 0)
        {
          return g_keymaps[i].to;
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: pcedit_plugin_fire_autocmd
 *
 * Description:
 *   Fire autocommands matching the given event and filename.
 *
 ****************************************************************************/

int pcedit_plugin_fire_autocmd(const char *event, const char *filename)
{
  int count = 0;

  for (int i = 0; i < g_autocmd_count; i++)
    {
      if (!g_autocmds[i].active) continue;
      if (strcmp(g_autocmds[i].event, event) != 0) continue;

      /* Simple pattern match: *.ext or * */

      const char *pat = g_autocmds[i].pattern;

      if (strcmp(pat, "*") == 0)
        {
          /* Match all */
        }
      else if (pat[0] == '*' && pat[1] == '.')
        {
          /* Extension match */

          const char *ext = strrchr(filename, '.');
          if (ext == NULL || strcmp(ext, pat + 1) != 0)
            {
              continue;
            }
        }
      else
        {
          continue;
        }

      syslog(LOG_INFO, "PLUGIN: Autocmd %s %s -> %s\n",
             event, pat, g_autocmds[i].command);

      lua_exec_string(g_autocmds[i].command);
      count++;
    }

  return count;
}

/****************************************************************************
 * Name: pcedit_plugin_exec_lua
 *
 * Description:
 *   Execute a Lua command string (from :lua command).
 *
 ****************************************************************************/

int pcedit_plugin_exec_lua(const char *code)
{
  return lua_exec_string(code);
}

/****************************************************************************
 * Name: pcedit_plugin_source
 *
 * Description:
 *   Source/execute a Lua script file (from :source command).
 *
 ****************************************************************************/

int pcedit_plugin_source(const char *path)
{
  return lua_exec_file(path);
}

/****************************************************************************
 * Name: pcedit_plugin_set_option
 *
 * Description:
 *   Set an editor option (from :set command).
 *
 ****************************************************************************/

int pcedit_plugin_set_option(const char *option)
{
  /* Parse "name=value" or "name" or "noname" */

  char name[32];
  char value[32];
  bool has_value = false;
  bool negate = false;

  const char *eq = strchr(option, '=');
  if (eq)
    {
      size_t nlen = eq - option;
      if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
      memcpy(name, option, nlen);
      name[nlen] = '\0';
      strncpy(value, eq + 1, sizeof(value) - 1);
      has_value = true;
    }
  else if (strncmp(option, "no", 2) == 0)
    {
      strncpy(name, option + 2, sizeof(name) - 1);
      negate = true;
    }
  else
    {
      strncpy(name, option, sizeof(name) - 1);
    }

  /* Apply option */

  if (strcmp(name, "tabstop") == 0 || strcmp(name, "ts") == 0)
    {
      if (has_value) g_options.tabstop = atoi(value);
    }
  else if (strcmp(name, "shiftwidth") == 0 || strcmp(name, "sw") == 0)
    {
      if (has_value) g_options.shiftwidth = atoi(value);
    }
  else if (strcmp(name, "expandtab") == 0 || strcmp(name, "et") == 0)
    {
      g_options.expandtab = !negate;
    }
  else if (strcmp(name, "number") == 0 || strcmp(name, "nu") == 0)
    {
      g_options.number = !negate;
    }
  else if (strcmp(name, "relativenumber") == 0 || strcmp(name, "rnu") == 0)
    {
      g_options.relativenumber = !negate;
    }
  else if (strcmp(name, "wrap") == 0)
    {
      g_options.wrap = !negate;
    }
  else if (strcmp(name, "ignorecase") == 0 || strcmp(name, "ic") == 0)
    {
      g_options.ignorecase = !negate;
    }
  else if (strcmp(name, "smartcase") == 0 || strcmp(name, "scs") == 0)
    {
      g_options.smartcase = !negate;
    }
  else if (strcmp(name, "hlsearch") == 0 || strcmp(name, "hls") == 0)
    {
      g_options.hlsearch = !negate;
    }
  else if (strcmp(name, "incsearch") == 0 || strcmp(name, "is") == 0)
    {
      g_options.incsearch = !negate;
    }
  else if (strcmp(name, "autoindent") == 0 || strcmp(name, "ai") == 0)
    {
      g_options.autoindent = !negate;
    }
  else if (strcmp(name, "smartindent") == 0 || strcmp(name, "si") == 0)
    {
      g_options.smartindent = !negate;
    }
  else if (strcmp(name, "cursorline") == 0 || strcmp(name, "cul") == 0)
    {
      g_options.cursorline = !negate;
    }
  else if (strcmp(name, "scrolloff") == 0 || strcmp(name, "so") == 0)
    {
      if (has_value) g_options.scrolloff = atoi(value);
    }
  else if (strcmp(name, "textwidth") == 0 || strcmp(name, "tw") == 0)
    {
      if (has_value) g_options.textwidth = atoi(value);
    }
  else if (strcmp(name, "syntax") == 0)
    {
      g_options.syntax = !negate;
    }
  else
    {
      syslog(LOG_WARNING, "PLUGIN: Unknown option: %s\n", name);
      return -1;
    }

  return 0;
}

/****************************************************************************
 * Name: pcedit_plugin_get_options
 *
 * Description:
 *   Get current editor options.
 *
 ****************************************************************************/

const editor_options_t *pcedit_plugin_get_options(void)
{
  return &g_options;
}

/****************************************************************************
 * Name: pcedit_plugin_list
 *
 * Description:
 *   List installed plugins and their status.
 *
 ****************************************************************************/

int pcedit_plugin_list(char *output, size_t output_size)
{
  size_t pos = 0;

  pos += snprintf(output + pos, output_size - pos,
                  "Plugins (%d):\n", g_plugin_count);

  for (int i = 0; i < g_plugin_count && pos < output_size - 64; i++)
    {
      pos += snprintf(output + pos, output_size - pos,
                      "  %s [%s] %s\n",
                      g_plugins[i].name,
                      g_plugins[i].loaded ? "loaded" : "not loaded",
                      g_plugins[i].enabled ? "" : "(disabled)");
    }

  pos += snprintf(output + pos, output_size - pos,
                  "\nKeymaps: %d  Autocmds: %d  Hooks: %d\n",
                  g_keymap_count, g_autocmd_count, g_hook_count);

  return g_plugin_count;
}

/****************************************************************************
 * Name: pcedit_plugin_cleanup
 *
 * Description:
 *   Clean up the plugin system.
 *
 ****************************************************************************/

void pcedit_plugin_cleanup(void)
{
#ifdef CONFIG_INTERPRETER_LUA
  if (g_lua)
    {
      lua_close(g_lua);
      g_lua = NULL;
    }
#endif

  g_hook_count = 0;
  g_keymap_count = 0;
  g_autocmd_count = 0;
  g_plugin_count = 0;

  syslog(LOG_INFO, "PLUGIN: Cleanup complete\n");
}
