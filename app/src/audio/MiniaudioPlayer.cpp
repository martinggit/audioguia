#include "MiniaudioPlayer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#endif

// This is the only translation unit that implements miniaudio.
#define MINIAUDIO_IMPLEMENTATION
#include "../../vendor/miniaudio/miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace audioguia {
namespace {

using Clock = std::chrono::steady_clock;
constexpr ma_uint32 sample_rate = 48000;
constexpr ma_uint32 channels = 2;
constexpr ma_uint32 buffer_frames = 16384;
constexpr ma_uint32 decode_frames = 2048;
constexpr auto worker_period = std::chrono::milliseconds{5};
#ifdef _WIN32
constexpr auto endpoint_poll_period = std::chrono::milliseconds{100};
#endif
constexpr const char* null_device_id = "test:null";

static_assert(std::atomic<bool>::is_always_lock_free,
              "The audio callback requires lock-free flags");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "The audio callback requires a lock-free generation counter");
static_assert(std::atomic<ma_result>::is_always_lock_free,
               "The audio callback requires lock-free error reporting");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "The audio callback requires a lock-free PCM counter");

bool valid_volume(float volume) noexcept {
    return std::isfinite(volume) && volume >= 0.0F && volume <= 1.0F;
}

std::string result_message(const char* operation, ma_result result) {
    return std::string{operation} + ": ma_result=" + std::to_string(result) +
           " (" + ma_result_description(result) + ")";
}

void require_success(const char* operation, ma_result result) {
    if (result != MA_SUCCESS) {
        throw AppError(result_message(operation, result));
    }
}

const char* language_id(Language language) noexcept {
    switch (language) {
    case Language::es: return "es";
    case Language::en: return "en";
    case Language::pt: return "pt";
    }
    return "?";
}

struct Decoder {
    ma_decoder value{};
    bool initialized{false};

    Decoder() = default;
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) = delete;
    Decoder& operator=(Decoder&&) = delete;
    ~Decoder() { close(); }

    void open(const std::filesystem::path& path) {
        auto config = ma_decoder_config_init(ma_format_f32, channels, sample_rate);
        auto extension = path.extension().u8string();
        for (char& ch : extension) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        if (extension == ".wav") {
            config.encodingFormat = ma_encoding_format_wav;
        } else if (extension == ".mp3") {
            config.encodingFormat = ma_encoding_format_mp3;
        } else {
            throw AppError("Formato no admitido: se requiere WAV o MP3");
        }
#ifdef _WIN32
        // Codec stdio can narrow paths on MinGW; the VFS uses CreateFileW.
        // Autodetection also keeps failed opens on the VFS cleanup path.
        config.encodingFormat = ma_encoding_format_unknown;
        const auto result = ma_decoder_init_vfs_w(nullptr, path.c_str(), &config, &value);
#else
        const auto result = ma_decoder_init_file(path.c_str(), &config, &value);
#endif
        require_success("ma_decoder_init_file", result);
        initialized = true;
    }

    ma_result close() noexcept {
        if (!initialized) {
            return MA_SUCCESS;
        }
        initialized = false;
        return ma_decoder_uninit(&value);
    }
};

#ifdef _WIN32
static_assert(std::atomic<ULONG>::is_always_lock_free,
              "COM notification reference counting must not block");

std::string hresult_message(const char* operation, HRESULT result) {
    std::ostringstream text;
    text << operation << ": HRESULT=0x" << std::hex
         << static_cast<unsigned long>(result);
    return text.str();
}

std::string endpoint_utf8(const wchar_t* id) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, id, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        throw AppError(hresult_message("WideCharToMultiByte",
                                      HRESULT_FROM_WIN32(GetLastError())));
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, id, -1, text.data(),
                            size, nullptr, nullptr) == 0) {
        throw AppError(hresult_message("WideCharToMultiByte",
                                      HRESULT_FROM_WIN32(GetLastError())));
    }
    text.pop_back();
    return text;
}

struct WorkerCom {
    HRESULT result{S_OK};
    bool initialized{false};

