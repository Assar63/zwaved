# src/orchestrator/CMakeLists.txt
# Bus-only policy / state-machine modules that coordinate multi-step
# flows on top of the protocol thread. Each reacts to bus events and
# issues commands back over the bus — never calling another module
# directly. Today: WakeUpOrchestrator (#68), which drains the pending
# queue on a node's wake-up and sends it back to sleep. Future:
# InclusionOrchestrator (#67).

target_sources(zwaved PRIVATE
    WakeUpOrchestrator.cpp
    InclusionOrchestrator.cpp
    SceneOrchestrator.cpp
    ThermostatOrchestrator.cpp
    SecurityBootstrapOrchestrator.cpp
    SecurityOutboundOrchestrator.cpp
    SecurityS2BootstrapOrchestrator.cpp
)

# SecurityBootstrapOrchestrator pulls in the S0 codec headers, whose
# generated SECURITY_* constants live under generated/application/.
target_include_directories(zwaved PRIVATE "${ZWAVED_GENERATED_DIR}/application")
