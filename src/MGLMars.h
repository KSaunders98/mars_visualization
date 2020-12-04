#pragma once

#include <array>
#include <map>
#include <set>
#include <thread>

#include "boost/lockfree/queue.hpp"

#include "MGL.h"

namespace Aftr {
    static const GLuint NUM_PATCHES_PER_BUFFER = 10;
    static const GLuint PATCH_RESOLUTION = 256;

    // derived values
    static const GLuint NUM_VERTS_PER_PATCH = PATCH_RESOLUTION * PATCH_RESOLUTION;
    static const GLuint NUM_TRIS_PER_PATCH = (PATCH_RESOLUTION - 1) * (PATCH_RESOLUTION - 1) * 2;

    struct GLVertex {
        Vector pos;
        Vector norm;
        aftrTexture4f texCoord;
    };

    template <GLuint CAPACITY>
    struct GLPatchArray {
        GLuint size; // size in number of patches
        const GLuint capacity = CAPACITY;

        GLVertex* vertexData;
        GLuint* indexData;

        GLuint vertexBuffer;
        GLuint indexBuffer;

        GLPatchArray()
        {
            size = 0;

            const GLuint num_verts = CAPACITY * NUM_VERTS_PER_PATCH;
            const GLuint num_indices = CAPACITY * NUM_TRIS_PER_PATCH * 3;
            vertexData = new GLVertex[num_verts];
            indexData = new GLuint[num_indices];

            // generate buffers
            glGenBuffers(1, &vertexBuffer);
            glGenBuffers(1, &indexBuffer);

            // bind them
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

            // allocate their data
            glBufferData(GL_ARRAY_BUFFER, num_verts * sizeof(GLVertex), nullptr, GL_DYNAMIC_DRAW);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_indices * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

            // unbind them
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        ~GLPatchArray()
        {
            delete[] vertexData;
            delete[] indexData;
            vertexData = nullptr;
            indexData = nullptr;

            glDeleteBuffers(1, &vertexBuffer);
            glDeleteBuffers(1, &indexBuffer);
        }

        GLuint getPatchVertexStartIndex(GLuint index)
        {
            assert(index < size);
            return index * NUM_VERTS_PER_PATCH;
        }

        GLVertex* getPatchVertexStart(GLuint index)
        {
            assert(index < size);
            return vertexData + index * NUM_VERTS_PER_PATCH;
        }

        GLuint getPatchIndexStartIndex(GLuint index)
        {
            assert(index < size);
            return index * NUM_TRIS_PER_PATCH * 3;
        }

        GLuint* getPatchIndexStart(GLuint index)
        {
            assert(index < size);
            return indexData + index * NUM_TRIS_PER_PATCH * 3;
        }

        void uploadVertexSegment(GLuint start, GLuint len)
        {
            assert(start < size);
            assert(len > 0 && start + len <= size);

            const GLuint baseIndex = start * NUM_VERTS_PER_PATCH;
            const GLuint baseIndexByte = baseIndex * sizeof(GLVertex);
            const GLuint numBytes = len * NUM_VERTS_PER_PATCH * sizeof(GLVertex);
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glBufferSubData(GL_ARRAY_BUFFER, baseIndexByte, numBytes, vertexData + baseIndex);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        void uploadIndexSegment(GLuint start, GLuint len)
        {
            assert(start < size);
            assert(len > 0 && start + len <= size);

            const GLuint baseIndex = start * NUM_TRIS_PER_PATCH * 3;
            const GLuint baseIndexByte = baseIndex * sizeof(GLuint);
            const GLuint numBytes = len * NUM_TRIS_PER_PATCH * 3 * sizeof(GLuint);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, baseIndexByte, numBytes, indexData + baseIndex);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    };

    // essentially a pointer to a patch (with pointers initialized to invalid)
    struct Patch {
        uint32_t id = std::numeric_limits<uint32_t>::max();

        size_t arrayGroup = std::numeric_limits<size_t>::max();
        GLuint arrayIndex = std::numeric_limits<GLuint>::max();

        Texture* texture = nullptr;

        bool elevLoaded = false;
        bool imgLoaded = false;
        std::vector<int16_t> elevData;
        std::vector<GLubyte> imgData;
        std::atomic<bool> elevReady = false;
        std::atomic<bool> imgReady = false;
        std::array<bool, 8> fixedGaps;
    };

    struct PatchComparator {
        bool operator()(const std::shared_ptr<Patch>& a, const std::shared_ptr<Patch>& b) const
        {
            return (a->id) < (b->id);
        }
    };

    class MGLMars : public MGL {
    public:
        MGLMars(WO* parentWO, double scale, const Mat4D& refMat);
        ~MGLMars();

        void init();

        void render(const Camera& cam) override;
        void renderSelection(const Camera& cam, GLubyte red, GLubyte green, GLubyte blue) override;

        void update(const Camera& cam);

    protected:
        double marsScale;
        Mat4D reference;
        Mat4D referenceInv;
        std::vector<std::thread> asyncThreads;
        std::atomic<bool> shutdownMsg;
        boost::lockfree::queue<Patch*> asyncPatchesToLoad;

        typedef GLPatchArray<NUM_PATCHES_PER_BUFFER> PatchArray;
        std::map<uint32_t, std::shared_ptr<Patch>> patches;
        std::set<std::shared_ptr<Patch>, PatchComparator> visiblePatches;
        std::vector<std::shared_ptr<PatchArray>> patchArrays;

        GLuint vao;

        //static uint64_t getNeighborPatchIndex(uint64_t x, uint64_t y, int64_t dx, int64_t dy, uint64_t dim);
        static uint32_t getPatchIndexFromSpherical(const VectorD& p);
        static VectorD getSphericalFromPatchIndex(uint32_t index);
        static bool loadElevation(uint32_t index, std::vector<int16_t>& data);
        static bool loadImagery(uint32_t index, std::vector<GLubyte>& data);

        VectorD getRelativeToCenter(const VectorD& p) const;
        std::shared_ptr<Patch> getPatch(uint32_t index);
        std::shared_ptr<Patch> createUpdateGetPatch(uint32_t index);
        std::shared_ptr<Patch> generatePatch(uint32_t index);
        void fixGaps();
    };
}