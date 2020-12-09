#pragma once

#include <array>
#include <map>
#include <set>
#include <thread>

#include "boost/lockfree/queue.hpp"

#include "AftrOpenGLIncludes.h"
#include "MGL.h"

#include "Constants.h"
#include "GLPatchArray.h"

namespace Aftr {
    // essentially a pointer to a patch (with pointers initialized to invalid)
    struct Patch {
        uint32_t id = std::numeric_limits<uint32_t>::max();

        size_t arrayGroup = std::numeric_limits<size_t>::max();
        GLuint arrayIndex = std::numeric_limits<GLuint>::max();

        Texture* texture = nullptr;

        bool elevLoaded = false;
        std::vector<int16_t> elevData;
        std::atomic<bool> elevReady = false;
        std::vector<GLubyte> imgData;
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

        static uint32_t getNeighborPatchIndex(uint32_t x, uint32_t y, int32_t dx, int32_t dy);

        VectorD getRelativeToCenter(const VectorD& p) const;
        std::shared_ptr<Patch> getPatch(uint32_t index);
        std::shared_ptr<Patch> createUpdateGetPatch(uint32_t index);
        std::shared_ptr<Patch> generatePatch(uint32_t index);
    };
}