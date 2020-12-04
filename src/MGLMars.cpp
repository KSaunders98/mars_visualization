#include "MGLMars.h"

#include <string>

#include "Camera.h"
#include "GLSLShaderDefaultGL32.h"
#include "Utils.h"

#include "cpprest/filestream.h"
#include "cpprest/http_client.h"

using namespace Aftr;

using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace concurrency;

constexpr double MARS_RADIUS = 3.3895e6; // mean radius of Mars in meters

static const std::string API_URL = "http://192.168.1.110:3000/";
static const std::string API_ELEV_URL = API_URL + "elevation";
static const std::string API_IMG_URL = API_URL + "imagery";

MGLMars::MGLMars(WO* parentWO, double scale, const Mat4D& refMat)
    : MGL(parentWO)
    , asyncPatchesToLoad(std::thread::hardware_concurrency())
{
    marsScale = scale;
    reference = refMat;
    Aftr::aftrGluInvertMatrix(reference.getPtr(), referenceInv.getPtr());

    init();
}

MGLMars::~MGLMars()
{
    shutdownMsg.store(true);

    // join background threads in
    for (size_t i = 0; i < asyncThreads.size(); i++) {
        asyncThreads[i].join();
    }
}

bool makeGetRequest(const std::string base_uri, uri_builder& uri, std::vector<unsigned char>& result)
{
    http_client client(utility::conversions::utf8_to_utf16(base_uri));
    http_response response;
    try {
        response = client.request(methods::GET, uri.to_string()).get();
    } catch (...) {
        std::cerr << "Unable to make get request: " << uri.to_string().c_str() << std::endl;
        return false;
    }

    if (response.status_code() != status_codes::OK) {
        std::cerr << "Get request failed: " << uri.to_string().c_str()
            << "\n\tStatus Code: " << response.status_code() << std::endl;
        return false;
    }

    std::vector<unsigned char> data;
    try {
        data = response.extract_vector().get();
    } catch (...) {
        std::cerr << "Get request failed: " << uri.to_string().c_str()
            << "\n\tUnable to get data from response" << std::endl;
        return false;
    }

    // copy data into result
    result.resize(data.size());
    std::copy(data.begin(), data.end(), result.begin());

    return true;
}

bool MGLMars::loadElevation(uint32_t id, std::vector<int16_t>& data)
{
    uri_builder builder{};
    builder.append_query(L"id", id);

    std::vector<unsigned char> result;
    bool success = makeGetRequest(API_ELEV_URL, builder, result);
    if (!success) {
        std::cerr << "Failed to load elevation data for tile id: " << id << std::endl;
        return false;
    }

    size_t expected_size = PATCH_RESOLUTION * PATCH_RESOLUTION * sizeof(int16_t);
    if (result.size() != expected_size) {
        std::cerr << "Unable to fetch elevation data for tile id: " << id
                  << "\n\tIncorrect response size: " << result.size() << " bytes (expected " << expected_size << " bytes)" << std::endl;
        return false;
    }

    data.resize(PATCH_RESOLUTION * PATCH_RESOLUTION);
    for (size_t i = 0; i < result.size(); i += 2) {
        // bytes are in big-endian int16 format
        int16_t e = static_cast<int16_t>(result[i]) << 8 | static_cast<int16_t>(result[i + 1]);
        data[i / 2] = e;
    }

    return true;
}

bool MGLMars::loadImagery(uint32_t id, std::vector<GLubyte>& data)
{
    uri_builder builder{};
    builder.append_query(L"id", id);

    std::vector<unsigned char> result;
    bool success = makeGetRequest(API_IMG_URL, builder, result);
    if (!success) {
        std::cerr << "Failed to load imagery data for tile id: " << id << std::endl;
        return false;
    }

    size_t expected_size = PATCH_RESOLUTION * PATCH_RESOLUTION * 3 * sizeof(GLubyte);
    if (result.size() != expected_size) {
        std::cerr << "Unable to fetch imagery data for tile id: " << id
                  << "\n\tIncorrect response size: " << result.size() << " bytes (expected " << expected_size << " bytes)" << std::endl;
        return false;
    }

    data.resize(PATCH_RESOLUTION * PATCH_RESOLUTION * 3);
    std::copy(result.begin(), result.end(), data.begin());

    return true;
}

