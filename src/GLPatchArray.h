#pragma once

#include "AftrOpenGLIncludes.h"
#include "Vector.h"

#include "Constants.h"

namespace Aftr {
    // derived constants
    constexpr GLuint NUM_VERTS_PER_PATCH = PATCH_RESOLUTION * PATCH_RESOLUTION;
    constexpr GLuint NUM_TRIS_PER_PATCH = (PATCH_RESOLUTION - 1) * (PATCH_RESOLUTION - 1) * 2;

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
};
