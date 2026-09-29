# Indiana Jones and the Emperor's Tomb (PC) as a reference

Same engine (The Collective's "Slayer": `GCore`, `GScript_*`, `R_D3D`) and developer, two years older
(2003). Steam app 560430; depot 560431 has only ever had one manifest (the 1.01 patch).

## What it ships

- `GameData\bin\*.map`: MSVC linker maps for `GCore.dll`, `G_Indy.sgl`, `GScript_Indy.dll`, `R_D3D.dll`,
  `indy.exe` -- about 40,000 named functions and data symbols (decorated C++ names).
- The maps are from the **1.0** build (March 2003); the DLLs are **1.01** (April 2003). Addresses shift
  between them; `tools/symbols` re-aligns them (see its README). `indy.exe` is encrypted by Steam's DRM.
- Configs in the same engine format as ours: `vars.cfg` (console variables), `binds.cfg` (`setbind` keyboard,
  mouse and controller bindings), `resource.cfg` (`SET_DIRECTORY` resource type table), `default.cfg`.

## Findings

- Class vtables: `GetTypeName` returns the class name (in our game it is always slot 3; in Indy it moves).
- Console variables are registered in `TGameOptions::InitConsole` (e.g. `fixedtick`, `timermax`,
  `singletick`, `microsecondmin`, `fpsLimit`); the same names exist in our executable.
- The engine has fixed-tick simulation and interpolation (`ICharacter::Interpolate`, `TCharState::Interpolate`,
  `RequiresFixedTick`, `ShouldBonesUpdateEveryFixedTick`): a lead for proper 60 fps.
- Our Xbox build has many virtual methods compiled down to stubs (level-editor `SlayEd_*`, debug rendering);
  vtable layouts of base classes (`IObject`, `IInstance`) otherwise stay in order.
- Code differs a lot between the games (compiler and two years of changes): identical functions are rare, so
  names carry over mainly through vtables, shared strings and rare constants (tools/symbols/report).