void MGLMars::init()
{
    // create and add skin
    ModelMeshSkin skin;
    skin.setGLPrimType(GL_TRIANGLES);
    skin.setMeshShadingType(MESH_SHADING_TYPE::mstFLAT);
    skin.setShader(static_cast<GLSLShader*>(ManagerShader::getDefaultShaderCopy()));
    MGL::addSkin(std::move(skin));

    // create and setup VAO
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // setup VertexPosition attrib
    glEnableVertexAttribArray(0);
    glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, offsetof(GLVertex, pos));
    glVertexAttribBinding(0, 0);

    // setup VertexNormal attrib
    glEnableVertexAttribArray(1);
    glVertexAttribFormat(1, 3, GL_FLOAT, GL_FALSE, offsetof(GLVertex, norm));
    glVertexAttribBinding(1, 0);

    // setup VertexTexCoord attrib
    glEnableVertexAttribArray(2);
    glVertexAttribFormat(2, 2, GL_FLOAT, GL_FALSE, offsetof(GLVertex, texCoord));
    glVertexAttribBinding(2, 0);

    glBindVertexArray(0);

    shutdownMsg.store(false); // ensure shutdown message is not true

    // spawn background threads that handle async texture loading
    for (size_t i = 0; i < std::max(std::thread::hardware_concurrency(), 1u); ++i) {
        asyncThreads.emplace_back([this, i]() {
            std::default_random_engine gen;
            gen.seed(static_cast<unsigned int>(i));
            std::uniform_int_distribution<unsigned int> dist(3, 10);

            while (!shutdownMsg.load()) {
                Patch* patch;
                if (asyncPatchesToLoad.pop(patch)) {
                    bool success = loadElevation(patch->id, patch->elevData);
                    if (success) {
                        patch->elevReady.store(true);
                    }

                    success = loadImagery(patch->id, patch->imgData);
                    if (success) {
                        patch->imgReady.store(true);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
                }
            }
        });
    }
}

void MGLMars::render(const Camera& cam)
{
    const Mat4 modelMatrix = getModelMatrix();
    const Mat4 normalMatrix = getNormalMatrix(cam);
    std::tuple<const Mat4&, const Mat4&, const Camera&> shaderParams(modelMatrix, normalMatrix, cam);
    getSkin().bind(&shaderParams);

    Texture* defaultTex = getSkin().getMultiTextureSet().at(0);

    // bind VAO
    glBindVertexArray(vao);

    // activate relevant texture unit
    glActiveTexture(GL_TEXTURE0);

    std::shared_ptr<PatchArray> array = nullptr;
    for (auto& patch : visiblePatches) {
        if (array != patchArrays.at(patch->arrayGroup)) {
            array = patchArrays.at(patch->arrayGroup);

            // bind buffer for rendering
            glBindVertexBuffer(0, array->vertexBuffer, 0, sizeof(GLVertex));

            // bind index buffer
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, array->indexBuffer);
        }

        // bind texture

        if (patch->texture != nullptr) {
            patch->texture->bind();
        } else {
            defaultTex->bind();
        }

        // draw
        glDrawElements(GL_TRIANGLES, NUM_TRIS_PER_PATCH * 3, GL_UNSIGNED_INT, (GLvoid*)(patch->arrayIndex * NUM_TRIS_PER_PATCH * 3 * sizeof(GLuint)));
    }
}

void MGLMars::renderSelection(const Camera& cam, GLubyte red, GLubyte green, GLubyte blue)
{
    for (auto& patch : visiblePatches) {
        //patch->renderSelection(cam, red, green, blue);
    }
}

