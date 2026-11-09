#include "app/AppController.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace audioguia;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Action>
void check_app_error(Action action) {
    try {
        action();
    } catch (const AppError& error) {
        check(std::string(error.what()).size() != 0, "AppError must explain the failure");
        return;
    }
    throw std::runtime_error("Expected AppError, but the operation succeeded");
}

class Tests {
public:
    template <typename Action>
    void run(const std::string& name, Action action) {
        ++total_;
        try {
            action();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        } catch (...) {
            ++failed_;
            std::cerr << "FAIL: " << name << ": unknown exception\n";
        }
    }

    int result() const {
        std::cout << "controller_tests: " << total_ - failed_ << '/' << total_
                  << " cases passed, " << failed_ << " failed\n";
        return failed_ == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int total_{0};
    int failed_{0};
};

class TemporaryRoot {
public:
    TemporaryRoot() {
        const auto parent = fs::temp_directory_path();
        const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = parent / ("audioguia_controller_tests_" + stamp + "_" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(root, error)) return;
            check(!error || error == std::errc::file_exists, "Cannot create temporary fixture: " + error.message());
        }
        throw std::runtime_error("Cannot reserve a unique temporary fixture");
    }

    ~TemporaryRoot() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    TemporaryRoot(const TemporaryRoot&) = delete;
    TemporaryRoot& operator=(const TemporaryRoot&) = delete;

    fs::path root;
};

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(input.is_open(), "Cannot read fixture: " + path.u8string());
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    check(!input.bad(), "Failed reading fixture: " + path.u8string());
    return bytes;
}

void write_bytes(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(output.is_open(), "Cannot write fixture: " + path.u8string());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    check(!output.fail(), "Failed writing fixture: " + path.u8string());
}

fs::path backup_path(const ConfigRepository& repository) {
    auto path = repository.file_path();
    path += ".bak";
    return path;
}

void check_state(const AudioEvent& actual, const AudioEvent& expected) {
    check(actual.session == expected.session && actual.status == expected.status &&
          actual.track_index == expected.track_index && actual.between_languages == expected.between_languages &&
          actual.message == expected.message, "Unexpected controller state/session");
}

void check_plan(const PlaybackPlan& actual, const PlaybackPlan& expected) {
    check(actual.point_id == expected.point_id && actual.point_name == expected.point_name &&
          actual.direction == expected.direction && actual.mode == expected.mode &&
          actual.gap_ms == expected.gap_ms, "Active plan metadata is not a snapshot");
    check(actual.tracks.size() == expected.tracks.size(), "Active plan track count changed");
    for (std::size_t i = 0; i < expected.tracks.size(); ++i) {
        check(actual.tracks[i].language == expected.tracks[i].language &&
              actual.tracks[i].path == expected.tracks[i].path, "Active plan language/path changed");
    }
}

enum class Command { play, stop, pause, volume };

struct PlayCall {
    std::uint64_t session;
    PlaybackPlan plan;
    std::string output_device;
    float volume;
};

class FakeIAudioPlayer final : public IAudioPlayer {
public:
    void play(std::uint64_t session, PlaybackPlan plan, std::string output_device, float volume) override {
        commands.push_back(Command::play);
        plays.push_back({session, std::move(plan), std::move(output_device), volume});
    }

    void stop() override { commands.push_back(Command::stop); }
    void toggle_pause() override { commands.push_back(Command::pause); }

    void set_volume(float volume) override {
        commands.push_back(Command::volume);
        volumes.push_back(volume);
    }

    std::vector<OutputDevice> devices() override {
        ++device_calls;
        return outputs;
    }

    std::vector<AudioEvent> poll_events() override {
        ++poll_calls;
        return std::exchange(pending, std::vector<AudioEvent>{});
    }

    // No implicit events, threads, hardware, timers or cancellation filtering.
    std::vector<Command> commands;
    std::vector<PlayCall> plays;
    std::vector<float> volumes;
    std::vector<AudioEvent> pending;
    std::vector<OutputDevice> outputs{{"fake:one", "Fake output one"}, {"fake:two", u8"Salida Jos\u00e9"}};
    int device_calls{0};
    int poll_calls{0};
};

