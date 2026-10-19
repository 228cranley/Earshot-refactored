# Re-EarShot

Re-EarShot — это переработанная версия оригинального плагина EarShot для GTA San Andreas, созданная на основе NextGen Weapon Sounds.

Проект направлен на улучшение исходного кода, исправление ошибок, улучшение управления памятью и упрощение использования пользовательских звуков оружия.

## Изменения
- Исправлена обработка путей, содержащих кириллицу.
- Проведён рефакторинг и оптимизация кода.
- Исправлены утечки памяти и улучшено управление ресурсами.
- Улучшена инициализация игровых событий.
- Поддерживается замена звуков оружия с помощью WAV-файлов.

## Установка
1. Скачайте `EarShot.asi` со страницы [Releases](../../releases)и поместите `EarShot.asi` в корневую папку GTA San Andreas, рядом с `gta_sa.exe`.
2. Создайте папку `EarShot` в корневой папке игры.
3. Скачайте 32-битную версию irrKlang 1.6. и поместите в ту же папку, где находится `gta_sa.exe`.

## Настройка звуков

Вы можете использовать собственные WAV-файлы или скачать готовый набор звуков:

**[Скачать готовый пак звуков Re-EarShot(на основе NextGen Weapon Sounds)](https://www.mediafire.com/file/2to3v3ddnzl2zpk/Re-Earshot.rar/file)**

Чтобы установить набор звуков:

1. Распакуйте архив.
2. Поместите папку с набором звуков внутрь каталога `EarShot` в корневой папке GTA San Andreas.
3. Убедитесь, что набор содержит необходимые WAV-файлы и соответствующий файл с расширением `.earshot`.

Пример структуры папок:

```text
GTA San Andreas/
├── gta_sa.exe
├── EarShot.asi
├── irrKlang.dll
└── EarShot/
    └── FolderName/
        ├── WeaponName.earshot
        ├── shoot.wav
        ├── reload.wav
        ├── hit.wav
        └── swing.wav
```

Название папки и файла `.earshot` должно соответствовать правилам именования оружия, которые использует плагин. Вы можете создавать несколько earshot файлов с названиями оружиями в 1 папке, чтобы два разных оружия использовали единый пак звуков.

### Поддерживаемые имена звуковых файлов

| Имя файла | Звук |
|---|---|
| `shoot.wav` | Выстрел |
| `reload.wav` | Перезарядка |
| `hit.wav` | Удар |
| `swing.wav` | Замах |

Вы можете заменять отдельные WAV-файлы, сохраняя их исходные имена. Если звуковой файл отсутствует или не может быть загружен, плагин использует оригинальный звук игры.

После внесения изменений перезапустите игру.

## Требования

- Grand Theft Auto: San Andreas для ПК.
- ASI Loader.
- 32-битная библиотека `irrKlang.dll` из irrKlang 1.6.

## Отказ от ответственности

Re-EarShot — неофициальная пользовательская модификация. Grand Theft Auto: San Andreas и все связанные с ней товарные знаки принадлежат соответствующим правообладателям.

------------------------------------------------------------------------------------------

Re-EarShot is a refactored version of the original EarShot plugin for GTA San Andreas, based on NextGen Weapon Sounds.

The project focuses on improving the original code, fixing bugs, improving memory management, and making custom weapon sound replacement easier to use.

## Changes

- Fixed handling of paths containing Cyrillic characters.
- Refactored and optimized the code.
- Fixed memory leaks and improved resource management.
- Improved game event initialization.
- Supports replacing weapon sounds using WAV files.

## Installation

1. Download `EarShot.asi` from the [Releases](../../releases) page and place it in your GTA San Andreas root directory, next to `gta_sa.exe`.
2. Create an `EarShot` folder in the game root directory.
3. Download the 32-bit version of irrKlang 1.6 and place `irrKlang.dll` in the same directory as `gta_sa.exe`.

## Sound Setup

You can use your own WAV files or download the ready-made sound pack:

**[Download the Re-EarShot sound pack (based on NextGen Weapon Sounds)](https://www.mediafire.com/file/2to3v3ddnzl2zpk/Re-Earshot.rar/file)**

To install a sound pack:

1. Extract the archive.
2. Place the sound-set folder inside the `EarShot` directory in your GTA San Andreas root folder.
3. Make sure the folder contains the required WAV files and the corresponding `.earshot` file.

Example folder structure:

```text
GTA San Andreas/
├── gta_sa.exe
├── EarShot.asi
├── irrKlang.dll
└── EarShot/
    └── FolderName/
        ├── WeaponName.earshot
        ├── shoot.wav
        ├── reload.wav
        ├── hit.wav
        └── swing.wav
```

The folder and `.earshot` filenames must follow the weapon naming conventions expected by the plugin. You can place multiple `.earshot` files with different weapon names in the same folder, allowing multiple weapons to use the same sound pack.

### Supported Sound Filenames

| Filename | Sound |
|---|---|
| `shoot.wav` | Gunshot |
| `reload.wav` | Reloading |
| `hit.wav` | Impact |
| `swing.wav` | Swing |

You can replace individual WAV files while keeping their original filenames. If a sound file is missing or cannot be loaded, the plugin uses the original in-game sound.

Restart the game after making changes.

## Requirements

- Grand Theft Auto: San Andreas for PC.
- ASI Loader.
- The 32-bit `irrKlang.dll` library from irrKlang 1.6.

## Disclaimer

Re-EarShot is an unofficial community-made modification. Grand Theft Auto: San Andreas and all related trademarks belong to their respective owners.
