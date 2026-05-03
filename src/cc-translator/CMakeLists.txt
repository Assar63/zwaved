# src/cc-translator/CMakeLists.txt
# Bridge module that subscribes to bus ApplicationCommand events,
# runs the application/ codec decoders, and republishes typed bus
# events (BinarySwitchReport, …). Lets the external-api thread emit
# typed D-Bus signals without including any CC codec header — the
# bus stays the only seam between modules.

target_sources(zwaved PRIVATE CcTranslator.cpp)
