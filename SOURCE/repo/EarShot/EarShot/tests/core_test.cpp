// Unit tests for EarShotCore.h. Build and run on any host, for example:
//   g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined
//       -fno-sanitize-recover=all core_test.cpp -o core_test && ./core_test
// The mocks mirror the parts of irrklang::ISound / ISoundSource that the core uses.
#include "../EarShotCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define EXPECT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "%s:%d: EXPECT_TRUE(%s)\n", __FILE__,        \
                         __LINE__, #cond);                                    \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define EXPECT_EQ(a, b)                                                       \
    do {                                                                      \
        if (!((a) == (b))) {                                                  \
            std::fprintf(stderr, "%s:%d: EXPECT_EQ(%s, %s)\n", __FILE__,      \
                         __LINE__, #a, #b);                                   \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define EXPECT_NEAR(a, b, eps)                                                \
    do {                                                                      \
        if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <=   \
              static_cast<double>(eps))) {                                    \
            std::fprintf(stderr, "%s:%d: EXPECT_NEAR(%s, %s)\n", __FILE__,    \
                         __LINE__, #a, #b);                                   \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// Live-handle counter: detects leaks and double drops.
int g_liveSounds = 0;
int g_totalDrops = 0;

struct MockSound {
    bool finished = false;
    int drops = 0;
    int stops = 0;

    MockSound() { ++g_liveSounds; }
    bool isFinished() const { return finished; }
    void stop() { ++stops; }
    void drop() {
        ++drops;
        ++g_totalDrops;
        if (drops == 1) {
            --g_liveSounds;
        } else {
            std::fprintf(stderr, "double drop detected\n");
            ++g_failures;
        }
    }
};

struct MockOwner {
    int id = 0;
};

struct MockSource {
    std::string path;
};

void TestParseWeaponStem() {
    auto plain = earshot::ParseWeaponStem("Deagle");
    EXPECT_EQ(plain.weapon, std::string("Deagle"));
    EXPECT_TRUE(!plain.hasModel);

    auto withModel = earshot::ParseWeaponStem("Deagle 348");
    EXPECT_EQ(withModel.weapon, std::string("Deagle"));
    EXPECT_EQ(withModel.model, std::string("348"));
    EXPECT_TRUE(withModel.hasModel);

    // Only the first space separates weapon and model, as in the original code.
    auto spaces = earshot::ParseWeaponStem("A B C");
    EXPECT_EQ(spaces.weapon, std::string("A"));
    EXPECT_EQ(spaces.model, std::string("B C"));

    // Very long names are neither truncated nor overflowing a fixed buffer.
    std::string longName(400, 'x');
    auto longParsed = earshot::ParseWeaponStem(longName + " " + longName);
    EXPECT_EQ(longParsed.weapon.size(), std::size_t(400));
    EXPECT_EQ(longParsed.model.size(), std::size_t(400));
}

void TestCaseConversion() {
    EXPECT_EQ(earshot::ToUpperAscii("deagle"), std::string("DEAGLE"));
    EXPECT_EQ(earshot::ToLowerAscii("DeAgLe"), std::string("deagle"));
    // Non-ASCII bytes pass through unchanged (no UB on negative char values);
    // only the ASCII letter 't' is converted.
    std::string high = "\xE9t\xE9";
    EXPECT_EQ(earshot::ToUpperAscii(high), std::string("\xE9T\xE9"));
}

void TestAttenuation() {
    EXPECT_NEAR(earshot::DistanceAttenuation(0.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(earshot::DistanceAttenuation(-20.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(earshot::DistanceAttenuation(150.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(earshot::DistanceAttenuation(999.0f), 0.0f, 1e-6f);
    // Half range: (0.5)^6.
    EXPECT_NEAR(earshot::DistanceAttenuation(75.0f), 0.015625f, 1e-6f);
    // Matches the original pow(factor, 6) formula across the range.
    for (float d = 0.0f; d <= 150.0f; d += 7.5f) {
        const float factor = std::pow((150.0f - d) / 150.0f, 6.0f);
        EXPECT_NEAR(earshot::DistanceAttenuation(d), factor, 1e-5f);
    }
    EXPECT_NEAR(earshot::ComputeVolume(0.5f, 0.0f), 0.5f, 1e-6f);
    EXPECT_NEAR(earshot::PlaybackSpeed(true), 0.5f, 1e-6f);
    EXPECT_NEAR(earshot::PlaybackSpeed(false), 1.0f, 1e-6f);
}

void TestStripPrefix() {
    const fs::path path("C:/GTA/plugins/EarShot/x.earshot");
    // Strip the first 4 characters ("C:/G").
    EXPECT_EQ(earshot::StripPrefix(path, 4), std::string("TA/plugins/EarShot/x.earshot"));
    // Prefix longer than the path: returns the whole path, no out-of-range read.
    EXPECT_EQ(earshot::StripPrefix(fs::path("ab"), 50), std::string("ab"));
}

void TestActiveSoundsOwnership() {
    const int liveBefore = g_liveSounds;
    const int dropsBefore = g_totalDrops;
    {
        earshot::ActiveSounds<MockSound, MockOwner> list;
        MockSound a, b, c;
        MockOwner owner;
        list.Add(&a, &owner);
        list.Add(&b, nullptr);
        list.Add(&c, &owner);
        EXPECT_EQ(list.Size(), std::size_t(3));

        // Finish the middle one: it is dropped once and removed.
        b.finished = true;
        int visited = 0;
        list.Tick([&](auto &) { ++visited; });
        EXPECT_EQ(visited, 2);
        EXPECT_EQ(b.drops, 1);
        EXPECT_EQ(list.Size(), std::size_t(2));
        EXPECT_EQ(a.drops, 0);
        EXPECT_EQ(c.drops, 0);

        // Clear stops and releases the rest exactly once.
        list.Clear();
        EXPECT_EQ(a.drops, 1);
        EXPECT_EQ(c.drops, 1);
        EXPECT_EQ(a.stops, 1);
        EXPECT_EQ(list.Size(), std::size_t(0));
    }
    EXPECT_EQ(g_liveSounds, liveBefore);
    EXPECT_EQ(g_totalDrops - dropsBefore, 3);
}

void TestActiveSoundsAllFinished() {
    const int liveBefore = g_liveSounds;
    {
        earshot::ActiveSounds<MockSound, MockOwner> list;
        MockSound a, b;
        list.Add(&a, nullptr);
        list.Add(&b, nullptr);
        a.finished = true;
        b.finished = true;
        list.Tick([](auto &) {});
        EXPECT_EQ(list.Size(), std::size_t(0));
        EXPECT_EQ(a.drops, 1);
        EXPECT_EQ(b.drops, 1);
    }
    EXPECT_EQ(g_liveSounds, liveBefore);
}

void TestRegistryResolvesOncePerSlot() {
    earshot::WeaponSoundRegistry<MockSource> registry;
    registry.Register(22, -1, fs::path("data/EarShot/Pistol"));
    registry.Register(22, 348, fs::path("data/EarShot/Deagle"));
    EXPECT_EQ(registry.Size(), std::size_t(2));

    int loaderCalls = 0;
    MockSource shootSource{"shoot.wav"};
    auto loader = [&](const fs::path &file) -> MockSource * {
        ++loaderCalls;
        // Only shoot.wav exists, and only in the Pistol folder.
        if (file.filename() == "shoot.wav" &&
            file.parent_path() == fs::path("data/EarShot/Pistol")) {
            return &shootSource;
        }
        return nullptr;
    };

    // Found, then served from cache.
    EXPECT_TRUE(registry.Find(22, -1, earshot::Action::Shoot, loader) == &shootSource);
    EXPECT_TRUE(registry.Find(22, -1, earshot::Action::Shoot, loader) == &shootSource);
    EXPECT_EQ(loaderCalls, 1);

    // Missing file is cached too: the disk is not hit again.
    EXPECT_TRUE(registry.Find(22, -1, earshot::Action::Reload, loader) == nullptr);
    EXPECT_TRUE(registry.Find(22, -1, earshot::Action::Reload, loader) == nullptr);
    EXPECT_EQ(loaderCalls, 2);

    // Unknown weapon: no load attempt at all.
    EXPECT_TRUE(registry.Find(99, -1, earshot::Action::Shoot, loader) == nullptr);
    EXPECT_EQ(loaderCalls, 2);

    // Model-specific entry uses its own folder.
    EXPECT_TRUE(registry.Find(22, 348, earshot::Action::Shoot, loader) == nullptr);
    EXPECT_EQ(loaderCalls, 3);
}

void TestRegistryMarkMissingStopsRetries() {
    earshot::WeaponSoundRegistry<MockSource> registry;
    registry.Register(5, -1, fs::path("f"));
    int calls = 0;
    MockSource src{"x"};
    auto loader = [&](const fs::path &) -> MockSource * {
        ++calls;
        return &src;
    };
    EXPECT_TRUE(registry.Find(5, -1, earshot::Action::Hit, loader) == &src);
    registry.MarkMissing(5, -1, earshot::Action::Hit);
    EXPECT_TRUE(registry.Find(5, -1, earshot::Action::Hit, loader) == nullptr);
    EXPECT_EQ(calls, 1);
}

void TestRegistryReplaceAndClear() {
    earshot::WeaponSoundRegistry<MockSource> registry;
    registry.Register(1, -1, fs::path("a"));
    // Replaces the previous entry, like the original map assignment did.
    registry.Register(1, -1, fs::path("b"));
    EXPECT_EQ(registry.Size(), std::size_t(1));
    std::string seen;
    auto loader = [&](const fs::path &file) -> MockSource * {
        seen = file.parent_path().string();
        return nullptr;
    };
    registry.Find(1, -1, earshot::Action::Swing, loader);
    EXPECT_EQ(seen, std::string("b"));
    registry.Clear();
    EXPECT_EQ(registry.Size(), std::size_t(0));
}

} // namespace

int main() {
    TestParseWeaponStem();
    TestCaseConversion();
    TestAttenuation();
    TestStripPrefix();
    TestActiveSoundsOwnership();
    TestActiveSoundsAllFinished();
    TestRegistryResolvesOncePerSlot();
    TestRegistryMarkMissingStopsRetries();
    TestRegistryReplaceAndClear();

    if (g_liveSounds != 0) {
        std::fprintf(stderr, "leaked sound handles: %d\n", g_liveSounds);
        ++g_failures;
    }
    if (g_failures != 0) {
        std::fprintf(stderr, "FAILED: %d check(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all core tests passed\n");
    return EXIT_SUCCESS;
}
