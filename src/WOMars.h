#pragma once

#include "WO.h"

#include "Mat4.h"

namespace Aftr {
    class WOMars : public WO {
    public:
        static WOMars* New();
        static WOMars* New(const Camera** cam, double scale = 1.0, const Mat4D& reference = Mat4D());

        void onUpdateWO() override;

    protected:
        const Camera** camPtrPtr;

        WOMars();
        void onCreate() override;
        void onCreate(const Camera** cam, double scale, const Mat4D& reference);
    };
}