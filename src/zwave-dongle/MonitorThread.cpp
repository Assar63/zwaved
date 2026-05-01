#include "../message-bus/MessageBus.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <libudev.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/time.h>  // NOLINT(misc-include-cleaner): provides timeval used by select()

namespace
{
// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct ZwaveMonitorState
{
    std::thread thread;
    std::atomic<bool> running{false};

    // DonglesConfig subscription — populates `accept` synchronously
    // via replay-on-subscribe in startZWaveMonitorThread(). The bus
    // delivers this once at startup; the list does not change at
    // runtime.
    std::vector<MessageBus::AcceptedDongleConfig> accept;
    MessageBus::SubscriptionId donglesSub{0};

    // Static-state destructor handles teardown — the C++ runtime tears
    // static-storage objects down via __cxa_atexit *before* it runs
    // __attribute__((destructor)) functions, so the join must happen
    // here. Otherwise ~thread() fires while the worker is still
    // joinable and calls std::terminate.
    ~ZwaveMonitorState()
    {
        if (donglesSub != 0)
        {
            MessageBus::unsubscribe(donglesSub);
            donglesSub = 0;
        }
        running = false;
        if (thread.joinable())
        {
            thread.join();
        }
        std::cout << "Z-Wave device monitor thread shutdown complete\n";
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

    auto* enumerate = udev_enumerate_new(udev);
    if (enumerate == nullptr)
    {
        return {};
    }

    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
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

        udev_device* ttyDevice = udev_device_new_from_syspath(udev, syspath);
        if (ttyDevice == nullptr)
        {
            continue;
        }

        const char* devnode = udev_device_get_devnode(ttyDevice);
        std::string result  = devnode != nullptr ? devnode : "";

        udev_device_unref(ttyDevice);
        udev_enumerate_unref(enumerate);
        return result;
    }

    udev_enumerate_unref(enumerate);
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
        udev_enumerate* enumerate = udev_enumerate_new(udev);
        if (enumerate == nullptr)
        {
            continue;
        }

        udev_enumerate_add_match_subsystem(enumerate, "usb");
        udev_enumerate_add_match_sysattr(enumerate, "idVendor", accepted.vid.c_str());
        udev_enumerate_add_match_sysattr(enumerate, "idProduct", accepted.pid.c_str());
        udev_enumerate_scan_devices(enumerate);

        udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry* entry   = nullptr;

        udev_list_entry_foreach(entry, devices)
        {
            const char* syspath = udev_list_entry_get_name(entry);
            if (syspath != nullptr)
            {
                std::string result = syspath;
                udev_enumerate_unref(enumerate);
                return result;
            }
        }
        udev_enumerate_unref(enumerate);
    }
    return {};
}

auto openUdevDeviceBySyspath(udev* udev, const std::string& syspath) -> udev_device*
{
    if (syspath.empty())
    {
        return nullptr;
    }

    return udev_device_new_from_syspath(udev, syspath.c_str());
}

auto logZWaveDongleDetails(udev* udev, udev_device* usbDevice, const std::string& prefix) -> std::string
{
    const char* devpath        = udev_device_get_devpath(usbDevice);
    std::string trackedDevpath = devpath != nullptr ? devpath : "";

    std::cout << prefix << trackedDevpath << '\n';

    std::string const ttyNode = findAttachedTtyNode(udev, usbDevice);
    if (!ttyNode.empty())
    {
        std::cout << "Z-Wave dongle tty node: " << ttyNode << '\n';
    }
    else
    {
        std::cout << "No tty node found for Z-Wave dongle\n";
    }

    return trackedDevpath;
}

auto setupMonitor(udev*& udev, udev_monitor*& mon) -> bool
{
    udev = udev_new();
    if (udev == nullptr)
    {
        std::cerr << "Failed to create udev context\n";
        return false;
    }

    mon = udev_monitor_new_from_netlink(udev, "udev");
    if (mon == nullptr)
    {
        std::cerr << "Failed to create udev monitor\n";
        udev_unref(udev);
        udev = nullptr;
        return false;
    }

    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", "usb_device");
    udev_monitor_enable_receiving(mon);
    return true;
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
            std::cout << "Z-Wave dongle path \"" << trackedDevpath << "\" removed\n";
            if (!trackedTtyNode.empty())
            {
                std::cout << "Z-Wave dongle tty node was: " << trackedTtyNode << '\n';
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

    std::cout << "Z-Wave device monitor started. Waiting for device events...\n";
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
            if (udev_device* dev = udev_monitor_receive_device(mon); dev != nullptr)
            {
                handleDeviceEvent(udev, dev, trackedDevpath, trackedTtyNode);
                udev_device_unref(dev);
            }
        }
    }
}

auto zwaveMonitorThread() -> void
{
    state().running = true;

    udev* udev        = nullptr;
    udev_monitor* mon = nullptr;

    if (!setupMonitor(udev, mon))
    {
        return;
    }

    std::string trackedDevpath;
    std::string trackedTtyNode;

    if (std::string const trackedSyspath = findAlreadyInsertedZWaveDongleSyspath(udev); !trackedSyspath.empty())
    {
        if (udev_device* usbDevice = openUdevDeviceBySyspath(udev, trackedSyspath); usbDevice != nullptr)
        {
            trackedTtyNode = findAttachedTtyNode(udev, usbDevice);
            trackedDevpath = logZWaveDongleDetails(udev, usbDevice, "Z-Wave dongle already inserted at startup: ");
            udev_device_unref(usbDevice);
            if (!trackedTtyNode.empty())
            {
                MessageBus::publish(MessageBus::DongleStatus{.connected = true, .ttyPath = trackedTtyNode});
            }
        }
    }

    runMonitorLoop(udev, mon, trackedDevpath, trackedTtyNode);

    std::cout << "Z-Wave device monitor stopping...\n";
    udev_monitor_unref(mon);
    udev_unref(udev);
}

__attribute__((constructor(CONFIG_ZWAVE_DONGLE_PRIO))) auto startZWaveMonitorThread() -> void
{
    // Subscribe to the accepted-dongle list before the worker starts.
    // Config has already published at priority 102, so the bus replays
    // the cached value synchronously and `state().accept` is populated
    // by the time `findAlreadyInsertedZWaveDongleSyspath()` runs.
    state().donglesSub = MessageBus::subscribe<MessageBus::DonglesConfig>(
        [](const MessageBus::DonglesConfig& cfg) -> void { state().accept = cfg.accept; });

    state().running = true;
    state().thread  = std::thread(zwaveMonitorThread);
}

// Shutdown lives in ZwaveMonitorState's destructor (see comment there).
}  // namespace