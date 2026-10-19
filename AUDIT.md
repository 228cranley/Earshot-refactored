# EarShot source audit and refactor notes

This audit is based on the source archive supplied for the original `HzanRsxa2959/Earshot` repository. It does not claim that the resulting binary has been tested inside GTA SA or multiplayer clients.

## Findings fixed

1. **Tracked irrKlang sounds were never released.** `play3D(..., startPaused=true, ...)` returns a tracked `ISound` interface whose reference must be dropped when the caller is done with it. The old code removed finished sounds from the vector without calling `drop()`. The refactor now drops the reference on completion, on vector-allocation failure, and on shutdown.
2. **The audio engine had no teardown path.** Added cleanup at `Events::shutdownRwEvent`: remove/free hooks, stop and release tracked sounds, stop and drop the engine, clear the camera pointer, and close the log if open.
3. **A temporary string was used through a pointer.** The old registration code took the address of `string()` and then kept using it. It now uses ordinary local `std::string` values and bounded, null-terminated character buffers for SDK APIs.
4. **A WAV's existence was treated as a successful replacement.** `AudioStream::play` now returns the actual result of `play3D`. If the engine cannot create the sound, the hook lets GTA's original sound path run instead of silently suppressing it.
5. **Several unchecked null / initialization cases.** The engine is checked before use; audio state tolerates the camera position not yet being available; game hooks avoid dereferencing null audio entities/ped pointers; pause callbacks verify that the engine still exists.
6. **Game entities could be dereferenced after destruction.** The old playing-sounds list retained raw `CPhysical*` pointers and dereferenced them every frame. The refactored list stores only `ISound*`; the emitter position is captured at the event, so the sound no longer needs a potentially stale game-entity pointer.
7. **Filesystem and logging operations were fragile.** Directory creation/scanning use `std::error_code` and skip inaccessible entries; regular-file checks avoid throwing from hot paths; path logging no longer erases a fixed number of characters from an arbitrary path.
8. **Unsafe / brittle string handling.** ASCII case conversion now casts to `unsigned char` for the C character routines, and fixed 255-byte `sprintf` buffers are replaced with sized `std::vector<char>` buffers.
9. **Hook setup previously ignored failures.** Hook creation and installation results are checked. Partial setup is cleaned up, and the plugin reports the failure rather than proceeding in an unknown state.

## Behavior and compatibility notes

- The hooks at `0x504F80`, `0x503690`, `0x4E1CC0`, and `0x4E1A40` are GTA San Andreas addresses. The automated build therefore targets **GTASA Release only**. The other legacy configurations in the original Visual Studio project are not asserted to work; they should be removed or given game-specific hook addresses before being advertised as supported.
- Spatial positions are now captured when each sound event occurs instead of following the entity every frame. This avoids dereferencing destroyed entities and matches the event-position semantics usually desired for one-shot weapon sounds. Listener position and orientation are updated from the game camera (with a safe fallback if the camera matrix is unavailable), rather than sending a zero look direction to irrKlang. Test longer reload/melee samples in-game to decide whether entity-following should be restored with a safe entity-lifetime mechanism.
- The AutoID3000 Mod Loader integration retains the original `fileCount - 1` convention because the supplied source does not document the export's count contract. Verify that API contract separately; if the count is the exact number of returned paths, this decrement would skip the final item.
- The supplied archive contains `irrKlang.lib` and headers but not `irrKlang.dll`. The CI artifact is the compiled `.asi`; a runnable deployment must also provide the compatible 32-bit `irrKlang.dll` according to irrKlang's redistribution terms. The build pipeline intentionally does not fetch or redistribute that binary.

## Automated build

`.github/workflows/build.yml` checks out Plugin-SDK, generates/builds its GTA SA static library, builds `EarShot.asi` with MSBuild, verifies the output exists, and uploads it as a GitHub Actions artifact. Run it from **Actions → Build EarShot (GTA San Andreas) → Run workflow**, or let it run on a push / pull request.

## Verification status

- Source archive and Visual Studio project structure inspected.
- Visual Studio project XML parsed successfully after edits.
- The workflow YAML structure parsed successfully; the configured GTA SA import library matches Plugin-SDK's documented `output/lib/plugin.lib` artifact.
- Static checks confirmed the sound-handle `drop()` paths, hook cleanup, and fallback flow are present; this is not a substitute for compiler or runtime testing.
- A Windows/MSVC build has **not** been executed in this environment; the first GitHub Actions run is the definitive compiler/linker check.
- Runtime verification still requires launching the mod in the intended GTA SA version and testing custom WAV success/fallback, pausing, rapid-fire loads, shutdown, and repeated startup/shutdown.
