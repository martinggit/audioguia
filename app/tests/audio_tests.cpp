#include "audio/MiniaudioPlayer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using audioguia::AppError;
using audioguia::AudioEvent;
using audioguia::Language;
using audioguia::MiniaudioPlayer;
using audioguia::PlaybackPlan;
using audioguia::PlaybackStatus;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

static_assert(!std::is_copy_constructible<MiniaudioPlayer>::value, "Player must not be copied");
static_assert(!std::is_copy_assignable<MiniaudioPlayer>::value, "Player must not be copied");
static_assert(!std::is_move_constructible<MiniaudioPlayer>::value, "Player must not be moved");
static_assert(!std::is_move_assignable<MiniaudioPlayer>::value, "Player must not be moved");

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// A stuck join or enumeration also fails the independent test process, rather
// than leaving CI hung. Assertions remain active in Release/NDEBUG builds.
class Watchdog {
public:
    Watchdog() : thread_([this] {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!wake_.wait_for(lock, 90s, [this] { return finished_; })) {
            std::fputs("audio_tests: global timeout (90 seconds)\n", stderr);
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }) {}

    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        wake_.notify_one();
        thread_.join();
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool finished_{false};
    std::thread thread_;
};

void write_u16(std::ofstream& stream, std::uint16_t value) {
    const char bytes[]{static_cast<char>(value & 0xffU),
                       static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes, 2);
}

void write_u32(std::ofstream& stream, std::uint32_t value) {
    const char bytes[]{static_cast<char>(value & 0xffU),
                       static_cast<char>((value >> 8U) & 0xffU),
                       static_cast<char>((value >> 16U) & 0xffU),
                       static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes, 4);
}

// Deliberately generated PCM WAV fixtures, never production audio or hardware.
void write_wav(const fs::path& path, std::uint32_t milliseconds) {
    constexpr std::uint32_t rate = 48000;
    const auto frames = rate * milliseconds / 1000;
    const auto data_bytes = frames * 2;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    check(stream.is_open(), "Could not create WAV fixture");
    stream.write("RIFF", 4);
    write_u32(stream, 36 + data_bytes);
    stream.write("WAVEfmt ", 8);
    write_u32(stream, 16);
    write_u16(stream, 1); // PCM, mono, 16-bit little endian.
    write_u16(stream, 1);
    write_u32(stream, rate);
    write_u32(stream, rate * 2);
    write_u16(stream, 2);
    write_u16(stream, 16);
    stream.write("data", 4);
    write_u32(stream, data_bytes);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::int16_t sample = (i % 96 < 48) ? 2000 : -2000;
        write_u16(stream, static_cast<std::uint16_t>(sample));
    }
    stream.close();
    check(!stream.fail(), "Could not write WAV fixture");
}

struct Fixtures {
    fs::path root;
    fs::path es;
    fs::path en;
    fs::path pt;
    fs::path medium;
    fs::path long_track;
    fs::path invalid_wav;
    fs::path invalid_mp3;
    fs::path empty_wav;
    fs::path directory_wav;
    fs::path unicode;
    fs::path missing;

    Fixtures() {
        const auto parent = fs::temp_directory_path();
        const auto stamp = std::to_string(Clock::now().time_since_epoch().count());
        bool created = false;
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            root = parent / ("audioguia_audio_tests_" + stamp + "_" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(root, error)) {
                created = true;
                break;
            }
            check(!error || error == std::errc::file_exists, "Could not create fixture directory");
        }
        check(created, "Could not reserve a unique fixture directory");
        try {
            es = root / "es.wav";
            en = root / "en.wav";
            pt = root / "pt.WAV";
            medium = root / "medium.wav";
            long_track = root / "long.wav";
            invalid_wav = root / "invalid.wav";
            invalid_mp3 = root / "invalid.mp3";
            empty_wav = root / "empty.wav";
            directory_wav = root / "directory.wav";
            missing = root / "missing-last.wav";
            const auto unicode_directory = root / fs::u8path(u8"\u00e1udio_\u4e2d_\U0001f3b5");
            fs::create_directory(unicode_directory);
            unicode = unicode_directory / fs::u8path(u8"gu\u00eda_\u0416_\u65e5.wav");
            fs::create_directory(directory_wav);
            write_wav(es, 220);
            write_wav(en, 220);
            write_wav(pt, 220);
            write_wav(medium, 900);
            write_wav(long_track, 1800);
            write_wav(unicode, 220);
            write_wav(empty_wav, 0);
            for (const auto& path : {invalid_wav, invalid_mp3}) {
                std::ofstream stream(path, std::ios::binary);
                stream.write("not an audio file\n", 18);
                stream.close();
                check(!stream.fail(), "Could not write invalid fixture");
            }
        } catch (...) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
            throw;
        }
    }

    ~Fixtures() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    Fixtures(const Fixtures&) = delete;
    Fixtures& operator=(const Fixtures&) = delete;
};

