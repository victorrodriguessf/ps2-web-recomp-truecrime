#pragma once

#include <cstdint>

namespace ps2::state {
class Stream;
}

namespace ps2::vif {

void state(ps2::state::Stream &s);

void reset();
void reset_vu_side();
void stats();
void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc);
void transfer_tag(int unit, const std::uint8_t *tag);
bool stalled(int unit);
std::uint32_t read(int unit, std::uint32_t reg);
void write(int unit, std::uint32_t reg, std::uint32_t value);

}
