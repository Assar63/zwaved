#ifndef ZWAVED_I_EXTERNAL_API_HPP
#define ZWAVED_I_EXTERNAL_API_HPP

#include <atomic>
#include <memory>

namespace ExternalApi
{
/// Backend abstraction for an external transport that exposes the
/// Z-Wave host API. The D-Bus backend is the only implementation today;
/// a ubus backend will plug in here without disturbing the protocol layer.
class IBackend
{
  public:
    IBackend()                                       = default;
    IBackend(const IBackend&)                        = delete;
    auto operator=(const IBackend&) -> IBackend&     = delete;
    IBackend(IBackend&&) noexcept                    = default;
    auto operator=(IBackend&&) noexcept -> IBackend& = default;
    virtual ~IBackend()                              = default;

    /// Run the backend's main loop until `running` becomes false. The
    /// implementation is responsible for translating inbound calls into
    /// HostApi::pushRequest(...) and consuming HostApi::popCallback(...)
    /// to publish signals/events.
    virtual auto run(const std::atomic<bool>& running) -> void = 0;

    /// Wake any blocking work in run() so it can observe a stop request.
    virtual auto stop() -> void = 0;
};

/// Factory for the configured backend. Returns nullptr if no backend
/// was compiled into the binary (only possible if ZWAVED_EXTERNAL_API
/// requested ubus, which is not yet implemented).
[[nodiscard]] auto createBackend() -> std::unique_ptr<IBackend>;
}  // namespace ExternalApi

#endif  // ZWAVED_I_EXTERNAL_API_HPP