PlaybackPlan single(const fs::path& path, const std::string& point = "point-one") {
    PlaybackPlan plan;
    plan.point_id = point;
    plan.point_name = "Generated test point";
    plan.tracks = {{Language::es, path}};
    plan.gap_ms = 0;
    return plan;
}

PlaybackPlan sequence(const Fixtures& fixture, int gap_ms) {
    auto plan = single(fixture.es);
    plan.tracks = {{Language::es, fixture.es}, {Language::en, fixture.en},
                   {Language::pt, fixture.pt}};
    plan.gap_ms = gap_ms;
    return plan;
}

struct RecordedEvent {
    AudioEvent event;
    Clock::time_point observed;
};

class EventLog {
public:
    explicit EventLog(MiniaudioPlayer& player) : player_(player) {}

    void pump() {
        for (auto& event : player_.poll_events()) {
            items.push_back({std::move(event), Clock::now()});
        }
    }

    void collect(std::chrono::milliseconds duration) {
        const auto end = Clock::now() + duration;
        do {
            pump();
            std::this_thread::sleep_for(10ms);
        } while (Clock::now() < end);
        pump();
    }

    template <typename Predicate>
    RecordedEvent wait(Predicate predicate, std::size_t from = 0,
                       std::chrono::milliseconds timeout = 5s) {
        const auto deadline = Clock::now() + timeout;
        do {
            pump();
            for (; from < items.size(); ++from) {
                const auto& item = items[from];
                if (predicate(item.event)) {
                    return item;
                }
                if (item.event.status == PlaybackStatus::error) {
                    throw std::runtime_error("Unexpected audio error: " + item.event.message);
                }
            }
            std::this_thread::sleep_for(10ms);
        } while (Clock::now() < deadline);
        throw std::runtime_error("Timed out waiting for an audio event");
    }

    RecordedEvent status(std::uint64_t session, PlaybackStatus expected, std::size_t from = 0) {
        return wait([=](const AudioEvent& event) {
            return event.session == session && event.status == expected;
        }, from);
    }

    RecordedEvent playing(std::uint64_t session, std::size_t index, bool gap,
                          std::size_t from = 0) {
        return wait([=](const AudioEvent& event) {
            return event.session == session && event.status == PlaybackStatus::playing &&
                   event.track_index == index && event.between_languages == gap;
        }, from);
    }

    void only_session(std::uint64_t session, std::size_t from) const {
        for (std::size_t i = from; i < items.size(); ++i) {
            check(items[i].event.session == session, "Stale session escaped the event queue");
        }
    }

    void no_error(std::size_t from = 0) const {
        for (std::size_t i = from; i < items.size(); ++i) {
            check(items[i].event.status != PlaybackStatus::error, items[i].event.message);
        }
    }

    void no_playing(std::size_t from) const {
        for (std::size_t i = from; i < items.size(); ++i) {
            check(items[i].event.status != PlaybackStatus::playing,
                  "Playback started after cancellation or failed preflight");
        }
    }

    std::vector<RecordedEvent> items;

private:
    MiniaudioPlayer& player_;
};

