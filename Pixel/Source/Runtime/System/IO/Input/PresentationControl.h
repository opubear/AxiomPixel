#pragma once

namespace pixel {
// Input expresses display intent without depending on a renderer or frame data.
// The application supplies the presentation implementation for this session.
class PresentationControl {
  public:
    virtual ~PresentationControl() = default;
    virtual void ResetPostProcessing() = 0;
    virtual void ToggleTint() = 0;
    virtual void ToggleFieldView() = 0;
};
} // namespace pixel
