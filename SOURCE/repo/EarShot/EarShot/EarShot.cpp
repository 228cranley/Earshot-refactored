#include "plugin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "CModelInfo.h"
#include "CCamera.h"
#include "CAEAudioHardware.h"
#include "CTimer.h"
#include "CMenuManager.h"
#include "CAEPedAudioEntity.h"
#include "include/irrKlang-1.6.0/include/irrKlang.h"
#define SUBHOOK_STATIC
#include "include/subhook-0.8.2/subhook.h"

using namespace plugin;
namespace fs = std::filesystem;

namespace {

const std::string kModName = "EarShot";
const std::string kModExtension = ".earshot";
constexpr float kMaxDistance = 150.0f;
constexpr int kModelUndefined = -1;

static fs::path gameRoot = fs::path(GAME_PATH(""));
static fs::path modFolder = fs::path(PLUGIN_PATH("")) / kModName;
static fs::path dataFolder = gameRoot / "data" / kModName;

static irrklang::ISoundEngine* audioEngine = nullptr;
static CVector* cameraPosition = nullptr;
static std::fstream logFile;
static int initializationStatus = -2;

struct PlayingAudio {
    irrklang::ISound* sound = nullptr;
};
static std::vector<PlayingAudio> playingSounds;


static subhook_t hookWeaponFire = nullptr;
static subhook_t hookWeaponReload = nullptr;
static subhook_t hookPedHit = nullptr;
static subhook_t hookPedSwing = nullptr;

#define LOG_LINE logFile << "\t" <<
#define LOG_SEPARATOR << " : " <<
#define LOG_NEWLINE << '\n'

static void showMessage(const std::string& text, UINT flags = MB_OK) {
    MessageBoxA(GetActiveWindow(), text.c_str(), kModName.c_str(), flags);
}

static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

static std::string relativePath(const fs::path& path) {
    std::error_code ec;
    const auto relative = fs::relative(path, gameRoot, ec);
    return ec ? path.string() : relative.string();
}

static void updateAudioState(irrklang::ISound* sound, CPhysical* entity) {
    if (!sound) return;

    // Set the position once when the event occurs. Keeping a raw CPhysical*
    // until playback completes is unsafe because the game can destroy the entity.
    if (entity) {
        const auto& position = entity->GetPosition();
        sound->setPosition(irrklang::vec3df(position.x, position.y, position.z));
    }

    float volume = AEAudioHardware.m_fEffectMasterScalingFactor;
    if (cameraPosition) {
        const auto& position = sound->getPosition();
        const float distance = CVector(
            cameraPosition->x - position.X,
            cameraPosition->y - position.Y,
            cameraPosition->z - position.Z
        ).Magnitude();
        const float clampedDistance = std::clamp(distance, 0.0f, kMaxDistance);
        const float distanceFactor = std::pow((kMaxDistance - clampedDistance) / kMaxDistance, 6.0f);
        volume *= distanceFactor;
    }

    sound->setVolume(std::clamp(volume, 0.0f, 1.0f));
    sound->setPlaybackSpeed(CTimer::GetIsSlowMotionActive() ? 0.5f : 1.0f);
}

static bool playAudio(const fs::path& audioPath, CPhysical* entity) {
    if (!audioEngine || audioPath.empty()) return false;

    // startPaused=true forces irrKlang to return a tracked interface. We own
    // that reference and release it when playback finishes or during shutdown.
    auto* sound = audioEngine->play3D(
        audioPath.string().c_str(), irrklang::vec3df(0.0f, 0.0f, 0.0f), false, true
    );
    if (!sound) return false;

    sound->setMinDistance(kMaxDistance);
    updateAudioState(sound, entity);

    try {
        playingSounds.push_back({ sound });
    }
    catch (...) {
        sound->stop();
        sound->drop();
        return false;
    }

    sound->setIsPaused(false);
    return true;
}

class AudioStream {
public:
    AudioStream() = default;
    explicit AudioStream(const fs::path& weaponFolder) : audioFolder(weaponFolder) {}

    bool play(const std::string& filename, CPhysical* entity) const {
        const fs::path path = audioFolder / fs::path(filename).replace_extension(".wav");
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) return false;
        // Do not claim the override succeeded merely because the WAV exists.
        return playAudio(path, entity);
    }

private:
    fs::path audioFolder;
};