void MGLMars::update(const Camera& cam)
{
    // calculate current tile from camera position
    VectorD v = getRelativeToCenter(cam.getPosition());
    VectorD camSpherical = toSpherical(v);
    uint32_t patchIndex = getPatchIndexFromSpherical(camSpherical);
    uint32_t patchX = patchIndex % 360;
    uint32_t patchY = patchIndex / 360;

    visiblePatches.clear(); // clear visible patches, we must recalculate them

    int64_t rad = 2;

    /*for (int64_t y = -rad; y <= rad; ++y) {
        for (int64_t x = -rad; x <= rad; ++x) {
            // get patch index
            uint64_t index = getNeighborPatchIndex(tileX, tileY, x, y, dim);
            std::shared_ptr<Patch> patch = getPatch(zoomLevel, index);
            visiblePatches.insert(patch);
        }
    }*/

    // add patches going outward from the center patch
    /*for (int64_t r = 0; r <= rad; ++r) {
        for (int64_t y = -r; y <= r; ++y) {
            for (int64_t x = -r; x <= r; ++x) {
                if ((y == -r || y == r) || (x == -r || x == r)) {
                    // get patch index
                    uint64_t index = getNeighborPatchIndex(patchX, patchY, x, y, dim);
                    std::shared_ptr<Patch> patch = createUpdateGetPatch(zoomLevel, index);
                    visiblePatches.insert(patch);
                }
            }
        }
    }

    fixGaps();*/

    visiblePatches.insert(createUpdateGetPatch(patchIndex));
}

/*uint64_t MGLMars::getNeighborPatchIndex(uint64_t x, uint64_t y, int64_t dx, int64_t dy, uint64_t dim)
{
    uint64_t tileX;
    if (dx < 0 && static_cast<uint64_t>(-dx) > x) { // underflow
        uint64_t wrap = static_cast<uint64_t>(-dx) - x;
        tileX = dim - wrap;
    }
    else if (dx > 0 && static_cast<uint64_t>(dx) > (dim - x - 1)) { // overflow
        uint64_t wrap = static_cast<uint64_t>(dx) - (dim - x);
        tileX = wrap;
    }
    else {
        tileX = x + dx;
    }

    uint64_t tileY;
    if (dy < 0 && static_cast<uint64_t>(-dy) > y) { // underflow
        // rotate X by 180 degrees
        /*if (tileX >= dim / 2) {
            tileX = tileX - dim / 2;
        } else {
            tileX = tileX + dim / 2;
        }

        uint64_t wrap = static_cast<uint64_t>(-dy) - y - 1;
        tileY = wrap;* /
        tileY = 0;
    }
    else if (dy > 0 && static_cast<uint64_t>(dy) > (dim - y - 1)) { // overflow
     /* //uint64_t wrap = static_cast<uint64_t>(dy) - (dim - y - 1);
     //tileY = wrap;

     // rotate X by 180 degrees
     if (tileX >= dim / 2) {
         tileX = tileX - dim / 2;
     } else {
         tileX = tileX + dim / 2;
     }

     uint64_t wrap = static_cast<uint64_t>(dy) - (dim - y - 1);
     tileY = dim - wrap;* /
        tileY = dim - 1;
    }
    else {
        tileY = y + dy;
    }

    return tileY * dim + tileX;
} */

uint32_t MGLMars::getPatchIndexFromSpherical(const VectorD& p)
{
    uint32_t x = static_cast<uint32_t>(p.x + 180.0);
    uint32_t y = static_cast<uint32_t>(90.0 - p.y);

    return x + y * 360;
}

VectorD MGLMars::getSphericalFromPatchIndex(uint32_t index)
{
    uint32_t x = index % 360;
    uint32_t y = index / 360;
    double theta = static_cast<double>(x) - 180.0;
    double phi = 90.0 - static_cast<double>(y);

    return VectorD(theta, phi, 0.0);
}

VectorD MGLMars::getRelativeToCenter(const VectorD& p) const
{
    Mat4D modelInv;
    aftrGluInvertMatrix(getModelMatrix().toMatD().getPtr(), modelInv.getPtr());

    Mat4D transform = reference * modelInv;
    double in[4] = { p.x, p.y, p.z, 1.0 };
    double out[4];
    transformVector4DThrough4x4Matrix(in, out, transform.getPtr());

    return VectorD(out[0], out[1], out[2]);
}