void creation_and_devices(const Fixtures&) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    log.collect(80ms);
    check(log.items.empty(), "Construction must not start sound or emit playback events");
    const auto devices = player.devices();
    check(devices.size() == 1 && devices.front().id == "test:null",
          "Tests must use the explicit null backend and stable test ID");
    check(!devices.front().name.empty(), "Output device must have a name");
    check(player.devices().front().id == devices.front().id, "Device ID must be stable");
    player.toggle_pause();
    player.set_volume(0.0F);
    player.set_volume(1.0F);
    player.devices(); // Worker barrier; idle commands must not start a device.
    check(player.poll_events().empty(), "Idle operations must not start playback");
}

void three_languages_and_indices(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    auto plan = sequence(fixture, 150);
    const auto started = Clock::now();
    player.play(1, plan, "test:null", 0.5F);
    plan.tracks.clear(); // The backend must own its by-value snapshot.
    plan.gap_ms = 100000;
    const auto stopped = log.status(1, PlaybackStatus::stopped);
    check(stopped.event.track_index == 2 && !stopped.event.between_languages,
          "Final stopped event must refer to the final zero-based track");
    std::vector<std::pair<std::size_t, bool>> order;
    std::size_t preparing = 0;
    for (const auto& item : log.items) {
        const auto& event = item.event;
        check(event.session == 1, "Wrong session in sequence");
        check(event.track_index < 3, "Track index must be zero-based and in range");
        if (event.status == PlaybackStatus::preparing) {
            ++preparing;
            check(event.track_index == 0 && !event.between_languages,
                  "Preparing must refer to track zero");
        } else if (event.status == PlaybackStatus::playing) {
            order.emplace_back(event.track_index, event.between_languages);
        }
    }
    const std::vector<std::pair<std::size_t, bool>> expected{
        {0, false}, {0, true}, {1, false}, {1, true}, {2, false}};
    check(preparing == 1 && order == expected, "Wrong language order, gap index or event sequence");
    check(stopped.observed - started >= 650ms, "Null device must run in real time, not skip PCM");
    for (std::size_t i = 0; i + 1 < log.items.size(); ++i) {
        if (log.items[i].event.between_languages) {
            check(log.items[i + 1].observed - log.items[i].observed >= 110ms,
                  "Language gap was shortened or skipped");
        }
    }
    log.no_error();
    check(player.poll_events().empty(), "poll_events must drain, not replay events");
}

void stop_during_track(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    auto plan = sequence(fixture, 150);
    plan.tracks.front().path = fixture.long_track;
    player.play(10, plan, {}, 0.5F);
    log.playing(10, 0, false);
    const auto from = log.items.size();
    const auto started = Clock::now();
    player.stop();
    const auto stopped = log.status(10, PlaybackStatus::stopped, from);
    check(stopped.observed - started < 1500ms, "Stop must not wait for track EOF");
    check(stopped.event.track_index == 0 && !stopped.event.between_languages,
          "Stop during track has incorrect metadata");
    log.collect(300ms);
    log.no_playing(from);
    log.no_error();

    const auto pending_from = log.items.size();
    player.play(11, plan, {}, 0.5F);
    player.stop();
    log.status(11, PlaybackStatus::stopped, pending_from);
    log.collect(100ms);
    log.only_session(11, pending_from);
    log.no_playing(pending_from);
}

void stop_during_gap(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    auto plan = sequence(fixture, 400);
    player.play(20, plan, {}, 0.5F);
    log.playing(20, 0, true);
    const auto from = log.items.size();
    player.stop();
    const auto stopped = log.status(20, PlaybackStatus::stopped, from);
    check(stopped.event.track_index == 0 && !stopped.event.between_languages,
          "Stop must clear the between-languages state");
    log.collect(600ms);
    log.no_playing(from);
    log.no_error();
}

