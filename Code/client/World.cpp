#include <TiltedOnlinePCH.h>

#include "World.h"

#include <Services/DiscoveryService.h>
#include <Services/InputService.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Services/ImguiService.h>
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/CalendarService.h>
#include <Services/StringCacheService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/MapService.h>

#include <Events/PreUpdateEvent.h>
#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>  

// launcher::Trace lives in immersive_launcher/Launcher.cpp (whole-archive-linked).
namespace launcher { void Trace(const char*); }

World::World()
    : m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_modSystem(m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{
    launcher::Trace("W0:world-body");
    ctx().emplace<ImguiService>();
    launcher::Trace("W1:imgui");
    ctx().emplace<DiscoveryService>(*this, m_dispatcher);
    launcher::Trace("W2:discovery");
    ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher);
    launcher::Trace("W3:overlay");
    ctx().emplace<InputService>(ctx().at<OverlayService>());
    launcher::Trace("W4:input");
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W5:character");
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
    launcher::Trace("W6:debug");
    ctx().emplace<PapyrusService>(m_dispatcher);
    launcher::Trace("W7:papyrus");
    ctx().emplace<DiscordService>(m_dispatcher);
    launcher::Trace("W8:discord");
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W9:object");
    ctx().emplace<CalendarService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W10:calendar");
    ctx().emplace<QuestService>(*this, m_dispatcher);
    launcher::Trace("W11:quest");
    ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W12:party");
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W13:actorvalue");
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W14:inventory");
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W15:magic");
    ctx().emplace<CommandService>(*this, m_transport, m_dispatcher);
    launcher::Trace("W16:command");
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W17:player");
    ctx().emplace<StringCacheService>(m_dispatcher);
    launcher::Trace("W18:stringcache");
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher);
    launcher::Trace("W19:combat");
    ctx().emplace<WeatherService>(*this, m_transport, m_dispatcher);
    launcher::Trace("W20:weather");
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    launcher::Trace("W21:map");

    launcher::Trace("Wb:pre-behaviorvar");
    BehaviorVar::Get()->Init();
    launcher::Trace("Wd:post-behaviorvar");
}

World::~World() = default;

void World::Update() noexcept
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

    m_dispatcher.trigger(PreUpdateEvent(cDeltaSeconds));

    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

ModSystem& World::GetModSystem() noexcept
{
    return m_modSystem;
}

uint64_t World::GetTick() const noexcept
{
    return m_transport.GetClock().GetCurrentTick();
}

void World::Create() noexcept
{
    if (!entt::locator<World>::has_value())
    {
        entt::locator<World>::emplace();
    }
}

World& World::Get() noexcept
{
    return entt::locator<World>::value();
}
