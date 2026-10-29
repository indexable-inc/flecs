# Flecs Shell

## Introduction
The Flecs Shell is a command line interface for inspecting and modifying a running world. Commands look and feel like the commands of a Unix shell, where the entity hierarchy takes the place of the file system. The shell can be used from code, and remotely with the [Remote API](FlecsRemoteApi.md), which makes it a good fit for terminals, debugging tools and AI agents that interact with an application.

Some of the features of the Flecs Shell are:

- Navigate the entity hierarchy (`ls`, `cd`, `tree`)
- Inspect and modify entities and components (`cat`, `get`, `set`, `add`, `rm`)
- Search entities and run queries (`find`, `grep`, `query`)
- Read and edit [Flecs Script](FlecsScript.md) code, with diffs and undo (`script_read`, `script_set`, `script_diff`)
- Emit mouse and keyboard events (`click`, `drag`, `type`)
- Add your own commands, which can return files like images (see [Resources](#resources))

Every command returns a typed value. The examples in this manual show results as JSON, which is how they are returned by the Remote API.

## Example
The examples in this manual use a world that is loaded from a [managed script](FlecsScript.md#managed-script) called `scene`:

```cpp
struct Position(x: f32, y: f32)
struct Velocity(x: f32, y: f32)

prefab SpaceShip {
  Velocity: {1, 0}
}

game {
  player : SpaceShip {
    Position: {10, 20}
  }
  enemy : SpaceShip {
    Position: {50, 30}
  }
}
```

The following session navigates to the `game` entity, changes the position of the player, and then queries for all moving entities:

```
> cd game
"game"

> ls
["game.player", "game.enemy"]

> get player Position
{"Position": {"x": 10, "y": 20}}

> set player Position {x: 15}
{"Position": {"x": 15, "y": 20}}

> query "Position, Velocity"
[
  {"entity": "game.player", "fields": [
    {"id": ["Position"], "value": {"Position": {"x": 15, "y": 20}}},
    {"id": ["Velocity"], "value": {"Velocity": {"x": 1, "y": 0}}}]},
  {"entity": "game.enemy", "fields": [
    {"id": ["Position"], "value": {"Position": {"x": 50, "y": 30}}},
    {"id": ["Velocity"], "value": {"Velocity": {"x": 1, "y": 0}}}]}
]
```

## The Basics
This section goes over the commands for navigating, inspecting and modifying a world.

### Commands
A command line starts with the name of the command, followed by arguments that are separated by whitespace:

```
> get game.player Position
{"Position": {"x": 10, "y": 20}}
```

When a command fails, it returns the errors that were logged while it ran:

```
> cat nothing
error: invalid argument 'nothing' for parameter 'entity' of command 'cat'
```

To list all commands, use `help`. To describe a single command, pass its name:

```
> help ls
[{"name": "ls", "brief": "Direct children",
  "params": [{"name": "scope", "type": "flecs.meta.entity", "optional": true,
    "brief": "Scope to list (default: session scope)"}],
  "return_type": "flecs.script.typecache.vector<entity>"}]
```

Parameters that are optional can be left out. See [Arguments](#arguments) for how arguments are parsed.

### Navigating
Commands run in a session, which has a current scope. Identifiers are first resolved relative to the scope, and then from the root. The scope is changed with `cd`, and `cd` without arguments goes back to the root:

```
> cd game
"game"

> pwd
"game"

> cd
"#0"
```

The `ls` command lists the children of an entity, and `tree` lists all descendants with their depth:

```
> ls game
["game.player", "game.enemy"]

> tree game
[{"entity": "game.player", "depth": 0},
 {"entity": "game.enemy", "depth": 0}]
```

Without an argument, `ls` and `tree` use the current scope. Entity arguments can also be relative paths, where `.` is the current scope and `..` its parent:

```
> cd game
"game"

> cat ./player
...

> ls ..
["flecs", "scene", "game", "Position", "Velocity", "SpaceShip"]
```

### Inspecting entities
The `cat` command shows an entity with its components and values, and the ids that don't have a value, like tags and relationships:

```
> cat game.player
{"entity": "game.player", "id": 615,
 "components": [
   {"id": ["Position"], "value": {"Position": {"x": 10, "y": 20}}},
   {"id": ["Velocity"], "value": {"Velocity": {"x": 1, "y": 0}}},
   {"id": ["flecs.core.Identifier", "flecs.core.Name"], "value": {"EcsIdentifier": {"value": "player"}}}],
 "ids": [
   ["flecs.script.Script", "scene"],
   ["flecs.core.ChildOf", "game"],
   ["flecs.core.IsA", "SpaceShip"]]}
```

Use `get` to get the value of a single component:

```
> get game.player Position
{"Position": {"x": 10, "y": 20}}
```

The `schema` command returns the definition of a type as script code, and `schema_value` returns its default value:

```
> schema Position
"struct Position(x: f32, y: f32)\n"

> schema_value Position
{"Position": {"x": 0, "y": 0}}
```

The `man` command returns the [documentation](/flecs/group__c__addons__doc.html) of an entity, such as its brief description and link.

### Modifying entities
The `set` command assigns a component value. The value is parsed into the current value of the component, which means that members that are not specified keep their value:

```
> set game.player Position {x: 15}
{"Position": {"x": 15, "y": 20}}
```

Components, tags and pairs are added with `add`, and removed with `rm`:

```
> add game.player Velocity
["Velocity"]

> add game.player (ChildOf, game.enemy)
["flecs.core.ChildOf", "game.enemy"]

> rm game.player Velocity
```

When `rm` is called without a component, it deletes the entity. New entities are created in the current scope with `new`, and entities are moved or renamed with `mv`:

```
> cd game
"game"

> new asteroid
"game.asteroid"

> mv asteroid game.rock
"game.rock"
```

To get or set a singleton, use `$` as entity:

```
> set $ TimeOfDay {t: 0.5}
{"TimeOfDay": {"t": 0.5}}
```

### Searching
The `find` command finds entities by name, and `grep` searches entity names and the names and values of components. Both search the descendants of the current scope:

```
> find play
["game.player"]

> grep 50
[{"entity": "game.enemy", "id": ["Position"], "text": "{x: 50, y: 30}"}]
```

Use `grep_i` for a case insensitive search. The `query` command runs a query written in the [query language](FlecsQueryLanguage.md), and `eval` evaluates an [expression](FlecsScript.md#expressions):

```
> query "Position, !Velocity"
[]

> eval "10 + 20"
{"i64": 30}
```

## Arguments
Arguments are separated by whitespace. To pass an argument that contains whitespace, put it between quotes. This is why the query and expression in the previous examples are quoted:

```
> eval "10 + 20"
{"i64": 30}

> eval 10 + 20
error: too many arguments for command 'eval' (usage: eval <expr>)
```

Text between `()`, `{}` and `[]` is a single argument, so values and pairs don't need quotes:

```
> set game.player Position {x: 10, y: 20}
> add game.player (ChildOf, game)
```

Arguments are parsed as follows:

- A token that starts with `"` ends at the next unescaped `"`. The quotes are removed and escape sequences (`\n`, `\t`, ...) are processed.
- A token that starts with three backticks ends at the next three backticks. The backticks are removed and the text in between is not processed, which is useful for passing multi-line code.
- `()`, `{}` and `[]` group a token and can be nested. They are passed to the command unchanged.

Arguments for string parameters are passed to the command as is. Arguments for other parameters are parsed as [expressions](FlecsScript.md#expressions) of the parameter type. This means that an argument can be a value like `{x: 10}`, but also an expression like `{x: 10 * 2}`.

## Editing scripts
The shell can read and edit the code of [managed scripts](FlecsScript.md#managed-script). Edits are applied to the script code, after which the script is reloaded. This keeps the code and the world in sync, so that changes can be saved back to the script file.

The `scripts` command lists the scripts, and `script_read` returns the lines of a script:

```
> scripts
[{"script": "scene", "filename": null, "lines": 14, "unsaved": false, "error": null}]

> script_read scene
[{"line": 1, "text": "struct Position(x: f32, y: f32)"},
 {"line": 2, "text": "struct Velocity(x: f32, y: f32)"},
 ...]
```

To find code, use `script_grep` to search for text and `script_where` to find the statement that declares an entity:

```
> script_grep enemy
[{"script": "scene", "line": 11, "text": "  enemy : SpaceShip {"}]

> script_where game.player
{"script": "scene", "line": 8, "column": 3, "template": "#0",
 "statement": "player : SpaceShip {"}
```

The `script_set` command sets a component value in the statement that declares the entity. Use `script_diff` to see the changes since the script was loaded or saved:

```
> script_set game.player Position {x: 99, y: 20}
{"line": 9, "text": "    Position: {99, 20}"}

> script_diff scene
"@@ -6,7 +6,7 @@
 }
 game {
   player : SpaceShip {
-    Position: {10, 20}
+    Position: {99, 20}
   }
   enemy : SpaceShip {
     Position: {50, 30}
"
```

Text in a script is replaced with `script_replace`, which requires the text to occur exactly once, and `script_replace_all`. Both return the changed lines. The last change is undone with `script_undo`:

```
> script_replace scene "50, 30" "60, 30"
[{"line": 12, "text": "    Position: {60, 30}"}]

> get game.enemy Position
{"Position": {"x": 60, "y": 30}}

> script_undo scene

> get game.enemy Position
{"Position": {"x": 50, "y": 30}}
```

Larger edits can be made with `script_patch`, which applies a unified diff. Hunks in a patch may start with an `@@ <anchor>` line instead of line numbers, where the anchor is text that identifies the location of the hunk.

The `script_write` command replaces the code of a script, and creates the script if it doesn't exist yet. Code that spans multiple lines is passed between triple backticks:

````
> script_write asteroids ```
asteroid_1 {
  Position: {100, 0}
}
```
"asteroids"

> get asteroid_1 Position
{"Position": {"x": 100, "y": 0}}
````

Changes that were made to the world without the shell are written back to the code of a script with `script_sync`. Scripts are saved to their file with `script_save` and reloaded from it with `script_reload`.

When reloading a script fails, an edit command returns both its result and the errors of the reload, so that the edit can be fixed or undone. Use `script_errors` to list the scripts that have errors.

## Input
The input commands emit mouse and keyboard events for scripts that use the script input addon (`FLECS_SCRIPT_INPUT`). This makes it possible to test user interfaces from the shell:

```
> click game.ui.start_button
> type "player one"
> key Enter
> drag game.ui.slider 100 0
```

Mouse buttons have the format `[mod+]*(left|middle|right)` and keys have the format `[mod+]*<key>`, where `mod` is `ctrl`, `shift`, `alt` or `meta`:

```
> click game.ui.item ctrl+right
> key shift+Tab
```

Use `listeners` to find the kinds of events that scripts wait for on an entity, and `pointer` to get the last mouse event and the entity that is pressed. See the [command reference](#input-commands) for all input commands.

## Custom commands
Commands are [script functions](FlecsScript.md#functions). Applications can add commands by creating functions in a scope, and adding that scope to the `command_paths` member of the `EcsScriptShell` singleton:

```c
ecs_entity_t commands = ecs_entity(world, { .name = "game.commands" });

ecs_entity_t spawn = ecs_function(world, {
    .name = "spawn",
    .parent = commands,
    .return_type = ecs_id(ecs_entity_t),
    .params = {
        { .name = "prefab", .type = ecs_id(ecs_entity_t),
          .brief = "Prefab to instantiate" },
        { .name = "count", .type = ecs_id(ecs_i32_t), .optional = true,
          .brief = "Number of instances (default: 1)" }
    },
    .callback = spawn_command
});

ecs_doc_set_brief(world, spawn, "Spawn instances of a prefab");

EcsScriptShell *shell = ecs_singleton_ensure(world, EcsScriptShell);
ecs_vec_append_t(NULL, &shell->command_paths, ecs_entity_t)[0] = commands;
ecs_singleton_modified(world, EcsScriptShell);
```

The command can then be used like any other command:

```
> spawn SpaceShip
> spawn SpaceShip 10
```

The `help` command describes the command with the doc brief of the function and the briefs of its parameters. When a command runs, the scopes in `command_paths` are searched in order, and the first function with a matching name is used. A command can override a builtin command by inserting its scope before the builtin scopes.

The callback is a regular script function callback. The `argc` argument is the number of arguments that were provided, so optional parameters can be detected. A command gets the session it runs in with `ecs_command_get_session()`, and reports errors by logging them with `ecs_err()`:

```c
void spawn_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t prefab = *(ecs_entity_t*)argv[0].ptr;
    int32_t count = argc > 1 ? *(int32_t*)argv[1].ptr : 1;

    if (!ecs_has_id(ctx->world, prefab, EcsPrefab)) {
        ecs_err("'%s' is not a prefab", ecs_get_name(ctx->world, prefab));
        return;
    }

    ecs_entity_t session = ecs_command_get_session(ctx->world);
    const EcsCommandSession *s = ecs_get(ctx->world, session, EcsCommandSession);

    ecs_entity_t e = 0;
    for (int32_t i = 0; i < count; i ++) {
        e = ecs_new_w_pair(ctx->world, EcsIsA, prefab);
        if (s->scope) {
            ecs_add_pair(ctx->world, e, EcsChildOf, s->scope);
        }
    }

    *(ecs_entity_t*)result->ptr = e;
}
```

## Resources
Commands can return files, like images, by storing them as resources of the session. A client that receives a resource can fetch its content to show it to the user. For example, a `capture` command could return a PNG with a capture of the screen:

```
> capture
{"name": "1", "content_type": "image/png"}
```

A command creates a resource with `ecs_command_resource_create()`, and signals that it returned a resource by returning the `flecs.script.shell.resource` type (`ecs_command_resource_t`):

```c
void capture_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    void *png = NULL;
    ecs_size_t png_size = 0;
    if (capture_screen_png(&png, &png_size)) {
        ecs_err("failed to capture screen");
        return;
    }

    ecs_command_resource_create(ctx->world,
        ecs_command_get_session(ctx->world), "image/png", png, png_size,
        result->ptr);

    free(png);
}

ecs_function(world, {
    .name = "capture",
    .parent = commands,
    .return_type = ecs_id(ecs_command_resource_t),
    .callback = capture_command
});
```

Resource names are unique within a session. The content of a resource is read with `ecs_command_resource_read()`, or remotely with `GET /shell/session/<id>/resource/<name>`. Only resources that were created in the session can be read.

Resources are stored in `.flecs/shell/<session>/<name>`, relative to the current working directory. The directory of a session is created when the first resource is created, and is deleted together with the session. Files are written, read and removed with the [OS API](/flecs/group__c__os__api.html), which applications can override to store resources elsewhere.

## Command reference
Builtin commands are stored in the `flecs.script.shell.commands` scope. Parameters between `<>` are required, and parameters between `[]` are optional.

### Core commands
Commands in `flecs.script.shell.commands.core`.

| Command | Description |
|---------|-------------|
| `help [command]` | Commands with parameters and descriptions |
| `ls [scope]` | Direct children |
| `tree [scope]` | Descendants in preorder |
| `cd [scope]` | Set the session scope, no argument goes to the root |
| `pwd` | Session scope |
| `cat <entity>` | Id, components with values, and ids without values |
| `get <entity> <component>` | Get component value |
| `set <entity> <component> <value>` | Set component value, parsed into the current value |
| `add <entity> <component>` | Add component, tag or pair |
| `rm <entity> [component]` | Remove component, or delete entity if no component is provided |
| `new [name]` | Create entity in the session scope |
| `mv <entity> <path>` | Move or rename entity |
| `schema <component>` | Type as script |
| `schema_value <component>` | Default value |
| `man <entity>` | Documentation (flecs.doc data) |
| `find <text>` | Find descendants of the session scope by name substring |
| `grep <text> [scope]` | Search names, component names and values of descendants |
| `grep_i <text> [scope]` | Case insensitive grep |
| `query <expr>` | Run query |
| `eval <expr>` | Evaluate expression |
| `clear` | Clear the session history |
| `quit` | Quit the application |

### Script commands
Commands in `flecs.script.shell.commands.script`.

| Command | Description |
|---------|-------------|
| `scripts [scope]` | Scripts in descendants |
| `script_read <script>` | Read the code of a script |
| `script_grep <text> [script]` | Find lines in script code |
| `script_where <entity>` | Script statement that declares an entity |
| `script_errors` | Scripts with errors |
| `script_set <entity> <component> <value>` | Set component value in the script that declares the entity |
| `script_remove <entity> <component>` | Remove component from the script that declares the entity |
| `script_delete <entity>` | Delete entity from the script that declares it |
| `script_replace <script> <old> <new>` | Replace text that occurs exactly once, returns the changed lines |
| `script_replace_all <script> <old> <new>` | Replace all occurrences of text, returns the changed lines |
| `script_patch <script> <patch>` | Apply a unified diff, returns the lines of the patched hunks |
| `script_write <script> <code>` | Replace the code of a script, creates the script if it doesn't exist |
| `script_sync <script>` | Update script code with the current component values |
| `script_diff <script>` | Changes since the script was loaded or saved |
| `script_undo <script>` | Undo the last change to the script code |
| `script_save <script> [filename]` | Save script to file |
| `script_reload <script>` | Reload script from file |

### Input commands
Commands in `flecs.script.shell.commands.input`. These commands require the script input addon (`FLECS_SCRIPT_INPUT`).

| Command | Description |
|---------|-------------|
| `click <entity> [button]` | Click an entity |
| `press <entity> [button]` | Press a mouse button |
| `release [entity]` | Release the held mouse buttons |
| `move <entity> [x] [y]` | Move the pointer to an entity, keeps held buttons held |
| `move_by <dx> <dy>` | Move the pointer, becomes a drag when buttons are held |
| `drag <entity> <dx> <dy> [button]` | Press, move and release |
| `key <key>` | Press and release a key |
| `key_down <key>` | Press a key |
| `key_up <key>` | Release a key |
| `type <text>` | Press and release a key for each character |
| `focus [entity]` | Get or set the focus, 0 clears the focus |
| `blur` | Clear the focus |
| `pointer` | Last mouse event and pressed entity |
| `mouse_event <event>` | Emit a mouse event |
| `keyboard_event <event>` | Emit a keyboard event |
| `listeners <entity>` | Kinds of events that scripts wait for on an entity |

## Sessions
A session is an entity with the `EcsCommandSession` component. A session stores:

- The current scope, which is changed with `cd`.
- A bounded history of the commands that ran in the session, together with their results.
- The resources created by commands that ran in the session.

The `history` member of `EcsCommandSession` contains the commands, oldest first. The `results` member contains a JSON snapshot of the result of each command, with the scope in which the command ran, and the type, result and errors of the command:

```json
{"scope": "game", "type": "flecs.meta.string", "result": "...", "errors": []}
```

The `type` and `result` members are omitted when the command did not return a value. The default history size is `FLECS_SCRIPT_SHELL_HISTORY_SIZE` (256). When the history is full, the oldest command is removed. The history is cleared with the `clear` command.

## API
This section goes over how to use the shell in an application. The shell is implemented by the script shell addon, which is enabled by defining `FLECS_SCRIPT_SHELL`. To use the shell, import its module:

```c
ECS_IMPORT(world, FlecsScriptShell);
```

### Creating a session
A session is created with `ecs_command_session_init()`. When no entity is provided, sessions are created in the `script.shell.sessions` scope:

```c
ecs_entity_t session = ecs_command_session_init(world, NULL);
```

A session can also be created for an existing entity, with an initial scope and a custom history size:

```c
ecs_entity_t session = ecs_command_session_init(world,
    &(ecs_command_session_desc_t){
        .entity = my_entity,
        .scope = ecs_lookup(world, "game"),
        .history_size = 100
    });
```

A session is closed by deleting its entity, which also deletes its resources:

```c
ecs_delete(world, session);
```

### Running a command
A command is run with `ecs_command_run()`, which returns the result of the command and the errors that were logged while it ran. The result must be freed with `ecs_command_result_fini()`:

```c
ecs_command_result_t r = ecs_command_run(world, session, "ls game");

if (ecs_vec_count(&r.errors)) {
    char **errors = ecs_vec_first(&r.errors);
    for (int i = 0; i < ecs_vec_count(&r.errors); i ++) {
        printf("error: %s\n", errors[i]);
    }
} else if (r.result.type) {
    char *json = ecs_ptr_to_json(world, r.result.type, r.result.ptr);
    printf("%s\n", json);
    ecs_os_free(json);
}

ecs_command_result_fini(world, &r);
```

The `result` member is empty when the command has no return value or failed. Script edit commands are the exception: when the edit was applied but reloading the script failed, the result is set together with the errors of the reload.

## Remote API
When the [REST API](FlecsRemoteApi.md) is enabled, sessions can be created and commands can be run remotely:

| Method | Path | Description |
|--------|------|-------------|
| `PUT` | `/shell/session` | Create a session. Returns `{"session": <id>}`. |
| `DELETE` | `/shell/session/<id>` | Delete a session. |
| `GET` | `/shell/session/<id>/history` | Get the history. |
| `GET` | `/shell/session/<id>/resource/<name>` | Get the content of a resource created in the session. |
| `PUT` | `/shell/session/<id>` | Run the command in the request body. |

The reply to a command contains the type of the result, the result and the errors:

```
PUT /shell/session/42
ls game
```

```json
{
  "type": "flecs.script.typecache.vector<entity>",
  "result": ["game.player", "game.enemy"],
  "more": false,
  "errors": []
}
```

See the [Remote API manual](FlecsRemoteApi.md#command-sessions) for the parameters of these endpoints.
