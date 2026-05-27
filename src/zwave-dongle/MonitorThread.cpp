#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <libudev.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/time.h>  // NOLINT(misc-include-cleaner): provides timeval used by select()

namespace
{
// unique_ptr wrappers around the four libudev handle types. Each
// owning local just declares one and forgets — no manual
// `udev_*_unref` on early-return paths, no leaks under exceptions.
struct UdevContextDeleter
{
    void operator()(udev* ctx) const noexcept
    {
        udev_unref(ctx);
    }
};
struct UdevDeviceDeleter
{
    void operator()(udev_device* dev) const noexcept
    {
        udev_device_unref(dev);
    }
};
struct UdevMonitorDeleter
{
    void operator()(udev_monitor* mon) const noexcept
    {
        udev_monitor_unref(mon);
    }
};
struct UdevEnumerateDeleter
{
    void operator()(udev_enumerate* enumerate) const noexcept
    {
        udev_enumerate_unref(enumerate);
    }
};
using UdevContextPtr   = std::unique_ptr<udev, UdevContextDeleter>;
using UdevDevicePtr    = std::unique_ptr<udev_device, UdevDeviceDeleter>;
using UdevMonitorPtr   = std::unique_ptr<udev_monitor, UdevMonitorDeleter>;
using UdevEnumeratePtr = std::unique_ptr<udev_enumerate, UdevEnumerateDeleter>;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct ZwaveMonitorState
{
    std::thread thread;
    std::atomic<bool> running{false};

    // DonglesConfig subscription — populates `accept` synchronously
    // via replay-on-subscribe in startZWaveMonitorThread(). The bus
    // delivers this once at startup; the list does not change at
    // runtime. Guard auto-unsubscribes on destruction.
    std::vector<MessageBus::AcceptedDongleConfig> accept;
    MessageBus::SubscriptionGuard donglesSub;

    // Static-state destructor handles teardown — the C++ runtime tears
    // static-storage objects down via __cxa_atexit *before* it runs
    // __attribute__((destructor)) functions, so the join must happen
    // here. Otherwise ~thread() fires while the worker is still
    // joinable and calls std::terminate.
    ~ZwaveMonitorState()
    {
        running = false;
        if (thread.joinable())
        {
            thread.join();
        }
        Logger::info("Z-Wave device monitor thread shutdown complete");
    }

    ZwaveMonitorState()                                                = default;
    ZwaveMonitorState(const ZwaveMonitorState&)                        = delete;
    auto operator=(const ZwaveMonitorState&) -> ZwaveMonitorState&     = delete;
    ZwaveMonitorState(ZwaveMonitorState&&) noexcept                    = delete;
    auto operator=(ZwaveMonitorState&&) noexcept -> ZwaveMonitorState& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> ZwaveMonitorState&
{
    static ZwaveMonitorState instance;
    return instance;
}

/// True if the udev device's idVendor / idProduct sysattrs match an
/// entry in the cached `[dongles] accept` list (delivered via
/// `MessageBus::DonglesConfig`). Defaults to the Aeotec Z-Stick Gen5
/// (0658:0200) when no config file is present.
auto isZWaveDongle(udev_device* dev) -> bool
{
    const auto* vid = udev_device_get_sysattr_value(dev, "idVendor");
    const auto* pid = udev_device_get_sysattr_value(dev, "idProduct");
    if (vid == nullptr || pid == nullptr)
    {
        return false;
    }
    return std::any_of(state().accept.begin(),
                       state().accept.end(),
                       [vid, pid](const auto& entry) { return entry.vid == vid && entry.pid == pid; });
}

auto findAttachedTtyNode(udev* udev, udev_device* usbDevice) -> std::string
{
    const auto* usbSyspath = udev_device_get_syspath(usbDevice);
    if (usbSyspath == nullptr)
    {
        return {};
    }

    const UdevEnumeratePtr enumerate(udev_enumerate_new(udev));
    if (!enumerate)
    {
        return {};
    }

    udev_enumerate_add_match_subsystem(enumerate.get(), "tty");
    udev_enumerate_scan_devices(enumerate.get());

    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate.get());
    udev_list_entry* entry   = nullptr;

    udev_list_entry_foreach(entry, devices)
    {
        const auto* syspath = udev_list_entry_get_name(entry);
        if (syspath == nullptr)
        {
            continue;
        }

        if (strncmp(syspath, usbSyspath, strlen(usbSyspath)) != 0)
        {
            continue;
        }

        const UdevDevicePtr ttyDevice(udev_device_new_from_syspath(udev, syspath));
        if (!ttyDevice)
        {
            continue;
        }

        const char* devnode = udev_device_get_devnode(ttyDevice.get());
        return devnode != nullptr ? devnode : std::string{};
    }

    return {};
}

/// Walk the cached accept list and ask udev for an already-plugged
/// device matching any of them. udev's match-sysattr is per-attribute,
/// so we can only filter on one (vid, pid) at a time — re-enumerate
/// per accepted entry and stop at the first hit.
auto findAlreadyInsertedZWaveDongleSyspath(udev* udev) -> std::string
{
    for (const auto& accepted : state().accept)
    {
        const UdevEnumeratePtr enumerate(udev_enumerate_new(udev));
        if (!enumerate)
        {
            continue;
        }

        udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
        udev_enumerate_add_match_sysattr(enumerate.get(), "idVendor", accepted.vid.c_str());
        udev_enumerate_add_match_sysattr(enumerate.get(), "idProduct", accepted.pid.c_str());
        udev_enumerate_scan_devices(enumerate.get());

        udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate.get());
        udev_list_entry* entry   = nullptr;

        udev_list_entry_foreach(entry, devices)
        {
            const char* syspath = udev_list_entry_get_name(entry);
            if (syspath != nullptr)
            {
                return syspath;
            }
        }
    }
    return {};
}