void pause_and_resume_track(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    auto plan = single(fixture.medium);
    plan.tracks.push_back({Language::en, fixture.en});
    plan.gap_ms = 100;
    player.play(30, plan, {}, 0.5F);
    log.playing(30, 0, false);
    log.collect(120ms);
    player.toggle_pause();
    const auto paused = log.status(30, PlaybackStatus::paused);
    check(paused.event.track_index == 0 && !paused.event.between_languages,
          "Paused track has incorrect metadata");
    const auto paused_from = log.items.size();
    log.collect(1100ms); // Longer than the entire first WAV.
    check(log.items.size() == paused_from, "Paused PCM must not advance or finish");
    const auto resumed_from = log.items.size();
    player.toggle_pause();
    const auto resumed = log.playing(30, 0, false, resumed_from);
    const auto next = log.playing(30, 1, false, resumed_from);
    check(next.observed - resumed.observed >= 450ms, "Pause consumed or discarded remaining PCM");
    log.status(30, PlaybackStatus::stopped, resumed_from);
    log.no_error();
}

void pause_and_resume_gap(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    auto plan = sequence(fixture, 1000);
    plan.tracks.resize(2);
    player.play(40, plan, {}, 0.5F);
    const auto gap = log.playing(40, 0, true);
    log.collect(400ms);
    player.toggle_pause();
    const auto paused = log.status(40, PlaybackStatus::paused);
    check(paused.event.track_index == 0 && paused.event.between_languages,
          "Paused gap must retain the preceding track's index and gap flag");
    const auto paused_from = log.items.size();
    log.collect(1150ms);
    check(log.items.size() == paused_from, "Paused gap must not expire");
    const auto resumed_from = log.items.size();
    player.toggle_pause();
    const auto resumed = log.playing(40, 0, true, resumed_from);
    const auto next = log.playing(40, 1, false, resumed_from);
    const auto remaining = 1000ms - (paused.observed - gap.observed);
    const auto elapsed = next.observed - resumed.observed;
    check(elapsed >= remaining - 120ms, "Resume skipped the remainder of the gap");
    check(elapsed <= remaining + 250ms, "Resume restarted the full gap instead of its remainder");
    log.status(40, PlaybackStatus::stopped, resumed_from);
    log.no_error();
}

void replace_point_and_discard_stale_events(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    player.play(50, single(fixture.long_track, "old-point"), {}, 0.5F);
    log.playing(50, 0, false);
    player.toggle_pause();
    player.devices(); // Leave an old-session paused event queued, deliberately unpolled.
    const auto from = log.items.size();
    player.play(51, single(fixture.es, "replacement-point"), {}, 0.5F);
    log.status(51, PlaybackStatus::stopped, from);
    log.collect(200ms);
    log.only_session(51, from);
    log.no_error();
    const auto playing = std::count_if(log.items.begin() + static_cast<std::ptrdiff_t>(from),
                                      log.items.end(), [](const RecordedEvent& item) {
        return item.event.status == PlaybackStatus::playing;
    });
    check(playing == 1, "Replacement must play one new track, not resume the old point");
}

void replace_during_gap(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    player.play(60, sequence(fixture, 500), {}, 0.5F);
    log.playing(60, 0, true);
    const auto from = log.items.size();
    player.play(61, single(fixture.en, "gap-replacement"), {}, 0.5F);
    log.status(61, PlaybackStatus::stopped, from);
    log.collect(650ms);
    log.only_session(61, from);
    log.no_error();
    for (std::size_t i = from; i < log.items.size(); ++i) {
        check(!log.items[i].event.between_languages && log.items[i].event.track_index == 0,
              "Replacement retained a pending language or gap");
    }
}

void preflight_last_missing_and_failed_replacement(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    auto plan = sequence(fixture, 100);
    plan.point_id = "missing-last-point";
    plan.tracks.back().path = fixture.missing;
    player.play(70, plan, {}, 0.5F);
    auto error = log.status(70, PlaybackStatus::error);
    check(error.event.track_index == 2 && !error.event.between_languages,
          "Preflight error must identify the failing last track");
    check(error.event.message.find(plan.point_id) != std::string::npos &&
          error.event.message.find("lengua=pt") != std::string::npos &&
          error.event.message.find(fixture.missing.u8string()) != std::string::npos,
          "Error must contain point, language and UTF-8 path");
    log.no_playing(0);

    auto from = log.items.size();
    player.play(71, single(fixture.long_track), {}, 0.5F);
    log.playing(71, 0, false, from);
    from = log.items.size();
    // Reusing the caller's session ID must not resurrect the previous request.
    player.play(71, plan, {}, 0.5F);
    error = log.status(71, PlaybackStatus::error, from);
    check(error.event.track_index == 2, "Failed replacement reports the wrong track");
    log.collect(300ms);
    log.no_playing(from);
    log.only_session(71, from);
}

