#include "ThermostatOperatingState.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto report(std::uint8_t stateByte) -> std::vector<std::uint8_t>
{
    return {ThermostatOperatingState::COMMAND_CLASS,
            ThermostatOperatingState::THERMOSTAT_OPERATING_STATE_REPORT,
            stateByte};
}
}  // namespace

TEST(ThermostatOperatingState, EncodeGet)
{
    const std::vector<std::uint8_t> expected{ThermostatOperatingState::COMMAND_CLASS,
                                             ThermostatOperatingState::THERMOSTAT_OPERATING_STATE_GET};
    EXPECT_EQ(ThermostatOperatingState::encodeGet(), expected);
}

TEST(ThermostatOperatingState, DecodeStates)
{
    const auto idle = ThermostatOperatingState::decodeReport(report(ThermostatOperatingState::STATE_IDLE));
    ASSERT_TRUE(idle.has_value());
    EXPECT_EQ(idle->state, ThermostatOperatingState::STATE_IDLE);

    const auto heating = ThermostatOperatingState::decodeReport(report(ThermostatOperatingState::STATE_HEATING));
    ASSERT_TRUE(heating.has_value());
    EXPECT_EQ(heating->state, ThermostatOperatingState::STATE_HEATING);

    const auto cooling = ThermostatOperatingState::decodeReport(report(ThermostatOperatingState::STATE_COOLING));
    ASSERT_TRUE(cooling.has_value());
    EXPECT_EQ(cooling->state, ThermostatOperatingState::STATE_COOLING);
}

TEST(ThermostatOperatingState, DecodeMasksReservedBits)
{
    // High 4 bits are reserved; low 4 bits carry the state. 0x21 → 1 (heating).
    const auto decoded = ThermostatOperatingState::decodeReport(report(0x21));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->state, 0x01);
}

TEST(ThermostatOperatingState, RejectsMalformed)
{
    // Too short (no state byte).
    const std::vector<std::uint8_t> tooShort{ThermostatOperatingState::COMMAND_CLASS,
                                             ThermostatOperatingState::THERMOSTAT_OPERATING_STATE_REPORT};
    EXPECT_FALSE(ThermostatOperatingState::decodeReport(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x40, ThermostatOperatingState::THERMOSTAT_OPERATING_STATE_REPORT, 0x01};
    EXPECT_FALSE(ThermostatOperatingState::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{
        ThermostatOperatingState::COMMAND_CLASS, ThermostatOperatingState::THERMOSTAT_OPERATING_STATE_GET, 0x01};
    EXPECT_FALSE(ThermostatOperatingState::decodeReport(wrongCmd).has_value());
}
