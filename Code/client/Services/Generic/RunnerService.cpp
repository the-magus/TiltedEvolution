#include <TiltedOnlinePCH.h>

#include <Services/RunnerService.h>

#include <Events/UpdateEvent.h>

namespace launcher { void Trace(const char*); }

RunnerService::RunnerService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
    launcher::Trace("MI-runner");
}

void RunnerService::Queue(std::function<void()> aFunctor) noexcept
{
    m_runner.Add(std::move(aFunctor));
}

void RunnerService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    m_runner.Drain();
}
