#include "plugin.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "CAEAudioHardware.h"
#include "CAEPedAudioEntity.h"
#include "CCamera.h"
#include "CMenuManager.h"
#include "CModelInfo.h"
#include "CTimer.h"
#include "include/irrKlang-1.6.0/include/irrKlang.h"
#define SUBHOOK_STATIC
#include "include/subhook-0.8.2/subhook.h"

#include "EarShotCore.h"

// The hook addresses below are San Andreas 1.0 (US) code addresses.
// The VC and III configurations share this source, so refuse to build them
// instead of writing jumps into unrelated code.
#ifndef GTASA
#error "EarShot hooks San Andreas 1.0 (US) code addresses. Build a GTASA configuration."
#endif

using namespace plugin;

namespace {

namespace fs = std::filesystem;
using earshot::Action;
using irrklang::ISound;
using irrklang::ISoundEngine;
using irrklang::ISoundSource;
using irrklang::vec3df;

constexpr const char *kModName = "EarShot";
constexpr const char *kModExtension = ".earshot";
constexpr int kAnyModel = -1; // "generic" sound, not tied to a model (MODELUNDEFINED)

// Original code addresses in San Andreas 1.0 (US).
constexpr std::uintptr_t kWeaponFireAddress = 0x504F80;    // CAEWeaponAudioEntity::WeaponFire
constexpr std::uintptr_t kWeaponReloadAddress = 0x503690;  // CAEWeaponAudioEntity::WeaponReload
constexpr std::uintptr_t kPedHitAddress = 0x4E1CC0;        // CAEPedAudioEntity::HandlePedHit
constexpr std::uintptr_t kPedSwingAddress = 0x4E1A40;      // CAEPedAudioEntity::HandlePedSwing

enum HookIndex { kHookWeaponFire, kHookWeaponReload, kHookPedHit, kHookPedSwing, kHookCount };

enum class Stage { Waiting, Running, Disabled };

// ---- Global state ----------------------------------------------------------

fs::path g_gameFolder;
fs::path g_modFolder;
std::size_t g_rootPrefixLength = 0;
std::ofstream g_log;

ISoundEngine *g_engine = nullptr;
bool g_scriptsReady = false;
Stage g_stage = Stage::Waiting;
bool g_pausedApplied = false;

earshot::WeaponSoundRegistry<ISoundSource> g_weapons;
std::map<fs::path, ISoundSource *> g_sourceByFile; // engine-owned; nullptr = unreadable file
std::size_t g_sourceCounter = 0;
earshot::ActiveSounds<ISound, CPhysical> g_sounds;

subhook_t g_hooks[kHookCount] = {};

// ---- Helpers ---------------------------------------------------------------

void LogLine(const std::string &text) {
    if (g_log.is_open()) {
        g_log << '\t' << text << '\n';
    }
}

void ShowMessage(const std::string &text) {
    MessageBoxA(GetActiveWindow(), text.c_str(), kModName, MB_OK);
}

void OpenLog() {
    g_log.open(g_modFolder / fs::path(kModName).replace_extension(".log"),
               std::ios::out | std::ios::trunc);
}

void CloseLog() {
    if (g_log.is_open()) {
        g_log.close();
    }
}

// ---- Audio -----------------------------------------------------------------

// Positions, volume and speed of a playing sound. Called once per frame.
void UpdateSound(ISound *sound, CPhysical *owner) {
    if (owner) {
        const CVector &position = owner->GetPosition();
        sound->setPosition(vec3df(position.x, position.y, position.z));
    }

    // getPosition() returns by value. Copy it; do not keep a pointer to the temporary.
    const vec3df location = sound->getPosition();
    const CVector &camera = TheCamera.GetPosition();
    const float distance = CVector(camera.x - location.X, camera.y - location.Y,
                                   camera.z - location.Z)
                               .Magnitude();

    sound->setVolume(earshot::ComputeVolume(AEAudioHardware.m_fEffectMasterScalingFactor, distance));
    sound->setPlaybackSpeed(earshot::PlaybackSpeed(CTimer::GetIsSlowMotionActive()));
}

// Loads an engine-owned source for one .wav file, cached per file. The engine owns
// sources, so they are never dropped here.
//
// The file is read here through std::filesystem (wide-character API on Windows) and
// handed to irrKlang as memory. irrKlang's file-name overloads take narrow strings in
// the ANSI code page, so paths with characters outside that page (e.g. Cyrillic folder
// names on some systems) could not be opened.
ISoundSource *LoadSoundSource(const fs::path &file) {
    if (const auto cached = g_sourceByFile.find(file); cached != g_sourceByFile.end()) {
        return cached->second;
    }

    ISoundSource *source = nullptr;
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        const std::streamsize size = in ? std::streamsize(in.tellg()) : std::streamsize(0);
        if (size > 0) {
            std::vector<char> bytes(static_cast<std::size_t>(size));
            in.seekg(0, std::ios::beg);
            if (in.read(bytes.data(), size)) {
                // irrKlang picks the decoder from the extension of this name. The name is
                // ASCII, so the original path encoding does not matter.
                const std::string name = "EarShot/" + std::to_string(g_sourceCounter++) + ".wav";
                source = g_engine->addSoundSourceFromMemory(bytes.data(), int(size), name.c_str(), true);
            }
        }
    }

