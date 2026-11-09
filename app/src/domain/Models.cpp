#include "domain/Models.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>

namespace audioguia {
namespace {

struct CatalogEntry {
    const char* id;
    const char* name;
    Direction direction;
    int number;
    const char* file;
};

constexpr std::array<CatalogEntry, 14> catalog{{
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

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) {
            return false;
        }
        if (first < 0x80) {
            continue;
        }
        std::uint32_t value{0};
        std::uint32_t minimum{0};
        std::size_t remaining{0};
        if (first >= 0xC2 && first <= 0xDF) {
            value = first & 0x1F;
            minimum = 0x80;
            remaining = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            value = first & 0x0F;
            minimum = 0x800;
            remaining = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            value = first & 0x07;
            minimum = 0x10000;
            remaining = 3;
        } else {
            return false;
        }
        if (remaining > text.size() - i) {
            return false;
        }
        while (remaining-- > 0) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            value = (value << 6) | (next & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            return false;
        }
    }
    return true;
}

void validate_audio_path(const std::filesystem::path& path) {
    if (path.empty()) {
        return; // An unconfigured association is reported by the planner when selected.
    }
    auto text = path.generic_u8string();
    std::replace(text.begin(), text.end(), '\\', '/');
    if (!valid_utf8(text) || text.compare(0, 6, "audio/") != 0 ||
        text.size() == 6 || text.back() == '/' || text.find(':') != std::string::npos) {
        throw AppError("La ruta de audio debe ser relativa a audio/: " + text);
    }
    // Check both separator styles even on POSIX, where a backslash is not a separator.
    for (std::size_t begin = 0; begin < text.size();) {
        const auto end = text.find('/', begin);
        const auto component = text.substr(begin, end == std::string::npos ? end : end - begin);
        if (component.empty() || component == "." || component == "..") {
            throw AppError("Ruta de audio con traversal o componente invalido: " + text);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
}

} // namespace

AppConfig default_config() {
    AppConfig config;
    config.points.reserve(catalog.size());
    for (const auto& entry : catalog) {
        Point point;
        point.id = entry.id;
        point.name = entry.name;
        point.direction = entry.direction;
        point.number = entry.number;
        point.audio = {
            {Language::es, std::filesystem::u8path("audio/0001-ESP") / entry.file},
            {Language::en, std::filesystem::u8path("audio/0002-ENG") / entry.file},
            {Language::pt, std::filesystem::u8path("audio/0003-PORR") / entry.file},
        };
        config.points.push_back(std::move(point));
    }
    return config;
}

std::string id(Direction value) {
    switch (value) {
    case Direction::up: return "up";
    case Direction::down: return "down";
    }
    throw AppError("Direccion desconocida");
}

std::string id(Mode value) {
    switch (value) {
    case Mode::simple: return "simple";
    case Mode::sequential: return "sequential";
    case Mode::custom: return "custom";
    }
    throw AppError("Modo desconocido");
}

std::string id(Language value) {
    switch (value) {
    case Language::es: return "es";
    case Language::en: return "en";
    case Language::pt: return "pt";
    }
    throw AppError("Idioma desconocido");
}

std::string label(Direction value) {
    switch (value) {
    case Direction::up: return "UP";
    case Direction::down: return "DOWN";
    }
    throw AppError("Direccion desconocida");
}

std::string label(Mode value) {
    switch (value) {
    case Mode::simple: return "Simple";
    case Mode::sequential: return "Secuencial";
    case Mode::custom: return "Personalizado";
    }
    throw AppError("Modo desconocido");
}

std::string label(Language value) {
    switch (value) {
    case Language::es: return u8"Espa\u00f1ol";
    case Language::en: return u8"Ingl\u00e9s";
    case Language::pt: return u8"Portugu\u00e9s";
    }
    throw AppError("Idioma desconocido");
}

std::string short_label(Language value) {
    switch (value) {
    case Language::es: return "ESP";
    case Language::en: return "ENG";
    case Language::pt: return "PORT";
    }
    throw AppError("Idioma desconocido");
}

Direction direction_from_id(const std::string& value) {
    if (value == "up") return Direction::up;
    if (value == "down") return Direction::down;
    throw AppError("ID de direccion desconocido: " + value);
}

Mode mode_from_id(const std::string& value) {
    if (value == "simple") return Mode::simple;
    if (value == "sequential") return Mode::sequential;
    if (value == "custom") return Mode::custom;
    throw AppError("ID de modo desconocido: " + value);
}

Language language_from_id(const std::string& value) {
    if (value == "es") return Language::es;
    if (value == "en") return Language::en;
    if (value == "pt") return Language::pt;
    throw AppError("ID de idioma desconocido: " + value);
}

void validate_config(const AppConfig& config) {
    if (config.schema_version != 1) {
        throw AppError("schema_version debe ser 1");
    }
    const auto& preferences = config.preferences;
    (void)id(preferences.direction);
    (void)id(preferences.mode);
    (void)id(preferences.simple_language);
    if (preferences.custom_languages.empty() || preferences.custom_languages.size() > 3) {
        throw AppError("custom_languages debe contener entre 1 y 3 idiomas");
    }
    std::set<Language> languages;
    for (const auto language : preferences.custom_languages) {
        (void)id(language);
        if (!languages.insert(language).second) {
            throw AppError("custom_languages contiene idiomas repetidos");
        }
    }
    if (preferences.gap_ms < 0 || preferences.gap_ms > 60000) {
        throw AppError("gap_ms debe estar entre 0 y 60000");
    }
    if (!std::isfinite(preferences.volume) || preferences.volume < 0.0F || preferences.volume > 1.0F) {
        throw AppError("volume debe estar entre 0 y 1");
    }
    if (!valid_utf8(preferences.output_device)) {
        throw AppError("output_device debe ser UTF-8 sin NUL");
    }
    std::set<int> keys;
    const auto add_key = [&keys](int key) {
        if (key < 0 || key > 9 || !keys.insert(key).second) {
            throw AppError("El teclado requiere digitos 0..9 sin colisiones");
        }
    };
    for (const auto key : preferences.keyboard.point_keys) {
        add_key(key);
    }
    add_key(preferences.keyboard.stop_key);
    // compact_x/y are signed screen coordinates; -1 is also the unset sentinel.
    if (config.points.size() != catalog.size()) {
        throw AppError("El catalogo debe contener exactamente 14 puntos");
    }
    std::set<std::string> point_ids;
    std::set<std::pair<Direction, int>> point_numbers;
    for (const auto& point : config.points) {
        (void)id(point.direction);
        const auto entry = std::find_if(catalog.begin(), catalog.end(), [&point](const auto& candidate) {
            return point.id == candidate.id;
        });
        if (entry == catalog.end()) {
            throw AppError("ID de punto desconocido: " + point.id);
        }
        if (!point_ids.insert(point.id).second ||
            !point_numbers.emplace(point.direction, point.number).second) {
            throw AppError("ID o numero de punto duplicado: " + point.id);
        }
        if (point.number < 1 || point.number > (point.direction == Direction::up ? 8 : 6) ||
            point.direction != entry->direction || point.number != entry->number) {
            throw AppError("Direccion o numero fuera del catalogo: " + point.id);
        }
        if (point.name.empty() || !valid_utf8(point.name)) {
            throw AppError("El nombre del punto debe ser UTF-8 no vacio: " + point.id);
        }
        for (const auto& association : point.audio) {
            (void)id(association.first);
            validate_audio_path(association.second);
        }
    }
}

} // namespace audioguia
