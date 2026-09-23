#include "features.h"

namespace stp {

const char* unitSuffix(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter: return "mm";
        case LengthUnit::Centimeter: return "cm";
        case LengthUnit::Meter: return "m";
        case LengthUnit::Inch: return "in";
        case LengthUnit::Foot: return "ft";
        case LengthUnit::Unknown: break;
    }
    return "";
}

Vec3 CircleFeature::pointAt(double angle) const {
    const Vec3 y = cross(normal, xAxis);
    return center + (xAxis * std::cos(angle) + y * std::sin(angle)) * radius;
}

}  // namespace stp
