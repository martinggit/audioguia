#pragma once
#include "audio/IAudioPlayer.h"
#include "config/ConfigRepository.h"
#include <memory>
#include <optional>

namespace audioguia {

class AppController {
public:
    AppController(std::filesystem::path root, std::unique_ptr<IAudioPlayer> player);
    const AppConfig& config() const noexcept { return config_; }
    const AudioEvent& state() const noexcept { return state_; }
    const std::optional<PlaybackPlan>& active_plan() const noexcept { return active_; }
    const std::filesystem::path& root() const noexcept { return repository_.root(); }
    const std::string& warning() const noexcept { return warning_; }
    void play(Direction direction, int number);
    void stop();
    void toggle_pause();
    void poll();
    void apply_config(AppConfig config);
    void set_preferences(Preferences preferences);
    void remember_compact_position(int x, int y);
    std::vector<OutputDevice> devices();
    std::string selection_text() const;
    std::string playback_title() const;
    std::string playback_detail() const;
    std::string status_text() const;
    void save();
private:
    ConfigRepository repository_;
    AppConfig config_;
    std::unique_ptr<IAudioPlayer> player_;
    AudioEvent state_;
    std::optional<PlaybackPlan> active_;
    std::uint64_t session_{0};
    std::string warning_;
    bool config_loaded_{true};
};

} // namespace audioguia