    g_sourceByFile[file] = source;
    return source;
}

// Plays the custom sound for one event. Returns true if the game's own sound must be skipped.
bool PlayCustomSound(eWeaponType weapon, int model, Action action, CPhysical *owner) {
    ISoundSource *source = g_weapons.Find(int(weapon), model, action, LoadSoundSource);
    if (!source) {
        return false;
    }

    // Start paused so position and volume are set before the first audible frame.
    ISound *sound = g_engine->play3D(source, vec3df(0.0f, 0.0f, 0.0f), false, true);
    if (!sound) {
        // Do not retry this file every shot; fall back to the game sound.
        g_weapons.MarkMissing(int(weapon), model, action);
        LogLine("Could not play sound for weapon " + std::to_string(int(weapon)) +
                " (model " + std::to_string(model) + ")");
        return false;
    }

    sound->setMinDistance(earshot::kHearingRange);
    UpdateSound(sound, owner);
    sound->setIsPaused(false);

    // The engine returned a reference; g_sounds.Tick()/Clear() release it.
    g_sounds.Add(sound, owner);
    return true;
}

// Peds try a generic sound first, then the sound for their model.
// Other entities (vehicles, objects) only use the model-specific sound.
bool PlayForEntity(eWeaponType weapon, CPhysical *entity, Action action) {
    if (entity->m_nType == eEntityType::ENTITY_TYPE_PED &&
        PlayCustomSound(weapon, kAnyModel, action, entity)) {
        return true;
    }
    return PlayCustomSound(weapon, entity->m_nModelIndex, action, entity);
}

void ApplyPause(bool paused) {
    if (!g_engine || g_pausedApplied == paused) {
        return;
    }
    g_engine->setAllSoundsPaused(paused);
    g_pausedApplied = paused;
}

// ---- Game hooks ------------------------------------------------------------
//
// Each hook plays a custom sound when one exists and otherwise calls the original
// through the subhook trampoline. The original is never unhooked during play, so
// there is no VirtualProtect round-trip per call.
//
// Calling convention: the original methods are __thiscall (this in ECX, arguments
// on the stack, callee cleans). A __fastcall function with a dummy EDX takes the
// same layout, which is why the hooks and the trampoline calls use that form.

using WeaponEventFn = void(__fastcall *)(CAEWeaponAudioEntity *, void *, eWeaponType, CPhysical *, int);
using PedHitFn = void(__fastcall *)(CAEPedAudioEntity *, void *, int, CPhysical *, std::uint8_t, float, unsigned int);
using PedSwingFn = char(__fastcall *)(CAEPedAudioEntity *, void *, int, int, int);

void *Trampoline(HookIndex index) {
    return subhook_get_trampoline(g_hooks[index]);
}

void __fastcall HookedWeaponFire(CAEWeaponAudioEntity *self, void *, eWeaponType weaponType,
                                 CPhysical *entity, int audioEventId) {
    if (self && entity && PlayForEntity(weaponType, entity, Action::Shoot)) {
        return;
    }
    reinterpret_cast<WeaponEventFn>(Trampoline(kHookWeaponFire))(self, nullptr, weaponType, entity,
                                                                 audioEventId);
}

