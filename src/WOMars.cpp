#include "WOMars.h"

#include <cmath>

#include "MGLMars.h"
#include "Utils.h"

using namespace Aftr;

WOMars* WOMars::New()
{
    WOMars* wo = new WOMars();
    wo->onCreate(nullptr, 1.0, Mat4D());

    return wo;
}

WOMars* WOMars::New(const Camera** cam, double scale)
{
    WOMars* wo = new WOMars();
    wo->onCreate(cam, scale, Mat4D());

    return wo;
}

WOMars* WOMars::New(const Camera** cam, const VectorD& reference, double scale)
{
    // calculate elevation at given reference point
    uint32_t index = getPatchIndexFromMars2000(reference);
    std::vector<int16_t> elev;
    bool success = loadElevation(index, elev);

    VectorD loc = reference;
    if (success) {
        double percentX = loc.y - std::floor(loc.y);
        double percentY = std::ceil(loc.x) - loc.x;
        GLuint x = static_cast<GLuint>(percentX * PATCH_RESOLUTION);
        x = std::min(x, PATCH_RESOLUTION);
        GLuint y = static_cast<GLuint>(percentY * PATCH_RESOLUTION);
        y = std::min(y, PATCH_RESOLUTION);

        loc.z += elev[x + y * PATCH_RESOLUTION];
    } else {
        std::cerr << "WARNING: Unable to load elevation data at reference point, using given value instead." << std::endl;
    }

    // calculate reference matrix from given mars2000 coordinate
    VectorD pos = toCartesianFromMars2000(loc, scale);
    VectorD z = pos.normalizeMe();
    VectorD np = toCartesianFromMars2000(VectorD(90, 0, 0), scale);
    VectorD x = np - pos;
    x = x.vectorProjectOnToPlane(z);
    x.normalize();
    VectorD y = z.crossProduct(x);
    y.normalize();

    Mat4D ref;
    ref[0] = x.x;
    ref[1] = x.y;
    ref[2] = x.z;
    ref[3] = 0;

    ref[4] = y.x;
    ref[5] = y.y;
    ref[6] = y.z;
    ref[7] = 0;

    ref[8] = z.x;
    ref[9] = z.y;
    ref[10] = z.z;
    ref[11] = 0;

    ref = ref.translate(VectorD(0, 0, pos.magnitude()));

    WOMars* wo = new WOMars();
    wo->onCreate(cam, scale, ref);

    return wo;
}

WOMars::WOMars()
    : IFace(this)
    , WO()
{
    camPtrPtr = nullptr;
}

void WOMars::onUpdateWO()
{
    if (camPtrPtr != nullptr) {
        getModelT<MGLMars>()->update(**camPtrPtr);
    }
}

void WOMars::onCreate(const Camera** cam, double scale, const Mat4D& reference)
{
    camPtrPtr = cam;
    model = new MGLMars(this, scale, reference);
}