static std::map<std::pair<eWeaponType, eModelID>, AudioStream> registeredWeapons;

static bool getWeaponType(const std::string& weaponName, eWeaponType& weaponType) {
    const auto normalized = toUpper(weaponName);
    std::vector<char> buffer(normalized.begin(), normalized.end());
    buffer.push_back('\0');
    weaponType = CWeaponInfo::FindWeaponType(buffer.data());
    return weaponType > eWeaponType::WEAPON_UNARMED;
}

static void registerWeapon(const fs::path& path) {
    std::string weaponName = path.stem().string();
    eModelID modelId = eModelID(kModelUndefined);

    const auto separator = weaponName.find(' ');
    if (separator != std::string::npos) {
        const auto modelName = weaponName.substr(separator + 1);
        std::vector<char> modelBuffer(modelName.begin(), modelName.end());
        modelBuffer.push_back('\0');

        int modelIndex = kModelUndefined;
        CModelInfo::GetModelInfo(modelBuffer.data(), &modelIndex);
        if (modelIndex > kModelUndefined) modelId = eModelID(modelIndex);
        weaponName.resize(separator);
    }

    eWeaponType weaponType;
    if (!getWeaponType(weaponName, weaponType)) return;

    registeredWeapons[std::make_pair(weaponType, modelId)] = AudioStream(path.parent_path());
    if (logFile.is_open()) {
        LOG_LINE weaponType LOG_SEPARATOR modelId LOG_SEPARATOR relativePath(path) LOG_NEWLINE;
    }
}

static bool findWeapon(eWeaponType weaponType, eModelID modelId,
                       const std::string& filename, CPhysical* entity) {
    const auto found = registeredWeapons.find(std::make_pair(weaponType, modelId));
    return found != registeredWeapons.end() && found->second.play(filename, entity);
}

static void releaseHook(subhook_t& hook) {
    if (!hook) return;
    subhook_remove(hook);
    subhook_free(hook);
    hook = nullptr;
}

static void releaseHooks() {
    releaseHook(hookWeaponFire);
    releaseHook(hookWeaponReload);
    releaseHook(hookPedHit);
    releaseHook(hookPedSwing);
}

static void releasePlayingSounds(bool stopSounds) {
    for (auto& playing : playingSounds) {
        if (!playing.sound) continue;
        if (stopSounds) playing.sound->stop();
        playing.sound->drop();
        playing.sound = nullptr;
    }
    playingSounds.clear();
}

static void shutdownAudio() {
    releaseHooks();
    releasePlayingSounds(true);
    if (audioEngine) {
        audioEngine->stopAllSounds();
        audioEngine->drop();
        audioEngine = nullptr;
    }
    cameraPosition = nullptr;
    if (logFile.is_open()) logFile.close();
}

#define MODEL_UNDEFINED eModelID(kModelUndefined)
#define AUDIO_PLAY(MODEL_ID, FILE_STEM, RETURN_VALUE) \
    if (findWeapon(weaponType, eModelID(MODEL_ID), FILE_STEM, entity)) RETURN_VALUE;
#define AUDIO_SHOOT(MODEL_ID, RETURN_VALUE) AUDIO_PLAY(MODEL_ID, "shoot", RETURN_VALUE)
#define AUDIO_RELOAD(MODEL_ID, RETURN_VALUE) AUDIO_PLAY(MODEL_ID, "reload", RETURN_VALUE)
#define AUDIO_HIT(MODEL_ID, RETURN_VALUE) AUDIO_PLAY(MODEL_ID, "hit", RETURN_VALUE)
#define AUDIO_SWING(MODEL_ID, RETURN_VALUE) AUDIO_PLAY(MODEL_ID, "swing", RETURN_VALUE)
#define AUDIO_CALL(AUDIO_MACRO, RETURN_VALUE) \
    if (entity->m_nType == eEntityType::ENTITY_TYPE_PED) { AUDIO_MACRO(MODEL_UNDEFINED, RETURN_VALUE) } \
    AUDIO_MACRO(entity->m_nModelIndex, RETURN_VALUE)

static void __fastcall hookedWeaponFire(CAEWeaponAudioEntity* self, void*,
                                        eWeaponType weaponType, CPhysical* entity, int audioEventId) {
    if (!self) return;
    if (entity) { AUDIO_CALL(AUDIO_SHOOT, return); }

    subhook_remove(hookWeaponFire);
    self->WeaponFire(weaponType, entity, audioEventId);
    subhook_install(hookWeaponFire);
}

