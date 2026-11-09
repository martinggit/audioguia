#pragma once
#include "domain/Models.h"

namespace audioguia {

class PlaybackPlanner {
public:
    static PlaybackPlan build(const AppConfig& config, Direction direction,
                              int number, const std::filesystem::path& portable_root);
};

} // namespace audioguia