struct Fixture {
    TemporaryRoot temporary;
    ConfigRepository repository{temporary.root};
    FakeIAudioPlayer* fake{nullptr};
    std::unique_ptr<AppController> app;

    explicit Fixture(const AppConfig& config = default_config()) {
        repository.save(config);
        auto player = std::make_unique<FakeIAudioPlayer>();
        fake = player.get();
        app = std::make_unique<AppController>(temporary.root, std::move(player));
    }
};

void check_commands(const FakeIAudioPlayer& fake, std::initializer_list<Command> expected) {
    check(fake.commands == std::vector<Command>(expected), "Wrong audio command count or order");
}

void initial_without_config() {
    const TemporaryRoot temporary;
    auto player = std::make_unique<FakeIAudioPlayer>();
    auto& fake = *player;
    AppController app(temporary.root, std::move(player));
    check_state(app.state(), AudioEvent{});
    check(!app.active_plan() && app.warning().empty(), "First run must be stopped without a warning or plan");
    check(app.root() == temporary.root && app.config().schema_version == 1 && app.config().points.size() == 14,
          "First run must use in-memory defaults and the supplied root");
    check_commands(fake, {});
    check(fake.plays.empty() && fake.volumes.empty() && fake.device_calls == 0 && fake.poll_calls == 0,
          "Construction must not interact with playback or enumerate real devices");
    app.toggle_pause();
    app.poll();
    check_commands(fake, {});
    check(fake.poll_calls == 1 && fake.pending.empty(), "Fake poll must be deterministic and drain its vector");
    check(!fs::exists(temporary.root / "app"), "Initial load must not create or save configuration");
    check(app.playback_title() == "Seleccione un punto del recorrido" &&
          app.playback_detail() == "Sin reproduccion automatica" && app.status_text() == "DETENIDO",
          "Idle presentation suggests automatic playback");
}

void initial_with_saved_preferences() {
    auto config = default_config();
    config.preferences.direction = Direction::down;
    config.preferences.mode = Mode::custom;
    config.preferences.simple_language = Language::pt;
    config.preferences.custom_languages = {Language::pt, Language::en};
    config.preferences.gap_ms = 457;
    config.preferences.volume = 0.375F;
    config.preferences.output_device = "fake:two";
    Fixture fixture(config);
    auto& app = *fixture.app;
    auto& fake = *fixture.fake;
    const auto original = read_bytes(fixture.repository.file_path());
    check_state(app.state(), AudioEvent{});
    check(!app.active_plan() && app.warning().empty(), "Saved selection must not start playback");
    check(app.config().preferences.direction == Direction::down && app.config().preferences.mode == Mode::custom &&
          app.config().preferences.custom_languages == config.preferences.custom_languages &&
          app.config().preferences.gap_ms == 457 && app.config().preferences.volume == 0.375F &&
          app.config().preferences.output_device == "fake:two", "Saved preferences were not loaded");
    check(app.selection_text() == "DOWN | Personalizado | PORT > ENG", "Selection does not reflect saved preferences");
    check_commands(fake, {});
    check(read_bytes(fixture.repository.file_path()) == original && !fs::exists(backup_path(fixture.repository)),
          "Construction must not rewrite saved preferences");
    app.play(Direction::down, 6);
    check_commands(fake, {Command::stop, Command::play});
    check(fake.plays.size() == 1 && fake.plays.front().output_device == "fake:two" &&
          fake.plays.front().volume == 0.375F, "Explicit play did not use the saved output and app volume");
}

