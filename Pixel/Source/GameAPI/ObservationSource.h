#pragma once
#include "Observations.h"

namespace pixel::api {
// Host-thread port. Capture may maintain a bounded cache, but never loads or
// activates world data. Implementations publish only complete commits.
class ObservationSource {
  public:
    virtual ~ObservationSource() = default;
    virtual ObservationResult CaptureObservation(Rect scope) = 0;
    virtual ObservationServiceStatus ObservationService() const = 0;
    virtual FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high,
                                    uint32_t limit = 256) const = 0;
};
} // namespace pixel::api
