#include "WOMars.h"

#include "MGLMars.h"

using namespace Aftr;

WOMars* WOMars::New()
{
    WOMars* wo = new WOMars();
    wo->onCreate();

    return wo;
}

WOMars* WOMars::New(const Camera** cam, double scale, const Mat4D& reference)
{
    WOMars* wo = new WOMars();
    wo->onCreate(cam, scale, reference);

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

void WOMars::onCreate()
{
    camPtrPtr = nullptr;
    model = new MGLMars(this, 1.0, Mat4D());
}

void WOMars::onCreate(const Camera** cam, double scale, const Mat4D& reference)
{
    camPtrPtr = cam;
    model = new MGLMars(this, scale, reference);
}