    explicit WorkerCom(bool test_backend) noexcept {
        if (!test_backend) {
            result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            initialized = SUCCEEDED(result);
        }
    }
    ~WorkerCom() {
        if (initialized) {
            CoUninitialize();
        }
    }
    WorkerCom(const WorkerCom&) = delete;
    WorkerCom& operator=(const WorkerCom&) = delete;
    WorkerCom(WorkerCom&&) = delete;
    WorkerCom& operator=(WorkerCom&&) = delete;
};

// Its owner keeps a permanent reference and a fixed address until unregistering.
// MMDevice does not AddRef/Release on registration/unregistration. Release never
// deletes this embedded object, so no callback can perform a final COM release.
class EndpointMonitor final : public IMMNotificationClient {
public:
    EndpointMonitor() = default;
    EndpointMonitor(const EndpointMonitor&) = delete;
    EndpointMonitor& operator=(const EndpointMonitor&) = delete;
    EndpointMonitor(EndpointMonitor&&) = delete;
    EndpointMonitor& operator=(EndpointMonitor&&) = delete;
    ~EndpointMonitor() { close(); }

    void open(const wchar_t* endpoint_id) {
        close();
        id_ = endpoint_id;
        lost_.store(false, std::memory_order_relaxed);
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                           CLSCTX_INPROC_SERVER,
                                           __uuidof(IMMDeviceEnumerator),
                                           reinterpret_cast<void**>(&enumerator_));
        if (FAILED(result)) {
            throw AppError(hresult_message("CoCreateInstance(MMDeviceEnumerator)", result));
        }
        result = enumerator_->GetDevice(id_.c_str(), &endpoint_);
        if (FAILED(result)) {
            throw AppError(hresult_message("IMMDeviceEnumerator::GetDevice", result));
        }
        result = enumerator_->RegisterEndpointNotificationCallback(this);
        if (FAILED(result)) {
            throw AppError(hresult_message("RegisterEndpointNotificationCallback", result));
        }
        registered_ = true;
        next_poll_ = Clock::time_point{};
        check();
    }

    void check() {
        if (lost_.load(std::memory_order_acquire)) {
            throw AppError("Endpoint WASAPI retirado o no activo; no se cambia de salida");
        }
        const auto now = Clock::now();
        if (now < next_poll_) {
            return;
        }
        next_poll_ = now + endpoint_poll_period;
        DWORD state = 0;
        const HRESULT result = endpoint_->GetState(&state);
        if (FAILED(result)) {
            throw AppError(hresult_message("IMMDevice::GetState", result));
        }
        if ((state & DEVICE_STATE_ACTIVE) == 0) {
            throw AppError("Endpoint WASAPI no activo (estado=" +
                           std::to_string(state) + "); no se cambia de salida");
        }
    }

    void close() noexcept {
        if (registered_) {
            enumerator_->UnregisterEndpointNotificationCallback(this);
            registered_ = false;
        }
        // Waiting and COM releases belong to the worker, never to a notification.
        while (callbacks_.load(std::memory_order_acquire) != 0) {
            std::this_thread::yield();
        }
        if (endpoint_) {
            endpoint_->Release();
            endpoint_ = nullptr;
        }
        if (enumerator_) {
            enumerator_->Release();
            enumerator_ = nullptr;
        }
        id_.clear();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(iid, __uuidof(IUnknown)) ||
            IsEqualIID(iid, __uuidof(IMMNotificationClient))) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        return references_.fetch_sub(1, std::memory_order_relaxed) - 1;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override {
        signal(id, (state & DEVICE_STATE_ACTIVE) == 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
        signal(id, true);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        return S_OK; // A new default must not replace this session's pinned endpoint.
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        return S_OK;
    }

private:
    void signal(LPCWSTR id, bool unavailable) noexcept {
        callbacks_.fetch_add(1, std::memory_order_acq_rel);
        if (unavailable && id && std::wcscmp(id, id_.c_str()) == 0) {
            lost_.store(true, std::memory_order_release);
        }
        callbacks_.fetch_sub(1, std::memory_order_release);
    }

    std::atomic<ULONG> references_{1};
    std::atomic<std::uint32_t> callbacks_{0};
    std::atomic<bool> lost_{false};
    std::wstring id_;
    IMMDeviceEnumerator* enumerator_{nullptr};
    IMMDevice* endpoint_{nullptr};
    bool registered_{false};
    Clock::time_point next_poll_{};
};
#endif

} // namespace

