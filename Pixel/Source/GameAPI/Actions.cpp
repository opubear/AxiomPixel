#include "Actions.h"
#include <stdexcept>

namespace pixel::api {
const char *ReasonName(Reason reason) {
    switch (reason) {
#define PIXEL_REASON(name) case Reason::name: return #name
    PIXEL_REASON(None); PIXEL_REASON(InvalidArgument); PIXEL_REASON(Unsupported);
    PIXEL_REASON(StaleEpoch); PIXEL_REASON(CallerExpired); PIXEL_REASON(DuplicateExpired);
    PIXEL_REASON(SessionUnavailable); PIXEL_REASON(WaitingForBudget); PIXEL_REASON(PermissionDenied);
    PIXEL_REASON(NotReady); PIXEL_REASON(Blocked); PIXEL_REASON(PreconditionFailed);
    PIXEL_REASON(StaleProgram); PIXEL_REASON(LaunchBlocked); PIXEL_REASON(LimitExceeded);
    PIXEL_REASON(StorageFailure); PIXEL_REASON(InvalidProgram);
#undef PIXEL_REASON
    }
    throw std::logic_error("unknown action rejection reason");
}
} // namespace pixel::api