void __fastcall HookedWeaponReload(CAEWeaponAudioEntity *self, void *, eWeaponType weaponType,
                                   CPhysical *entity, int audioEventId) {
    if (self && entity && PlayForEntity(weaponType, entity, Action::Reload)) {
        return;
    }
    reinterpret_cast<WeaponEventFn>(Trampoline(kHookWeaponReload))(self, nullptr, weaponType, entity,
                                                                   audioEventId);
}

void __fastcall HookedPedHit(CAEPedAudioEntity *self, void *, int a2, CPhysical *a3, std::uint8_t a4,
                             float a5, unsigned int a6) {
    if (self && a3 && self->m_pPed) {
        const auto weapon = self->m_pPed->m_aWeapons[self->m_pPed->m_nSelectedWepSlot].m_eWeaponType;
        if (PlayForEntity(weapon, a3, Action::Hit)) {
            return;
        }
    }
    reinterpret_cast<PedHitFn>(Trampoline(kHookPedHit))(self, nullptr, a2, a3, a4, a5, a6);
}

char __fastcall HookedPedSwing(CAEPedAudioEntity *self, void *, int a2, int a3, int a4) {
    if (self && self->m_pPed) {
        CPed *ped = self->m_pPed;
        const auto weapon = ped->m_aWeapons[ped->m_nSelectedWepSlot].m_eWeaponType;
        if (PlayForEntity(weapon, (CPhysical *)ped, Action::Swing)) {
            return char(-1);
        }
    }
    return reinterpret_cast<PedSwingFn>(Trampoline(kHookPedSwing))(self, nullptr, a2, a3, a4);
}

// ---- Hook lifetime ---------------------------------------------------------

void RemoveHooks() {
    for (subhook_t hook : g_hooks) {
        if (hook && subhook_is_installed(hook)) {
            subhook_remove(hook); // must come before free: free does not unpatch the game code
        }
    }
}

void FreeHooks() {
    for (subhook_t &hook : g_hooks) {
        subhook_free(hook); // accepts nullptr
        hook = nullptr;
    }
}

bool InstallHooks() {
    const std::uintptr_t targets[kHookCount] = {kWeaponFireAddress, kWeaponReloadAddress, kPedHitAddress,
                                                kPedSwingAddress};
    void *detours[kHookCount] = {
        reinterpret_cast<void *>(&HookedWeaponFire), reinterpret_cast<void *>(&HookedWeaponReload),
        reinterpret_cast<void *>(&HookedPedHit), reinterpret_cast<void *>(&HookedPedSwing)};

    for (int index = 0; index < kHookCount; ++index) {
        g_hooks[index] = subhook_new(reinterpret_cast<void *>(targets[index]), detours[index],
                                     subhook_flags_t(0));
        if (!g_hooks[index]) {
            FreeHooks();
            return false;
        }
    }

    for (int index = 0; index < kHookCount; ++index) {
        if (subhook_install(g_hooks[index]) != 0) {
            RemoveHooks();
            FreeHooks();
            return false;
        }
    }
    return true;
}

// ---- Weapon folders --------------------------------------------------------

void RegisterWeapon(const fs::path &file) {
    const earshot::WeaponStem parsed = earshot::ParseWeaponStem(file.stem().string());

    // "Deagle 348.earshot" also applies to model 348 (by model name).
    int model = kAnyModel;
    if (parsed.hasModel) {
        std::string modelName = parsed.model;
        int found = kAnyModel;
        CModelInfo::GetModelInfo(&modelName[0], &found);
        if (found >= 0) {
            model = found;
        }
    }

    std::string weaponName = earshot::ToUpperAscii(parsed.weapon);
    const eWeaponType weapon = CWeaponInfo::FindWeaponType(&weaponName[0]);
    if (weapon <= eWeaponType::WEAPONTYPE_UNARMED) {
        LogLine("Unknown weapon name: " + parsed.weapon);
        return;
    }

    g_weapons.Register(int(weapon), model, file.parent_path());
    LogLine(std::to_string(int(weapon)) + " : " + std::to_string(model) + " : " +
            earshot::StripPrefix(file, g_rootPrefixLength));
}

