#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace audioguia {

enum class Direction { up, down };
enum class Mode { simple, sequential, custom };
enum class Language { es, en, pt };
enum class PlaybackStatus { stopped, preparing, playing, paused, error };

class AppError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Point {
    std::string id;
    std::string name;
    Direction direction{Direction::up};
    int number{1};
    std::map<Language, std::filesystem::path> audio;
};

struct KeyboardBindings {
    std::array<int, 8> point_keys{{1, 2, 3, 4, 5, 6, 7, 8}};
    int stop_key{0};
    bool enter_pauses{true};
};

struct Preferences {
    Direction direction{Direction::up};
    Mode mode{Mode::simple};
    Language simple_language{Language::es};
    std::vector<Language> custom_languages{Language::es, Language::en, Language::pt};
    int gap_ms{1000};
    float volume{0.75F};
    std::string output_device;
    KeyboardBindings keyboard;
    // Legacy schema-1 fields: retained for configuration compatibility only.
    int compact_x{-1};
    int compact_y{-1};
    bool always_on_top{false};
};

struct AppConfig {
    int schema_version{1};
    std::vector<Point> points;
    Preferences preferences;
};

struct Track {
    Language language{Language::es};
    std::filesystem::path path;
};

struct PlaybackPlan {
    std::string point_id;
    std::string point_name;
    Direction direction{Direction::up};
    Mode mode{Mode::simple};
    std::vector<Track> tracks;
    int gap_ms{1000};
};

AppConfig default_config();
std::string id(Direction value);
std::string id(Mode value);
std::string id(Language value);
std::string label(Direction value);
std::string label(Mode value);
std::string label(Language value);
std::string short_label(Language value);
Direction direction_from_id(const std::string& value);
Mode mode_from_id(const std::string& value);
Language language_from_id(const std::string& value);
void validate_config(const AppConfig& config);

} // namespace audioguia