struct MiniaudioPlayer::Impl {
    enum class CommandType { play, stop, pause, volume, devices };
    enum class Phase { idle, track, gap, draining };

    struct Command {
        CommandType type{CommandType::stop};
        std::uint32_t generation{0};
        std::uint64_t session{0};
        PlaybackPlan plan;
        std::string output_device;
        float volume{1.0F};
        std::shared_ptr<std::promise<std::vector<OutputDevice>>> reply;
    };

    struct DeviceEntry {
        ma_device_id native_id{};
        OutputDevice output;
        bool is_default{false};
    };

    explicit Impl(bool test_backend) : test_backend_(test_backend) {
        worker_ = std::thread([this] { run(); });
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quitting_ = true;
            generation_.fetch_add(1, std::memory_order_release);
        }
        wake_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void replace(Command command) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            command.generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (command.type == CommandType::play) {
                requested_session_ = command.session;
            } else {
                command.session = requested_session_;
            }
            // Preserve synchronous enumeration replies, but cancel all old transport.
            commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
                [](const Command& queued) { return queued.type != CommandType::devices; }),
                commands_.end());
            events_.clear();
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
    }

    void enqueue(Command command) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            command.generation = generation_.load(std::memory_order_relaxed);
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
    }

    std::vector<AudioEvent> poll_events() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AudioEvent> events;
        events.swap(events_);
        return events;
    }

    bool current() const noexcept {
        return active_generation_ == generation_.load(std::memory_order_acquire);
    }

    void post(PlaybackStatus status, bool between = false, std::string message = {}, bool progress_only = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!quitting_ && current()) {
            AudioEvent event{session_, status, track_index_, between, std::move(message),
                             between ? std::optional<std::size_t>{next_available(track_index_ + 1)}
                                     : std::nullopt};
            if (status == PlaybackStatus::playing || status == PlaybackStatus::paused) {
                const auto frames = consumed_frames_.load(std::memory_order_relaxed);
                event.position_ms = frames * 1000 / sample_rate;
                if (duration_frames_) {
                    event.duration_ms = *duration_frames_ * 1000 / sample_rate;
                    event.position_ms = std::min(event.position_ms, *event.duration_ms);
                }
            }
            event.progress_only = progress_only;
            // A slow/minimized UI needs only the latest progress sample, not a backlog.
            if (progress_only && !events_.empty() && events_.back().progress_only &&
                events_.back().session == session_ && events_.back().track_index == track_index_) {
                events_.back() = std::move(event);
            } else events_.push_back(std::move(event));
        }
    }

    void publish_progress() {
        if (phase_ != Phase::track || render_paused_.load(std::memory_order_acquire)) return;
        const auto now = Clock::now();
        if (now < next_progress_) return;
        next_progress_ = now + std::chrono::milliseconds{100};
        post(PlaybackStatus::playing, false, {}, true);
    }

    std::string contextual_message(const std::string& detail) const {
        std::string text = "Punto [" + plan_.point_id + "] " + plan_.point_name;
        if (track_index_ < plan_.tracks.size()) {
            const auto& track = plan_.tracks[track_index_];
            text += "; lengua=" + std::string{language_id(track.language)};
            text += "; ruta=" + track.path.u8string();
        } else {
            text += "; lengua=<sin pista>; ruta=<sin pista>";
        }
        return text + "; " + detail;
    }

    void ensure_context() {
        if (context_initialized_) {
            return;
        }
        if (!test_backend_) {
#ifdef _WIN32
            if (FAILED(com_result_)) {
                throw AppError(hresult_message("CoInitializeEx(worker)", com_result_));
            }
#else
            throw AppError("WASAPI solo esta disponible en Windows; no hay backend alternativo");
#endif
        }
        const ma_backend backend = test_backend_ ? ma_backend_null : ma_backend_wasapi;
        const auto config = ma_context_config_init();
        require_success("ma_context_init", ma_context_init(&backend, 1, &config, &context_));
        context_initialized_ = true;
    }

    std::vector<DeviceEntry> enumerate() {
        ensure_context();
        ma_device_info* playback = nullptr;
        ma_uint32 count = 0;
        require_success("ma_context_get_devices",
                        ma_context_get_devices(&context_, &playback, &count, nullptr, nullptr));
        if (count == 0) {
            throw AppError("No hay dispositivos de salida disponibles");
        }
        std::vector<DeviceEntry> entries;
        entries.reserve(count);
        for (ma_uint32 i = 0; i < count; ++i) {
            std::string id;
            if (test_backend_) {
                id = null_device_id;
            } else {
#ifdef _WIN32
                id = endpoint_utf8(playback[i].id.wasapi);
#endif
            }
            if (id.empty()) {
                throw AppError("La enumeracion devolvio un endpoint sin ID");
            }
            entries.push_back({playback[i].id, {std::move(id), playback[i].name},
                               playback[i].isDefault != MA_FALSE});
        }
        return entries;
    }

    std::size_t next_available(std::size_t start) const {
        while (start < available_.size() && !available_[start]) ++start;
        return start;
    }

    bool preflight() {
        available_.assign(plan_.tracks.size(), false);
        for (track_index_ = 0; track_index_ < plan_.tracks.size(); ++track_index_) {
            if (!current()) {
                return false;
            }
            const auto& path = plan_.tracks[track_index_].path;
            std::error_code error;
            const auto status = std::filesystem::status(path, error);
            if (error == std::errc::no_such_file_or_directory ||
                (!error && !std::filesystem::exists(status))) {
                continue;
            }
            if (error || !std::filesystem::is_regular_file(status)) {
                throw AppError("El archivo no existe o no es un archivo regular" +
                               (error ? ": " + error.message() : std::string{}));
            }
            Decoder probe;
            probe.open(path);
            if (!current()) {
                return false;
            }
            float frame[channels]{};
            ma_uint64 read = 0;
            const auto result = ma_decoder_read_pcm_frames(&probe.value, frame, 1, &read);
            if (result != MA_SUCCESS && result != MA_AT_END) {
                throw AppError(result_message("ma_decoder_read_pcm_frames(preflight)", result));
            }
            if (read != 1) {
                throw AppError(result_message("ma_decoder_read_pcm_frames(preflight)", result) +
                               "; no se pudo leer el primer marco PCM");
            }
            require_success("ma_decoder_seek_to_pcm_frame(preflight)",
                            ma_decoder_seek_to_pcm_frame(&probe.value, 0));
            require_success("ma_decoder_uninit(preflight)", probe.close());
            available_[track_index_] = true;
        }
        track_index_ = next_available(0);
        if (track_index_ == plan_.tracks.size()) {
            track_index_ = 0; // Retain point/language/path context in the error.
            throw AppError("No hay audios disponibles para los idiomas seleccionados");
        }
        return current();
    }

    void fill_buffer() {
        if (source_eof_.load(std::memory_order_acquire)) {
            return;
        }
        ma_uint32 count = std::min(decode_frames, ma_pcm_rb_available_write(&ring_));
        if (count == 0) {
            return;
        }
        void* pcm = nullptr;
        require_success("ma_pcm_rb_acquire_write", ma_pcm_rb_acquire_write(&ring_, &count, &pcm));
        if (count == 0) {
            return;
        }
        ma_uint64 read = 0;
        const auto result = ma_decoder_read_pcm_frames(&decoder_.value, pcm, count, &read);
        if (result != MA_SUCCESS && result != MA_AT_END) {
            throw AppError(result_message("ma_decoder_read_pcm_frames", result));
        }
        require_success("ma_pcm_rb_commit_write",
                        ma_pcm_rb_commit_write(&ring_, static_cast<ma_uint32>(read)));
        if (result == MA_AT_END) {
            source_eof_.store(true, std::memory_order_release);
        } else if (read == 0) {
            throw AppError(result_message("ma_decoder_read_pcm_frames", result) +
                           "; lectura sin progreso, no se supone EOF");
        }
    }

    bool prepare_track() {
        // The callback's done latch is closed, and the SPSC ring is empty here.
        // Never reset/uninitialize a ring that a live callback could access.
        source_eof_.store(false, std::memory_order_release);
        consumed_frames_.store(0, std::memory_order_relaxed);
        duration_frames_.reset();
        next_progress_ = Clock::now() + std::chrono::milliseconds{100};
        decoder_.open(plan_.tracks[track_index_].path);
        ma_uint64 length = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder_.value, &length) == MA_SUCCESS && length > 0) {
            duration_frames_ = length;
        }
        while (current() && !source_eof_.load(std::memory_order_acquire) &&
               ma_pcm_rb_available_write(&ring_) != 0) {
            fill_buffer();
        }
        if (!current()) {
            return false;
        }
        if (ma_pcm_rb_available_read(&ring_) == 0) {
            throw AppError(result_message("ma_decoder_read_pcm_frames(inicio)", MA_AT_END));
        }
        check_endpoint();
        return true;
    }

    void start_play(Command& command) {
        // Cancellation and device teardown precede all validation of the new plan.
        const auto stop_result = release_audio();
        active_generation_ = command.generation;
        session_ = command.session;
        plan_ = std::move(command.plan);
        track_index_ = 0;
        volume_ = command.volume;
        post(PlaybackStatus::preparing);
        require_success("ma_device_stop/ma_decoder_uninit(reemplazo)", stop_result);
        if (!valid_volume(volume_)) {
            throw AppError("El volumen debe ser finito y estar entre 0 y 1");
        }
        if (plan_.tracks.empty()) {
            throw AppError("El plan no contiene pistas");
        }
        if (plan_.gap_ms < 0) {
            throw AppError("El intervalo entre idiomas no puede ser negativo");
        }
        if (!preflight()) {
            return;
        }
        const auto entries = enumerate();
        const auto selected = std::find_if(entries.begin(), entries.end(),
            [&command](const DeviceEntry& entry) {
                return command.output_device.empty() ? entry.is_default
                                                      : entry.output.id == command.output_device;
            });
        if (selected == entries.end()) {
            throw AppError(command.output_device.empty()
                ? "No se pudo resolver el dispositivo predeterminado de la enumeracion"
                : "El dispositivo solicitado no esta disponible: " + command.output_device);
        }
        selected_id_ = selected->native_id;
        if (!current()) {
            return;
        }
#ifdef _WIN32
        if (!test_backend_) {
            monitor_.open(selected_id_.wasapi);
            monitor_open_ = true;
        }
#endif
        require_success("ma_pcm_rb_init",
                        ma_pcm_rb_init(ma_format_f32, channels, buffer_frames, nullptr, nullptr, &ring_));
        ring_initialized_ = true;
        track_done_.store(true, std::memory_order_release);
        if (!prepare_track()) {
            return;
        }

        render_generation_ = active_generation_;
        callback_error_.store(MA_SUCCESS, std::memory_order_relaxed);
        unexpected_stop_.store(false, std::memory_order_relaxed);
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = &selected_id_; // Never NULL, including a resolved default.
        config.playback.format = ma_format_f32;
        config.playback.channels = channels;
        config.sampleRate = sample_rate;
        config.periodSizeInMilliseconds = 10;
        config.periods = 3;
        config.noFixedSizedCallback = MA_TRUE;
        config.wasapi.noAutoStreamRouting = MA_TRUE;
        config.dataCallback = data_callback;
        config.notificationCallback = notification_callback;
        config.pUserData = this;
        require_success("ma_device_init", ma_device_init(&context_, &config, &device_));
        device_initialized_ = true;
        require_success("ma_device_set_master_volume", ma_device_set_master_volume(&device_, volume_));
        if (!current()) {
            return;
        }
        check_endpoint();
        render_paused_.store(false, std::memory_order_release);
        track_done_.store(false, std::memory_order_release);
        expected_stop_.store(false, std::memory_order_release);
        require_success("ma_device_start", ma_device_start(&device_));
        device_started_ = true;
        phase_ = Phase::track;
        post(PlaybackStatus::playing);
    }

    void check_endpoint() {
#ifdef _WIN32
        if (monitor_open_) {
            monitor_.check();
        }
#endif
    }

    Clock::duration tail_duration() const {
        const auto& playback = device_.playback;
        const auto rate = playback.internalSampleRate != 0 ? playback.internalSampleRate : sample_rate;
        ma_uint64 frames = static_cast<ma_uint64>(playback.internalPeriodSizeInFrames) *
                           (static_cast<ma_uint64>(playback.internalPeriods) + 2);
#if defined(_WIN32) && defined(MA_SUPPORT_WASAPI)
        if (!test_backend_) {
            frames = std::max(frames, static_cast<ma_uint64>(device_.wasapi.actualBufferSizeInFramesPlayback) +
                                     2 * static_cast<ma_uint64>(playback.internalPeriodSizeInFrames));
        }
#endif
        return std::max(std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds{50}),
                        std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>{static_cast<double>(frames) / rate}));
    }

    void tick() {
        // Also checked while paused, between languages, and while draining the tail.
        check_endpoint();
        const auto callback_result = callback_error_.load(std::memory_order_acquire);
        require_success("callback PCM", callback_result);
        if (unexpected_stop_.load(std::memory_order_acquire) ||
            (device_started_ && ma_device_get_state(&device_) != ma_device_state_started)) {
            throw AppError("El dispositivo se detuvo o fue redirigido sin solicitud (sin ma_result)");
        }
        if (render_paused_.load(std::memory_order_acquire)) {
            return;
        }
        if (phase_ == Phase::track) {
            fill_buffer();
            if (!track_done_.load(std::memory_order_acquire)) {
                return;
            }
            require_success("ma_decoder_uninit", decoder_.close());
            // EOF establishes the actual duration, including codecs without length metadata.
            duration_frames_ = consumed_frames_.load(std::memory_order_relaxed);
            if (next_available(track_index_ + 1) < plan_.tracks.size()) {
                phase_ = Phase::gap;
                deadline_ = Clock::now() + std::chrono::milliseconds{plan_.gap_ms};
                post(PlaybackStatus::playing, true);
            } else {
                // PCM consumption is not hardware presentation. Leave the stream
                // running silence long enough to avoid cutting off its final buffer.
                phase_ = Phase::draining;
                deadline_ = Clock::now() + tail_duration();
                post(PlaybackStatus::playing, false, {}, true);
            }
        } else if (phase_ == Phase::gap && Clock::now() >= deadline_) {
            track_index_ = next_available(track_index_ + 1);
            phase_ = Phase::track;
            if (!prepare_track() || !current()) {
                return;
            }
            track_done_.store(false, std::memory_order_release);
            post(PlaybackStatus::playing);
        } else if (phase_ == Phase::draining && Clock::now() >= deadline_) {
            require_success("ma_device_stop(fin)", release_audio());
            post(PlaybackStatus::stopped);
        }
    }

    void toggle_pause() {
        if (phase_ == Phase::idle) {
            return;
        }
        const bool paused = !render_paused_.load(std::memory_order_acquire);
        const auto now = Clock::now();
        if (phase_ == Phase::gap || phase_ == Phase::draining) {
            if (paused) {
                remaining_ = std::max(Clock::duration::zero(), deadline_ - now);
            } else {
                deadline_ = now + remaining_;
            }
        }
        // Keep the same device alive with silence: no PCM is consumed on pause.
        render_paused_.store(paused, std::memory_order_release);
        post(paused ? PlaybackStatus::paused : PlaybackStatus::playing, phase_ == Phase::gap);
    }

    ma_result release_audio() noexcept {
        render_paused_.store(true, std::memory_order_release);
        expected_stop_.store(true, std::memory_order_release);
        ma_result result = MA_SUCCESS;
        if (device_initialized_) {
            result = ma_device_stop(&device_);
            ma_device_uninit(&device_); // Joins miniaudio callbacks before freeing their state.
            device_initialized_ = false;
            device_started_ = false;
        }
        const auto decoder_result = decoder_.close();
        if (result == MA_SUCCESS) {
            result = decoder_result;
        }
        if (ring_initialized_) {
            ma_pcm_rb_uninit(&ring_);
            ring_initialized_ = false;
        }
#ifdef _WIN32
        monitor_.close();
        monitor_open_ = false;
#endif
        phase_ = Phase::idle;
        return result;
    }

    void fail(const std::string& detail) {
        const bool between = phase_ == Phase::gap;
        auto message = contextual_message(detail);
        const auto result = release_audio();
        if (result != MA_SUCCESS) {
            message += "; " + result_message("limpieza", result);
        }
        post(PlaybackStatus::error, between, std::move(message));
    }

    void handle(Command& command) {
        if (command.type == CommandType::devices) {
            try {
                std::vector<OutputDevice> outputs;
                for (const auto& entry : enumerate()) {
                    outputs.push_back(entry.output);
                }
                command.reply->set_value(std::move(outputs));
            } catch (const std::exception& error) {
                command.reply->set_exception(std::make_exception_ptr(AppError(error.what())));
            } catch (...) {
                command.reply->set_exception(std::make_exception_ptr(AppError("Fallo al enumerar salidas")));
            }
            return;
        }
        if (command.generation != generation_.load(std::memory_order_acquire)) {
            return;
        }
        try {
            switch (command.type) {
            case CommandType::play:
                start_play(command);
                break;
            case CommandType::stop: {
                if (session_ != command.session) {
                    track_index_ = 0;
                    plan_ = PlaybackPlan{};
                }
                session_ = command.session;
                active_generation_ = command.generation;
                require_success("ma_device_stop", release_audio());
                post(PlaybackStatus::stopped);
                break;
            }
            case CommandType::pause:
                toggle_pause();
                break;
            case CommandType::volume:
                volume_ = command.volume;
                if (device_initialized_) {
                    require_success("ma_device_set_master_volume",
                                    ma_device_set_master_volume(&device_, volume_));
                }
                break;
            case CommandType::devices:
                break;
            }
        } catch (const std::exception& error) {
            fail(error.what());
        } catch (...) {
            fail("Fallo de audio desconocido");
        }
    }

    void run() {
#ifdef _WIN32
        WorkerCom com(test_backend_);
        com_result_ = com.result;
#endif
        for (;;) {
            Command command;
            bool have_command = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                const auto ready = [this] { return quitting_ || !commands_.empty(); };
                if (phase_ == Phase::idle) {
                    wake_.wait(lock, ready);
                } else {
                    wake_.wait_for(lock, worker_period, ready);
                }
                if (quitting_) {
                    for (auto& pending : commands_) {
                        if (pending.reply) {
                            pending.reply->set_exception(std::make_exception_ptr(AppError("Audio cerrado")));
                        }
                    }
                    commands_.clear();
                    break;
                }
                if (!commands_.empty()) {
                    command = std::move(commands_.front());
                    commands_.pop_front();
                    have_command = true;
                }
            }
            if (have_command) {
                handle(command);
            }
            if (phase_ != Phase::idle && current()) {
                try {
                    tick();
                    publish_progress();
                } catch (const std::exception& error) {
                    fail(error.what());
                } catch (...) {
                    fail("Fallo de audio desconocido");
                }
            }
        }
        release_audio();
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
        // WorkerCom outlives all devices, subscriptions and the miniaudio context.
    }

    static void record_callback_error(Impl& self, ma_result result) noexcept {
        ma_result expected = MA_SUCCESS;
        self.callback_error_.compare_exchange_strong(expected, result, std::memory_order_release,
                                                    std::memory_order_relaxed);
    }

    static void data_callback(ma_device* device, void* output, const void*, ma_uint32 frames) noexcept {
        auto& self = *static_cast<Impl*>(device->pUserData);
        auto* destination = static_cast<float*>(output);
        std::memset(output, 0, static_cast<std::size_t>(frames) * channels * sizeof(float));
        if (self.render_generation_ != self.generation_.load(std::memory_order_acquire) ||
            self.render_paused_.load(std::memory_order_acquire) ||
            self.track_done_.load(std::memory_order_acquire)) {
            return;
        }
        ma_uint32 copied = 0;
        while (copied < frames) {
            ma_uint32 count = frames - copied;
            void* pcm = nullptr;
            auto result = ma_pcm_rb_acquire_read(&self.ring_, &count, &pcm);
            if (result != MA_SUCCESS) {
                record_callback_error(self, result);
                return;
            }
            if (count == 0) {
                break; // Underrun is silence, never evidence of EOF.
            }
            std::memcpy(destination + static_cast<std::size_t>(copied) * channels, pcm,
                        static_cast<std::size_t>(count) * channels * sizeof(float));
            result = ma_pcm_rb_commit_read(&self.ring_, count);
            if (result != MA_SUCCESS) {
                record_callback_error(self, result);
                return;
            }
            copied += count;
        }
        self.consumed_frames_.fetch_add(copied, std::memory_order_relaxed);
        if (self.source_eof_.load(std::memory_order_acquire) &&
            ma_pcm_rb_available_read(&self.ring_) == 0) {
            // This MUST be the callback's final state access. Until the worker
            // publishes a fully prepared next track, later callbacks only silence.
            self.track_done_.store(true, std::memory_order_release);
        }
    }

    static void notification_callback(const ma_device_notification* notification) noexcept {
        auto& self = *static_cast<Impl*>(notification->pDevice->pUserData);
        if ((notification->type == ma_device_notification_type_stopped ||
             notification->type == ma_device_notification_type_rerouted ||
             notification->type == ma_device_notification_type_interruption_began) &&
            !self.expected_stop_.load(std::memory_order_acquire)) {
            self.unexpected_stop_.store(true, std::memory_order_release);
        }
    }

    const bool test_backend_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::vector<AudioEvent> events_;
    bool quitting_{false};
    std::uint64_t requested_session_{0};
    std::atomic<std::uint32_t> generation_{0};
    std::thread worker_;

    // Only the worker owns these fixed-address objects and their init/uninit.
    ma_context context_{};
    ma_device device_{};
    ma_pcm_rb ring_{};
    Decoder decoder_;
    ma_device_id selected_id_{};
    bool context_initialized_{false};
    bool device_initialized_{false};
    bool device_started_{false};
    bool ring_initialized_{false};
    std::uint32_t active_generation_{0};
    std::uint32_t render_generation_{0};
    std::uint64_t session_{0};
    PlaybackPlan plan_;
    std::vector<bool> available_;
    std::size_t track_index_{0};
    Phase phase_{Phase::idle};
    float volume_{1.0F};
    Clock::time_point deadline_{};
    Clock::duration remaining_{};
    std::atomic<bool> render_paused_{true};
    std::atomic<bool> source_eof_{false};
    std::atomic<bool> track_done_{true};
    std::atomic<std::uint64_t> consumed_frames_{0};
    std::optional<std::uint64_t> duration_frames_;
    Clock::time_point next_progress_{};
    std::atomic<bool> expected_stop_{true};
    std::atomic<bool> unexpected_stop_{false};
    std::atomic<ma_result> callback_error_{MA_SUCCESS};
