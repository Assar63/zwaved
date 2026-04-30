# src/zwave-protocol/CMakeLists.txt
# Z-Wave protocol and thread management sources

target_sources(zwaved PRIVATE
    ProtocolThread.cpp
    ZwaveDataFrame.cpp
    ZwaveACCFrame.cpp
    ZwaveNAKFrame.cpp
    ZwaveCANFrame.cpp
    SerialPort.cpp
    FrameTransport.cpp
    HostApi.cpp
    HostApiSession.cpp
    HostApiRequestQueue.cpp
    HostApiCallbackDispatcher.cpp
    BinarySwitch.cpp
    Association.cpp
)
