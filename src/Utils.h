#pragma once

#include "Vector.h"

namespace Aftr {
    VectorD toSpherical(const VectorD& p);
    VectorD toCartesian(const VectorD& p, double scale);
};