void ScanModFolder() {
    LogLine("File(s) (Overwritten by Mod Loader):");
    std::error_code ec;
    for (fs::recursive_directory_iterator it(g_modFolder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path &path = it->path();
        if (fs::is_directory(path, ec)) {
            continue;
        }
        if (earshot::ToLowerAscii(path.extension().string()) == kModExtension) {
            RegisterWeapon(path);
        }
    }
}

// Files registered by AutoID3000ML (Mod Loader) are reported through its exported function.
void AppendModLoaderFiles() {
    using GetFilesFn = const char **(__cdecl *)(const char *, int *);

    HMODULE module = GetModuleHandleA("AutoID3000ML.dll");
    if (!module) {
        LogLine("Mod Loader support requires AutoID3000.");
        return;
    }
    auto getFiles = reinterpret_cast<GetFilesFn>(GetProcAddress(module, "getFiles"));
    if (!getFiles) {
        return;
    }

    LogLine("File(s) (Mod Loader):");
    int amount = 0;
    const char **files = getFiles(kModExtension, &amount);
    --amount; // The last entry is not a file (original behaviour).
    for (int index = 0; files && index < amount; ++index) {
        if (files[index]) {
            RegisterWeapon(g_gameFolder / fs::path(files[index]));
        }
    }
}

// ---- Lifecycle -------------------------------------------------------------

void CreateEngine() {
    if (g_engine) {
        return;
    }
    g_engine = irrklang::createIrrKlangDevice();
    if (g_engine) {
        g_engine->setRolloffFactor(0.0f);
    }
}

// Runs once, on the first frame after the game has loaded its scripts (models are then
// available for name lookups). Returns false if EarShot should stay inactive this session.
bool Initialize() {
    OpenLog();

    if (!g_engine) {
        CloseLog();
        ShowMessage("Could not start up irrKlang audio engine.");
        return false;
    }

    ScanModFolder();
    AppendModLoaderFiles();

    if (g_weapons.Size() == 0) {
        LogLine("");
        LogLine("No file(s) found. Stopping work for this session.");
        CloseLog();
        return false;
    }

    LogLine("");
    LogLine("Total: " + std::to_string(g_weapons.Size()));

    if (!InstallHooks()) {
        LogLine("Hook installation failed. Stopping work for this session.");
        CloseLog();
        return false;
    }

    CloseLog();
    return true;
}

void OnGameProcess() {
    if (g_stage == Stage::Waiting) {
        if (!g_scriptsReady) {
            return;
        }
        g_stage = Initialize() ? Stage::Running : Stage::Disabled;
    }
    if (g_stage != Stage::Running) {
        return;
    }

    const CVector &camera = TheCamera.GetPosition();
    g_engine->setListenerPosition(vec3df(camera.x, camera.y, camera.z), vec3df(0.0f, 0.0f, 0.0f),
                                  vec3df(0.0f, 0.0f, 0.0f), vec3df(0.0f, 0.0f, 1.0f));

    g_sounds.Tick([](auto &entry) { UpdateSound(entry.sound, entry.owner); });

    ApplyPause(FrontEndMenuManager.m_bMenuActive);
}

// Releases everything the plugin owns. Hooks come off first, so the game never
// jumps into a freed trampoline; then sounds, then the engine.
void Shutdown() {
    // Every step is safe to run when the feature never started, so no early-out is needed.
    g_stage = Stage::Disabled;

    RemoveHooks();
    FreeHooks();
    g_sounds.Clear();
    g_weapons.Clear();

    if (g_engine) {
        g_engine->drop(); // also releases the sources it owns
        g_engine = nullptr;
    }
    g_sourceByFile.clear();
    CloseLog();
}

class EarShot {
public:
    EarShot() {
        g_gameFolder = fs::path(GAME_PATH(""));
        g_rootPrefixLength = g_gameFolder.string().length();

        g_modFolder = fs::path(PLUGIN_PATH("")) / fs::path(kModName);
        const fs::path dataFolder = g_gameFolder / "data" / kModName;
        std::error_code ec;
        if (fs::exists(dataFolder, ec)) {
            g_modFolder = dataFolder;
        } else {
            fs::create_directories(g_modFolder, ec);
        }

        Events::initRwEvent += [] { CreateEngine(); };
        Events::processScriptsEvent += [] { g_scriptsReady = true; };
        Events::gameProcessEvent += [] { OnGameProcess(); };
        Events::onPauseAllSounds += [] { ApplyPause(true); };
        Events::onResumeAllSounds += [] {
            if (!FrontEndMenuManager.m_bMenuActive) {
                ApplyPause(false);
            }
        };
    }

    ~EarShot() { Shutdown(); }
};

} // namespace

EarShot earShot;