void invalid_files_and_first_frame(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    std::uint64_t session = 80;
    for (const auto& path : {fixture.invalid_wav, fixture.invalid_mp3,
                             fixture.empty_wav, fixture.directory_wav}) {
        const auto from = log.items.size();
        auto plan = single(path, "invalid-file-point");
        plan.tracks.front().language = Language::en;
        player.play(session, plan, {}, 0.5F);
        const auto error = log.status(session, PlaybackStatus::error, from);
        check(error.event.track_index == 0, "Invalid file must identify track zero");
        check(error.event.message.find("invalid-file-point") != std::string::npos &&
              error.event.message.find("lengua=en") != std::string::npos &&
              error.event.message.find(path.u8string()) != std::string::npos,
              "Invalid-file error lacks playback context");
        if (path != fixture.directory_wav) {
            check(error.event.message.find("ma_result=") != std::string::npos,
                  "Decoder failure must propagate the actual ma_result");
        }
        log.no_playing(from);
        ++session;
    }
}

void unicode_paths(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    player.play(90, single(fixture.unicode, "unicode-point"), "test:null", 0.5F);
    log.status(90, PlaybackStatus::stopped);
    log.no_error();
    const auto from = log.items.size();
    const auto missing = fixture.unicode.parent_path() / fs::u8path(u8"ausente_\u00f1_\u4e2d.wav");
    player.play(91, single(missing, "unicode-missing-point"), {}, 0.5F);
    const auto error = log.status(91, PlaybackStatus::error, from);
    check(error.event.message.find(missing.u8string()) != std::string::npos,
          "Unicode error path must round-trip as UTF-8");
    log.no_playing(from);
}

void volume_validation(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    const std::vector<float> invalid{-0.01F, 1.01F,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};
    for (const auto volume : invalid) {
        bool threw = false;
        try {
            player.set_volume(volume);
        } catch (const AppError&) {
            threw = true;
        }
        check(threw, "Invalid set_volume must throw AppError, never clamp");
    }
    check(player.poll_events().empty(), "Invalid set_volume must not emit playback events");
    EventLog log(player);
    player.play(100, single(fixture.long_track), {}, 0.0F);
    log.playing(100, 0, false);
    player.set_volume(0.25F);
    player.set_volume(1.0F);
    player.toggle_pause();
    log.status(100, PlaybackStatus::paused);
    player.set_volume(0.0F);
    player.devices();
    player.stop();
    log.status(100, PlaybackStatus::stopped);
    log.no_error();

    std::uint64_t session = 101;
    for (const auto volume : invalid) {
        const auto from = log.items.size();
        // Unlike set_volume, failures of play are asynchronous error events.
        player.play(session, single(fixture.es), {}, volume);
        const auto error = log.status(session, PlaybackStatus::error, from);
        check(error.event.message.find("volumen") != std::string::npos,
              "Invalid play volume must identify the volume failure");
        log.no_playing(from);
        ++session;
    }
}

void invalid_device_and_plan(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    EventLog log(player);
    player.play(110, single(fixture.es), "missing-device-not-a-fallback", 0.5F);
    const auto error = log.status(110, PlaybackStatus::error);
    check(error.event.message.find("missing-device-not-a-fallback") != std::string::npos,
          "An unavailable explicit device must fail, never fall back");
    log.no_playing(0);

    auto from = log.items.size();
    auto empty = single(fixture.es);
    empty.tracks.clear();
    player.play(111, empty, {}, 0.5F);
    log.status(111, PlaybackStatus::error, from);
    log.no_playing(from);
    from = log.items.size();
    auto negative_gap = sequence(fixture, -1);
    player.play(112, negative_gap, {}, 0.5F);
    log.status(112, PlaybackStatus::error, from);
    log.no_playing(from);

    from = log.items.size();
    player.play(113, single(fixture.es), {}, 0.5F);
    log.status(113, PlaybackStatus::stopped, from);
    log.no_error(from);
}

