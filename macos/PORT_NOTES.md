# Cyber Engine Tweaks on macOS: port notes

Target: Cyberpunk 2077 **2.3.1 (patch 2.31), Steam, Apple silicon**, game executable LC_UUID
`A6656ADC-FBE2-36A4-9B9D-B4A9DE645089`, with the macOS port of RED4ext (jackmaxwil/RED4ext-macos).

**Status: compiles (all 58 macOS translation units pass a full clang type check), unit tests pass, nothing has been run
in the game yet.** Everything marked "verify in game" below is unproven until then.

## How it loads

On Windows CET is an ASI module loaded by its own `version.dll`. On macOS it is a RED4ext plugin:

```
<game>/red4ext/plugins/CyberEngineTweaks/CyberEngineTweaks.dylib
<game>/bin/x64/plugins/cyber_engine_tweaks/      <- CET's data folder, same layout as on Windows
    fonts/  scripts/  tweakdb/  mods/<mod>/init.lua
```

The data folder keeps the Windows layout, so mod archives that contain `bin/x64/plugins/cyber_engine_tweaks/mods/...`
extract unchanged. `platform/macos/PluginMain.cpp` replaces `dllmain.cpp`.

## What changed, by area

| Area | Windows | macOS |
| --- | --- | --- |
| Hooks | MinHook | RED4ext's hooking API (`platform/macos/Hooking.h`, with MinHook-compatible names so the call sites are unchanged) |
| Game addresses | CET's own resolver in `version.dll` | the SDK's resolver: `RED4ext_ResolveAddress`, which only resolves addresses **verified** for this game build; anything else is null and its feature is skipped |
| Main-thread ticks | hooks on the four game-state tick functions | RED4ext game-state callbacks |
| Mods loaded / `onHook` | hook on `CScriptDataBinder::LoadOpcodes` | hook on `IRTTISystem::InitializeScriptRuntime` (vtable slot 0xF8 of the verified RTTI system; the neighbouring slots were checked in the binary) |
| `onTweak` | TweakDB load hook | same hook (verified address), ordered after `onHook` |
| `onInit` | hook on `cp::PlayerSystem::OnPlayerMainObjectSpawned` (address not verified on macOS) | first `PlayerPuppet::OnGameAttached` (an internal observer), run on the next game tick |
| Script `Log*` natives, `TDBID.ToStringDEBUG` | hooks on their addresses | their entries in the script native handler table (`OpcodeHandlers`, verified) are replaced |
| `Observe`/`Override` | x64 trampolines generated with xbyak; game function allocator hooked | a pool of 4096 compiled thunks (no executable memory is created); the RTTI function pool's `Allocate` is rebound in the game's import table so function objects get room for the swap; objects too small for it are refused with an error instead of being overwritten |
| `TweakDBID` derive hook | `(base, out, name)` | `TweakDBID (const TweakDBID* base, const char* name)` returned in x0, the macOS signature (SDK-macos ADDRESS_AUDIT) |
| Overlay | Direct3D 12 present hook | Metal: the drawable is remembered at `-[MTLCommandBuffer presentDrawable:…]` and CET's pass is added when the command buffer is committed, after all of the game's encoders. Float/sRGB targets get ImGui colors converted to linear (`imgui_impl/metal.mm`) |
| Input | WNDPROC subclass of the game window, raw input | NSEvent local monitor (CET's own plus wrappers around the game's), scoped to this process; key codes translated to Windows VK codes so `bindings.json` stays compatible |
| Pointer while the overlay is open | game's `ForceCursor` (address not verified on macOS) | the game's own calls to `CGAssociateMouseAndMouseCursorPosition`/`CGWarpMouseCursorPosition` are routed through CET (import table only); ImGui draws the pointer |
| Lua | LuaJIT with JIT | LuaJIT interpreter (`-DLUAJIT_DISABLE_JIT`): no runtime code generation. FFI stays compiled in for 64-bit integer cdata; mods cannot reach `ffi` |
| sqlite3 | bundled amalgamation | system `libsqlite3` |
| Allocator | mimalloc | system malloc (TiltedCore's `MimallocAllocator` reimplemented) |
| Fonts | FreeType | stb_truetype (ImGui default) |

## Security changes (from review/cet.md)

- **Path jail rewritten for POSIX/APFS** (`platform/macos/PathJail.h`): `weakly_canonical` on root and request, so `..`,
  absolute paths and symlinked folders cannot escape the mod folder; case-insensitive comparison; `\` accepted as a
  separator. The reserved `cet` mod name and the `db.sqlite3` self-protection are case-insensitive.
- **SQLite** (all platforms): `db:load_extension` removed; an authorizer refuses `ATTACH` of files (including computed
  names and `VACUUM INTO`), `PRAGMA temp_store_directory`/`data_store_directory` and the SQL `load_extension()`;
  `sqlite3.temp_directory` (process-wide) is not exposed.
- No global input capture; nothing needs Accessibility permission. No network code.
- Unit tests: `macos/tests/` (19 path-jail cases, 12 SQLite cases, 12 UTF-8 cases). All pass.

## Not available on macOS

- `WorldFunctionalTests.SpawnEntity`: it drives the game's spawner through hand-mapped Windows structures and
  unverified addresses. `exEntitySpawner` (Spawn, SpawnRecord, Despawn) is provided instead by a small Lua table on
  top of Codeware's `DynamicEntitySystem` (`Scripting.cpp`, `RegisterEntitySpawnerShim`); without Codeware, Spawn
  returns nil.
- `GameOptions` (the list is collected by a hook on `Config::IConfigVar::Register`, not verified on macOS) and the
  patches: disable vignette, disable boundary teleport, skip intro, Win7 vsync.
- TweakDB editor names and resource paths: the macOS game links Oodle statically without exporting it, so
  `tweakdbstr.kark`/`usedhashes.kark` cannot be decompressed. An uncompressed `tweakdb.str` in
  `bin/x64/plugins/cyber_engine_tweaks/tweakdb/` still works.

## Game addresses used

All resolve through RED4ext's address database; none is new.

| Use | Hash / source | macOS status |
| --- | --- | --- |
| `CBaseFunction::ExecuteScripted` (Observe/Override, pure script calls) | 3791200470 | verified `0x102172BF4` |
| `CBaseFunction::InternalExecute` (call the original) | SDK `CBaseFunction_InternalExecute` | verified `0x102173120` |
| `TweakDB::LoadOptimized` | 3602585178 | verified `0x102B75570`, `(TweakDB*, CString&)` |
| `TweakDBID` derive | 326438016 | verified `0x103453B14`, macOS signature used |
| script native handler table | SDK `OpcodeHandlers` | verified `0x10908B798` |
| `IRTTISystem::InitializeScriptRuntime` | vtable slot 0xF8 of `CRTTISystem::Get()` (verified) | read at runtime |
| `PoolStorageProxy<PoolRTTIFunction>::Allocate` | exported symbol, rebound in the game's GOT (105 call sites, all through the stub) | symbol |
| `CClassFunction`/`CClassStaticFunction`/`CGlobalFunction` constructors, RTTI system, CName pool, TweakDB getters | SDK | verified |

Everything else CET's Windows build hooks resolves to null on macOS and is skipped with a log line.

## Verify in game

1. `red4ext/logs` shows the plugin loaded; `bin/x64/plugins/cyber_engine_tweaks/cyber_engine_tweaks.log` shows
   "Metal present hooks complete", "IRTTISystem::InitializeScriptRuntime function hook complete", "RTTI function
   allocator hook complete", "NSEvent local monitor hook complete", "Pointer lock hooks complete".
2. The overlay appears on first start (first-time binding setup), its colors look right (not washed out), and closing it
   gives mouse look back.
3. `print(Game.GetPlayer())` in the console after loading a save; `Game.AddToInventory("Items.Preset_Yukimura_Default", 1)`.
4. A mod with `Observe('PlayerPuppet', 'OnGameAttached', ...)` and one with `Override` of a script function.
5. `scripting.log` shows mods' `onInit` after the player spawns.

## Build

Requires a current Xcode or Command Line Tools (the machine this was prepared on has Command Line Tools from 2020 with
the macOS 11 SDK and Apple clang 12, too old for this C++20 code; update them first) and CMake 3.23+.

```
git submodule update --init vendor/RED4ext.SDK-macos vendor/luajit vendor/sol2 vendor/imgui vendor/spdlog vendor/json \
    vendor/TiltedCore vendor/hopscotch-map vendor/stb vendor/fishhook
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build
codesign -f -s - build/CyberEngineTweaks.dylib
```

Install: copy `build/CyberEngineTweaks.dylib` to `<game>/red4ext/plugins/CyberEngineTweaks/`, and the repository's
`scripts/` and `fonts/` folders to `<game>/bin/x64/plugins/cyber_engine_tweaks/`.

Building runs LuaJIT's own makefile, which compiles and runs its host tools (`minilua`, `buildvm`) to generate the
interpreter.