auto openUdevDeviceBySyspath(udev* udev, const std::string& syspath) -> UdevDevicePtr
{
    if (syspath.empty())
    {
        return {};
    }

    return UdevDevicePtr(udev_device_new_from_syspath(udev, syspath.c_str()));
}

auto logZWaveDongleDetails(udev* udev, udev_device* usbDevice, const std::string& prefix) -> std::string
{
    const char* devpath        = udev_device_get_devpath(usbDevice);
    std::string trackedDevpath = devpath != nullptr ? devpath : "";

    Logger::info(prefix + trackedDevpath);

    std::string const ttyNode = findAttachedTtyNode(udev, usbDevice);
    if (!ttyNode.empty())
    {
        Logger::info("Z-Wave dongle tty node: " + ttyNode);
    }
    else
    {
        Logger::warn("No tty node found for Z-Wave dongle");
    }

    return trackedDevpath;
}

struct MonitorContext
{
    UdevContextPtr udev;
    UdevMonitorPtr mon;
};

auto setupMonitor() -> MonitorContext
{
    UdevContextPtr udev(udev_new());
    if (!udev)
    {
        Logger::error("Failed to create udev context");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_CRITICAL,
            .source   = "zwave-dongle",
            .code     = MessageBus::DaemonError::CODE_UDEV_INIT_FAILED,
            .message  = "udev_new() returned NULL",
        });
        return {};
    }

    UdevMonitorPtr mon(udev_monitor_new_from_netlink(udev.get(), "udev"));
    if (!mon)
    {
        Logger::error("Failed to create udev monitor");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_CRITICAL,
            .source   = "zwave-dongle",
            .code     = MessageBus::DaemonError::CODE_UDEV_INIT_FAILED,
            .message  = "udev_monitor_new_from_netlink() returned NULL",
        });
        return {};
    }

    udev_monitor_filter_add_match_subsystem_devtype(mon.get(), "usb", "usb_device");
    udev_monitor_enable_receiving(mon.get());
    return MonitorContext{.udev = std::move(udev), .mon = std::move(mon)};
}

