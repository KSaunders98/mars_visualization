#include "Utils.h"

using namespace Aftr;

constexpr double MARS_RADIUS = 3.3895e6; // mean radius of Mars in meters

VectorD Aftr::toSpherical(const VectorD& p)
{
    double theta = std::atan2(p.y, p.x) * Aftr::RADtoDEGd;
    double phi = std::atan2(std::sqrt(p.x * p.x + p.y * p.y), p.z) * Aftr::RADtoDEGd;
    phi = 90.0 - phi;

    return VectorD(theta, phi, 0.0);
}

VectorD Aftr::toCartesian(const VectorD& p, double scale)
{
    double effective_radius = (MARS_RADIUS + p.z) * scale;
    double theta = p.x * Aftr::DEGtoRADd;
    double phi = (90.0 - p.y) * Aftr::DEGtoRADd;
    double x = std::cos(theta) * std::sin(phi) * effective_radius;
    double y = std::sin(theta) * std::sin(phi) * effective_radius;
    double z = std::cos(phi) * effective_radius;

    return VectorD(x, y, z);
}