void replacement_and_snapshots() {
    auto config = default_config();
    config.preferences.mode = Mode::custom;
    config.preferences.custom_languages = {Language::pt, Language::en};
    config.preferences.gap_ms = 321;
    config.preferences.output_device = "fake:one";
    config.preferences.volume = 0.25F;
    Fixture fixture(config);
    auto& app = *fixture.app;
    auto& fake = *fixture.fake;
    app.play(Direction::up, 2);
    check_commands(fake, {Command::stop, Command::play});
    check(app.active_plan().has_value() && fake.plays.size() == 1, "Play did not publish a plan");
    const auto old_plan = app.active_plan().value();
    const auto old_session = app.state().session;
    check(old_session > 0 && fake.plays.front().session == old_session, "Session was not forwarded");
    check(old_plan.mode == Mode::custom && old_plan.direction == Direction::up && old_plan.gap_ms == 321 &&
          old_plan.tracks.size() == 2 && old_plan.tracks[0].language == Language::pt &&
          old_plan.tracks[0].path == fixture.temporary.root / "audio/0003-PORT/0002.mp3" &&
          old_plan.tracks[1].language == Language::en, "Initial plan mode/languages/gap/path incorrect");
    check_plan(fake.plays.front().plan, old_plan);
    fake.pending = {{old_session, PlaybackStatus::paused, 0, true, "paused old gap"}};
    app.poll();

    auto updated = app.config();
    updated.preferences.mode = Mode::sequential;
    updated.preferences.simple_language = Language::en;
    updated.preferences.custom_languages = {Language::es};
    updated.preferences.gap_ms = 789;
    updated.points[10].name = u8"Punto nuevo con acentuaci\u00f3n";
    updated.points[10].audio = {{Language::es, "audio/new/es.mp3"}, {Language::en, "audio/new/en.mp3"},
                                {Language::pt, "audio/new/pt.mp3"}};
    app.apply_config(updated);
    check_plan(app.active_plan().value(), old_plan);
    check_plan(fake.plays.front().plan, old_plan);
    fake.commands.clear();
    app.play(Direction::down, 3);
    check_commands(fake, {Command::stop, Command::play});
    check(fake.plays.size() == 2 && app.active_plan().has_value(), "Replacement must issue exactly one new play");
    check_state(app.state(), {old_session + 1, PlaybackStatus::preparing, 0, false, {}});
    const auto& call = fake.plays.back();
    check(call.session == old_session + 1 && call.output_device == "fake:one" && call.volume == 0.25F,
          "Replacement reused the old session or lost audio preferences");
    const PlaybackPlan expected{"estancia-harberton", updated.points[10].name, Direction::down, Mode::sequential,
        {{Language::es, fixture.temporary.root / "audio/new/es.mp3"},
         {Language::en, fixture.temporary.root / "audio/new/en.mp3"},
         {Language::pt, fixture.temporary.root / "audio/new/pt.mp3"}}, 789};
    check_plan(app.active_plan().value(), expected);
    check_plan(call.plan, expected);
    updated.points[10].audio.clear();
    updated.points[10].name.clear();
    updated.preferences.gap_ms = 0;
    check_plan(call.plan, expected);
    check_plan(app.active_plan().value(), expected);
    check_plan(fake.plays.front().plan, old_plan);
    check(fixture.repository.load().preferences.direction == Direction::down, "Explicit play must remember its direction");
    check(!fs::exists(fixture.temporary.root / "audio"), "Controller test must not require real audio files");
}

void stale_events_and_poll_order() {
    auto config = default_config();
    config.preferences.mode = Mode::sequential;
    Fixture fixture(config);
    auto& app = *fixture.app;
    auto& fake = *fixture.fake;
    app.play(Direction::up, 1);
    const auto old_session = app.state().session;
    app.play(Direction::down, 1);
    const auto current_session = app.state().session;
    check(current_session > old_session, "New play must advance the session");
    const auto plan = app.active_plan().value();
    const AudioEvent current{current_session, PlaybackStatus::playing, 0, true, "current gap", 2};
    fake.pending = {{old_session, PlaybackStatus::error, 2, false, "old error"}, current,
                    {old_session, PlaybackStatus::stopped, 0, false, "old stopped"},
                    {current_session + 1, PlaybackStatus::error, 0, false, "unrequested future session"}};
    fake.commands.clear();
    check(app.state().status == PlaybackStatus::preparing, "Queued fake events must not apply before poll");
    app.poll();
    check_state(app.state(), current);
    check(fake.pending.empty() && fake.poll_calls == 1, "poll must consume exactly the queued vector");
    check_plan(app.active_plan().value(), plan);
    check(app.status_text() == "PAUSA ENTRE IDIOMAS" &&
          app.playback_detail().find("Siguiente: " + label(Language::pt)) != std::string::npos,
          "Gap presentation must describe the current plan's next language");
    const AudioEvent last{current_session, PlaybackStatus::playing, 2, false, "last current event"};
    fake.pending = {{current_session, PlaybackStatus::paused, 1, true, "paused"}, last,
                    {old_session, PlaybackStatus::playing, 0, false, "stale final event"}};
    app.poll();
    check_state(app.state(), last);
    app.poll();
    check_state(app.state(), last);
    check(fake.pending.empty() && fake.poll_calls == 3, "Empty polling must not replay old events");
    check_commands(fake, {});
}

