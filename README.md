# EarShot

Original project: <https://forum.mixmods.com.br/f267-scripts-codes/t7444-sa-asi-earshot>

This repository contains a refactored source version of EarShot, a GTA San Andreas ASI plugin that replaces supported weapon/ped sounds with custom `.earshot` registrations and adjacent WAV files.

## Build on GitHub

The GitHub Actions workflow builds the **GTA San Andreas Release** configuration on push, pull request, or manual dispatch. To run it manually, open **Actions → Build EarShot (GTA San Andreas) → Run workflow**. The generated `EarShot.asi` is uploaded as the `EarShot-GTASA` artifact.

The workflow builds against the current `DK22Pac/plugin-sdk` source. The hooks in this project use GTA San Andreas executable addresses; the legacy GTA III / Vice City project configurations are not validated by this workflow.

## Runtime dependency

The source archive contains the irrKlang headers and import library, but not `irrKlang.dll`. A compatible 32-bit `irrKlang.dll` is required at runtime and must be acquired and distributed in accordance with the irrKlang license. The CI workflow uploads the `.asi` only and does not bundle that DLL.

## Audit

See [`AUDIT.md`](AUDIT.md) for the findings fixed, compatibility notes, and remaining verification items.
