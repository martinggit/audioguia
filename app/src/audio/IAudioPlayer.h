#pragma once
#include "domain/Models.h"
#include <optional>
#include <vector>

namespace audioguia {

struct OutputDevice {
    std::string id;
    std::string name;
};

struct AudioEvent {
    std::uint64_t session{0};
    PlaybackStatus status{PlaybackStatus::stopped};
    std::size_t track_index{0};
    bool between_languages{false};
    std::string message;
    std::optional<std::size_t> next_track_index{};
    std::uint64_t position_ms{0};
    std::optional<std::uint64_t> duration_ms{};
    bool progress_only{false};
};

class IAudioPlayer {
public:
    virtual ~IAudioPlayer() = default;
    virtual void play(std::uint64_t session, PlaybackPlan plan,
                      std::string output_device, float volume) = 0;
    virtual void stop() = 0;
    virtual void toggle_pause() = 0;
    virtual void set_volume(float volume) = 0;
    virtual std::vector<OutputDevice> devices() = 0;
    virtual std::vector<AudioEvent> poll_events() = 0;
};

} // namespace audioguia
