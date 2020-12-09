#include "MGLMars.h"

#include <random>
#include <string>

#include "Camera.h"
#include "GLSLShaderDefaultGL32.h"
#include "Utils.h"

using namespace Aftr;

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

void MGLMars::init()
{
    // create and add skin
    ModelMeshSkin skin;
    skin.setGLPrimType(GL_TRIANGLES);
    skin.setMeshShadingType(MESH_SHADING_TYPE::mstFLAT);
    skin.setShader(static_cast<GLSLShader*>(ManagerShader::getDefaultShaderCopy()));
    MGL::addSkin(std::move(skin));

    // set default texture that is generally the color of Mars's surface
    GLubyte color[4] = {0x90, 0x69, 0x61, 0x00};
    getSkin().getMultiTextureSet().at(0) = ManagerTexture::loadDynamicTexture(GL_TEXTURE_2D, 0, 1, 1, GL_RGB, 0, GL_RGB, GL_UNSIGNED_BYTE, color);

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

    // spawn background threads that handle async elevation + imagery fetching
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
    VectorD camMars2000 = toMars2000FromCartesian(v, marsScale);
    uint32_t patchIndex = getPatchIndexFromMars2000(camMars2000);
    uint32_t patchX = patchIndex % 360;
    uint32_t patchY = patchIndex / 360;

    visiblePatches.clear(); // clear visible patches, we must recalculate them

    // add patches going outward from the center patch
    for (int32_t r = 0; r <= PATCH_RENDER_RADIUS; ++r) {
        for (int32_t y = -r; y <= r; ++y) {
            for (int32_t x = -r; x <= r; ++x) {
                if ((y == -r || y == r) || (x == -r || x == r)) {
                    // get patch index
                    uint32_t index = getNeighborPatchIndex(patchX, patchY, x, y);
                    std::shared_ptr<Patch> patch = createUpdateGetPatch(index);
                    visiblePatches.insert(patch);
                }
            }
        }
    }
}

uint32_t MGLMars::getNeighborPatchIndex(uint32_t x, uint32_t y, int32_t dx, int32_t dy)
{
    uint32_t patchX;
    if (dx < 0 && static_cast<uint32_t>(-dx) > x) { // underflow
        uint32_t wrap = static_cast<uint32_t>(-dx) - x;
        patchX = 360 - wrap;
    } else if (dx > 0 && static_cast<uint32_t>(dx) > (360 - x - 1)) { // overflow
        uint32_t wrap = static_cast<uint32_t>(dx) - (360 - x);
        patchX = wrap;
    } else {
        patchX = x + dx;
    }

    uint32_t patchY;
    if (dy < 0 && static_cast<uint32_t>(-dy) > y) { // underflow
        patchY = 0;
    } else if (dy > 0 && static_cast<uint32_t>(dy) > (360 - y - 1)) { // overflow
        patchY = 360 - 1;
    } else {
        patchY = y + dy;
    }

    return patchX + patchY * 360;
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

    // create OpenGL texture if the data has been loaded
    if (patch->texture == nullptr && patch->imgReady.load()) {
        GLuint texID;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        // use tightly packed data
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, PATCH_RESOLUTION, PATCH_RESOLUTION,
            0, GL_RGB, GL_UNSIGNED_BYTE, &patch->imgData[0]);
        glGenerateMipmap(GL_TEXTURE_2D);

        // reset to default
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        // generate CPU side texture data
        TextureDataOwnsGLHandle* tex = new TextureDataOwnsGLHandle("DynamicTexture");
        tex->isMipmapped(true);
        tex->setTextureDimensionality(GL_TEXTURE_2D);
        tex->setGLInternalFormat(GL_RGB);
        tex->setGLRawTexelFormat(GL_RGB);
        tex->setGLRawTexelType(GL_UNSIGNED_BYTE);
        tex->setTextureDimensions(PATCH_RESOLUTION, PATCH_RESOLUTION);
        tex->setGLTex(texID);

        patch->texture = new TextureOwnsTexDataOwnsGLHandle(tex);
        patch->texture->setWrapS(GL_CLAMP_TO_EDGE);
        patch->texture->setWrapT(GL_CLAMP_TO_EDGE);
    }

    // apply elevation offsets if the data has been loaded
    if (!patch->elevLoaded && patch->elevReady.load()) {
        uint32_t patchX = index % 360;
        uint32_t patchY = index / 360;

        uint32_t nextIndex = (patchX + 1) + (patchY + 1) * 360;

        VectorD ul = getMars2000FromPatchIndex(index);
        VectorD lr = getMars2000FromPatchIndex(nextIndex);

        std::shared_ptr<PatchArray> array = patchArrays.at(patch->arrayGroup);
        GLVertex* vertPtr = array->getPatchVertexStart(patch->arrayIndex);
        for (GLuint y = 0; y < PATCH_RESOLUTION; ++y) {
            // calculate the lattitude at this subdivison level
            double v = static_cast<double>(y) / (PATCH_RESOLUTION - 1);
            double lat = ul.x + (lr.x - ul.x) * v;

            for (GLuint x = 0; x < PATCH_RESOLUTION; ++x) {
                // calculate the longitude at this subdivision level
                double u = static_cast<double>(x) / (PATCH_RESOLUTION - 1);
                double lon = ul.y + (lr.y - ul.y) * u;

                // combine lat and lon into spherical coordinate
                VectorD mars2000 = VectorD(lat, lon, 0);
                mars2000.z = static_cast<double>(patch->elevData[x + y * PATCH_RESOLUTION]);
                VectorD cart = toCartesianFromMars2000(mars2000, marsScale);

                // transform based on reference
                double in[4] = { cart.x, cart.y, cart.z, 1.0 };
                double out[4];
                transformVector4DThrough4x4Matrix(in, out, referenceInv.getPtr());

                // write back into a VectorD
                VectorD pos(out[0], out[1], out[2]);

                vertPtr->pos = pos.toVecS();

                vertPtr++; // advance pointer
            }
        }

        // post data to OpenGL
        array->uploadVertexSegment(patch->arrayIndex, 1);
        patch->elevLoaded = true;
    }

    return patch;
}

std::shared_ptr<Patch> MGLMars::generatePatch(uint32_t index)
{
    uint32_t patchX = index % 360;
    uint32_t patchY = index / 360;

    uint32_t nextIndex = (patchX + 1) + (patchY + 1) * 360;

    VectorD ul = getMars2000FromPatchIndex(index);
    VectorD lr = getMars2000FromPatchIndex(nextIndex);

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

    // generate patch vertices and tex coords
    GLVertex* vertPtr = array->getPatchVertexStart(patch->arrayIndex);
    for (GLuint y = 0; y < PATCH_RESOLUTION; ++y) {
        // calculate the lattitude at this subdivison level
        double v = static_cast<double>(y) / (PATCH_RESOLUTION - 1);
        double lat = ul.x + (lr.x - ul.x) * v;

        for (GLuint x = 0; x < PATCH_RESOLUTION; ++x) {
            // calculate the longitude at this subdivision level
            double u = static_cast<double>(x) / (PATCH_RESOLUTION - 1);
            double lon = ul.y + (lr.y - ul.y) * u;

            // combine lat and lon into mars2000 coordinate
            VectorD mars2000 = VectorD(lat, lon, 0.0);
            VectorD cart = toCartesianFromMars2000(mars2000, marsScale);

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

    asyncPatchesToLoad.push(patch.get());

    return patch;
}
