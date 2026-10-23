#include "LLM.h"

namespace pixel::llm {
uint64_t ModelInput::RetainedBytes() const {
    return sizeof(*this) + ObservedAt.WorldId.capacity() + Intent.Prayer.capacity() + 2
        + Materials.capacity() * sizeof(CellSample) + Devices.capacity() * sizeof(api::DeviceObservation);
}
} // namespace pixel::llm