void save_failure_warning_and_retry() {
    Fixture fixture;
    auto& app = *fixture.app;
    auto& fake = *fixture.fake;
    const auto original = read_bytes(fixture.repository.file_path());
    const auto backup = backup_path(fixture.repository);
    fs::create_directory(backup);
    write_bytes(backup / "sentinel", "preserve blocked backup");
    auto preferences = app.config().preferences;
    preferences.mode = Mode::custom;
    preferences.custom_languages = {Language::en, Language::es};
    preferences.gap_ms = 44;
    app.set_preferences(preferences);
    check_commands(fake, {Command::volume});
    check(app.config().preferences.mode == Mode::custom && app.config().preferences.gap_ms == 44,
          "Save failure must not discard valid in-memory changes");
    check(app.warning().find("No se pudo guardar") != std::string::npos, "Save failure must be exposed as a warning");
    app.save();
    check(!app.warning().empty(), "Repeated failure must retain its warning");
    check(read_bytes(fixture.repository.file_path()) == original, "Failed controller save overwrote original bytes");
    check(read_bytes(backup / "sentinel") == "preserve blocked backup", "Failed save damaged blocked backup");
    check(!app.active_plan() && app.state().status == PlaybackStatus::stopped && fake.plays.empty(),
          "Persistence failure must not start playback");
    fs::remove_all(backup);
    fake.commands.clear();
    app.save();
    check(app.warning().empty(), "Successful explicit retry must clear the save warning");
    check(fixture.repository.load().preferences.gap_ms == 44 &&
          fixture.repository.load().preferences.custom_languages == preferences.custom_languages,
          "Retry did not persist the in-memory preferences");
    check(read_bytes(backup) == original, "Successful retry did not preserve the original backup");
    check_commands(fake, {});
}

void malformed_config_is_never_overwritten() {
    const TemporaryRoot temporary;
    const ConfigRepository repository(temporary.root);
    auto configured = default_config();
    configured.preferences.gap_ms = 777;
    configured.preferences.mode = Mode::sequential;
    repository.save(configured);
    configured.preferences.gap_ms = 888;
    repository.save(configured);
    const auto backup = read_bytes(backup_path(repository));
    const std::string malformed = "{\"schema_version\":1,\"preferences\":{\"gap_ms\":";
    write_bytes(repository.file_path(), malformed);
    auto player = std::make_unique<FakeIAudioPlayer>();
    auto& fake = *player;
    AppController app(temporary.root, std::move(player));
    const auto warning = app.warning();
    check(!warning.empty() && warning.find("SOLO en memoria") != std::string::npos,
          "Malformed configuration must be reported, not silently replaced");
    check(app.config().schema_version == 1 && app.config().points.size() == 14 &&
          app.config().preferences.gap_ms == 1000 && app.config().preferences.mode == Mode::simple,
          "Fallback defaults must be in memory, not a hidden backup load");
    check_state(app.state(), AudioEvent{});
    check(!app.active_plan(), "Malformed configuration must not autoplay");
    check_commands(fake, {});
    app.save();
    auto preferences = app.config().preferences;
    preferences.mode = Mode::custom;
    preferences.custom_languages = {Language::pt, Language::es};
    preferences.gap_ms = 25;
    app.set_preferences(preferences);
    auto in_memory = app.config();
    in_memory.points.front().name = "Only changed in memory";
    app.apply_config(in_memory);
    app.remember_compact_position(-1200, 50);
    app.play(Direction::up, 1);
    check(fake.plays.size() == 1 && fake.plays.front().plan.gap_ms == 25,
          "In-memory fallback must remain usable with the fake player");
    app.stop();
    app.save();
    check(app.warning() == warning, "Saving disabled after a bad load must not clear the original warning");
    check(app.config().points.front().name == "Only changed in memory" && app.config().preferences.compact_x == -1200,
          "In-memory edits were unexpectedly discarded");
    check(read_bytes(repository.file_path()) == malformed, "Controller auto-repaired or overwrote malformed JSON");
    check(read_bytes(backup_path(repository)) == backup, "Controller modified the recoverable backup");
    std::size_t entries{0};
    for (const auto& entry : fs::directory_iterator(repository.file_path().parent_path())) {
        check(entry.path() == repository.file_path() || entry.path() == backup_path(repository),
              "Disabled saving left an unexpected file");
        ++entries;
    }
    check(entries == 2 && !fs::exists(temporary.root / "audio"), "Malformed-config case touched unexpected files");
}

} // namespace

