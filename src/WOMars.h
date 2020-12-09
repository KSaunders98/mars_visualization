#pragma once

#include "Mat4.h"
#include "WO.h"

namespace Aftr {
    class WOMars : public WO {
    public:
        static WOMars* New();
        static WOMars* New(const Camera** cam, double scale = 1.0);
        static WOMars* New(const Camera** cam, const VectorD& reference, double scale = 1.0);

        void onUpdateWO() override;

    protected:
        const Camera** camPtrPtr;

        WOMars();
        void onCreate(const Camera** cam, double scale, const Mat4D& reference);
    };
}