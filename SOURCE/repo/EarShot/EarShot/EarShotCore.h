// EarShotCore.h
// Platform-independent part of EarShot. Contains no GTA:SA or irrKlang
// headers, so it can be unit-tested on any host (see tests/core_test.cpp).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace earshot {

namespace fs = std::filesystem;

// Distance (metres) at which custom sounds become silent.
inline constexpr float kHearingRange = 150.0f;
// Playback speed while slow motion is active.
inline constexpr float kSlowMotionSpeed = 0.5f;

// Sound events the mod can replace. Used as an index into per-weapon slots.
enum class Action : std::uint8_t { Shoot = 0, Reload, Hit, Swing };
inline constexpr std::size_t kActionCount = 4;

// File stem (without extension) for each action: Shoot -> "shoot" -> shoot.wav.
inline const char *ActionFileStem(Action action) {
    switch (action) {
    case Action::Shoot:  return "shoot";
    case Action::Reload: return "reload";
    case Action::Hit:    return "hit";
    case Action::Swing:  return "swing";
    }
    return "";
}

// ASCII-only case conversion. Bytes >= 0x80 are left untouched, which avoids
// the undefined behaviour of passing negative char values to tolower().
inline char AsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
inline char AsciiUpper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}
inline std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), AsciiLower);
    return text;
}
inline std::string ToUpperAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), AsciiUpper);
    return text;
}

// "Deagle 348" -> weapon "Deagle", model "348". Only the first space separates.
struct WeaponStem {
    std::string weapon;
    std::string model;
    bool hasModel = false;
};

inline WeaponStem ParseWeaponStem(const std::string &stem) {
    WeaponStem parsed;
    const auto separator = stem.find(' ');
    if (separator == std::string::npos) {
        parsed.weapon = stem;
        return parsed;
    }
    parsed.weapon = stem.substr(0, separator);
    parsed.model = stem.substr(separator + 1);
    parsed.hasModel = true;
    return parsed;
}

// Volume factor by distance: (1 - d / range)^6, with d clamped to [0, range].
inline float DistanceAttenuation(float distance) {
    const float clamped = std::clamp(distance, 0.0f, kHearingRange);
    const float linear = (kHearingRange - clamped) / kHearingRange;
    const float squared = linear * linear;
    const float fourth = squared * squared;
    return fourth * squared;
}

inline float ComputeVolume(float masterScale, float distance) {
    return masterScale * DistanceAttenuation(distance);
}

inline float PlaybackSpeed(bool slowMotion) {
    return slowMotion ? kSlowMotionSpeed : 1.0f;
}

// Drops the game-root prefix from a path, for log output. Never reads out of range.
inline std::string StripPrefix(const fs::path &path, std::size_t prefixLength) {
    const std::string text = path.string();
    return text.size() >= prefixLength ? text.substr(prefixLength) : text;
}

// Owns the reference-counted sound handles returned by the audio engine.
// Each handle is dropped exactly once: when it finishes playing, or in Clear().
// `Owner` is the game entity that emits the sound; the pointer is not owned.
template <class Sound, class Owner>
class ActiveSounds {
public:
    struct Entry {
        Sound *sound;
        Owner *owner;
    };

    void Add(Sound *sound, Owner *owner) {
        entries_.push_back(Entry{sound, owner});
    }

    // Calls visit(entry) for every sound still playing. Finished sounds are
    // dropped and removed from the list.
    template <class Visit>
    void Tick(Visit &&visit) {
        for (std::size_t index = 0; index < entries_.size();) {
            Entry &entry = entries_[index];
            if (entry.sound->isFinished()) {
                entry.sound->drop();
                entry = entries_.back();
                entries_.pop_back();
            } else {
                visit(entry);
                ++index;
            }
        }
    }

    // Stops and releases everything. Call before the audio engine is destroyed.
    void Clear() {
        for (auto &entry : entries_) {
            entry.sound->stop();
            entry.sound->drop();
        }
        entries_.clear();
    }

    std::size_t Size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

// Maps (weapon, model) to the folder that holds its sound files.
// Each file is resolved at most once per action and then cached, including
// the "missing" result, so the game thread never touches the disk per shot.
// `Source` is the audio source type (irrklang::ISoundSource in the plugin).
template <class Source>
class WeaponSoundRegistry {
public:
    enum class SlotState : std::uint8_t { Unresolved, Ready, Missing };

    struct Slot {
        SlotState state = SlotState::Unresolved;
        Source *source = nullptr;
    };

    struct Entry {
        fs::path folder;
        std::array<Slot, kActionCount> slots{};
    };

    // Registers (or replaces) a folder. model == -1 means "generic ped sound".
    void Register(int weapon, int model, fs::path folder) {
        entries_[Key{weapon, model}] = Entry{std::move(folder), {}};
    }

    // Returns the source for this event, loading it on first use.
    // load(path) returns nullptr when the file is missing or cannot be decoded.
    template <class Loader>
    Source *Find(int weapon, int model, Action action, Loader &&load) {
        const auto found = entries_.find(Key{weapon, model});
        if (found == entries_.end()) {
            return nullptr;
        }

        Slot &slot = found->second.slots[static_cast<std::size_t>(action)];
        if (slot.state == SlotState::Unresolved) {
            const fs::path file =
                found->second.folder / (std::string(ActionFileStem(action)) + ".wav");
            slot.source = load(file);
            slot.state = slot.source ? SlotState::Ready : SlotState::Missing;
        }
        return slot.state == SlotState::Ready ? slot.source : nullptr;
    }

    // Used when playback fails, so the same broken file is not retried every frame.
    void MarkMissing(int weapon, int model, Action action) {
        const auto found = entries_.find(Key{weapon, model});
        if (found == entries_.end()) {
            return;
        }
        Slot &slot = found->second.slots[static_cast<std::size_t>(action)];
        slot.state = SlotState::Missing;
        slot.source = nullptr;
    }

    void Clear() { entries_.clear(); }
    std::size_t Size() const { return entries_.size(); }

private:
    struct Key {
        int weapon;
        int model;
        bool operator<(const Key &other) const {
            return weapon != other.weapon ? weapon < other.weapon : model < other.model;
        }
    };

    std::map<Key, Entry> entries_;
};

} // namespace earshot
