#include "app/AppController.h"
#include "audio/MiniaudioPlayer.h"
#include "input/KeyboardController.h"
#include "ui/MainFrame.h"
#include "ui/CompactFrame.h"
#include <wx/wx.h>
#include <wx/apptrait.h>
#include <wx/evtloop.h>
#include <wx/stdpaths.h>
#include <wx/weakref.h>
#include <functional>
#include <memory>

namespace audioguia {

// This intercepts only this process's event loops, including local modal loops.
// It is not a Windows keyboard hook, Raw Input registration or global hotkey.
class InputEventLoop final : public wxGUIEventLoop {
public:
    explicit InputEventLoop(std::function<bool(void*)> handle) : handle_(std::move(handle)) {}
    bool PreProcessMessage(WXMSG* message) override {
        return handle_(message) || wxGUIEventLoop::PreProcessMessage(message);
    }
private:
    std::function<bool(void*)> handle_;
};

class AppTraits final : public wxGUIAppTraits {
public:
    explicit AppTraits(std::function<bool(void*)> handle) : handle_(std::move(handle)) {}
    wxEventLoopBase* CreateEventLoop() override { return new InputEventLoop(handle_); }
private:
    std::function<bool(void*)> handle_;
};

class AudioguiaApp final : public wxApp {
public:
    wxAppTraits* CreateTraits() override {
        return new AppTraits([this](void* message) {
            return keyboard_ && keyboard_->handle_native_message(message);
        });
    }

    bool OnInit() override {
        SetAppName("TolkeyenAudioguia");
        try {
            const auto executable = std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring());
            controller_ = std::make_unique<AppController>(executable.parent_path().parent_path(),
                                                         std::make_unique<MiniaudioPlayer>());
            main_ = new MainFrame(*controller_, [this] { show_compact(); });
            compact_ = new CompactFrame(*controller_, [this] { show_full(); });
            keyboard_ = std::make_unique<KeyboardController>(*controller_, *main_, *compact_);
            SetTopWindow(main_);
            main_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
                timer_.Stop();
                keyboard_.reset();
                controller_->stop();
                if (compact_) {
                    compact_->Destroy();
                    compact_ = nullptr;
                }
                if (main_) {
                    main_->Destroy();
                    main_ = nullptr;
                }
            });
            timer_.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                controller_->poll();
                if (main_) main_->refresh_state();
                if (compact_) compact_->refresh_state();
            }, timer_.GetId());
            main_->Show();
            main_->focus_transport();
            timer_.Start(50);
            return true;
        } catch (const std::exception& error) {
            wxMessageBox(wxString::FromUTF8(error.what()), "No se pudo iniciar Tolkeyen Audioguia", wxOK | wxICON_ERROR);
            return false;
        }
    }

    int OnExit() override {
        timer_.Stop();
        keyboard_.reset();
        controller_.reset();
        return wxApp::OnExit();
    }

private:
    void show_compact() {
        compact_->refresh_state();
        compact_->Show();
        main_->Hide();
        compact_->Raise(); // Only an explicit operator action changes activation.
    }
    void show_full() {
        main_->refresh_state();
        main_->Show();
        compact_->Hide();
        main_->Raise();
        main_->focus_transport();
    }

    std::unique_ptr<AppController> controller_;
    std::unique_ptr<KeyboardController> keyboard_;
    wxWeakRef<MainFrame> main_;
    wxWeakRef<CompactFrame> compact_;
    wxTimer timer_;
};

} // namespace audioguia

wxIMPLEMENT_APP(audioguia::AudioguiaApp);
