#pragma once
#include "Runtime/System/IO/StorageError.h"

namespace pixel {
// Domain name retained for callers and saved-data validation failures.
using WorldStorageError = io::StorageError;
namespace storage { using io::CaptureFailure; }
} // namespace pixel
