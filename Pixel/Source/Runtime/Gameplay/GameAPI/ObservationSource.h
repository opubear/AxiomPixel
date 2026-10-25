#pragma once
#include "Observations.h"

namespace pixel::api {
struct ObservationAuthorization {
    uint64_t Version = 0;
    bool Allowed = false;
};

// Host-thread port. Capture may maintain a bounded cache, but never loads or
// activates world data. Implementations publish only complete commits.
class ObservationSource {
  public:
    virtual ~ObservationSource() = default;
    virtual ObservationResult CaptureObservation(Rect scope) = 0;
    virtual ObservationServiceStatus ObservationService() const = 0;
    // Host-bound subject and complete granted region, never external request
    // claims. A version change revokes every snapshot opened under that grant.
    virtual ObservationAuthorization AuthorizeObservation(uint64_t, Rect) const { return {}; }
    virtual FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high,
                                    uint32_t limit = 256) const = 0;
};
} // namespace pixel::api
