#include "app/AppController.h"
#include "audio/MiniaudioPlayer.h"
#include "input/KeyboardController.h"
#include "ui/MainFrame.h"
#include <wx/wx.h>
#include <wx/apptrait.h>
#include <wx/evtloop.h>
#include <wx/stdpaths.h>
#include <wx/weakref.h>
#include <wx/iconbndl.h>
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
            main_ = new MainFrame(*controller_);
#ifdef _WIN32
            const wxIconBundle icons("APP_ICON", static_cast<WXHINSTANCE>(nullptr));
            main_->SetIcons(icons);
#endif
            keyboard_ = std::make_unique<KeyboardController>(*controller_, *main_);
            SetTopWindow(main_);
            main_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
                timer_.Stop();
                keyboard_.reset();
                controller_->stop();
                if (main_) {
                    main_->Destroy();
                    main_ = nullptr;
                }
            });
            timer_.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                controller_->poll();
                if (main_) main_->refresh_state();
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
    std::unique_ptr<AppController> controller_;
    std::unique_ptr<KeyboardController> keyboard_;
    wxWeakRef<MainFrame> main_;
    wxTimer timer_;
};

} // namespace audioguia

wxIMPLEMENT_APP(audioguia::AudioguiaApp);
