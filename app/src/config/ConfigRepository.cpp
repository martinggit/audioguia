#include "config/ConfigRepository.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace audioguia {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

void require_keys(const Json& object, std::initializer_list<const char*> allowed, const char* context) {
    if (!object.is_object()) throw AppError(std::string(context) + " debe ser un objeto JSON");
    for (auto field = object.begin(); field != object.end(); ++field) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&field](const char* key) {
            return field.key() == key;
        });
        if (!known) throw AppError(std::string("Clave desconocida: ") + context + "." + field.key());
    }
}

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw AppError("No se puede abrir la configuracion: " + path.u8string());
    }
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw AppError("Error al leer la configuracion: " + path.u8string());
    }
    return bytes;
}

int integer(const Json& value) {
    if (!value.is_number_integer()) {
        throw AppError("Se esperaba un entero JSON");
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw AppError("Entero JSON fuera de rango");
        }
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        throw AppError("Entero JSON fuera de rango");
    }
    return static_cast<int>(number);
}

Json to_json(const AppConfig& config) {
    const auto& p = config.preferences;
    Json languages = Json::array();
    for (const auto language : p.custom_languages) {
        languages.push_back(id(language));
    }
    Json points = Json::array();
    for (const auto& point : config.points) {
        Json audio = Json::object();
        for (const auto& association : point.audio) {
            auto relative = association.second.generic_u8string();
            std::replace(relative.begin(), relative.end(), '\\', '/');
            audio[id(association.first)] = relative;
        }
        points.push_back({{"id", point.id}, {"name", point.name},
                          {"direction", id(point.direction)}, {"number", point.number}, {"audio", audio}});
    }
    return {
        {"schema_version", config.schema_version},
        {"preferences", {
            {"direction", id(p.direction)}, {"mode", id(p.mode)},
            {"simple_language", id(p.simple_language)}, {"custom_languages", languages},
            {"gap_ms", p.gap_ms}, {"volume", p.volume}, {"output_device", p.output_device},
            {"keyboard", {{"point_keys", p.keyboard.point_keys}, {"stop_key", p.keyboard.stop_key},
                          {"enter_pauses", p.keyboard.enter_pauses}}},
            {"compact_x", p.compact_x}, {"compact_y", p.compact_y}, {"always_on_top", p.always_on_top},
        }},
        {"points", points},
    };
}

AppConfig from_json(const Json& document) {
    require_keys(document, {"schema_version", "preferences", "points"}, "configuracion");
    AppConfig config;
    config.schema_version = integer(document.at("schema_version"));
    const auto& preferences = document.at("preferences");
    require_keys(preferences, {"direction", "mode", "simple_language", "custom_languages", "gap_ms",
        "volume", "output_device", "keyboard", "compact_x", "compact_y", "always_on_top"}, "preferences");
    auto& p = config.preferences;
    p.direction = direction_from_id(preferences.at("direction").get<std::string>());
    p.mode = mode_from_id(preferences.at("mode").get<std::string>());
    p.simple_language = language_from_id(preferences.at("simple_language").get<std::string>());
    const auto& languages = preferences.at("custom_languages");
    if (!languages.is_array()) {
        throw AppError("custom_languages debe ser un array");
    }
    p.custom_languages.clear();
    for (const auto& language : languages) {
        p.custom_languages.push_back(language_from_id(language.get<std::string>()));
    }
    p.gap_ms = integer(preferences.at("gap_ms"));
    const auto& volume = preferences.at("volume");
    if (!volume.is_number() || !(volume.get<double>() >= 0.0 && volume.get<double>() <= 1.0)) {
        throw AppError("volume debe estar entre 0 y 1");
    }
    p.volume = volume.get<float>();
    p.output_device = preferences.at("output_device").get<std::string>();
    const auto& keyboard = preferences.at("keyboard");
    require_keys(keyboard, {"point_keys", "stop_key", "enter_pauses"}, "keyboard");
    const auto& point_keys = keyboard.at("point_keys");
    if (!point_keys.is_array() || point_keys.size() != p.keyboard.point_keys.size()) {
        throw AppError("keyboard.point_keys debe ser un array de 8 digitos");
    }
    for (std::size_t i = 0; i < p.keyboard.point_keys.size(); ++i) {
        p.keyboard.point_keys[i] = integer(point_keys.at(i));
    }
    p.keyboard.stop_key = integer(keyboard.at("stop_key"));
    p.keyboard.enter_pauses = keyboard.at("enter_pauses").get<bool>();
    p.compact_x = integer(preferences.at("compact_x"));
    p.compact_y = integer(preferences.at("compact_y"));
    p.always_on_top = preferences.at("always_on_top").get<bool>();
    const auto& points = document.at("points");
    if (!points.is_array()) {
        throw AppError("points debe ser un array");
    }
    for (const auto& entry : points) {
        require_keys(entry, {"id", "name", "direction", "number", "audio"}, "punto");
        Point point;
        point.id = entry.at("id").get<std::string>();
        point.name = entry.at("name").get<std::string>();
        point.direction = direction_from_id(entry.at("direction").get<std::string>());
        point.number = integer(entry.at("number"));
        const auto& audio = entry.at("audio");
        if (!audio.is_object()) {
            throw AppError("audio debe ser un objeto de idiomas y rutas UTF-8");
        }
        for (auto association = audio.begin(); association != audio.end(); ++association) {
            auto relative = association.value().get<std::string>();
            std::replace(relative.begin(), relative.end(), '\\', '/');
            point.audio.emplace(language_from_id(association.key()), fs::u8path(relative));
        }
        config.points.push_back(std::move(point));
    }
    validate_config(config);
    return config;
}

