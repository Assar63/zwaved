#ifndef ZWAVED_DBUS_BACKEND_HPP
#define ZWAVED_DBUS_BACKEND_HPP

#include "IExternalApi.hpp"

#include <atomic>
#include <memory>

namespace ExternalApi
{
/// sdbus-c++ backed implementation of IBackend. Exposes the host API
/// on the system bus at com.tiunda.ZWaved / /com/tiunda/ZWaved.
class DBusBackend : public IBackend
{
  public:
    DBusBackend();
    DBusBackend(const DBusBackend&)                        = delete;
    auto operator=(const DBusBackend&) -> DBusBackend&     = delete;
    DBusBackend(DBusBackend&&) noexcept                    = default;
    auto operator=(DBusBackend&&) noexcept -> DBusBackend& = default;
    ~DBusBackend() override;

    auto run(const std::atomic<bool>& running) -> void override;
    auto stop() -> void override;

    // Pimpl forward declaration. The full definition lives in
    // DBusBackendInternal.hpp so the generator-emitted method
    // bindings (DBusMethods.gen.cpp) and the hand-written `custom:`
    // handlers can reach the cached state and subscription IDs.
    // Public here so those translation units can reference Impl& as
    // a parameter type without a friend declaration per handler.
    struct Impl;

  private:
    std::unique_ptr<Impl> impl;
};
}  // namespace ExternalApi

#endif  // ZWAVED_DBUS_BACKEND_HPP