auto handleDeviceEvent(udev* udev, udev_device* dev, std::string& trackedDevpath, std::string& trackedTtyNode) -> void
{
    const char* action  = udev_device_get_action(dev);
    const char* devpath = udev_device_get_devpath(dev);

    if (action == nullptr || devpath == nullptr)
    {
        return;
    }

    if (strcmp(action, "add") == 0)
    {
        if (isZWaveDongle(dev))
        {
            trackedDevpath = logZWaveDongleDetails(udev, dev, "Z-Wave dongle inserted: ");
            trackedTtyNode = findAttachedTtyNode(udev, dev);
            if (!trackedTtyNode.empty())
            {
                MessageBus::publish(MessageBus::DongleStatus{.connected = true, .ttyPath = trackedTtyNode});
            }
        }
    }
    else if (strcmp(action, "remove") == 0)
    {
        if (!trackedDevpath.empty() && trackedDevpath == devpath)
        {
            Logger::info("Z-Wave dongle path \"" + trackedDevpath + "\" removed");
            if (!trackedTtyNode.empty())
            {
                Logger::info("Z-Wave dongle tty node was: " + trackedTtyNode);
            }

            MessageBus::publish(MessageBus::DongleStatus{.connected = false, .ttyPath = {}});
            trackedDevpath.clear();
            trackedTtyNode.clear();
        }
    }
}

auto runMonitorLoop(udev* udev, udev_monitor* mon, std::string& trackedDevpath, std::string& trackedTtyNode) -> void
{
    int const fileDescriptor = udev_monitor_get_fd(mon);

    Logger::info("Z-Wave device monitor started. Waiting for device events...");
    prctl(PR_SET_NAME, "ZWaveComm", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>

    while (state().running)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fileDescriptor, &fds);

        // NOLINTNEXTLINE(misc-include-cleaner): timeval from <sys/time.h>
        timeval time = {
            .tv_sec  = 1,
            .tv_usec = 0,
        };

        if (int const ret = select(fileDescriptor + 1, &fds, nullptr, nullptr, &time);
            ret > 0 && FD_ISSET(fileDescriptor, &fds))
        {
            if (const UdevDevicePtr dev(udev_monitor_receive_device(mon)); dev)
            {
                handleDeviceEvent(udev, dev.get(), trackedDevpath, trackedTtyNode);
            }
        }
    }
}

auto zwaveMonitorThread() -> void
{
    state().running = true;

    auto ctx = setupMonitor();
    if (!ctx.udev || !ctx.mon)
    {
        return;
    }

    std::string trackedDevpath;
    std::string trackedTtyNode;

    if (std::string const trackedSyspath = findAlreadyInsertedZWaveDongleSyspath(ctx.udev.get());
        !trackedSyspath.empty())
    {
        if (const UdevDevicePtr usbDevice = openUdevDeviceBySyspath(ctx.udev.get(), trackedSyspath); usbDevice)
        {
            trackedTtyNode = findAttachedTtyNode(ctx.udev.get(), usbDevice.get());
            trackedDevpath =
                logZWaveDongleDetails(ctx.udev.get(), usbDevice.get(), "Z-Wave dongle already inserted at startup: ");
            if (!trackedTtyNode.empty())
            {
                MessageBus::publish(MessageBus::DongleStatus{.connected = true, .ttyPath = trackedTtyNode});
            }
        }
    }

    runMonitorLoop(ctx.udev.get(), ctx.mon.get(), trackedDevpath, trackedTtyNode);

    Logger::info("Z-Wave device monitor stopping...");
}

__attribute__((constructor(CONFIG_ZWAVE_DONGLE_PRIO))) auto startZWaveMonitorThread() -> void
{
    // Subscribe to the accepted-dongle list before the worker starts.
    // Config has already published at priority 102, so the bus replays
    // the cached value synchronously and `state().accept` is populated
    // by the time `findAlreadyInsertedZWaveDongleSyspath()` runs.
    state().donglesSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DonglesConfig>(
        [](const MessageBus::DonglesConfig& cfg) -> void { state().accept = cfg.accept; }));

    state().running = true;
    state().thread  = std::thread(zwaveMonitorThread);
}

// Shutdown lives in ZwaveMonitorState's destructor (see comment there).
}  // namespace