std::shared_ptr<Patch> MGLMars::getPatch(uint32_t tile)
{
    auto i = patches.find(tile);

    if (i != patches.end()) {
        return i->second;
    } else {
        return nullptr;
    }
}

std::shared_ptr<Patch> MGLMars::createUpdateGetPatch(uint32_t index)
{
    auto i = patches.insert(std::make_pair(index, nullptr));

    double r = 90.0;

    std::shared_ptr<Patch> patch;
    if (i.second) { // inserted new element
        patch = generatePatch(index);
        i.first->second = patch;
    } else {
        patch = i.first->second;
    }

    /*if (patch->tex != nullptr && patch->tex->isTexFullyInitialized() && patch->parentTex != nullptr) {
        // texture loaded, need to update UVs accordingly and make parentTex null
        patch->parentTex = nullptr;
        patch->useParentTex = false;

        // calculate ul and lr of patch from tile index
        uint64_t dim = 1ull << (static_cast<uint64_t>(level) + 1);
        uint64_t tileX = tile % dim;
        uint64_t tileY = tile / dim;

        VectorD ul = getWGS84FromPatchIndex(tileX, tileY, dim);
        VectorD lr = getWGS84FromPatchIndex(tileX + 1, tileY + 1, dim);

        auto& buffer = buffers.at(patch->bufferGroup);

        GLuint baseVertIndex = patch->bufferIndex * NUM_VERTS_PER_PATCH;
        GLVertex* vert = &buffer->vertexData[baseVertIndex];
        for (GLuint x = 0; x <= NUM_SUBDIVISIONS_PER_PATCH; ++x) {
            // calculate the lattitude at this subdivison level
            double v = static_cast<double>(x) / NUM_SUBDIVISIONS_PER_PATCH;
            //double lat = ul.x + (lr.x - ul.x) * v;

            for (GLuint y = 0; y <= NUM_SUBDIVISIONS_PER_PATCH; ++y) {
                // calculate the longitude at this subdivision level
                double u = static_cast<double>(y) / NUM_SUBDIVISIONS_PER_PATCH;
                //double lon = ul.y + (lr.y - ul.y) * u;

                // combine lat and lon into WGS84 coordinate
                //VectorD wgs = VectorD(lat, lon, 0);
                /*VectorD wgs = getWGS84FromPatchIndexD(static_cast<double>(tileX) + u, static_cast<double>(tileY) + v, dim);

                vert->texCoord = getUVFromWGS84(wgs, tileX, tileY, dim);* /

                vert->texCoord = aftrTexture4f(static_cast<float>(u), static_cast<float>(1.0 - v));

                vert++; // advance pointer
            }
        }

        // post data to OpenGL
        glBindBuffer(GL_ARRAY_BUFFER, buffer->vertexBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, baseVertIndex * sizeof(GLVertex), NUM_VERTS_PER_PATCH * sizeof(GLVertex), &buffer->vertexData[baseVertIndex]);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (!patch->elevLoaded && patch->elevReady.load()) {
        // elevation data loaded, need to apply it
        patch->elevLoaded = true;

        // calculate ul and lr of patch from tile index
        uint64_t dim = 1ull << (static_cast<uint64_t>(level) + 1);
        uint64_t tileX = tile % dim;
        uint64_t tileY = tile / dim;

        VectorD ul = getWGS84FromPatchIndex(tileX, tileY, dim);
        VectorD lr = getWGS84FromPatchIndex(tileX + 1, tileY + 1, dim);

        auto& buffer = buffers.at(patch->bufferGroup);

        GLuint baseVertIndex = patch->bufferIndex * NUM_VERTS_PER_PATCH;
        GLVertex* vert = &buffer->vertexData[baseVertIndex];
        for (GLuint x = 0; x <= NUM_SUBDIVISIONS_PER_PATCH; ++x) {
            // calculate the lattitude at this subdivison level
            double v = static_cast<double>(x) / NUM_SUBDIVISIONS_PER_PATCH;
            //double lat = ul.x + (lr.x - ul.x) * v;

            for (GLuint y = 0; y <= NUM_SUBDIVISIONS_PER_PATCH; ++y) {
                // calculate the longitude at this subdivision level
                double u = static_cast<double>(y) / NUM_SUBDIVISIONS_PER_PATCH;
                //double lon = ul.y + (lr.y - ul.y) * u;

                // combine lat and lon into WGS84 coordinate
                //VectorD wgs = VectorD(lat, lon, 0);
                VectorD wgs = getWGS84FromPatchIndexD(static_cast<double>(tileX) + u, static_cast<double>(tileY) + v, dim);
                wgs.z = static_cast<double>(patch->elevData[x * (NUM_SUBDIVISIONS_PER_PATCH + 1) + y]) * 1;

                VectorD ecef = wgs.toECEFfromWGS84() * MarsScale;

                // transform based on reference
                double in[4] = { ecef.x, ecef.y, ecef.z, 1.0 };
                double out[4];
                transformVector4DThrough4x4Matrix(in, out, referenceInv.getPtr());

                // write back into a VectorD
                VectorD pos(out[0], out[1], out[2]);

                vert->pos = pos.toVecS();

                vert++; // advance pointer
            }
        }

        // post data to OpenGL
        glBindBuffer(GL_ARRAY_BUFFER, buffer->vertexBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, baseVertIndex * sizeof(GLVertex), NUM_VERTS_PER_PATCH * sizeof(GLVertex), &buffer->vertexData[baseVertIndex]);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }*/

    return patch;
}

