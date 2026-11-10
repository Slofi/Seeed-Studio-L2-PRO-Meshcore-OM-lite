# MeshCore companion code, vendored unmodified

These six source files are upstream MeshCore's `examples/companion_radio` at tag
`companion-v1.18.0` (commit `81dbae5`), copied byte for byte:

- `MyMesh.cpp`, `MyMesh.h`
- `DataStore.cpp`, `DataStore.h`
- `NodePrefs.h`
- `AbstractUITask.h`

MIT licence; see `LICENSE-MeshCore-MIT.txt` in this folder.

**Do not edit anything here.** Our board glue lives in `src/target.h` / `src/target.cpp`
and our screens talk to the mesh through the `src/mesh_link.h` facade. A MeshCore
update is `tools/update_meshcore.py <tag>` (run by Claude), which replaces this folder
and writes `lib/MESHCORE_VERSION` with the sha256 manifest that
`test/host/test_meshcore_manifest.py` checks.

`library.json` is the only local file (PlatformIO build metadata); the updater keeps it.
