# src/spike/CMakeLists.txt
# Temporary spike aids, each behind its own off-by-default build option so
# a normal build excludes them entirely. Remove a probe (file + option)
# once its spike reaches a conclusion.
#
# EncapProbe (#143, E1 Tier 2): logs inbound Multi Channel encapsulated
# frames so the spike operator can see whether a real device addresses a
# controller endpoint. Direct source of `zwaved` (not archived) so its
# __attribute__((constructor)) runs.
if(ZWAVED_SPIKE_ENCAP_LOG)
    target_sources(zwaved PRIVATE EncapProbe.cpp)
    message(STATUS "zwaved: E1 Tier 2 encap-probe ENABLED (spike #143)")
endif()