[[noreturn]] void io_error(const std::string& operation, unsigned long code) {
    throw AppError(operation + " (codigo " + std::to_string(code) + ")");
}

// The exclusive sibling reservation and all native handles are owned until cleanup.
class TemporaryFile {
public:
    explicit TemporaryFile(const fs::path& target) {
        static std::atomic<unsigned long> serial{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 64; ++attempt) {
            path_ = target;
            path_ += fs::u8path("." + std::to_string(tick) + "." + std::to_string(serial++) + ".tmp");
#ifdef _WIN32
            handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return;
            const auto error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
                io_error("No se pudo crear el temporal", error);
            }
#else
            handle_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (handle_ != -1) return;
            const auto error = errno;
            if (error != EEXIST) {
                io_error("No se pudo crear el temporal", static_cast<unsigned long>(error));
            }
#endif
        }
        throw AppError("No se pudo reservar un temporal unico");
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    ~TemporaryFile() {
        close_noexcept();
        std::error_code ignored;
        fs::remove(path_, ignored);
    }

    const fs::path& path() const noexcept { return path_; }

    void write(const std::string& bytes) {
        std::size_t offset{0};
        while (offset < bytes.size()) {
            const auto chunk = std::min<std::size_t>(bytes.size() - offset, 1024 * 1024);
#ifdef _WIN32
            DWORD written{0};
            if (!WriteFile(handle_, bytes.data() + offset, static_cast<DWORD>(chunk), &written, nullptr)) {
                io_error("No se pudo escribir el temporal", GetLastError());
            }
            if (written == 0) throw AppError("Escritura incompleta del temporal");
#else
            const auto written = ::write(handle_, bytes.data() + offset, chunk);
            if (written < 0) {
                if (errno == EINTR) continue;
                io_error("No se pudo escribir el temporal", static_cast<unsigned long>(errno));
            }
            if (written == 0) throw AppError("Escritura incompleta del temporal");
#endif
            offset += static_cast<std::size_t>(written);
        }
#ifdef _WIN32
        if (!FlushFileBuffers(handle_)) io_error("FlushFileBuffers fallo", GetLastError());
        const auto handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (!CloseHandle(handle)) io_error("No se pudo cerrar el temporal", GetLastError());
#else
        while (::fsync(handle_) != 0) {
            if (errno != EINTR) io_error("fsync fallo", static_cast<unsigned long>(errno));
        }
        const auto handle = std::exchange(handle_, -1);
        if (::close(handle) != 0) io_error("No se pudo cerrar el temporal", static_cast<unsigned long>(errno));
#endif
    }

private:
    void close_noexcept() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_ != -1) ::close(handle_);
        handle_ = -1;
