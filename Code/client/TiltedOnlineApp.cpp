#include <TiltedOnlinePCH.h>

#include <TiltedOnlineApp.h>

#include <DInputHook.hpp>
#include <dinput.h>
#include <WindowsHook.hpp>

#include <World.h>
#include <PlayerCharacter.h>

#include <fstream>
#include <cstdlib>
#include <exception>
#include <cstdio>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Systems/RenderSystemD3D11.h>

#include <Services/OverlayService.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>

#include <ScriptExtender.h>
#include <NvidiaUtil.h>

using TiltedPhoques::Debug;

// launcher::Trace lives in immersive_launcher/Launcher.cpp (whole-archive-linked).
namespace launcher { void Trace(const char*); }

namespace
{
struct AutoCfg
{
    std::string address = "127.0.0.1";
    uint16_t port = 10578;
    std::string password;
};

// Reads client.cfg (key=value lines) next to logs/tp_client.log; missing file/keys keep defaults.
AutoCfg ReadAutoCfg()
{
    AutoCfg cfg;
    std::ifstream file(TiltedPhoques::GetPath() / "client.cfg");
    std::string line;
    while (std::getline(file, line))
    {
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
            value.pop_back();
        if (key == "address")
            cfg.address = value;
        else if (key == "port")
            cfg.port = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, 10));
        else if (key == "password")
            cfg.password = value;
    }
    return cfg;
}
} // namespace

TiltedOnlineApp::TiltedOnlineApp()
{
    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    auto logPath = TiltedPhoques::GetPath() / "logs";

    std::error_code ec;
    create_directory(logPath, ec);

    auto rotatingLogger = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath / "tp_client.log", 1048576 * 5, 3);
    // rotatingLogger->set_level(spdlog::level::debug);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console, rotatingLogger});
    logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] %$ %v");
    spdlog::flush_every(std::chrono::seconds(1));
    set_default_logger(logger);
}

TiltedOnlineApp::~TiltedOnlineApp() = default;

void* TiltedOnlineApp::GetMainAddress() const
{
    POINTER_SKYRIMSE(void, winMain, 36544);

    return winMain.GetPtr();
}

bool TiltedOnlineApp::BeginMain()
{
    try
    {
        launcher::Trace("B1:pre-world");
        World::Create();
        launcher::Trace("B2:pre-discord");
        World::Get().ctx().at<DiscordService>().Init();
        launcher::Trace("B3:pre-render");
        World::Get().ctx().emplace<RenderSystemD3D11>(World::Get().ctx().at<OverlayService>(), World::Get().ctx().at<ImguiService>());

        launcher::Trace("B4:pre-script");
        LoadScriptExender();
        launcher::Trace("B5:post-script");

        // TODO: Figure out a way to un-blacklist NvCamera64.dll (see DllBlocklist.cpp). Then this hack can be removed
        if (IsNvidiaOverlayLoaded())
            ApplyNvidiaFix();

        launcher::Trace("B6:beginmain-ret");
    }
    catch (const std::exception& e)
    {
        char buf[256];
        sprintf_s(buf, "BEGINMAIN-THROW std::exception: %s", e.what());
        launcher::Trace(buf);
    }
    catch (...)
    {
        launcher::Trace("BEGINMAIN-THROW unknown");
    }

    return true;
}

bool TiltedOnlineApp::EndMain()
{
    UninstallHooks();
    if (m_pDevice)
        m_pDevice->Release();

    return true;
}

void TiltedOnlineApp::Update()
{
    // Reverting a change that used to be here to disable bUseFaceGenPreprocessedHeads==true (which is 
    // the default) handling. Extensive testing over months by multiple parties showed that enabling 
    // the flag introduces no issues WITH PROPERLY GENERATED CHARACTERS (in-game character generation 
    // or showracemenu). The shortcut of  "coc riverwood" from the main menu skips proper character generation.
    // 
    // Plus, having it on  has some benefits like helping with neck seams. Comment to avoid revisiting.
    // 
    // There are still some issues to track down, like hair color and maybe face tint not syncing correctly,
    // but they are unrelated and unchanged by this flag.
    // 
 
    // Make sure the window stays active
    POINTER_SKYRIMSE(uint32_t, bAlwaysActive, 380768);

    *bAlwaysActive = 1;

    World::Get().Update();

    // Config-driven auto-connect: replicates OverlayClient::ProcessConnectMessage so co-op works
    // even when the CEF overlay never renders under Wine. Fires once, after the player is in-world.
    static bool s_autoConnected = false;
    if (!s_autoConnected)
    {
        auto* pPlayer = PlayerCharacter::Get();
        if (pPlayer && pPlayer->GetNiNode() && !World::Get().GetTransport().IsOnline())
        {
            const AutoCfg cfg = ReadAutoCfg();
            World::Get().GetTransport().SetServerPassword(cfg.password);
            const std::string endpoint = cfg.address + ":" + std::to_string(cfg.port);
            spdlog::info("Auto-connecting to {}", endpoint);
            World::Get().GetRunner().Queue([endpoint] { World::Get().GetTransport().Connect(endpoint); });
            s_autoConnected = true;
        }
    }
}

bool TiltedOnlineApp::Attach()
{
    TiltedPhoques::Debug::OnAttach();

    // TiltedPhoques::Nop(0x1405D3FA1, 6);
    return true;
}

bool TiltedOnlineApp::Detach()
{
    TiltedPhoques::Debug::OnDetach();
    return true;
}

void TiltedOnlineApp::InstallHooks2()
{
    TiltedPhoques::Initializer::RunAll();

    TiltedPhoques::DInputHook::Install();
    TiltedPhoques::DInputHook::Get().SetToggleKeys({DIK_F2, DIK_RCONTROL});
}

void TiltedOnlineApp::UninstallHooks()
{
}

void TiltedOnlineApp::ApplyNvidiaFix() noexcept
{
    auto d3dFeatureLevelOut = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = CreateEarlyDxDevice(&m_pDevice, &d3dFeatureLevelOut);
    if (FAILED(hr))
        spdlog::error("D3D11CreateDevice failed. Detected an NVIDIA GPU, error code={0:x}", hr);

    if (d3dFeatureLevelOut < D3D_FEATURE_LEVEL_11_0)
        spdlog::warn("Unexpected D3D11 feature level detected (< 11.0), may cause issues");
}
