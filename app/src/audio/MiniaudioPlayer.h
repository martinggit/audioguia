#pragma once

#include "IAudioPlayer.h"

#include <memory>

namespace audioguia {

class MiniaudioPlayer final : public IAudioPlayer {
public:
    // The null backend is an explicit, real-time test fixture, never a fallback.
    explicit MiniaudioPlayer(bool test_null_backend = false);
    ~MiniaudioPlayer() override;

    MiniaudioPlayer(const MiniaudioPlayer&) = delete;
    MiniaudioPlayer& operator=(const MiniaudioPlayer&) = delete;
    MiniaudioPlayer(MiniaudioPlayer&&) = delete;
    MiniaudioPlayer& operator=(MiniaudioPlayer&&) = delete;

    // Asynchronous: play/stop invalidate pending transport and unpolled events.
    // Empty output_device pins the enumerated default; play failures are events.
    void play(std::uint64_t session, PlaybackPlan plan,
              std::string output_device, float volume) override;
    void stop() override;
    void toggle_pause() override;
    // Invalid values (including non-finite values) throw AppError, not clamp.
    void set_volume(float volume) override;
    // Synchronous enumeration on the worker; failures throw AppError.
    std::vector<OutputDevice> devices() override;

    // Drain from the UI timer (e.g. every 50 ms). No UI callbacks are made.
    // Gap events retain the zero-based index of the track just completed.
    std::vector<AudioEvent> poll_events() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audioguia
