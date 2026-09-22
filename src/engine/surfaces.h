// Analytic surfaces with a (u,v) parameterization, evaluation and inversion.
// Anything the engine does not model analytically becomes Type::Freeform and is
// tessellated on a best-fit plane through its boundary.
#pragma once

#include "geom.h"

namespace stp {

struct Surface {
    enum class Type { Plane, Cylinder, Cone, Sphere, Torus, Freeform };

    Type type = Type::Plane;
    Frame frame;
    double radius = 0.0;     // cylinder/sphere radius, cone base radius, torus major radius
    double minorRadius = 0.0;  // torus tube radius
    double halfAngle = 0.0;    // cone half angle, radians
    bool flipped = false;      // face orientation (same_sense == false)

    bool analytic() const { return type != Type::Freeform; }
    bool uPeriodic() const { return type != Type::Plane && type != Type::Freeform; }
    bool vPeriodic() const { return type == Type::Torus; }

    Vec3 eval(double u, double v) const;
    Vec3 normal(double u, double v) const;
    Vec2 invert(const Vec3& p) const;
};

}  // namespace stp