int main() {
    Tests tests;
    tests.run("initial stopped state, missing config and no autoplay", initial_without_config);
    tests.run("saved selection is loaded without autoplay or rewriting", initial_with_saved_preferences);

    const std::array<std::pair<Direction, int>, 6> invalid_points{{
        {Direction::up, -1}, {Direction::up, 0}, {Direction::up, 9},
        {Direction::down, 0}, {Direction::down, 7}, {Direction::down, 8},
    }};
    for (const auto& point : invalid_points) {
        for (const bool active : {false, true}) {
            tests.run("invalid " + id(point.first) + " " + std::to_string(point.second) +
                      (active ? " preserves playback" : " issues no idle command"), [&] {
                Fixture fixture;
                auto& app = *fixture.app;
                auto& fake = *fixture.fake;
                if (active) {
                    app.play(Direction::up, 3);
                    fake.pending = {{app.state().session, PlaybackStatus::playing, 0, false, "current audio"}};
                    app.poll();
                }
                const auto state = app.state();
                const auto plan = app.active_plan();
                const auto preferences = app.config().preferences;
                const auto original = read_bytes(fixture.repository.file_path());
                const auto plays = fake.plays.size();
                fake.commands.clear();
                app.play(point.first, point.second);
                check_commands(fake, {});
                check(fake.plays.size() == plays, "Invalid point reached the player");
                check_state(app.state(), state);
                check(app.active_plan().has_value() == plan.has_value(), "Invalid point changed the active selection");
                if (plan) check_plan(app.active_plan().value(), *plan);
                check(app.config().preferences.direction == preferences.direction &&
                      read_bytes(fixture.repository.file_path()) == original, "Invalid point modified preferences or disk");
            });
        }
    }
    tests.run("new play cancels a paused gap, advances session and owns plan snapshots", replacement_and_snapshots);

    for (const auto mode : {Mode::simple, Mode::sequential, Mode::custom}) {
        for (const bool active : {false, true}) {
            tests.run("mode/config change " + id(mode) + (active ? " preserves active snapshot" : " does not activate"), [&] {
                auto config = default_config();
                config.preferences.mode = Mode::sequential;
                Fixture fixture(config);
                auto& app = *fixture.app;
                auto& fake = *fixture.fake;
                if (active) {
                    app.play(Direction::up, 4);
                    fake.pending = {{app.state().session, PlaybackStatus::playing, 1, true, "existing gap"}};
                    app.poll();
                }
                const auto state = app.state();
                const auto snapshot = app.active_plan();
                check(snapshot.has_value() == active, "Wrong initial playback state for config-change case");
                const auto title = app.playback_title();
                const auto detail = app.playback_detail();
                const auto play_count = fake.plays.size();
                auto updated = app.config();
                updated.preferences.direction = Direction::down;
                updated.preferences.mode = mode;
                updated.preferences.simple_language = Language::pt;
                updated.preferences.custom_languages = {Language::pt, Language::en};
                updated.preferences.gap_ms = 12;
                updated.points[3].name = "Edited source, not active title";
                updated.points[3].audio[Language::en] = "audio/edited-source.mp3";
                fake.commands.clear();
                app.apply_config(updated);
                check_commands(fake, {Command::volume});
                check(fake.plays.size() == play_count, "Config change automatically started a new plan");
                check_state(app.state(), state);
                check(app.active_plan().has_value() == snapshot.has_value(), "Config change activated or cleared a plan");
                if (snapshot) {
                    check_plan(app.active_plan().value(), *snapshot);
                    check_plan(fake.plays.back().plan, *snapshot);
                }
                check(app.playback_title() == title && app.playback_detail() == detail, "Active presentation read mutable config");
                check(app.selection_text().find("DOWN | " + label(mode)) == 0, "Pending selection did not reflect the new mode");
                check(fixture.repository.load().preferences.mode == mode && app.warning().empty(), "New mode was not saved");
            });
        }
    }

    for (const auto status : {PlaybackStatus::preparing, PlaybackStatus::playing, PlaybackStatus::paused}) {
        tests.run("output switch stops and resets status " + std::to_string(static_cast<int>(status)), [=] {
            auto config = default_config();
            config.preferences.mode = Mode::sequential;
            Fixture fixture(config);
            auto& app = *fixture.app;
            auto& fake = *fixture.fake;
            app.play(Direction::up, 1);
            const auto old_session = app.state().session;
            if (status != PlaybackStatus::preparing) {
                fake.pending = {{old_session, status, 1, true, "old output state"}};
                app.poll();
            }
            auto preferences = app.config().preferences;
            preferences.output_device = "fake:two";
            const auto plays = fake.plays.size();
            fake.commands.clear();
            app.set_preferences(preferences);
            check_commands(fake, {Command::stop, Command::volume});
            check_state(app.state(), {old_session + 1, PlaybackStatus::stopped, 0, false, {}});
            check(!app.active_plan() && fake.plays.size() == plays, "Output change replayed audio or retained an active plan");
            check(fixture.repository.load().preferences.output_device == "fake:two", "Output selection was not saved");
            fake.pending = {{old_session, PlaybackStatus::playing, 2, false, "late old-output event"}};
            app.poll();
            check_state(app.state(), {old_session + 1, PlaybackStatus::stopped, 0, false, {}});
            fake.commands.clear();
            app.play(Direction::down, 1);
            check_commands(fake, {Command::stop, Command::play});
            check(fake.plays.back().output_device == "fake:two", "Next explicit play used the old device");
        });
    }
    for (const auto status : {PlaybackStatus::stopped, PlaybackStatus::preparing, PlaybackStatus::playing, PlaybackStatus::paused}) {
        tests.run("volume only reaches app player at status " + std::to_string(static_cast<int>(status)), [=] {
            Fixture fixture;
            auto& app = *fixture.app;
            auto& fake = *fixture.fake;
            if (status != PlaybackStatus::stopped) {
                app.play(Direction::up, 1);
                if (status != PlaybackStatus::preparing) {
                    fake.pending = {{app.state().session, status, 0, false, "volume state"}};
                    app.poll();
                }
            }
            const auto state = app.state();
            const auto snapshot = app.active_plan();
            const auto plays = fake.plays.size();
            auto preferences = app.config().preferences;
            preferences.volume = 0.125F;
            fake.commands.clear();
            app.set_preferences(preferences);
            check_commands(fake, {Command::volume});
            check(fake.volumes.size() == 1 && fake.volumes.back() == 0.125F, "App volume was not forwarded exactly once");
            check(fake.plays.size() == plays, "Volume change restarted playback");
            check_state(app.state(), state);
            check(app.active_plan().has_value() == snapshot.has_value(), "Volume change modified the active plan");
            if (snapshot) check_plan(app.active_plan().value(), *snapshot);
            check(app.config().preferences.volume == 0.125F && fixture.repository.load().preferences.volume == 0.125F,
                  "Volume change was not kept in app preferences");
        });
    }
    tests.run("poll ignores old and unrequested sessions and preserves vector order", stale_events_and_poll_order);

    for (const auto status : {PlaybackStatus::playing, PlaybackStatus::paused, PlaybackStatus::error}) {
        tests.run("stop resets plan, gap, index and message from status " + std::to_string(static_cast<int>(status)), [=] {
            auto config = default_config();
            config.preferences.mode = Mode::sequential;
            Fixture fixture(config);
            auto& app = *fixture.app;
            auto& fake = *fixture.fake;
            app.play(Direction::down, 4);
            const auto old_session = app.state().session;
            fake.pending = {{old_session, status, 1, true, "old message"}};
            app.poll();
            const auto original = read_bytes(fixture.repository.file_path());
            fake.commands.clear();
            app.stop();
            check_commands(fake, {Command::stop});
            check_state(app.state(), {old_session + 1, PlaybackStatus::stopped, 0, false, {}});
            check(!app.active_plan() && app.status_text() == "DETENIDO" &&
                  app.playback_detail() == "Sin reproduccion automatica", "Stop left an active selection or gap");
            fake.pending = {{old_session, PlaybackStatus::error, 2, false, "late error"},
                            {old_session, PlaybackStatus::playing, 0, false, "late playback"}};
            app.poll();
            check_state(app.state(), {old_session + 1, PlaybackStatus::stopped, 0, false, {}});
            app.stop();
            check_commands(fake, {Command::stop, Command::stop});
            check_state(app.state(), {old_session + 2, PlaybackStatus::stopped, 0, false, {}});
            check(read_bytes(fixture.repository.file_path()) == original && fake.plays.size() == 1,
                  "Stop unexpectedly saved preferences or replayed audio");
        });
    }
    for (const auto status : {PlaybackStatus::stopped, PlaybackStatus::preparing, PlaybackStatus::playing,
                              PlaybackStatus::paused, PlaybackStatus::error}) {
        tests.run("pause command is gated by status " + std::to_string(static_cast<int>(status)), [=] {
            Fixture fixture;
            auto& app = *fixture.app;
            auto& fake = *fixture.fake;
            if (status != PlaybackStatus::stopped) {
                app.play(Direction::up, 1);
                if (status != PlaybackStatus::preparing) {
                    fake.pending = {{app.state().session, status, 0, false, "pause state"}};
                    app.poll();
                }
            }
            const auto state = app.state();
            const auto original = read_bytes(fixture.repository.file_path());
            fake.commands.clear();
            app.toggle_pause();
            if (status == PlaybackStatus::playing || status == PlaybackStatus::paused) {
                check_commands(fake, {Command::pause});
            } else {
                check_commands(fake, {});
            }
            check_state(app.state(), state);
            check(read_bytes(fixture.repository.file_path()) == original, "Pause must not save or modify preferences");
        });
    }
    tests.run("pause/resume state changes only after fake events are polled", [] {
        Fixture fixture;
        auto& app = *fixture.app;
        auto& fake = *fixture.fake;
        app.play(Direction::up, 1);
        const auto session = app.state().session;
        fake.pending = {{session, PlaybackStatus::playing, 0, false, {}}};
        app.poll();
        fake.commands.clear();
        app.toggle_pause();
        check(app.state().status == PlaybackStatus::playing, "Pause command fabricated an immediate event");
        fake.pending = {{session, PlaybackStatus::paused, 0, false, {}}};
        app.poll();
        check(app.status_text() == "PAUSADO", "Paused event was not reflected");
        app.toggle_pause();
        check(app.state().status == PlaybackStatus::paused, "Resume command fabricated an immediate event");
        fake.pending = {{session, PlaybackStatus::playing, 0, false, {}}};
        app.poll();
        check(app.status_text() == "REPRODUCIENDO", "Playing event was not reflected after resume");
        check_commands(fake, {Command::pause, Command::pause});
    });

    tests.run("missing mapping cancels previous audio and reports point/language without play", [] {
        auto config = default_config();
        config.points[0].audio.erase(Language::es);
        Fixture fixture(config);
        auto& app = *fixture.app;
        auto& fake = *fixture.fake;
        app.play(Direction::up, 2);
        const auto old_session = app.state().session;
        fake.commands.clear();
        app.play(Direction::up, 1);
        check_commands(fake, {Command::stop});
        check(app.state().session == old_session + 1 && app.state().status == PlaybackStatus::error &&
              app.state().track_index == 0 && !app.state().between_languages && !app.active_plan(),
              "Failed plan left the previous plan or state active");
        check(fake.plays.size() == 1 && app.state().message.find("bienvenida") != std::string::npos &&
              app.state().message.find("idioma es") != std::string::npos, "Planner error lost its point/language context");
        app.toggle_pause();
        check_commands(fake, {Command::stop});
        app.play(Direction::down, 1);
        check_commands(fake, {Command::stop, Command::stop, Command::play});
        check(app.state().status == PlaybackStatus::preparing && app.state().message.empty() &&
              app.active_plan().has_value() && fake.plays.size() == 2, "Next valid play did not recover from planner error");
    });
    for (const bool preferences_only : {false, true}) {
        tests.run(preferences_only ? "invalid preferences have no side effects" : "invalid config has no side effects", [=] {
            Fixture fixture;
            auto& app = *fixture.app;
            auto& fake = *fixture.fake;
            app.play(Direction::up, 1);
            const auto state = app.state();
            const auto plan = app.active_plan().value();
            const auto original = read_bytes(fixture.repository.file_path());
            const auto backup = read_bytes(backup_path(fixture.repository));
            auto invalid = app.config();
            invalid.preferences.gap_ms = -1;
            invalid.preferences.output_device = "fake:must-not-stop-before-validation";
            fake.commands.clear();
            check_app_error([&] {
                if (preferences_only) app.set_preferences(invalid.preferences);
                else app.apply_config(invalid);
            });
            check_commands(fake, {});
            check_state(app.state(), state);
            check_plan(app.active_plan().value(), plan);
            check(app.config().preferences.gap_ms == 1000 && app.config().preferences.output_device.empty() &&
                  app.warning().empty(), "Invalid update changed config or warning");
            check(read_bytes(fixture.repository.file_path()) == original &&
                  read_bytes(backup_path(fixture.repository)) == backup, "Invalid update modified persisted data");
        });
    }
    tests.run("save failure warns, preserves disk and permits a successful retry", save_failure_warning_and_retry);
    tests.run("malformed config and backup remain untouched by every save entry point", malformed_config_is_never_overwritten);
    tests.run("device enumeration delegates only to the fake without state changes", [] {
        Fixture fixture;
        auto& app = *fixture.app;
        auto& fake = *fixture.fake;
        const auto original = read_bytes(fixture.repository.file_path());
        const auto outputs = app.devices();
        check(outputs.size() == 2 && outputs[0].id == fake.outputs[0].id && outputs[0].name == fake.outputs[0].name &&
              outputs[1].id == fake.outputs[1].id && outputs[1].name == fake.outputs[1].name,
              "Device enumeration did not return fake IDs and names");
        check(fake.device_calls == 1 && fake.plays.empty(), "Device enumeration unexpectedly started playback");
        check_commands(fake, {});
        check_state(app.state(), AudioEvent{});
        check(read_bytes(fixture.repository.file_path()) == original, "Device enumeration saved config");
    });
    tests.run("compact position accepts signed coordinates without audio commands", [] {
        Fixture fixture;
        auto& app = *fixture.app;
        app.remember_compact_position(-1920, -50);
        check(app.config().preferences.compact_x == -1920 && app.config().preferences.compact_y == -50,
              "Signed compact position was not retained");
        const auto persisted = fixture.repository.load();
        check(persisted.preferences.compact_x == -1920 && persisted.preferences.compact_y == -50,
              "Signed compact position was not saved");
        check_commands(*fixture.fake, {});
        check_state(app.state(), AudioEvent{});
    });
    return tests.result();
}
