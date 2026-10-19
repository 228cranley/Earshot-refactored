# Earshot
https://forum.mixmods.com.br/f267-scripts-codes/t7444-sa-asi-earshot

## Building

The Windows build runs in GitHub Actions (`.github/workflows/build.yml`) and produces
`EarShot.asi` for GTA San Andreas (Win32, `GTASA Release`). Pushing a `v*` tag publishes a release.

Local build: install Visual Studio with the v141 toolset, clone
[Plugin-SDK](https://github.com/DK22Pac/plugin-sdk), build its `output/lib/plugin.lib`,
and set the `PLUGIN_SDK_DIR` environment variable to the SDK folder.

Platform-independent tests (run on Linux, with sanitizers):

```sh
cd SOURCE/repo/EarShot/EarShot/tests
g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined core_test.cpp -o core_test
./core_test
```

`irrKlang.dll` is required at runtime and is not included in this repository.

See [AUDIT.md](AUDIT.md) for the list of changes and fixes.
