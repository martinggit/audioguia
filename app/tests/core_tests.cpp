#include "config/ConfigRepository.h"
#include "domain/PlaybackPlanner.h"
#include "../vendor/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace audioguia;
using Json = nlohmann::json;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Action>
std::string check_app_error(Action action) {
    try {
        action();
    } catch (const AppError& error) {
        check(std::string(error.what()).size() != 0, "AppError must explain the failure");
        return error.what();
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
        std::cout << "core_tests: " << total_ - failed_ << '/' << total_
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
            root = parent / ("audioguia_core_tests_" + stamp + "_" + std::to_string(attempt));
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

void check_config(const AppConfig& actual, const AppConfig& expected) {
    check(actual.schema_version == expected.schema_version, "schema_version did not round-trip");
    check(actual.points.size() == expected.points.size(), "Point count did not round-trip");
    for (std::size_t i = 0; i < expected.points.size(); ++i) {
        const auto& a = actual.points[i];
        const auto& e = expected.points[i];
        check(a.id == e.id && a.name == e.name && a.direction == e.direction &&
              a.number == e.number && a.audio == e.audio, "Point did not round-trip: " + e.id);
    }
    const auto& a = actual.preferences;
    const auto& e = expected.preferences;
    check(a.direction == e.direction && a.mode == e.mode && a.simple_language == e.simple_language &&
          a.custom_languages == e.custom_languages, "Language/mode preferences did not round-trip");
    check(a.gap_ms == e.gap_ms && a.volume == e.volume && a.output_device == e.output_device,
          "Audio preferences did not round-trip");
    check(a.keyboard.point_keys == e.keyboard.point_keys && a.keyboard.stop_key == e.keyboard.stop_key &&
          a.keyboard.enter_pauses == e.keyboard.enter_pauses, "Keyboard preferences did not round-trip");
    check(a.compact_x == e.compact_x && a.compact_y == e.compact_y && a.always_on_top == e.always_on_top,
          "Window preferences did not round-trip");
}

void check_plan(const PlaybackPlan& actual, const PlaybackPlan& expected) {
    check(actual.point_id == expected.point_id && actual.point_name == expected.point_name &&
          actual.direction == expected.direction && actual.mode == expected.mode &&
          actual.gap_ms == expected.gap_ms, "Plan metadata changed");
    check(actual.tracks.size() == expected.tracks.size(), "Plan track count changed");
    for (std::size_t i = 0; i < expected.tracks.size(); ++i) {
        check(actual.tracks[i].language == expected.tracks[i].language &&
              actual.tracks[i].path == expected.tracks[i].path, "Plan track changed: " + std::to_string(i));
    }
}

struct CatalogExpectation {
    const char* point_id;
    const char* name;
    Direction direction;
    int number;
    const char* file;
};

constexpr std::array<CatalogExpectation, 14> catalog{{
    {"bienvenida", "Bienvenida", Direction::up, 1, "0001.mp3"},
    {"carcel-y-ciudad", u8"C\u00e1rcel y ciudad", Direction::up, 2, "0002.mp3"},
    {"isla-de-los-lobos", "Isla de los Lobos", Direction::up, 3, "0003.mp3"},
    {"isla-de-los-pajaros", u8"Isla de los P\u00e1jaros", Direction::up, 4, "0004.mp3"},
    {"canal-beagle", "Canal Beagle", Direction::up, 5, "0005.mp3"},
    {"yamanas", u8"Y\u00e1manas", Direction::up, 6, "0006.mp3"},
    {"isla-bridge", "Isla Bridge", Direction::up, 7, "0007.mp3"},
    {"archipielago-les-eclaireurs", u8"Archipi\u00e9lago Les \u00c9claireurs", Direction::up, 8, "0008.mp3"},
    {"monte-cervantes", "Monte Cervantes", Direction::down, 1, "0009.mp3"},
    {"pinguinera", u8"Ping\u00fcinera", Direction::down, 2, "0010.mp3"},
    {"estancia-harberton", "Estancia Harberton", Direction::down, 3, "0011.mp3"},
    {"puerto-williams", "Puerto Williams", Direction::down, 4, "0012.mp3"},
    {"estancia-tunel", u8"Estancia T\u00fanel", Direction::down, 5, "0013.mp3"},
    {"saint-christopher", "Saint Christopher", Direction::down, 6, "0014.mp3"},
}};

const std::array<std::pair<Language, const char*>, 3> language_folders{{
    {Language::es, "0001-ESP"}, {Language::en, "0002-ENG"}, {Language::pt, "0003-PORT"},
}};

fs::path expected_audio(Language language, const char* file) {
    const auto folder = std::find_if(language_folders.begin(), language_folders.end(), [language](const auto& item) {
        return item.first == language;
    });
    check(folder != language_folders.end(), "Invalid language in test expectation");
    return fs::path("audio") / folder->second / file;
}

void check_config_directory(const ConfigRepository& repository, bool has_backup) {
    std::size_t entries{0};
    for (const auto& entry : fs::directory_iterator(repository.file_path().parent_path())) {
        check(entry.path() == repository.file_path() || (has_backup && entry.path() == backup_path(repository)),
              "Unexpected file or leaked temporary: " + entry.path().u8string());
        ++entries;
    }
    check(entries == (has_backup ? 2U : 1U), "Unexpected configuration directory contents");
}

void default_catalog() {
    const auto config = default_config();
    validate_config(config);
    check(config.schema_version == 1, "Default schema must be 1");
    check(config.points.size() == 14, "Default catalog must contain 14 points");
    const Preferences defaults;
    auto expected = config;
    expected.preferences = defaults;
    check_config(config, expected);
    check(config.preferences.direction == Direction::up && config.preferences.mode == Mode::simple &&
          config.preferences.simple_language == Language::es && config.preferences.gap_ms == 1000 &&
          config.preferences.volume == 0.75F && config.preferences.output_device.empty(), "Wrong initial preferences");
    std::size_t associations{0};
    for (const auto& entry : catalog) {
        const auto point = std::find_if(config.points.begin(), config.points.end(), [&entry](const Point& candidate) {
            return candidate.direction == entry.direction && candidate.number == entry.number;
        });
        check(point != config.points.end(), "Missing catalog point: " + std::string(entry.point_id));
        check(point->id == entry.point_id && point->name == entry.name, "Wrong point identity: " + std::string(entry.point_id));
        check(point->audio.size() == 3, "Every default point must map all three languages");
        for (const auto& language : language_folders) {
            const auto audio = point->audio.find(language.first);
            check(audio != point->audio.end(), "Missing default language: " + point->id + "/" + id(language.first));
            check(audio->second == expected_audio(language.first, entry.file),
                  "Wrong default path: " + point->id + "/" + id(language.first));
            ++associations;
        }
    }
    check(associations == 42, "Expected exactly 42 default audio mappings");
}

void repository_round_trip() {
    const TemporaryRoot temporary;
    const ConfigRepository repository(temporary.root);
    const auto original = default_config();
    repository.save(original);
    check(fs::is_regular_file(repository.file_path()), "save must create app/config and its JSON file");
    check(!fs::exists(backup_path(repository)), "First save must not invent a backup");
    check_config(repository.load(), original);
    check_config_directory(repository, false);
    const auto original_bytes = read_bytes(repository.file_path());

    auto changed = original;
    auto& p = changed.preferences;
    p.direction = Direction::down;
    p.mode = Mode::custom;
    p.simple_language = Language::pt;
    p.custom_languages = {Language::pt, Language::es};
    p.gap_ms = 321;
    p.volume = 0.375F;
    p.output_device = "fake:output with spaces";
    p.keyboard.point_keys = {{8, 7, 6, 5, 4, 3, 2, 1}};
    p.keyboard.stop_key = 9;
    p.keyboard.enter_pauses = false;
    p.compact_x = -1800;
    p.compact_y = 73;
    p.always_on_top = true;
    std::reverse(changed.points.begin(), changed.points.end());
    changed.points.front().name = "Edited point name";
    changed.points.front().audio.erase(Language::en);
    changed.points.front().audio[Language::pt].clear();
    repository.save(changed);
    check_config(repository.load(), changed);
    check(read_bytes(backup_path(repository)) == original_bytes, "Backup must preserve the previous bytes exactly");
    const auto changed_bytes = read_bytes(repository.file_path());
    repository.save(original);
    check_config(repository.load(), original);
    check(read_bytes(backup_path(repository)) == changed_bytes, "Backup must rotate to the immediately previous file");
    check_config_directory(repository, true);
}

void truncated_json_and_backup() {
    const TemporaryRoot temporary;
    const ConfigRepository repository(temporary.root);
    const auto original = default_config();
    repository.save(original);
    auto changed = original;
    changed.preferences.gap_ms = 37;
    repository.save(changed);
    const auto backup = read_bytes(backup_path(repository));
    const std::string truncated = "{\"schema_version\":1,\"preferences\":{\"mode\":\"custom\"";
    write_bytes(repository.file_path(), truncated);
    check_app_error([&] { (void)repository.load(); });
    check(read_bytes(repository.file_path()) == truncated, "load must not repair or overwrite truncated JSON");
    check(read_bytes(backup_path(repository)) == backup, "Failed load must not touch the backup");

    // Recovery is explicit: load does not silently fall back to .bak.
    const ConfigRepository recovery(temporary.root / "manual backup reader");
    recovery.save(default_config());
    write_bytes(recovery.file_path(), backup);
    check_config(recovery.load(), original);
    check(read_bytes(repository.file_path()) == truncated, "Reading the backup changed the damaged original");
    check_config_directory(repository, true);
}

struct InvalidSave {
    const char* name;
    void (*mutate)(AppConfig&);
};

const std::vector<InvalidSave> invalid_saves{
    {"schema", [](AppConfig& c) { c.schema_version = 2; }},
    {"direction enum", [](AppConfig& c) { c.preferences.direction = static_cast<Direction>(99); }},
    {"mode enum", [](AppConfig& c) { c.preferences.mode = static_cast<Mode>(99); }},
    {"simple language enum", [](AppConfig& c) { c.preferences.simple_language = static_cast<Language>(99); }},
    {"custom language enum", [](AppConfig& c) { c.preferences.custom_languages = {static_cast<Language>(99)}; }},
    {"empty custom languages", [](AppConfig& c) { c.preferences.custom_languages.clear(); }},
    {"duplicate custom languages", [](AppConfig& c) { c.preferences.custom_languages = {Language::es, Language::es}; }},
    {"too many custom languages", [](AppConfig& c) { c.preferences.custom_languages.push_back(Language::es); }},
    {"negative gap", [](AppConfig& c) { c.preferences.gap_ms = -1; }},
    {"excessive gap", [](AppConfig& c) { c.preferences.gap_ms = 60001; }},
    {"negative volume", [](AppConfig& c) { c.preferences.volume = -0.01F; }},
    {"excessive volume", [](AppConfig& c) { c.preferences.volume = 1.01F; }},
    {"NaN volume", [](AppConfig& c) { c.preferences.volume = std::numeric_limits<float>::quiet_NaN(); }},
    {"infinite volume", [](AppConfig& c) { c.preferences.volume = std::numeric_limits<float>::infinity(); }},
    {"negative infinite volume", [](AppConfig& c) { c.preferences.volume = -std::numeric_limits<float>::infinity(); }},
    {"invalid UTF-8 device", [](AppConfig& c) { c.preferences.output_device = std::string(1, static_cast<char>(0xff)); }},
    {"NUL device", [](AppConfig& c) { c.preferences.output_device = std::string("fake\0device", 11); }},
    {"duplicate point key", [](AppConfig& c) { c.preferences.keyboard.point_keys[1] = 1; }},
    {"stop key collision", [](AppConfig& c) { c.preferences.keyboard.stop_key = 1; }},
    {"negative point key", [](AppConfig& c) { c.preferences.keyboard.point_keys[0] = -1; }},
    {"point key above digit range", [](AppConfig& c) { c.preferences.keyboard.point_keys[0] = 10; }},
    {"negative stop key", [](AppConfig& c) { c.preferences.keyboard.stop_key = -1; }},
    {"stop key above digit range", [](AppConfig& c) { c.preferences.keyboard.stop_key = 10; }},
    {"missing point", [](AppConfig& c) { c.points.pop_back(); }},
    {"extra point", [](AppConfig& c) { c.points.push_back(c.points.front()); }},
    {"duplicate point ID", [](AppConfig& c) { c.points[1].id = c.points[0].id; }},
    {"duplicate point number", [](AppConfig& c) { c.points[1].number = 1; }},
    {"unknown point ID", [](AppConfig& c) { c.points[0].id = "unknown-point"; }},
    {"point direction enum", [](AppConfig& c) { c.points[0].direction = static_cast<Direction>(99); }},
    {"wrong point direction", [](AppConfig& c) { c.points[0].direction = Direction::down; }},
    {"DOWN 7 outside catalog", [](AppConfig& c) { c.points.back().number = 7; }},
    {"zero point number", [](AppConfig& c) { c.points[0].number = 0; }},
    {"empty point name", [](AppConfig& c) { c.points[0].name.clear(); }},
    {"invalid UTF-8 point name", [](AppConfig& c) { c.points[0].name = std::string(1, static_cast<char>(0xff)); }},
    {"NUL point name", [](AppConfig& c) { c.points[0].name = std::string("a\0b", 3); }},
    {"audio language enum", [](AppConfig& c) { c.points[0].audio.emplace(static_cast<Language>(99), "audio/test.mp3"); }},
};

struct InvalidJson {
    const char* name;
    const char* pointer;
    Json value;
};

const std::vector<InvalidJson> invalid_json{
    {"unknown schema", "/schema_version", 2},
    {"floating schema", "/schema_version", 1.0},
    {"string schema", "/schema_version", "1"},
    {"overflow schema", "/schema_version", 2147483648ULL},
    {"unknown direction", "/preferences/direction", "sideways"},
    {"unknown mode", "/preferences/mode", "shuffle"},
    {"unknown language", "/preferences/simple_language", "fr"},
    {"unknown custom language", "/preferences/custom_languages", Json::array({"es", "xx"})},
    {"duplicate custom languages", "/preferences/custom_languages", Json::array({"pt", "pt"})},
    {"empty custom languages", "/preferences/custom_languages", Json::array()},
    {"custom languages not array", "/preferences/custom_languages", "es"},
    {"negative gap", "/preferences/gap_ms", -1},
    {"excessive gap", "/preferences/gap_ms", 60001},
    {"fractional gap", "/preferences/gap_ms", 1.5},
    {"string gap", "/preferences/gap_ms", "1000"},
    {"boolean gap", "/preferences/gap_ms", true},
    {"overflow gap", "/preferences/gap_ms", 2147483648ULL},
    {"underflow gap", "/preferences/gap_ms", -2147483649LL},
    {"negative volume", "/preferences/volume", -0.01},
    {"excessive volume", "/preferences/volume", 1.01},
    {"string volume", "/preferences/volume", "0.75"},
    {"null volume", "/preferences/volume", nullptr},
    {"non-string output device", "/preferences/output_device", 23},
    {"NUL output device", "/preferences/output_device", std::string("fake\0device", 11)},
    {"duplicate point keys", "/preferences/keyboard/point_keys", Json::array({1, 1, 3, 4, 5, 6, 7, 8})},
    {"short point keys", "/preferences/keyboard/point_keys", Json::array({1, 2})},
    {"point keys not array", "/preferences/keyboard/point_keys", "12345678"},
    {"negative point key", "/preferences/keyboard/point_keys/0", -1},
    {"out of range point key", "/preferences/keyboard/point_keys/0", 10},
    {"fractional point key", "/preferences/keyboard/point_keys/0", 1.0},
    {"stop key collision", "/preferences/keyboard/stop_key", 1},
    {"out of range stop key", "/preferences/keyboard/stop_key", 10},
    {"non-boolean pause key", "/preferences/keyboard/enter_pauses", 1},
    {"fractional window coordinate", "/preferences/compact_x", 1.5},
    {"non-boolean on-top flag", "/preferences/always_on_top", "true"},
    {"points not array", "/points", Json::object()},
    {"missing catalog", "/points", Json::array()},
    {"unknown point ID", "/points/0/id", "unknown-point"},
    {"duplicate point ID", "/points/1/id", "bienvenida"},
    {"duplicate point number", "/points/1/number", 1},
    {"unknown point direction", "/points/0/direction", "sideways"},
    {"wrong catalog direction", "/points/0/direction", "down"},
    {"DOWN 7 outside catalog", "/points/13/number", 7},
    {"fractional point number", "/points/0/number", 1.0},
    {"empty point name", "/points/0/name", ""},
    {"NUL point name", "/points/0/name", std::string("a\0b", 3)},
    {"audio not object", "/points/0/audio", Json::array()},
    {"unknown audio language key", "/points/0/audio/xx", "audio/test.mp3"},
    {"non-string audio path", "/points/0/audio/es", 123},
    {"NUL audio path", "/points/0/audio/es", std::string("audio/a\0b.mp3", 13)},
    {"unknown root key", "/unknown", true},
    {"unknown preferences key", "/preferences/unknown", true},
    {"unknown keyboard key", "/preferences/keyboard/unknown", true},
    {"unknown point key", "/points/0/unknown", true},
};

void reject_json(const Json& document, const ConfigRepository& repository) {
    const auto bytes = document.dump();
    const auto backup = read_bytes(backup_path(repository));
    write_bytes(repository.file_path(), bytes);
    check_app_error([&] { (void)repository.load(); });
    check(read_bytes(repository.file_path()) == bytes, "Failed load changed its input");
    check(read_bytes(backup_path(repository)) == backup, "Failed load changed the backup");
    check_config_directory(repository, true);
}

} // namespace

int main() {
    Tests tests;
    tests.run("default schema, preferences and all 42 catalog mappings", default_catalog);
    tests.run("enum IDs round-trip and reject unknown values", [] {
        for (const auto direction : {Direction::up, Direction::down}) {
            check(direction_from_id(id(direction)) == direction, "Direction ID mismatch");
        }
        for (const auto mode : {Mode::simple, Mode::sequential, Mode::custom}) {
            check(mode_from_id(id(mode)) == mode, "Mode ID mismatch");
        }
        for (const auto language : {Language::es, Language::en, Language::pt}) {
            check(language_from_id(id(language)) == language, "Language ID mismatch");
            check(!label(language).empty() && !short_label(language).empty(), "Missing language label");
        }
        check_app_error([] { (void)direction_from_id("UP"); });
        check_app_error([] { (void)mode_from_id("unknown"); });
        check_app_error([] { (void)language_from_id("ESP"); });
    });

    for (const auto direction : {Direction::up, Direction::down}) {
        for (const auto mode : {Mode::simple, Mode::sequential, Mode::custom}) {
            tests.run("planner snapshot " + id(direction) + "/" + id(mode), [=] {
                const TemporaryRoot temporary;
                const auto root = temporary.root / "portable root does not exist";
                auto config = default_config();
                config.preferences.direction = direction == Direction::up ? Direction::down : Direction::up;
                config.preferences.mode = mode;
                config.preferences.simple_language = Language::pt;
                config.preferences.custom_languages = {Language::en, Language::es};
                config.preferences.gap_ms = 347;
                const auto& point = catalog[direction == Direction::up ? 1 : 9];
                const auto languages = mode == Mode::simple ? std::vector<Language>{Language::pt}
                    : mode == Mode::sequential ? std::vector<Language>{Language::es, Language::en, Language::pt}
                    : std::vector<Language>{Language::en, Language::es};
                PlaybackPlan expected{point.point_id, point.name, direction, mode, {}, 347};
                for (const auto language : languages) {
                    expected.tracks.push_back({language, root / expected_audio(language, point.file)});
                }
                const auto plan = PlaybackPlanner::build(config, direction, 2, root);
                check_plan(plan, expected);
                config.preferences.mode = mode == Mode::simple ? Mode::custom : Mode::simple;
                config.preferences.simple_language = Language::en;
                config.preferences.custom_languages = {Language::pt};
                config.preferences.gap_ms = 59;
                for (auto& configured_point : config.points) {
                    configured_point.name = "Changed after building plan";
                    configured_point.audio.clear();
                }
                check_plan(plan, expected);
                check(!fs::exists(root), "Planner must not require or create a filesystem tree");
            });
        }
    }

    const std::vector<std::vector<Language>> custom_orders{
        {Language::es}, {Language::en}, {Language::pt},
        {Language::es, Language::en}, {Language::en, Language::es},
        {Language::es, Language::pt}, {Language::pt, Language::es},
        {Language::en, Language::pt}, {Language::pt, Language::en},
        {Language::es, Language::en, Language::pt}, {Language::es, Language::pt, Language::en},
        {Language::en, Language::es, Language::pt}, {Language::en, Language::pt, Language::es},
        {Language::pt, Language::es, Language::en}, {Language::pt, Language::en, Language::es},
    };
    for (const auto& order : custom_orders) {
        std::string name = "custom subset/order";
        for (const auto language : order) name += " " + id(language);
        tests.run(name, [&] {
            const TemporaryRoot temporary;
            auto config = default_config();
            config.preferences.mode = Mode::custom;
            config.preferences.custom_languages = order;
            config.preferences.gap_ms = 0;
            const auto plan = PlaybackPlanner::build(config, Direction::down, 6, temporary.root);
            check(plan.tracks.size() == order.size() && plan.gap_ms == 0, "Custom plan size or zero gap incorrect");
            for (std::size_t i = 0; i < order.size(); ++i) {
                check(plan.tracks[i].language == order[i] &&
                      plan.tracks[i].path == temporary.root / expected_audio(order[i], "0014.mp3"),
                      "Custom order/path incorrect at track " + std::to_string(i));
            }
            check(!fs::exists(temporary.root / "audio"), "Planner unexpectedly created audio files");
        });
    }

    const std::array<std::pair<Direction, int>, 6> invalid_points{{
        {Direction::up, -1}, {Direction::up, 0}, {Direction::up, 9},
        {Direction::down, 0}, {Direction::down, 7}, {Direction::down, 8},
    }};
    for (const auto& point : invalid_points) {
        tests.run("planner rejects " + id(point.first) + " " + std::to_string(point.second), [&] {
            const TemporaryRoot temporary;
            const auto error = check_app_error([&] {
                (void)PlaybackPlanner::build(default_config(), point.first, point.second, temporary.root);
            });
            check(error.find(id(point.first)) != std::string::npos &&
                  error.find(std::to_string(point.second)) != std::string::npos, "Missing invalid-point error context");
        });
    }
    for (const auto& language : language_folders) {
        for (const bool empty : {false, true}) {
            tests.run("planner missing mapping " + id(language.first) + (empty ? " empty" : " absent"), [&] {
                const TemporaryRoot temporary;
                auto config = default_config();
                config.preferences.mode = Mode::sequential;
                auto& point = config.points[9];
                if (empty) point.audio[language.first].clear();
                else point.audio.erase(language.first);
                validate_config(config);
                const auto error = check_app_error([&] {
                    (void)PlaybackPlanner::build(config, Direction::down, 2, temporary.root);
                });
                check(error.find(point.id) != std::string::npos && error.find(point.name) != std::string::npos &&
                      error.find("idioma " + id(language.first)) != std::string::npos &&
                      error.find(label(language.first)) != std::string::npos, "Mapping error lacks point/language context");
            });
        }
    }
    tests.run("unselected missing languages do not prevent a simple plan", [] {
        const TemporaryRoot temporary;
        auto config = default_config();
        config.points[0].audio.erase(Language::en);
        config.points[0].audio[Language::pt].clear();
        const auto plan = PlaybackPlanner::build(config, Direction::up, 1, temporary.root);
        check(plan.tracks.size() == 1 && plan.tracks.front().language == Language::es, "Unselected mapping was required");
    });

    tests.run("missing repository is read-only and resolves root/app/config", [] {
        const TemporaryRoot temporary;
        const auto root = temporary.root / "portable root not created";
        const ConfigRepository repository(root);
        check(repository.root() == root && repository.file_path() == root / "app/config/audioguia.json", "Wrong repository root");
        check_config(repository.load(), default_config());
        check(!fs::exists(root), "First load must not persist defaults or create directories");
    });
    tests.run("strict round-trip, edited catalog, preferences and byte-exact backup rotation", repository_round_trip);
    tests.run("UTF-8 names, devices, audio paths and filesystem roots with spaces and accents", [] {
        const TemporaryRoot temporary;
        const auto root = temporary.root / fs::u8path(u8"Ra\u00edz port\u00e1til con espacios");
        const ConfigRepository repository(root);
        auto config = default_config();
        config.points[0].name = u8"Gu\u00eda del r\u00edo y ni\u00f1os";
        config.points[0].audio[Language::es] = fs::u8path(u8"audio/Espa\u00f1ol con espacios/canci\u00f3n n\u00famero 1.mp3");
        config.preferences.output_device = u8"Auriculares Jos\u00e9, salida n\u00famero 2";
        repository.save(config);
        check_config(repository.load(), config);
        check(fs::is_regular_file(repository.file_path()), "UTF-8 filesystem root was not used");
        const auto bytes = read_bytes(repository.file_path());
        check(bytes.compare(0, 3, "\xef\xbb\xbf") != 0, "Saved JSON must not have a BOM");
        const auto plan = PlaybackPlanner::build(repository.load(), Direction::up, 1, root);
        check(plan.tracks.front().path == root / config.points[0].audio.at(Language::es), "UTF-8 planner path changed");
        check(!fs::exists(root / "audio"), "Tests must not depend on real audio files");
    });
    tests.run("Windows relative separators normalize on load, save and planning", [] {
        const TemporaryRoot temporary;
        const ConfigRepository repository(temporary.root);
        auto config = default_config();
        config.points[0].audio[Language::es] = fs::u8path("audio\\folder with spaces\\test.mp3");
        repository.save(config);
        auto expected = config;
        expected.points[0].audio[Language::es] = "audio/folder with spaces/test.mp3";
        check_config(repository.load(), expected);
        const auto plan = PlaybackPlanner::build(config, Direction::up, 1, temporary.root);
        check(plan.tracks.front().path == temporary.root / "audio/folder with spaces/test.mp3", "Windows separator was not normalized");
        auto document = Json::parse(read_bytes(repository.file_path()));
        document["points"][0]["audio"]["es"] = "audio\\otra carpeta\\archivo.mp3";
        write_bytes(repository.file_path(), document.dump());
        expected.points[0].audio[Language::es] = "audio/otra carpeta/archivo.mp3";
        check_config(repository.load(), expected);
    });
    tests.run("truncated JSON preserves the original and permits explicit backup recovery", truncated_json_and_backup);

    for (const auto& invalid : invalid_saves) {
        tests.run(std::string("strict save rejects ") + invalid.name, [&] {
            const TemporaryRoot temporary;
            const ConfigRepository repository(temporary.root);
            repository.save(default_config());
            repository.save(default_config());
            const auto original = read_bytes(repository.file_path());
            const auto backup = read_bytes(backup_path(repository));
            auto config = default_config();
            invalid.mutate(config);
            check_app_error([&] { validate_config(config); });
            check_app_error([&] { repository.save(config); });
            check(read_bytes(repository.file_path()) == original, "Rejected save changed the original");
            check(read_bytes(backup_path(repository)) == backup, "Rejected save changed the backup");
            check_config_directory(repository, true);
        });
    }
    for (const auto& invalid : invalid_json) {
        tests.run(std::string("strict load rejects ") + invalid.name, [&] {
            const TemporaryRoot temporary;
            const ConfigRepository repository(temporary.root);
            repository.save(default_config());
            repository.save(default_config());
            auto document = Json::parse(read_bytes(repository.file_path()));
            document[Json::json_pointer(invalid.pointer)] = invalid.value;
            reject_json(document, repository);
        });
    }

    const std::vector<std::string> invalid_paths{
        "../outside.mp3", "audio/../outside.mp3", "audio/sub/../../outside.mp3", "audio/./file.mp3",
        "/audio/file.mp3", "//server/share/file.mp3", "C:/audio/file.mp3", "C:\\audio\\file.mp3", "C:audio\\file.mp3",
        "\\\\server\\share\\file.mp3", "\\audio\\file.mp3", "audio\\..\\outside.mp3", "audio/sub\\..\\file.mp3",
        "audio/file.mp3:stream", "audio/", "other/file.mp3",
    };
    for (const auto& path : invalid_paths) {
        tests.run("load/save reject unsafe path " + path, [&] {
            const TemporaryRoot temporary;
            const ConfigRepository repository(temporary.root);
            repository.save(default_config());
            repository.save(default_config());
            const auto original = read_bytes(repository.file_path());
            const auto backup = read_bytes(backup_path(repository));
            auto config = default_config();
            config.points[0].audio[Language::es] = fs::u8path(path);
            check_app_error([&] { repository.save(config); });
            check(read_bytes(repository.file_path()) == original && read_bytes(backup_path(repository)) == backup,
                  "Unsafe-path save modified persisted data");
            auto document = Json::parse(original);
            document["points"][0]["audio"]["es"] = path;
            reject_json(document, repository);
        });
    }

    const std::array<std::pair<const char*, const char*>, 5> duplicate_keys{{
        {"root", "\"schema_version\":1"},
        {"preferences", "\"gap_ms\":1000"},
        {"keyboard", "\"enter_pauses\":true"},
        {"point", "\"id\":\"bienvenida\""},
        {"audio", "\"es\":\"audio/0001-ESP/0001.mp3\""},
    }};
    for (const auto& duplicate : duplicate_keys) {
        tests.run(std::string("strict load rejects duplicate JSON key in ") + duplicate.first, [&] {
            const TemporaryRoot temporary;
            const ConfigRepository repository(temporary.root);
            repository.save(default_config());
            auto bytes = Json::parse(read_bytes(repository.file_path())).dump();
            const std::string member = duplicate.second;
            const auto position = bytes.find(member);
            check(position != std::string::npos, "Duplicate-key fixture member not found");
            bytes.replace(position, member.size(), member + "," + member);
            write_bytes(repository.file_path(), bytes);
            check_app_error([&] { (void)repository.load(); });
            check(read_bytes(repository.file_path()) == bytes, "Duplicate-key load changed the original");
            check_config_directory(repository, false);
        });
    }

    tests.run("strict load requires every serialized object member", [] {
        const TemporaryRoot temporary;
        const ConfigRepository repository(temporary.root);
        repository.save(default_config());
        repository.save(default_config());
        const auto original = Json::parse(read_bytes(repository.file_path()));
        const std::vector<std::string> objects{"", "/preferences", "/preferences/keyboard", "/points/0"};
        for (const auto& pointer : objects) {
            const auto& object = original.at(Json::json_pointer(pointer));
            for (auto member = object.begin(); member != object.end(); ++member) {
                auto document = original;
                document.at(Json::json_pointer(pointer)).erase(member.key());
                reject_json(document, repository);
            }
        }
    });
    tests.run("strict JSON syntax, types and UTF-8 are not silently repaired", [] {
        const TemporaryRoot temporary;
        const ConfigRepository repository(temporary.root);
        repository.save(default_config());
        const auto valid = Json::parse(read_bytes(repository.file_path()));
        auto bad_utf8 = valid;
        bad_utf8["points"][0]["name"] = "INVALID_UTF8_MARKER";
        auto bytes = bad_utf8.dump();
        const auto position = bytes.find("INVALID_UTF8_MARKER");
        check(position != std::string::npos, "UTF-8 fixture marker missing");
        bytes.replace(position, std::string("INVALID_UTF8_MARKER").size(), std::string(1, static_cast<char>(0xff)));
        const std::vector<std::string> malformed{"", "null", "[]", "{}", "{", valid.dump() + " trailing", bytes};
        for (const auto& malformed_bytes : malformed) {
            write_bytes(repository.file_path(), malformed_bytes);
            check_app_error([&] { (void)repository.load(); });
            check(read_bytes(repository.file_path()) == malformed_bytes, "Malformed load modified original bytes");
        }
    });
    tests.run("valid preference boundaries, signed coordinates and single custom language", [] {
        const TemporaryRoot temporary;
        const ConfigRepository repository(temporary.root);
        for (const int gap : {0, 60000}) {
            for (const float volume : {0.0F, 1.0F}) {
                auto config = default_config();
                config.preferences.gap_ms = gap;
                config.preferences.volume = volume;
                config.preferences.custom_languages = {Language::en};
                config.preferences.compact_x = std::numeric_limits<int>::min();
                config.preferences.compact_y = std::numeric_limits<int>::max();
                repository.save(config);
                check_config(repository.load(), config);
            }
        }
    });
    tests.run("I/O save failure keeps original and blocked backup intact and can be retried", [] {
        const TemporaryRoot temporary;
        const ConfigRepository repository(temporary.root);
        repository.save(default_config());
        const auto original = read_bytes(repository.file_path());
        const auto backup = backup_path(repository);
        fs::create_directory(backup);
        write_bytes(backup / "sentinel", "do not replace this directory");
        auto changed = default_config();
        changed.preferences.gap_ms = 99;
        for (int attempt = 0; attempt < 2; ++attempt) {
            check_app_error([&] { repository.save(changed); });
            check(read_bytes(repository.file_path()) == original, "I/O save failure changed original bytes");
            check(read_bytes(backup / "sentinel") == "do not replace this directory", "Save replaced blocked backup");
            check_config_directory(repository, true);
        }
        fs::remove_all(backup);
        repository.save(changed);
        check_config(repository.load(), changed);
        check(read_bytes(backup) == original, "Retry did not preserve original in backup");
        check_config_directory(repository, true);
    });
    tests.run("save refuses non-regular configuration targets", [] {
        const TemporaryRoot temporary;
        const ConfigRepository repository(temporary.root);
        repository.save(default_config());
        fs::remove(repository.file_path());
        fs::create_directory(repository.file_path());
        write_bytes(repository.file_path() / "sentinel", "preserve target");
        check_app_error([&] { repository.save(default_config()); });
        check(read_bytes(repository.file_path() / "sentinel") == "preserve target", "Save destroyed a non-regular target");
    });
    return tests.result();
}