std::shared_ptr<Patch> MGLMars::generatePatch(uint32_t index)
{
    uint32_t patchX = index % 360;
    uint32_t patchY = index / 360;

    uint32_t nextIndex = (patchX + 1) + (patchY + 1) * 360;

    VectorD ul = getSphericalFromPatchIndex(index);
    VectorD lr = getSphericalFromPatchIndex(nextIndex);
    VectorD center = (ul + lr) / 2;

    // create new patch
    std::shared_ptr<Patch> patch = std::make_shared<Patch>();
    patch->id = index;

    if (patchArrays.empty() || patchArrays.back()->size == patchArrays.back()->capacity) {
        // generate a new patch array
        patchArrays.push_back(std::make_shared<PatchArray>());
    }

    auto& array = patchArrays.back();

    patch->arrayGroup = patchArrays.size() - 1;
    patch->arrayIndex = array->size++;

    std::vector<int16_t> elev;
    bool success = loadElevation(index, elev);

    // generate patch vertices and tex coords
    GLVertex* vertPtr = array->getPatchVertexStart(patch->arrayIndex);
    for (GLuint y = 0; y < PATCH_RESOLUTION; ++y) {
        // calculate the lattitude at this subdivison level
        double v = static_cast<double>(y) / (PATCH_RESOLUTION - 1);
        double lat = ul.y + (lr.y - ul.y) * v;

        for (GLuint x = 0; x < PATCH_RESOLUTION; ++x) {
            // calculate the longitude at this subdivision level
            double u = static_cast<double>(x) / (PATCH_RESOLUTION - 1);
            double lon = ul.x + (lr.x - ul.x) * u;

            // combine lat and lon into spherical coordinate
            VectorD sph = VectorD(lon, lat, 0.0);
            if (success) {
                sph.z = static_cast<double>(elev[x + y * PATCH_RESOLUTION]) * 10.0;
            }
            VectorD cart = toCartesian(sph, marsScale);

            // transform based on reference
            double in[4] = { cart.x, cart.y, cart.z, 1.0 };
            double out[4];
            transformVector4DThrough4x4Matrix(in, out, referenceInv.getPtr());

            // write back into a VectorD
            VectorD pos(out[0], out[1], out[2]);

            vertPtr->pos = pos.toVecS();
            vertPtr->norm = (referenceInv * cart).normalizeMe().toVecS();
            vertPtr->texCoord = aftrTexture4f(static_cast<GLfloat>(u), static_cast<GLfloat>(v));

            vertPtr++; // advance pointer
        }
    }

    GLuint width = PATCH_RESOLUTION;

    // generate indices
    GLuint baseVertIndex = array->getPatchVertexStartIndex(patch->arrayIndex);
    GLuint* indexPtr = array->getPatchIndexStart(patch->arrayIndex);
    for (GLuint y = 0; y < PATCH_RESOLUTION - 1; ++y) {
        for (GLuint x = 0; x < PATCH_RESOLUTION - 1; ++x) {
            // convert 2d array indices to 1d array indices
            GLuint ul = x + y * width + baseVertIndex;
            GLuint ll = x + (y + 1) * width + baseVertIndex;
            GLuint lr = (x + 1) + (y + 1) * width + baseVertIndex;
            GLuint ur = (x + 1) + y * width + baseVertIndex;

            // top-left triangle
            indexPtr[0] = ul;
            indexPtr[1] = ll;
            indexPtr[2] = ur;

            // bottom-right triangle
            indexPtr[3] = ll;
            indexPtr[4] = lr;
            indexPtr[5] = ur;

            indexPtr += 6; // advance pointer
        }
    }

    // post data to OpenGL
    array->uploadVertexSegment(patch->arrayIndex, 1);
    array->uploadIndexSegment(patch->arrayIndex, 1);

    //asyncPatchesToLoad.push(patch.get());

    return patch;
}

