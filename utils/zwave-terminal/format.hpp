#ifndef ZWAVE_TERMINAL_FORMAT_HPP
#define ZWAVE_TERMINAL_FORMAT_HPP

#include <cstdint>
#include <string>
#include <vector>

// Pure formatting helpers for the zwave-terminal client: status-code and
// command-class name tables, and human-readable renderings of wire values.
// No shared state — every function maps inputs to a string. See #111.
namespace zwt
{
[[nodiscard]] auto formatNetworkStatus(std::uint8_t status) -> const char*;

[[nodiscard]] auto formatStatusEntry(const char* operation,
                                     std::uint8_t sessionId,
                                     std::uint8_t status,
                                     std::uint16_t nodeId) -> std::string;

[[nodiscard]] auto formatTxStatus(std::uint8_t status) -> const char*;

[[nodiscard]] auto formatSwitchState(std::uint8_t state) -> const char*;

[[nodiscard]] auto sensorTypeName(std::uint8_t sensorType) -> const char*;
[[nodiscard]] auto sensorUnit(std::uint8_t sensorType, std::uint8_t scale) -> const char*;
[[nodiscard]] auto meterTypeName(std::uint8_t meterType) -> const char*;
[[nodiscard]] auto meterUnit(std::uint8_t meterType, std::uint8_t scale) -> const char*;
[[nodiscard]] auto thermostatModeName(std::uint8_t mode) -> const char*;
[[nodiscard]] auto thermostatOperatingStateName(std::uint8_t state) -> const char*;
[[nodiscard]] auto thermostatFanModeName(std::uint8_t mode) -> const char*;

[[nodiscard]] auto commandClassName(std::uint8_t commandClass) -> const char*;
[[nodiscard]] auto formatCcRange(std::vector<std::uint8_t>::const_iterator begin,
                                 std::vector<std::uint8_t>::const_iterator end) -> std::string;
[[nodiscard]] auto formatCcList(const std::vector<std::uint8_t>& ccs) -> std::string;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_FORMAT_HPP
