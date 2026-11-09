#include "domain/PlaybackPlanner.h"

#include <algorithm>

namespace audioguia {

PlaybackPlan PlaybackPlanner::build(const AppConfig& config, Direction direction,
                                   int number, const std::filesystem::path& portable_root) {
    validate_config(config);
    (void)id(direction);
    const auto point = std::find_if(config.points.begin(), config.points.end(),
                                    [direction, number](const Point& candidate) {
        return candidate.direction == direction && candidate.number == number;
    });
    if (point == config.points.end()) {
        throw AppError("No existe el punto " + id(direction) + " " + std::to_string(number));
    }

    PlaybackPlan plan;
    plan.point_id = point->id;
    plan.point_name = point->name;
    plan.direction = direction;
    plan.mode = config.preferences.mode;
    plan.gap_ms = config.preferences.gap_ms;

    std::vector<Language> languages;
    switch (plan.mode) {
    case Mode::simple: languages = {config.preferences.simple_language}; break;
    case Mode::sequential: languages = {Language::es, Language::en, Language::pt}; break;
    case Mode::custom: languages = config.preferences.custom_languages; break;
    }
    plan.tracks.reserve(languages.size());
    for (const auto language : languages) {
        const auto audio = point->audio.find(language);
        if (audio == point->audio.end() || audio->second.empty()) {
            throw AppError("Audio no configurado para el punto " + point->id + " (" + point->name +
                           "), idioma " + id(language) + " (" + label(language) + ")");
        }
        auto relative = audio->second.generic_u8string();
        std::replace(relative.begin(), relative.end(), '\\', '/');
        plan.tracks.push_back({language, (portable_root / std::filesystem::u8path(relative)).lexically_normal()});
    }
    // Paths and metadata are values. No filesystem or playback state is consulted.
    return plan;
}

} // namespace audioguia