static void __fastcall hookedWeaponReload(CAEWeaponAudioEntity* self, void*,
                                          eWeaponType weaponType, CPhysical* entity, int audioEventId) {
    if (!self) return;
    if (entity) { AUDIO_CALL(AUDIO_RELOAD, return); }

    subhook_remove(hookWeaponReload);
    self->WeaponReload(weaponType, entity, audioEventId);
    subhook_install(hookWeaponReload);
}

static void __fastcall hookedPedHit(CAEPedAudioEntity* self, void*, int a2, CPhysical* entity,
                                    unsigned __int8 a4, float a5, unsigned int a6) {
    if (!self) return;
    if (entity && self->m_pPed) {
        auto weaponType = self->m_pPed->m_aWeapons[self->m_pPed->m_nActiveWeaponSlot].m_nType;
        AUDIO_CALL(AUDIO_HIT, return);
    }

    subhook_remove(hookPedHit);
    plugin::CallMethod<0x4E1CC0, CAEPedAudioEntity*, int, CPhysical*, unsigned __int8, float, unsigned int>(
        self, a2, entity, a4, a5, a6
    );
    subhook_install(hookPedHit);
}

static char __fastcall hookedPedSwing(CAEPedAudioEntity* self, void*, int a2, int a3, int a4) {
    if (!self) return 0;
    auto* ped = self->m_pPed;
    if (ped) {
        auto* entity = static_cast<CPhysical*>(ped);
        auto weaponType = ped->m_aWeapons[ped->m_nActiveWeaponSlot].m_nType;
        AUDIO_CALL(AUDIO_SWING, return char(-1));
    }

    subhook_remove(hookPedSwing);
    const auto returnValue = plugin::CallMethodAndReturn<char, 0x4E1A40, CAEPedAudioEntity*, int, int, int>(
        self, a2, a3, a4
    );
    subhook_install(hookPedSwing);
    return returnValue;
}

static bool installHooks() {
    hookWeaponFire = subhook_new(reinterpret_cast<void*>(0x504F80),
                                 reinterpret_cast<void*>(hookedWeaponFire), subhook_flags_t(0));
    hookWeaponReload = subhook_new(reinterpret_cast<void*>(0x503690),
                                   reinterpret_cast<void*>(hookedWeaponReload), subhook_flags_t(0));
    hookPedHit = subhook_new(reinterpret_cast<void*>(0x4E1CC0),
                              reinterpret_cast<void*>(hookedPedHit), subhook_flags_t(0));
    hookPedSwing = subhook_new(reinterpret_cast<void*>(0x4E1A40),
                                reinterpret_cast<void*>(hookedPedSwing), subhook_flags_t(0));

    if (!hookWeaponFire || !hookWeaponReload || !hookPedHit || !hookPedSwing) {
        releaseHooks();
        return false;
    }

    if (subhook_install(hookWeaponFire) != 0 ||
        subhook_install(hookWeaponReload) != 0 ||
        subhook_install(hookPedHit) != 0 ||
        subhook_install(hookPedSwing) != 0) {
        releaseHooks();
        return false;
    }
    return true;
}