/*void MGLMars::fixGaps()
{
    for (auto& patch : visiblePatches) {
        if (!patch->elevLoaded) {
            bool dataChanged = false;
            uint64_t dim = 1ull << (static_cast<uint64_t>(patch->level) + 1);
            uint64_t tileX = patch->index % dim;
            uint64_t tileY = patch->index / dim;

            auto& buffer = buffers.at(patch->bufferGroup);
            GLuint baseVertIndex = patch->bufferIndex * NUM_VERTS_PER_PATCH;

            for (int64_t i = 0; static_cast<size_t>(i) < patch->fixedGaps.size(); ++i) {
                if (!patch->fixedGaps[i]) {
                    int64_t dx, dy;
                    if (i < 3) {
                        dy = -1;
                        dx = i - 1;
                    }
                    else if (i > 4) {
                        dy = 1;
                        dx = i - 6;
                    }
                    else {
                        dy = 0;
                        dx = i == 3 ? -1 : 1;
                    }
                    uint64_t adjIndex = getNeighborPatchIndex(tileX, tileY, dx, dy, dim);
                    std::shared_ptr<Patch> adjPatch = getPatch(patch->level, adjIndex);

                    if (adjPatch != nullptr && adjPatch != patch && adjPatch->elevLoaded) {
                        if (dx == 0) {
                            size_t srcRow = dy < 0 ? NUM_SUBDIVISIONS_PER_PATCH : 0;
                            size_t dstRow = dy < 0 ? 0 : NUM_SUBDIVISIONS_PER_PATCH;

                            GLVertex* vert = &buffer->vertexData[baseVertIndex];
                            vert += dstRow * (NUM_SUBDIVISIONS_PER_PATCH + 1);
                            double v = static_cast<double>(dstRow) / NUM_SUBDIVISIONS_PER_PATCH;
                            for (GLuint y = 0; y <= NUM_SUBDIVISIONS_PER_PATCH; ++y) {
                                double u = static_cast<double>(y) / NUM_SUBDIVISIONS_PER_PATCH;

                                // combine lat and lon into WGS84 coordinate
                                VectorD wgs = getWGS84FromPatchIndexD(static_cast<double>(tileX) + u, static_cast<double>(tileY) + v, dim);
                                wgs.z = static_cast<double>(adjPatch->elevData[srcRow * (NUM_SUBDIVISIONS_PER_PATCH + 1) + y]) * 1;

                                VectorD ecef = wgs.toECEFfromWGS84() * MarsScale;

                                // transform based on reference
                                double in[4] = { ecef.x, ecef.y, ecef.z, 1.0 };
                                double out[4];
                                transformVector4DThrough4x4Matrix(in, out, referenceInv.getPtr());

                                // write back into a VectorD
                                VectorD pos(out[0], out[1], out[2]);

                                vert->pos = pos.toVecS();

                                vert++; // advance pointer
                            }
                        }
                        else if (dy == 0) {
                            size_t srcCol = dx < 0 ? NUM_SUBDIVISIONS_PER_PATCH : 0;
                            size_t dstCol = dx < 0 ? 0 : NUM_SUBDIVISIONS_PER_PATCH;

                            GLVertex* vert = &buffer->vertexData[baseVertIndex];
                            vert += dstCol;
                            double u = static_cast<double>(dstCol) / NUM_SUBDIVISIONS_PER_PATCH;
                            for (GLuint x = 0; x <= NUM_SUBDIVISIONS_PER_PATCH; ++x) {
                                double v = static_cast<double>(x) / NUM_SUBDIVISIONS_PER_PATCH;

                                // combine lat and lon into WGS84 coordinate
                                VectorD wgs = getWGS84FromPatchIndexD(static_cast<double>(tileX) + u, static_cast<double>(tileY) + v, dim);
                                wgs.z = static_cast<double>(adjPatch->elevData[x * (NUM_SUBDIVISIONS_PER_PATCH + 1) + srcCol]) * 1;

                                VectorD ecef = wgs.toECEFfromWGS84() * MarsScale;

                                // transform based on reference
                                double in[4] = { ecef.x, ecef.y, ecef.z, 1.0 };
                                double out[4];
                                transformVector4DThrough4x4Matrix(in, out, referenceInv.getPtr());

                                // write back into a VectorD
                                VectorD pos(out[0], out[1], out[2]);

                                vert->pos = pos.toVecS();

                                vert += NUM_SUBDIVISIONS_PER_PATCH + 1; // advance pointer
                            }
                        }
                        else {
                            size_t srcRow = dy < 0 ? NUM_SUBDIVISIONS_PER_PATCH : 0;
                            size_t dstRow = dy < 0 ? 0 : NUM_SUBDIVISIONS_PER_PATCH;
                            size_t srcCol = dx < 0 ? NUM_SUBDIVISIONS_PER_PATCH : 0;
                            size_t dstCol = dx < 0 ? 0 : NUM_SUBDIVISIONS_PER_PATCH;

                            GLVertex* vert = &buffer->vertexData[baseVertIndex];
                            vert += dstRow * (NUM_SUBDIVISIONS_PER_PATCH + 1) + dstCol;
                            double v = static_cast<double>(dstRow) / NUM_SUBDIVISIONS_PER_PATCH;
                            double u = static_cast<double>(dstCol) / NUM_SUBDIVISIONS_PER_PATCH;

                            // combine lat and lon into WGS84 coordinate
                            VectorD wgs = getWGS84FromPatchIndexD(static_cast<double>(tileX) + u, static_cast<double>(tileY) + v, dim);
                            wgs.z = static_cast<double>(adjPatch->elevData[srcRow * (NUM_SUBDIVISIONS_PER_PATCH + 1) + srcCol]) * 1;

                            VectorD ecef = wgs.toECEFfromWGS84() * MarsScale;

                            // transform based on reference
                            double in[4] = { ecef.x, ecef.y, ecef.z, 1.0 };
                            double out[4];
                            transformVector4DThrough4x4Matrix(in, out, referenceInv.getPtr());

                            // write back into a VectorD
                            VectorD pos(out[0], out[1], out[2]);

                            vert->pos = pos.toVecS();
                        }

                        dataChanged = true;
                        patch->fixedGaps[i] = true;
                    }
                }
            }

            if (dataChanged) {
                // post data to OpenGL
                glBindBuffer(GL_ARRAY_BUFFER, buffer->vertexBuffer);
                glBufferSubData(GL_ARRAY_BUFFER, baseVertIndex * sizeof(GLVertex), NUM_VERTS_PER_PATCH * sizeof(GLVertex), &buffer->vertexData[baseVertIndex]);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }
    }
}*/