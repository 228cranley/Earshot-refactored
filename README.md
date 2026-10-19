# EarShot

**EarShot** is an ASI plugin for the original 32-bit PC version of **Grand Theft Auto: San Andreas (1.0 US)**. It replaces supported weapon and pedestrian combat sounds with WAV files described by `.earshot` files.

EarShot uses [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk), [irrKlang 1.6](https://www.ambiera.com/irrklang/), and [subhook](https://github.com/Zeex/subhook).

## Build status

GitHub Actions runs the portable core tests and builds the GTA SA Win32 Release artifact. Pushing a tag such as `v1.0.0` creates a GitHub Release with the built plugin archive.

The workflow builds both Plugin-SDK and EarShot with the same MSVC toolset to avoid mixing compiler versions. It currently uses Visual Studio 2022 / MSVC v143 because the current Plugin-SDK source requires a modern C++ compiler.

## Build locally

1. Install Visual Studio 2022 with **Desktop development with C++**, MSVC v143 x86/x64 tools, and a Windows 10 SDK.
2. Clone Plugin-SDK into `SOURCE/repo/EarShot/plugin-sdk`:

   ```powershell
   git clone https://github.com/DK22Pac/plugin-sdk.git SOURCE/repo/EarShot/plugin-sdk
   ```

3. Generate and build Plugin-SDK from a **Developer PowerShell for VS 2022**:

   ```powershell
   cd SOURCE/repo/EarShot/plugin-sdk
   $env:PLUGIN_SDK_DIR = (Get-Location).Path
   .\tools\premake\premake5.exe vs2022 --file=.\tools\premake\premake5.lua
   msbuild .\plugin.sln /m /p:Configuration=Release '/p:Platform=Mixed Platforms' /t:Plugin_SA /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0
   ```

4. Build EarShot with the same toolset as Plugin-SDK:

   ```powershell
   msbuild .\SOURCE\repo\EarShot\EarShot.sln /m '/p:Configuration=GTASA Release' /p:Platform=x86 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0
   ```

   Or open `SOURCE/repo/EarShot/EarShot.sln` in Visual Studio and select **GTASA Release | x86**, ensuring the project toolset is v143.
5. The output is `SOURCE/release/EarShot/EarShot.asi`.

The Visual Studio project retains the legacy `v141_xp` / Windows SDK 7.0 defaults from the original project for reference. **Do not mix libraries built with different MSVC toolsets.** To produce a true legacy/Windows 7-targeted build, Plugin-SDK and EarShot must both be built with a compatible v141_xp-era Plugin-SDK revision and toolset. The current GitHub Actions artifact is built with v143; Windows 7 compatibility is not guaranteed by that artifact.

## Runtime dependency

`irrKlang.dll` is **not included** in this repository or release archive. Obtain the 32-bit DLL from a legitimate irrKlang 1.6 distribution and place it beside `gta_sa.exe`. The plugin loads it at runtime. Do not substitute a 64-bit DLL.

An ASI loader compatible with the game is also required.

## Sound files

Place `.earshot` files in the `EarShot` plugin folder or in `data/EarShot`, according to your Mod Loader setup. Each file associates a supported weapon name (and optionally a model ID/name) with a folder containing the replacement WAV sound files. The plugin's `EarShot.log` lists the sound entries it discovers when initialization succeeds.

## Tests

`SOURCE/repo/EarShot/EarShot/tests/core_test.cpp` contains platform-independent tests for the parsing and sound registry helpers. GitHub Actions builds and runs these tests on Linux.

## Русский

EarShot — ASI-плагин для оригинальной 32-битной GTA San Andreas (PC, версия 1.0 US), заменяющий поддерживаемые звуки оружия и боевых действий пешеходов WAV-файлами, описанными файлами `.earshot`.

### Сборка

GitHub Actions запускает тесты и собирает `EarShot.asi`. Чтобы создать релиз, отправьте тег вида `v1.0.0` — workflow опубликует архив сборки в GitHub Releases.

Для сборки текущего исходника Plugin-SDK используется MSVC v143. Проект сохраняет исходные настройки `v141_xp` и Windows SDK 7.0, однако современный Plugin-SDK требует более нового компилятора. **Не смешивайте `Plugin.lib`, собранную одним toolset, с EarShot, собранным другим.** Для настоящей сборки с поддержкой Windows 7 потребуется совместимая старая версия Plugin-SDK, собранная тем же v141_xp. Артефакт GitHub Actions собирается с v143, поэтому совместимость именно этого артефакта с Windows 7 не гарантируется.

`irrKlang.dll` в репозиторий не включена. Нужна 32-битная версия из официального дистрибутива irrKlang 1.6; положите DLL рядом с `gta_sa.exe`. Также нужен совместимый ASI Loader.

p.s я просто навайбкодил рефакторинг через клод сначала, а потом исправлял все баги через чатгпт