class EarShot {
public:
    EarShot() {
        std::error_code ec;
        if (fs::is_directory(dataFolder, ec) && !ec) {
            modFolder = dataFolder;
        } else {
            ec.clear();
            fs::create_directories(modFolder, ec);
            if (ec) {
                showMessage("Could not create EarShot folder: " + ec.message());
                initializationStatus = -1;
                return;
            }
        }

        logFile.open(modFolder / (kModName + ".log"), std::ios::out | std::ios::trunc);
        if (!logFile.is_open()) {
            showMessage("Could not open EarShot log file.");
        }

        Events::initRwEvent += [] {
            if (audioEngine) return;
            audioEngine = irrklang::createIrrKlangDevice();
            if (audioEngine) audioEngine->setRolloffFactor(0.0f);
        };

        Events::shutdownRwEvent += [] {
            shutdownAudio();
            initializationStatus = -1;
        };

        Events::processScriptsEvent += [] {
            if (initializationStatus != -2) return;
            initializationStatus = 0;

            Events::gameProcessEvent += [] {
                if (initializationStatus == 0) {
                    if (!audioEngine) {
                        showMessage("Could not start up irrKlang audio engine.");
                        initializationStatus = -1;
                        return;
                    }

                    if (logFile.is_open()) logFile << "File(s) (Overwritten by Mod Loader):" LOG_NEWLINE;
                    std::error_code scanError;
                    for (fs::recursive_directory_iterator it(
                             modFolder, fs::directory_options::skip_permission_denied, scanError), end;
                         !scanError && it != end; it.increment(scanError)) {
                        std::error_code fileError;
                        if (!it->is_regular_file(fileError) || fileError) continue;
                        if (toLower(it->path().extension().string()) == kModExtension) {
                            registerWeapon(it->path());
                        }
                    }
                    if (scanError && logFile.is_open()) {
                        logFile << "Directory scan stopped: " << scanError.message() << LOG_NEWLINE;
                    }

                    auto module = GetModuleHandleA("AutoID3000ML.dll");
                    if (module) {
                        auto getFiles = GetProcAddress(module, "getFiles");
                        if (getFiles) {
                            using AutoIdFiles = const char**(__cdecl*)(const char*, int*);
                            auto getModLoaderFiles = reinterpret_cast<AutoIdFiles>(getFiles);
                            int fileCount = 0;
                            const char** files = getModLoaderFiles(kModExtension.c_str(), &fileCount);
                            // The original API integration reserves the final entry; keep
                            // its established convention but guard invalid/empty results.
                            if (fileCount > 0) --fileCount;
                            if (files && fileCount > 0) {
                                if (logFile.is_open()) logFile << "File(s) (Mod Loader):" LOG_NEWLINE;
                                for (int index = 0; index < fileCount; ++index) {
                                    if (files[index]) registerWeapon(gameRoot / fs::path(files[index]));
                                }
                            }
                        }
                    } else if (logFile.is_open()) {
                        logFile << "Mod Loader support requires AutoID3000." << LOG_NEWLINE;
                    }

                    if (registeredWeapons.empty()) {
                        if (logFile.is_open()) logFile << "No audio files found. Stopping work for this session." LOG_NEWLINE;
                        shutdownAudio();
                        initializationStatus = -1;
                        return;
                    }
                    if (logFile.is_open()) logFile << "Total: " << registeredWeapons.size() << LOG_NEWLINE;

                    if (!installHooks()) {
                        if (logFile.is_open()) logFile << "Failed to install one or more hooks." LOG_NEWLINE;
                        showMessage("EarShot could not install its game hooks. Check the log file.");
                        shutdownAudio();
                        initializationStatus = -1;
                        return;
                    }

                    Events::onPauseAllSounds += [] {
                        if (audioEngine) audioEngine->setAllSoundsPaused(true);
                    };
                    Events::onResumeAllSounds += [] {
                        if (audioEngine && !FrontEndMenuManager.m_bMenuActive) audioEngine->setAllSoundsPaused(false);
                    };
                    Events::gameProcessEvent += [] {
                        if (audioEngine) audioEngine->setAllSoundsPaused(FrontEndMenuManager.m_bMenuActive);
                    };
                    if (logFile.is_open()) logFile.close();
                    initializationStatus = 1;
                }

                if (initializationStatus != 1 || !audioEngine) return;

                cameraPosition = &TheCamera.GetPosition();
                const auto forward = TheCamera.m_matrix
                    ? TheCamera.GetForward() : CVector(0.0f, 1.0f, 0.0f);
                const auto up = TheCamera.m_matrix
                    ? TheCamera.GetUp() : CVector(0.0f, 0.0f, 1.0f);
                audioEngine->setListenerPosition(
                    irrklang::vec3df(cameraPosition->x, cameraPosition->y, cameraPosition->z),
                    irrklang::vec3df(forward.x, forward.y, forward.z),
                    irrklang::vec3df(0.0f, 0.0f, 0.0f),
                    irrklang::vec3df(up.x, up.y, up.z)
                );

                auto it = playingSounds.begin();
                while (it != playingSounds.end()) {
                    auto* sound = it->sound;
                    if (!sound || sound->isFinished()) {
                        if (sound) sound->drop();
                        it = playingSounds.erase(it);
                    } else {
                        updateAudioState(sound, nullptr);
                        ++it;
                    }
                }
            };
        };
    }
} earShot;

} // namespace
