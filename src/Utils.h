#pragma once

#include "cpprest/http_client.h"

#include "AftrOpenGLIncludes.h"
#include "Vector.h"

namespace Aftr {
    constexpr GLuint PATCH_RESOLUTION = 256;

    VectorD toMars2000FromCartesian(const VectorD& p, double scale);
    VectorD toCartesianFromMars2000(const VectorD& p, double scale);
    uint32_t getPatchIndexFromMars2000(const VectorD& p);
    VectorD getMars2000FromPatchIndex(uint32_t index);

    bool makeGetRequest(const std::string base_uri, web::http::uri_builder& uri, std::vector<unsigned char>& result);
    bool loadElevation(uint32_t index, std::vector<int16_t>& data);
    bool loadImagery(uint32_t index, std::vector<GLubyte>& data);
};