#endif
    }

    fs::path path_;
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int handle_{-1};
#endif
};

bool regular_or_missing(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(status))) {
        return false;
    }
    if (error || !fs::is_regular_file(status)) {
        throw AppError("El destino debe ser un archivo regular: " + path.u8string());
    }
    return true;
}

} // namespace

ConfigRepository::ConfigRepository(std::filesystem::path portable_root)
    : root_(std::move(portable_root)) {}

std::filesystem::path ConfigRepository::file_path() const {
    return root_ / "app" / "config" / "audioguia.json";
}

AppConfig ConfigRepository::load() const {
    const auto path = file_path();
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) {
        throw AppError("No se pudo consultar la configuracion: " + path.u8string());
    }
    // First run is read-only: defaults are returned, not persisted or auto-repaired.
    if (!exists) return default_config();
    try {
        std::vector<std::set<std::string>> keys;
        const auto unique_keys = [&keys](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                throw AppError("Clave JSON repetida: " + value.get<std::string>());
            else if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        return from_json(Json::parse(read_bytes(path), unique_keys));
    } catch (const Json::exception& exception) {
        throw AppError("JSON de configuracion invalido en " + path.u8string() + ": " + exception.what());
    } catch (const fs::filesystem_error& exception) {
        throw AppError("Ruta UTF-8 invalida en la configuracion: " + std::string(exception.what()));
    }
}

void ConfigRepository::save(const AppConfig& config) const {
    validate_config(config);
    std::string bytes;
    try {
        // JSON is UTF-8 without BOM. Escaped non-ASCII also survives ANSI editors.
        bytes = to_json(config).dump(2, ' ', true) + "\n";
    } catch (const Json::exception& exception) {
        throw AppError("No se pudo codificar JSON UTF-8: " + std::string(exception.what()));
    }
    const auto path = file_path();
    auto backup = path;
    backup += ".bak";
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) throw AppError("No se pudo crear app/config: " + error.message());
    const bool replacing = regular_or_missing(path);
    if (replacing) (void)regular_or_missing(backup);
    TemporaryFile replacement(path);
    replacement.write(bytes);
    if (!replacing) {
#ifdef _WIN32
        if (!MoveFileExW(replacement.path().c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
            io_error("No se pudo instalar la configuracion", GetLastError());
        }
#else
        fs::rename(replacement.path(), path, error);
        if (error) throw AppError("No se pudo instalar la configuracion: " + error.message());
#endif
        return;
    }

    TemporaryFile previous(path);
    previous.write(read_bytes(path));
#ifdef _WIN32
    // Stage ReplaceFileW's backup so an earlier .bak is untouched on failure.
    TemporaryFile staged_backup(backup);
    staged_backup.write("");
    fs::remove(staged_backup.path(), error);
    if (error) throw AppError("No se pudo preparar el respaldo: " + error.message());
    const auto rollback = [&previous, &path](unsigned long cause) {
        if (!MoveFileExW(previous.path().c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw AppError("Fallo el guardado y el rollback (codigo " + std::to_string(GetLastError()) +
                           "); error original " + std::to_string(cause));
        }
        io_error("Guardado cancelado; original restaurado", cause);
    };
    if (!ReplaceFileW(path.c_str(), replacement.path().c_str(), staged_backup.path().c_str(), 0, nullptr, nullptr)) {
        rollback(GetLastError());
    }
    if (!MoveFileExW(staged_backup.path().c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        rollback(GetLastError());
    }
#else
    fs::rename(replacement.path(), path, error);
    if (error) throw AppError("No se pudo reemplazar la configuracion: " + error.message());
    fs::rename(previous.path(), backup, error);
    if (error) {
        const auto cause = error.message();
        fs::rename(previous.path(), path, error);
        if (error) throw AppError("Fallo el guardado y el rollback: " + error.message() + "; " + cause);
        throw AppError("Guardado cancelado; original restaurado: " + cause);
    }
#endif
}

} // namespace audioguia