void pending_commands_and_concurrent_enumeration(const Fixtures& fixture) {
    MiniaudioPlayer player(true);
    auto enumerations = std::async(std::launch::async, [&player] {
        for (unsigned i = 0; i < 20; ++i) {
            const auto outputs = player.devices();
            check(outputs.size() == 1 && outputs.front().id == "test:null",
                  "Concurrent enumeration returned a non-test device");
        }
    });
    for (std::uint64_t session = 120; session < 130; ++session) {
        player.play(session, single(fixture.long_track), {}, 0.5F);
        player.toggle_pause();
        player.set_volume(0.25F);
    }
    player.stop();
    player.play(130, single(fixture.es, "last-request-wins"), {}, 0.5F);
    EventLog log(player);
    log.status(130, PlaybackStatus::stopped);
    log.collect(150ms);
    log.only_session(130, 0);
    log.no_error();
    check(enumerations.wait_for(4s) == std::future_status::ready,
          "Replacement discarded a synchronous enumeration reply");
    enumerations.get();
}

void shutdown_joins_all_states(const Fixtures& fixture) {
    {
        const auto start = Clock::now();
        {
            MiniaudioPlayer player(true);
            player.devices();
        }
        check(Clock::now() - start < 2s, "Idle shutdown did not join promptly");
    }
    {
        const auto start = Clock::now();
        {
            MiniaudioPlayer player(true);
            auto plan = sequence(fixture, 1000);
            plan.tracks.assign(200, {Language::es, fixture.unicode});
            player.play(140, std::move(plan), {}, 0.5F);
            // Destruct during queued play/preflight, with no caller-side stop.
        }
        check(Clock::now() - start < 2s, "Shutdown did not cancel pending preflight");
    }
    for (unsigned state = 0; state != 4; ++state) {
        auto player = std::make_unique<MiniaudioPlayer>(true);
        EventLog log(*player);
        const bool gap = state >= 2;
        auto plan = sequence(fixture, 1000);
        if (!gap) {
            plan.tracks.front().path = fixture.long_track;
        }
        player->play(141 + state, plan, {}, 0.5F);
        log.playing(141 + state, 0, gap);
        if (state % 2 != 0) {
            player->toggle_pause();
            log.status(141 + state, PlaybackStatus::paused);
        }
        const auto start = Clock::now();
        player.reset();
        check(Clock::now() - start < 2s, "Destructor did not promptly join the audio worker");
    }
}

} // namespace

int main() {
    Watchdog watchdog;
    try {
        const Fixtures fixture;
        using Test = std::pair<const char*, void (*)(const Fixtures&)>;
        const std::vector<Test> tests{
            {"construction and explicit null devices", creation_and_devices},
            {"three languages, real time, order and gap indices", three_languages_and_indices},
            {"stop during track and pending preflight", stop_during_track},
            {"stop cancels gap", stop_during_gap},
            {"pause and resume PCM", pause_and_resume_track},
            {"pause and resume remaining gap", pause_and_resume_gap},
            {"replace point and discard stale events", replace_point_and_discard_stale_events},
            {"replace during gap", replace_during_gap},
            {"last missing file and failed same-ID replacement", preflight_last_missing_and_failed_replacement},
            {"invalid WAV/MP3, first frame and regular file", invalid_files_and_first_frame},
            {"Unicode paths and UTF-8 errors", unicode_paths},
            {"volume validation", volume_validation},
            {"no device fallback and invalid plans", invalid_device_and_plan},
            {"pending commands and concurrent devices", pending_commands_and_concurrent_enumeration},
            {"shutdown joins in all states", shutdown_joins_all_states},
        };
        for (const auto& test : tests) {
            test.second(fixture);
            std::cout << "PASS: " << test.first << '\n';
        }
        std::cout << tests.size() << " audio tests passed (explicit real-time null backend)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "FAIL: unknown exception\n";
        return EXIT_FAILURE;
    }
}
