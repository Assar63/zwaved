# src/message-bus/CMakeLists.txt
# In-process publish/subscribe bus. Thin wrapper around eventpp; kept
# in its own module so the underlying library can be replaced without
# disturbing publishers or subscribers.

target_sources(zwaved PRIVATE
    MessageBus.cpp
)

target_link_libraries(zwaved PRIVATE eventpp::eventpp)
