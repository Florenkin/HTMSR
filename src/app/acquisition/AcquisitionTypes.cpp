#include "app/acquisition/AcquisitionTypes.h"

namespace htmsr::app {

std::string toString(CameraState state)
{
    switch (state) {
    case CameraState::Disconnected:
        return "Disconnected";
    case CameraState::Connected:
        return "Connected";
    case CameraState::Streaming:
        return "Streaming";
    case CameraState::Error:
        return "Error";
    }
    return "Unknown";
}

} // namespace htmsr::app