#ifdef _WIN32
    HRESULT com_result_{S_OK};
    EndpointMonitor monitor_;
    bool monitor_open_{false};
#endif
};

MiniaudioPlayer::MiniaudioPlayer(bool test_null_backend)
    : impl_(std::make_unique<Impl>(test_null_backend)) {}

MiniaudioPlayer::~MiniaudioPlayer() = default;

void MiniaudioPlayer::play(std::uint64_t session, PlaybackPlan plan,
                           std::string output_device, float volume) {
    Impl::Command command;
    command.type = Impl::CommandType::play;
    command.session = session;
    command.plan = std::move(plan);
    command.output_device = std::move(output_device);
    command.volume = volume;
    impl_->replace(std::move(command));
}

void MiniaudioPlayer::stop() {
    Impl::Command command;
    command.type = Impl::CommandType::stop;
    impl_->replace(std::move(command));
}

void MiniaudioPlayer::toggle_pause() {
    Impl::Command command;
    command.type = Impl::CommandType::pause;
    impl_->enqueue(std::move(command));
}

void MiniaudioPlayer::set_volume(float volume) {
    if (!valid_volume(volume)) {
        throw AppError("El volumen debe ser finito y estar entre 0 y 1");
    }
    Impl::Command command;
    command.type = Impl::CommandType::volume;
    command.volume = volume;
    impl_->enqueue(std::move(command));
}

std::vector<OutputDevice> MiniaudioPlayer::devices() {
    auto reply = std::make_shared<std::promise<std::vector<OutputDevice>>>();
    auto future = reply->get_future();
    Impl::Command command;
    command.type = Impl::CommandType::devices;
    command.reply = std::move(reply);
    impl_->enqueue(std::move(command));
    return future.get();
}

std::vector<AudioEvent> MiniaudioPlayer::poll_events() {
    return impl_->poll_events();
}

} // namespace audioguia
