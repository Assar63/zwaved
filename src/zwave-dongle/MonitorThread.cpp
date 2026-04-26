#include <thread>
#include <iostream>
#include <atomic>
#include <libudev.h>
#include <chrono>
#include <cstring>
#include <sys/select.h>
#include <sys/prctl.h>
#include <string>
#include "../zwaved.h"

namespace
{
    std::thread monitorThread;
    std::atomic<bool> monitorRunning(false);

    auto* const ZWAVE_VID = "0658";
    auto* const ZWAVE_PID = "0200";

    auto isZWaveDongle(udev_device* dev) -> bool
    {
        const char* vid = udev_device_get_sysattr_value(dev, "idVendor");
        const char* pid = udev_device_get_sysattr_value(dev, "idProduct");
        return vid != nullptr && pid != nullptr && strcmp(vid, ZWAVE_VID) == 0 && strcmp(pid, ZWAVE_PID) == 0;
    }

    auto findAttachedTtyNode(udev* udev, udev_device* usbDevice) -> std::string
    {
        const char* usbSyspath = udev_device_get_syspath(usbDevice);
        if (usbSyspath == nullptr)
        {
            return {};
        }

        udev_enumerate* enumerate = udev_enumerate_new(udev);
        if (enumerate == nullptr)
        {
            return {};
        }

        udev_enumerate_add_match_subsystem(enumerate, "tty");
        udev_enumerate_scan_devices(enumerate);

        udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry* entry = nullptr;

        udev_list_entry_foreach(entry, devices)
        {
            const char* syspath = udev_list_entry_get_name(entry);
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
            std::string result = devnode != nullptr ? devnode : "";

            udev_device_unref(ttyDevice);
            udev_enumerate_unref(enumerate);
            return result;
        }

        udev_enumerate_unref(enumerate);
        return {};
    }

    auto findAlreadyInsertedZWaveDongleSyspath(udev* udev) -> std::string
    {
        udev_enumerate* enumerate = udev_enumerate_new(udev);
        if (enumerate == nullptr)
        {
            return {};
        }

        udev_enumerate_add_match_subsystem(enumerate, "usb");
        udev_enumerate_add_match_sysattr(enumerate, "idVendor", ZWAVE_VID);
        udev_enumerate_add_match_sysattr(enumerate, "idProduct", ZWAVE_PID);
        udev_enumerate_scan_devices(enumerate);

        udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry* entry = nullptr;

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
        const char* devpath = udev_device_get_devpath(usbDevice);
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

    auto handleDeviceEvent(udev* udev, udev_device* dev, std::string& trackedDevpath,
                           std::string& trackedTtyNode) -> void
    {
        const char* action = udev_device_get_action(dev);
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

                trackedDevpath.clear();
                trackedTtyNode.clear();
            }
        }
    }

    auto runMonitorLoop(udev* udev, udev_monitor* mon, std::string& trackedDevpath, std::string& trackedTtyNode) -> void
    {
        int const fileDescriptor = udev_monitor_get_fd(mon);

        std::cout << "Z-Wave device monitor started. Waiting for device events...\n";
        prctl(PR_SET_NAME, "ZWaveComm", 0, 0, 0);

        while (monitorRunning)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fileDescriptor, &fds);

            timeval time = {
                .tv_sec = 1,
                .tv_usec = 0,
            };

            int const ret = select(fileDescriptor + 1, &fds, nullptr, nullptr, &time);
            if (ret > 0 && FD_ISSET(fileDescriptor, &fds))
            {
                udev_device* dev = udev_monitor_receive_device(mon);
                if (dev != nullptr)
                {
                    handleDeviceEvent(udev, dev, trackedDevpath, trackedTtyNode);
                    udev_device_unref(dev);
                }
            }
        }
    }

    void zwaveMonitorThread()
    {
        monitorRunning = true;

        udev* udev = nullptr;
        udev_monitor* mon = nullptr;

        if (!setupMonitor(udev, mon))
        {
            return;
        }

        std::string trackedDevpath;
        std::string trackedTtyNode;

        std::string const trackedSyspath = findAlreadyInsertedZWaveDongleSyspath(udev);
        if (!trackedSyspath.empty())
        {
            udev_device* usbDevice = openUdevDeviceBySyspath(udev, trackedSyspath);
            if (usbDevice != nullptr)
            {
                trackedTtyNode = findAttachedTtyNode(udev, usbDevice);
                trackedDevpath = logZWaveDongleDetails(udev, usbDevice, "Z-Wave dongle already inserted at startup: ");
                udev_device_unref(usbDevice);
            }
        }

        runMonitorLoop(udev, mon, trackedDevpath, trackedTtyNode);

        std::cout << "Z-Wave device monitor stopping...\n";
        udev_monitor_unref(mon);
        udev_unref(udev);
    }

    __attribute__((constructor(CONFIG_ZWAVE_DONGLE_PRIO))) void startZWaveMonitorThread()
    {
        monitorThread = std::thread(zwaveMonitorThread);
    }

    __attribute__((destructor(CONFIG_ZWAVE_DONGLE_PRIO))) void stopZWaveMonitorThread()
    {
        monitorRunning = false;
        if (monitorThread.joinable())
        {
            monitorThread.join();
        }
    }
} // namespace
