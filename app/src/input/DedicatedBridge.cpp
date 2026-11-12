#include "input/DedicatedBridge.h"
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <shlobj.h>
#include <stdexcept>
#include <vector>

namespace audioguia {
namespace {
constexpr auto session_property = L"TolkeyenAudioguia.BridgeSession.v1";
constexpr auto state_property = L"TolkeyenAudioguia.BridgeState.v1";
constexpr auto pulse_property = L"TolkeyenAudioguia.BridgePulse.v1";
}

DedicatedBridge::DedicatedBridge(HWND window) : window_(window) {
    wchar_t local[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local)))
        throw std::runtime_error("No se pudo localizar la configuracion local de la botonera.");
    settings_ = std::filesystem::path(local) / L"TolkeyenAudioguia" / L"dedicated.ini";
    // Isolated integration tests can redirect machine-local settings.
    wchar_t override_path[32768]{};
    const auto length = GetEnvironmentVariableW(L"AUDIOGUIA_BRIDGE_SETTINGS", override_path, 32768);
    if (length > 0 && length < 32768) settings_ = std::filesystem::path(override_path);
    std::filesystem::create_directories(settings_.parent_path());
    enabled_ = GetPrivateProfileIntW(L"Settings", L"Enabled", 0, settings_.c_str()) != 0;
}

DedicatedBridge::~DedicatedBridge() { stop(); }

void DedicatedBridge::enable(bool value) {
    if (!WritePrivateProfileStringW(L"Settings", L"Enabled", value ? L"1" : L"0", settings_.c_str()))
        throw std::runtime_error("No se pudo guardar la preferencia de botonera dedicada.");
    enabled_ = value;
    error_.clear();
    if (!value) stop();
    else if (!process_) start();
}

void DedicatedBridge::start() {
    if (process_ || !enabled_) return;
    error_.clear();
    const auto app = std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();
    const auto tools = app / L"bridge";
    BOOL wow = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow);
    const auto ahk = tools / L"AutoHotkey" / (wow ? L"AutoHotkey64.exe" : L"AutoHotkey32.exe");
    const auto script = app / L"scripts" / L"interception-managed.ahk";
    if (!std::filesystem::exists(ahk) || !std::filesystem::exists(script) ||
        !std::filesystem::exists(tools / L"Lib" / L"AutoHotInterception.dll")) {
        error_ = "Faltan componentes del puente. Revise el paquete de distribucion.";
        return;
    }
    session_ = (static_cast<ULONG_PTR>(GetTickCount()) ^ GetCurrentProcessId()) & 0x7fffffff;
    if (session_ < 2) session_ = 2;
    SetPropW(window_, session_property, reinterpret_cast<HANDLE>(session_));
    RemovePropW(window_, state_property);
    RemovePropW(window_, pulse_property);
    auto command = L"\"" + ahk.wstring() + L"\" /ErrorStdOut \"" + script.wstring() + L"\" " +
        std::to_wstring(reinterpret_cast<ULONG_PTR>(window_)) + L" " + std::to_wstring(session_) +
        L" \"" + settings_.wstring() + L"\" \"" + tools.wstring() + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION info{};
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        error_ = "No se pudo preparar el proceso del puente.";
        stop();
        return;
    }
    if (!CreateProcessW(ahk.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, app.c_str(), &startup, &info)) {
        error_ = "Windows no pudo iniciar el puente (codigo " + std::to_string(GetLastError()) + ").";
        stop();
        return;
    }
    if (!AssignProcessToJobObject(job_, info.hProcess)) {
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        error_ = "No se pudo supervisar el puente.";
        stop();
        return;
    }
    process_ = info.hProcess;
    started_ = GetTickCount();
    state_ = 0;
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
}

void DedicatedBridge::stop() {
    RemovePropW(window_, session_property); // Cooperative shutdown within 500 ms.
    if (process_) {
        if (WaitForSingleObject(process_, 1500) == WAIT_TIMEOUT && job_) {
            TerminateJobObject(job_, 1);
            WaitForSingleObject(process_, 1500);
        }
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (job_) { CloseHandle(job_); job_ = nullptr; }
    RemovePropW(window_, state_property);
    RemovePropW(window_, pulse_property);
    RemovePropW(window_, L"TolkeyenAudioguia.BridgeConfigure.v1");
    state_ = 0;
    session_ = 0;
}

void DedicatedBridge::tick() {
    if (!enabled_) return;
    if (!process_) {
        if (error_.empty()) {
            try { start(); }
            catch (const std::exception& error) { error_ = error.what(); stop(); }
        }
        return;
    }
    if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) {
        DWORD code = 0;
        GetExitCodeProcess(process_, &code);
        error_ = code == 6 ? "OTRO PUENTE ACTIVO | Cierre la prueba anterior y reactive la casilla."
            : "Puente detenido (codigo " + std::to_string(code) + "). Revise Diagnostico.";
        stop();
        return;
    }
    state_ = static_cast<int>(reinterpret_cast<ULONG_PTR>(GetPropW(window_, state_property)));
    const auto pulse = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(GetPropW(window_, pulse_property)));
    const auto age = static_cast<DWORD>(GetTickCount() - (pulse ? pulse : started_));
    if (age > (pulse ? 5000u : 15000u)) {
        error_ = "El puente no responde. Revise el controlador y Diagnostico.";
        stop();
    }
}

bool DedicatedBridge::accepts(ULONG_PTR session) const {
    return enabled_ && process_ && session_ == session && state_ == 1 &&
        WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

void DedicatedBridge::configure() {
    if (!enabled_) enable(true);
    if (!process_) { error_.clear(); start(); }
    if (process_) SetPropW(window_, L"TolkeyenAudioguia.BridgeConfigure.v1", reinterpret_cast<HANDLE>(1));
}

void DedicatedBridge::diagnostics() {
    const auto log = settings_.parent_path() / L"bridge.log";
    if (std::filesystem::exists(log)) wxLaunchDefaultApplication(log.wstring());
    else wxLaunchDefaultApplication(settings_.parent_path().wstring());
}

std::string DedicatedBridge::status() const {
    if (!error_.empty()) return error_;
    switch (state_) {
    case 1: return "NUMPAD ACTIVO | Control en segundo plano";
    case 2: return "ESPERANDO AL NUMPAD | Reconexion automatica";
    case 3: return "DISPOSITIVO AMBIGUO | Configure el numpad";
    case 4: return "ERROR DEL PUENTE | Abra Diagnostico";
    case 5: return "SELECCIONE EL NUMPAD | Pulse Configurar botonera";
    case 6: return "OTRO PUENTE ACTIVO | Cierre la prueba anterior";
    default: return "CONECTANDO BOTONERA...";
    }
}
} // namespace audioguia
