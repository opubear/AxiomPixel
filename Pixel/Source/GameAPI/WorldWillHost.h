#pragma once
#include "Actions.h"
#include "ObservationSource.h"

namespace pixel::api {
struct WorldWillHostStatus {
    CommitStamp Stamp;
    CallerSessionId Caller = 0;
    uint64_t NextSequence = 1;
    ProgramRef Program;
    InterventionResources Resources;
    bool Ready = false, Pending = false, Paused = true;
};

// Main-thread port. The model receives owned observation/proposal values, never
// this live host or the identity that authorizes its action submissions.
class WorldWillHost : public ObservationSource {
  public:
    virtual WorldWillHostStatus WorldWillStatus() const = 0;
    virtual ActionReceipt Submit(CallerSessionId caller, const ActionRequest &request) = 0;
};
} // namespace pixel::api
