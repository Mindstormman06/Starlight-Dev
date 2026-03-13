#include <file/game/phive/starlight_physics/ai/hkaiNavMeshGeometryGenerator.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <meshoptimizer.h>
#include <util/Logger.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr float DEG_TO_RAD = 0.01745329251994329576923690768489f;
    constexpr uint16_t INVALID_FACE_INDEX = 0xFFFFu;

    struct QuantizedVertexKey
    {
        uint16_t mX = 0;
        uint16_t mY = 0;
        uint16_t mZ = 0;

        bool operator==(const QuantizedVertexKey& other) const
        {
            return mX == other.mX && mY == other.mY && mZ == other.mZ;
        }
    };

    struct QuantizedVertexKeyHash
    {
        size_t operator()(const QuantizedVertexKey& key) const
        {
            const uint64_t h0 = static_cast<uint64_t>(key.mX) * 0x9E3779B185EBCA87ull;
            const uint64_t h1 = static_cast<uint64_t>(key.mY) * 0xC2B2AE3D27D4EB4Full;
            const uint64_t h2 = static_cast<uint64_t>(key.mZ) * 0x165667B19E3779F9ull;
            return static_cast<size_t>(h0 ^ h1 ^ h2);
        }
    };

    struct TriangleKey
    {
        uint32_t mA = 0;
        uint32_t mB = 0;
        uint32_t mC = 0;

        bool operator==(const TriangleKey& other) const
        {
            return mA == other.mA && mB == other.mB && mC == other.mC;
        }
    };

    struct TriangleKeyHash
    {
        size_t operator()(const TriangleKey& key) const
        {
            const uint64_t h0 = static_cast<uint64_t>(key.mA) * 0x9E3779B185EBCA87ull;
            const uint64_t h1 = static_cast<uint64_t>(key.mB) * 0xC2B2AE3D27D4EB4Full;
            const uint64_t h2 = static_cast<uint64_t>(key.mC) * 0x165667B19E3779F9ull;
            return static_cast<size_t>(h0 ^ h1 ^ h2);
        }
    };

    struct UndirectedEdgeKey
    {
        uint32_t mMin = 0;
        uint32_t mMax = 0;

        bool operator==(const UndirectedEdgeKey& other) const
        {
            return mMin == other.mMin && mMax == other.mMax;
        }
    };

    struct UndirectedEdgeKeyHash
    {
        size_t operator()(const UndirectedEdgeKey& key) const
        {
            const uint64_t h0 = static_cast<uint64_t>(key.mMin) * 0x9E3779B185EBCA87ull;
            const uint64_t h1 = static_cast<uint64_t>(key.mMax) * 0xC2B2AE3D27D4EB4Full;
            return static_cast<size_t>(h0 ^ h1);
        }
    };

    struct EdgeRef
    {
        uint32_t mFace = 0;
        uint8_t mLocalEdge = 0;
        uint32_t mA = 0;
        uint32_t mB = 0;
    };

    enum class QuantizationFailureReason : uint8_t
    {
        None = 0,
        EmptyRaw,
        SpanOverflow,
        VertexOverflow,
        FaceOverflow,
        Collapsed
    };

    struct VoxelCoord
    {
        int mX = 0;
        int mY = 0;
        int mZ = 0;
    };

    inline bool IsFiniteVec3(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    inline float ClampToRange(float value, float minValue, float maxValue)
    {
        return std::max(minValue, std::min(value, maxValue));
    }

    inline float SnapToBoundary(float value, float minValue, float maxValue, float tolerance)
    {
        if (std::fabs(value - minValue) <= tolerance)
        {
            return minValue;
        }

        if (std::fabs(value - maxValue) <= tolerance)
        {
            return maxValue;
        }

        return value;
    }

    inline uint16_t ClampToU16(int32_t value)
    {
        if (value <= 0)
        {
            return 0;
        }

        if (value >= 0xFFFF)
        {
            return 0xFFFF;
        }

        return static_cast<uint16_t>(value);
    }

    inline TriangleKey MakeTriangleKey(uint32_t a, uint32_t b, uint32_t c)
    {
        uint32_t sorted[3] = { a, b, c };
        std::sort(sorted, sorted + 3);
        return TriangleKey{ sorted[0], sorted[1], sorted[2] };
    }

    inline UndirectedEdgeKey MakeUndirectedEdgeKey(uint32_t a, uint32_t b)
    {
        return (a < b) ? UndirectedEdgeKey{ a, b } : UndirectedEdgeKey{ b, a };
    }

    inline uint64_t MakeVoxelKey(int x, int y, int z)
    {
        const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x) & 0x1FFFFFu);
        const uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(y) & 0x1FFFFFu);
        const uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z) & 0x1FFFFFu);
        return (ux << 42) | (uy << 21) | uz;
    }

    float DistancePointTriangleSquared(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 ab = b - a;
        const glm::vec3 ac = c - a;
        const glm::vec3 ap = p - a;

        const float d1 = glm::dot(ab, ap);
        const float d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
        {
            return glm::dot(ap, ap);
        }

        const glm::vec3 bp = p - b;
        const float d3 = glm::dot(ab, bp);
        const float d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3)
        {
            return glm::dot(bp, bp);
        }

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / (d1 - d3);
            const glm::vec3 proj = a + v * ab;
            const glm::vec3 diff = p - proj;
            return glm::dot(diff, diff);
        }

        const glm::vec3 cp = p - c;
        const float d5 = glm::dot(ab, cp);
        const float d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6)
        {
            return glm::dot(cp, cp);
        }

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / (d2 - d6);
            const glm::vec3 proj = a + w * ac;
            const glm::vec3 diff = p - proj;
            return glm::dot(diff, diff);
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            const glm::vec3 proj = b + w * (c - b);
            const glm::vec3 diff = p - proj;
            return glm::dot(diff, diff);
        }

        const glm::vec3 n = glm::cross(ab, ac);
        const float nLenSq = glm::dot(n, n);
        if (nLenSq <= 1e-12f)
        {
            return glm::dot(ap, ap);
        }

        const float dist = glm::dot(ap, n) / std::sqrt(nLenSq);
        return dist * dist;
    }

    void RemoveDegenerateAndDuplicateTriangles(
        const std::vector<glm::vec3>& vertices,
        std::vector<uint32_t>& indicesInOut)
    {
        if (indicesInOut.size() < 3 || vertices.empty())
        {
            indicesInOut.clear();
            return;
        }

        std::vector<uint32_t> filtered;
        filtered.reserve(indicesInOut.size());

        std::unordered_set<TriangleKey, TriangleKeyHash> seen;
        seen.reserve(indicesInOut.size() / 3);

        for (size_t i = 0; i + 2 < indicesInOut.size(); i += 3)
        {
            const uint32_t a = indicesInOut[i + 0];
            const uint32_t b = indicesInOut[i + 1];
            const uint32_t c = indicesInOut[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
            {
                continue;
            }

            if (a == b || b == c || c == a)
            {
                continue;
            }

            const glm::vec3& va = vertices[a];
            const glm::vec3& vb = vertices[b];
            const glm::vec3& vc = vertices[c];
            const glm::vec3 cross = glm::cross(vb - va, vc - va);
            if (glm::length(cross) <= 1e-9f)
            {
                continue;
            }

            const TriangleKey key = MakeTriangleKey(a, b, c);
            if (!seen.insert(key).second)
            {
                continue;
            }

            filtered.push_back(a);
            filtered.push_back(b);
            filtered.push_back(c);
        }

        indicesInOut.swap(filtered);
    }

    bool WeldRawMeshExact(
        std::vector<glm::vec3>& verticesInOut,
        std::vector<uint32_t>& indicesInOut)
    {
        if (verticesInOut.empty() || indicesInOut.empty())
        {
            return false;
        }

        std::vector<uint32_t> remap(verticesInOut.size());
        const size_t uniqueVertexCount = meshopt_generateVertexRemap(
            remap.data(),
            indicesInOut.data(),
            indicesInOut.size(),
            verticesInOut.data(),
            verticesInOut.size(),
            sizeof(glm::vec3));

        if (uniqueVertexCount == 0)
        {
            return false;
        }

        std::vector<glm::vec3> weldedVertices(uniqueVertexCount);
        std::vector<uint32_t> weldedIndices(indicesInOut.size());

        meshopt_remapVertexBuffer(
            weldedVertices.data(),
            verticesInOut.data(),
            verticesInOut.size(),
            sizeof(glm::vec3),
            remap.data());
        meshopt_remapIndexBuffer(
            weldedIndices.data(),
            indicesInOut.data(),
            indicesInOut.size(),
            remap.data());

        verticesInOut.swap(weldedVertices);
        indicesInOut.swap(weldedIndices);
        RemoveDegenerateAndDuplicateTriangles(verticesInOut, indicesInOut);
        return !verticesInOut.empty() && !indicesInOut.empty();
    }

    bool SimplifyRawMeshByRatioWithOptions(
        std::vector<glm::vec3>& verticesInOut,
        std::vector<uint32_t>& indicesInOut,
        float targetRatio,
        float targetError,
        unsigned int options)
    {
        if (!WeldRawMeshExact(verticesInOut, indicesInOut))
        {
            return false;
        }

        targetRatio = std::clamp(targetRatio, 0.003f, 1.0f);
        if (targetRatio >= 0.999f || indicesInOut.size() < 6)
        {
            return true;
        }

        const size_t indexCount = indicesInOut.size();
        size_t targetIndexCount = static_cast<size_t>(std::floor(static_cast<double>(indexCount) * static_cast<double>(targetRatio)));
        targetIndexCount = std::max<size_t>(6, (targetIndexCount / 3) * 3);
        targetIndexCount = std::min(targetIndexCount, indexCount);

        if (targetIndexCount >= indexCount)
        {
            return true;
        }

        std::vector<uint32_t> simplified(indexCount);
        float resultError = 0.0f;
        size_t simplifiedCount = meshopt_simplify(
            simplified.data(),
            indicesInOut.data(),
            indexCount,
            &verticesInOut[0].x,
            verticesInOut.size(),
            sizeof(glm::vec3),
            targetIndexCount,
            std::max(1e-4f, targetError),
            options,
            &resultError);

        if (simplifiedCount < 6 || simplifiedCount >= indexCount)
        {
            return true;
        }

        simplifiedCount = (simplifiedCount / 3) * 3;
        simplified.resize(simplifiedCount);
        indicesInOut.swap(simplified);

        std::vector<glm::vec3> fetchedVertices(verticesInOut.size());
        const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
            fetchedVertices.data(),
            indicesInOut.data(),
            indicesInOut.size(),
            verticesInOut.data(),
            verticesInOut.size(),
            sizeof(glm::vec3));

        if (fetchedVertexCount == 0)
        {
            return false;
        }

        fetchedVertices.resize(fetchedVertexCount);
        verticesInOut.swap(fetchedVertices);

        return WeldRawMeshExact(verticesInOut, indicesInOut);
    }

    [[maybe_unused]] bool SimplifyRawMeshByRatioSloppy(
        std::vector<glm::vec3>& verticesInOut,
        std::vector<uint32_t>& indicesInOut,
        float targetRatio,
        float targetError)
    {
        if (!WeldRawMeshExact(verticesInOut, indicesInOut))
        {
            return false;
        }

        targetRatio = std::clamp(targetRatio, 0.003f, 1.0f);
        if (targetRatio >= 0.999f || indicesInOut.size() < 6)
        {
            return true;
        }

        const size_t indexCount = indicesInOut.size();
        size_t targetIndexCount = static_cast<size_t>(std::floor(static_cast<double>(indexCount) * static_cast<double>(targetRatio)));
        targetIndexCount = std::max<size_t>(6, (targetIndexCount / 3) * 3);
        targetIndexCount = std::min(targetIndexCount, indexCount);

        if (targetIndexCount >= indexCount)
        {
            return true;
        }

        std::vector<uint32_t> simplified(indexCount);
        float resultError = 0.0f;
        size_t simplifiedCount = meshopt_simplifySloppy(
            simplified.data(),
            indicesInOut.data(),
            indexCount,
            &verticesInOut[0].x,
            verticesInOut.size(),
            sizeof(glm::vec3),
            targetIndexCount,
            std::max(1e-4f, targetError),
            &resultError);

        if (simplifiedCount < 6 || simplifiedCount >= indexCount)
        {
            return true;
        }

        simplifiedCount = (simplifiedCount / 3) * 3;
        simplified.resize(simplifiedCount);
        indicesInOut.swap(simplified);

        std::vector<glm::vec3> fetchedVertices(verticesInOut.size());
        const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
            fetchedVertices.data(),
            indicesInOut.data(),
            indicesInOut.size(),
            verticesInOut.data(),
            verticesInOut.size(),
            sizeof(glm::vec3));

        if (fetchedVertexCount == 0)
        {
            return false;
        }

        fetchedVertices.resize(fetchedVertexCount);
        verticesInOut.swap(fetchedVertices);

        return WeldRawMeshExact(verticesInOut, indicesInOut);
    }

    bool SimplifyRawMeshToPackedLimits(
        std::vector<glm::vec3>& verticesInOut,
        std::vector<uint32_t>& indicesInOut,
        float baseError)
    {
        constexpr size_t maxPackedVertices = 0xFFFFu;
        constexpr size_t maxPackedFaces = 0xFFFFu;

        auto FitsPackedLimits = [&]()
            {
                return verticesInOut.size() <= maxPackedVertices &&
                    (indicesInOut.size() / 3) <= maxPackedFaces &&
                    !verticesInOut.empty() &&
                    !indicesInOut.empty();
            };

        if (!WeldRawMeshExact(verticesInOut, indicesInOut))
        {
            return false;
        }

        if (FitsPackedLimits())
        {
            return true;
        }

        float targetError = std::max(1e-4f, baseError);
        std::vector<uint32_t> simplified(indicesInOut.size());

        constexpr int maxPasses = 14;
        for (int pass = 0; pass < maxPasses; ++pass)
        {
            const size_t indexCount = indicesInOut.size();
            if (indexCount < 6 || verticesInOut.empty())
            {
                break;
            }

            const double faceScale = std::min(
                1.0,
                static_cast<double>(maxPackedFaces * 3ull) / std::max<size_t>(indexCount, 1));
            const double vertexScale = std::min(
                1.0,
                static_cast<double>(maxPackedVertices) / std::max<size_t>(verticesInOut.size(), 1));
            const double requestedScale = std::min(faceScale, vertexScale) * 0.88;
            const double clampedScale = std::clamp(requestedScale, 0.05, 0.95);

            size_t targetIndexCount = static_cast<size_t>(std::floor(static_cast<double>(indexCount) * clampedScale));
            targetIndexCount = std::max<size_t>(6, (targetIndexCount / 3) * 3);
            if (targetIndexCount >= indexCount)
            {
                targetIndexCount = std::max<size_t>(6, ((indexCount * 7) / 10 / 3) * 3);
            }

            float resultError = 0.0f;
            size_t simplifiedCount = meshopt_simplify(
                simplified.data(),
                indicesInOut.data(),
                indexCount,
                &verticesInOut[0].x,
                verticesInOut.size(),
                sizeof(glm::vec3),
                targetIndexCount,
                targetError,
                meshopt_SimplifyLockBorder,
                &resultError);

            if (simplifiedCount < 6 || simplifiedCount > indexCount || simplifiedCount == indexCount)
            {
                targetError = std::min(100.0f, targetError * 2.0f + 0.001f);
                continue;
            }

            simplifiedCount = (simplifiedCount / 3) * 3;
            simplified.resize(simplifiedCount);
            indicesInOut.swap(simplified);
            simplified.resize(indexCount);

            std::vector<glm::vec3> fetchedVertices(verticesInOut.size());
            const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
                fetchedVertices.data(),
                indicesInOut.data(),
                indicesInOut.size(),
                verticesInOut.data(),
                verticesInOut.size(),
                sizeof(glm::vec3));

            if (fetchedVertexCount == 0)
            {
                return false;
            }

            fetchedVertices.resize(fetchedVertexCount);
            verticesInOut.swap(fetchedVertices);

            if (!WeldRawMeshExact(verticesInOut, indicesInOut))
            {
                return false;
            }

            if (FitsPackedLimits())
            {
                return true;
            }

            targetError = std::min(100.0f, std::max(targetError * 1.6f, resultError * 1.5f + 1e-4f));
        }

        return FitsPackedLimits();
    }

    void SnapVerticesToBounds(
        std::vector<glm::vec3>& verticesInOut,
        float minX,
        float maxX,
        float minZ,
        float maxZ,
        float tolerance)
    {
        if (verticesInOut.empty())
        {
            return;
        }

        const float tol = std::max(0.0f, tolerance);
        for (glm::vec3& v : verticesInOut)
        {
            if (std::fabs(v.x - minX) <= tol)
            {
                v.x = minX;
            }
            else if (std::fabs(v.x - maxX) <= tol)
            {
                v.x = maxX;
            }

            if (std::fabs(v.z - minZ) <= tol)
            {
                v.z = minZ;
            }
            else if (std::fabs(v.z - maxZ) <= tol)
            {
                v.z = maxZ;
            }
        }
    }

    void SmoothRawMeshLaplacian(
        std::vector<glm::vec3>& verticesInOut,
        const std::vector<uint32_t>& indices,
        int iterations,
        float alpha,
        const std::vector<uint8_t>* fixedMask)
    {
        if (verticesInOut.empty() || indices.size() < 3 || iterations <= 0)
        {
            return;
        }

        const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
        if (clampedAlpha <= 0.0f)
        {
            return;
        }

        std::vector<std::vector<uint32_t>> neighbors(verticesInOut.size());
        neighbors.reserve(verticesInOut.size());

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t a = indices[i + 0];
            const uint32_t b = indices[i + 1];
            const uint32_t c = indices[i + 2];
            if (a >= verticesInOut.size() || b >= verticesInOut.size() || c >= verticesInOut.size())
            {
                continue;
            }

            neighbors[a].push_back(b);
            neighbors[a].push_back(c);
            neighbors[b].push_back(a);
            neighbors[b].push_back(c);
            neighbors[c].push_back(a);
            neighbors[c].push_back(b);
        }

        for (auto& list : neighbors)
        {
            if (list.size() <= 1)
            {
                continue;
            }
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }

        std::vector<glm::vec3> next(verticesInOut.size());
        for (int iter = 0; iter < iterations; ++iter)
        {
            next = verticesInOut;
            for (size_t i = 0; i < verticesInOut.size(); ++i)
            {
                if (fixedMask != nullptr && i < fixedMask->size() && (*fixedMask)[i])
                {
                    continue;
                }

                const auto& list = neighbors[i];
                if (list.empty())
                {
                    continue;
                }

                glm::vec3 sum(0.0f);
                for (uint32_t n : list)
                {
                    sum += verticesInOut[n];
                }

                const float invCount = 1.0f / static_cast<float>(list.size());
                const glm::vec3 avg = sum * invCount;
                next[i] = verticesInOut[i] + clampedAlpha * (avg - verticesInOut[i]);
            }

            verticesInOut.swap(next);
        }
    }

    float TriangleAreaSquared(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 cross = glm::cross(b - a, c - a);
        return 0.25f * glm::dot(cross, cross);
    }

    float MaxTriangleAreaSquared(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices)
    {
        float maxAreaSq = 0.0f;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t a = indices[i + 0];
            const uint32_t b = indices[i + 1];
            const uint32_t c = indices[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
            {
                continue;
            }

            const float areaSq = TriangleAreaSquared(vertices[a], vertices[b], vertices[c]);
            if (areaSq > maxAreaSq)
            {
                maxAreaSq = areaSq;
            }
        }
        return maxAreaSq;
    }

    bool CullSmallTrianglesAndBatches(
        std::vector<glm::vec3>& verticesInOut,
        std::vector<uint32_t>& indicesInOut,
        float minTriangleArea,
        float minBatchExtent,
        float minX,
        float maxX,
        float minZ,
        float maxZ,
        float borderBandWorld)
    {
        if (verticesInOut.empty() || indicesInOut.size() < 3)
        {
            return false;
        }

        const float minAreaSq = std::max(0.0f, minTriangleArea * minTriangleArea);
        const float minExtent = std::max(0.0f, minBatchExtent);
        const float borderBand = std::max(0.0f, borderBandWorld);
        auto IsNearBorder = [&](const glm::vec3& v) -> bool
            {
                return v.x <= minX + borderBand || v.x >= maxX - borderBand ||
                    v.z <= minZ + borderBand || v.z >= maxZ - borderBand;
            };

        const size_t triCount = indicesInOut.size() / 3;
        std::vector<uint8_t> keep(triCount, 1u);

        if (minAreaSq > 0.0f)
        {
            for (size_t t = 0; t < triCount; ++t)
            {
                const uint32_t a = indicesInOut[t * 3 + 0];
                const uint32_t b = indicesInOut[t * 3 + 1];
                const uint32_t c = indicesInOut[t * 3 + 2];
                if (a >= verticesInOut.size() || b >= verticesInOut.size() || c >= verticesInOut.size())
                {
                    keep[t] = 0u;
                    continue;
                }

                const glm::vec3& va = verticesInOut[a];
                const glm::vec3& vb = verticesInOut[b];
                const glm::vec3& vc = verticesInOut[c];

                if (IsNearBorder(va) || IsNearBorder(vb) || IsNearBorder(vc))
                {
                    continue;
                }

                if (TriangleAreaSquared(va, vb, vc) < minAreaSq)
                {
                    keep[t] = 0u;
                }
            }
        }

        std::vector<uint32_t> parent(triCount);
        std::vector<uint32_t> rank(triCount, 0u);
        for (size_t i = 0; i < triCount; ++i)
        {
            parent[i] = static_cast<uint32_t>(i);
        }

        auto Find = [&](uint32_t x)
            {
                uint32_t root = x;
                while (parent[root] != root)
                {
                    root = parent[root];
                }
                while (parent[x] != x)
                {
                    const uint32_t next = parent[x];
                    parent[x] = root;
                    x = next;
                }
                return root;
            };

        auto Union = [&](uint32_t a, uint32_t b)
            {
                uint32_t ra = Find(a);
                uint32_t rb = Find(b);
                if (ra == rb)
                {
                    return;
                }
                if (rank[ra] < rank[rb])
                {
                    parent[ra] = rb;
                }
                else if (rank[ra] > rank[rb])
                {
                    parent[rb] = ra;
                }
                else
                {
                    parent[rb] = ra;
                    ++rank[ra];
                }
            };

        std::unordered_map<UndirectedEdgeKey, uint32_t, UndirectedEdgeKeyHash> edgeOwner;
        edgeOwner.reserve(triCount * 2 + 1);

        for (uint32_t t = 0; t < triCount; ++t)
        {
            if (!keep[t])
            {
                continue;
            }

            const uint32_t a = indicesInOut[t * 3 + 0];
            const uint32_t b = indicesInOut[t * 3 + 1];
            const uint32_t c = indicesInOut[t * 3 + 2];
            const UndirectedEdgeKey edges[3] = {
                MakeUndirectedEdgeKey(a, b),
                MakeUndirectedEdgeKey(b, c),
                MakeUndirectedEdgeKey(c, a)
            };

            for (const UndirectedEdgeKey& edge : edges)
            {
                auto [it, inserted] = edgeOwner.emplace(edge, t);
                if (!inserted)
                {
                    Union(t, it->second);
                }
            }
        }

        struct Aabb
        {
            glm::vec3 mMin = glm::vec3(0.0f);
            glm::vec3 mMax = glm::vec3(0.0f);
            bool mValid = false;
        };

        std::vector<Aabb> aabbs(triCount);
        for (uint32_t t = 0; t < triCount; ++t)
        {
            if (!keep[t])
            {
                continue;
            }

            const uint32_t a = indicesInOut[t * 3 + 0];
            const uint32_t b = indicesInOut[t * 3 + 1];
            const uint32_t c = indicesInOut[t * 3 + 2];
            if (a >= verticesInOut.size() || b >= verticesInOut.size() || c >= verticesInOut.size())
            {
                continue;
            }

            const uint32_t root = Find(t);
            Aabb& box = aabbs[root];
            const glm::vec3& va = verticesInOut[a];
            const glm::vec3& vb = verticesInOut[b];
            const glm::vec3& vc = verticesInOut[c];
            if (!box.mValid)
            {
                box.mMin = va;
                box.mMax = va;
                box.mValid = true;
            }

            auto expand = [&](const glm::vec3& v)
                {
                    box.mMin.x = std::min(box.mMin.x, v.x);
                    box.mMin.y = std::min(box.mMin.y, v.y);
                    box.mMin.z = std::min(box.mMin.z, v.z);
                    box.mMax.x = std::max(box.mMax.x, v.x);
                    box.mMax.y = std::max(box.mMax.y, v.y);
                    box.mMax.z = std::max(box.mMax.z, v.z);
                };

            expand(vb);
            expand(vc);
        }

        std::vector<uint8_t> keepComponent(triCount, 0u);
        if (minExtent <= 0.0f)
        {
            for (size_t i = 0; i < triCount; ++i)
            {
                if (aabbs[i].mValid)
                {
                    keepComponent[i] = 1u;
                }
            }
        }
        else
        {
            for (size_t i = 0; i < triCount; ++i)
            {
                if (!aabbs[i].mValid)
                {
                    continue;
                }

                const float extentX = aabbs[i].mMax.x - aabbs[i].mMin.x;
                const float extentZ = aabbs[i].mMax.z - aabbs[i].mMin.z;
                if (extentX >= minExtent || extentZ >= minExtent)
                {
                    keepComponent[i] = 1u;
                }
            }
        }

        std::vector<uint32_t> filtered;
        filtered.reserve(indicesInOut.size());

        for (uint32_t t = 0; t < triCount; ++t)
        {
            if (!keep[t])
            {
                continue;
            }

            const uint32_t root = Find(t);
            if (!keepComponent[root])
            {
                continue;
            }

            filtered.push_back(indicesInOut[t * 3 + 0]);
            filtered.push_back(indicesInOut[t * 3 + 1]);
            filtered.push_back(indicesInOut[t * 3 + 2]);
        }

        if (filtered.empty())
        {
            verticesInOut.clear();
            indicesInOut.clear();
            return false;
        }

        indicesInOut.swap(filtered);
        WeldRawMeshExact(verticesInOut, indicesInOut);
        return !verticesInOut.empty() && !indicesInOut.empty();
    }

    bool SimplifyInteriorRegion(
        std::vector<glm::vec3>& verticesInOut,
        std::vector<uint32_t>& indicesInOut,
        float minX,
        float maxX,
        float minZ,
        float maxZ,
        float borderBandWorld,
        float targetRatio,
        float targetError,
        float maxTriangleArea)
    {
        if (verticesInOut.empty() || indicesInOut.size() < 3)
        {
            return false;
        }

        const float clampedBand = std::max(0.0f, borderBandWorld);
        auto IsNearBorder = [&](const glm::vec3& v) -> bool
            {
                return v.x <= minX + clampedBand || v.x >= maxX - clampedBand ||
                    v.z <= minZ + clampedBand || v.z >= maxZ - clampedBand;
            };

        std::vector<uint32_t> borderIndices;
        std::vector<uint32_t> interiorIndices;
        borderIndices.reserve(indicesInOut.size());
        interiorIndices.reserve(indicesInOut.size());

        for (size_t i = 0; i + 2 < indicesInOut.size(); i += 3)
        {
            const uint32_t a = indicesInOut[i + 0];
            const uint32_t b = indicesInOut[i + 1];
            const uint32_t c = indicesInOut[i + 2];
            if (a >= verticesInOut.size() || b >= verticesInOut.size() || c >= verticesInOut.size())
            {
                continue;
            }

            const bool nearBorder =
                IsNearBorder(verticesInOut[a]) ||
                IsNearBorder(verticesInOut[b]) ||
                IsNearBorder(verticesInOut[c]);

            if (nearBorder)
            {
                borderIndices.push_back(a);
                borderIndices.push_back(b);
                borderIndices.push_back(c);
            }
            else
            {
                interiorIndices.push_back(a);
                interiorIndices.push_back(b);
                interiorIndices.push_back(c);
            }
        }

        if (interiorIndices.empty())
        {
            return true;
        }

        auto BuildSubmesh = [&](const std::vector<uint32_t>& srcIndices,
                                std::vector<glm::vec3>& outVertices,
                                std::vector<uint32_t>& outIndices)
            {
                outVertices.clear();
                outIndices.clear();
                if (srcIndices.empty())
                {
                    return;
                }

                std::vector<uint32_t> remap(verticesInOut.size(), std::numeric_limits<uint32_t>::max());
                outVertices.reserve(srcIndices.size());
                outIndices.reserve(srcIndices.size());

                for (uint32_t idx : srcIndices)
                {
                    if (idx >= verticesInOut.size())
                    {
                        continue;
                    }

                    uint32_t& mapped = remap[idx];
                    if (mapped == std::numeric_limits<uint32_t>::max())
                    {
                        mapped = static_cast<uint32_t>(outVertices.size());
                        outVertices.push_back(verticesInOut[idx]);
                    }

                    outIndices.push_back(mapped);
                }
            };

        std::vector<glm::vec3> borderVertices;
        std::vector<uint32_t> borderIndicesRemapped;
        BuildSubmesh(borderIndices, borderVertices, borderIndicesRemapped);

        std::vector<glm::vec3> interiorVertices;
        std::vector<uint32_t> interiorIndicesRemapped;
        BuildSubmesh(interiorIndices, interiorVertices, interiorIndicesRemapped);

        if (!interiorVertices.empty() && interiorIndicesRemapped.size() >= 6)
        {
            const float areaCap = std::max(0.0f, maxTriangleArea);
            std::vector<glm::vec3> bestVertices = interiorVertices;
            std::vector<uint32_t> bestIndices = interiorIndicesRemapped;
            float ratio = std::clamp(targetRatio, 0.003f, 1.0f);
            float error = std::max(1e-4f, targetError);
            const float areaCapSq = (areaCap > 0.0f) ? (areaCap * areaCap) : 0.0f;

            constexpr int maxAttempts = 4;
            for (int attempt = 0; attempt < maxAttempts; ++attempt)
            {
                std::vector<glm::vec3> simplifiedVertices = interiorVertices;
                std::vector<uint32_t> simplifiedIndices = interiorIndicesRemapped;
                if (!SimplifyRawMeshByRatioWithOptions(
                        simplifiedVertices,
                        simplifiedIndices,
                        ratio,
                        error,
                        meshopt_SimplifyLockBorder))
                {
                    break;
                }

                const float maxAreaSq = MaxTriangleAreaSquared(simplifiedVertices, simplifiedIndices);
                bestVertices.swap(simplifiedVertices);
                bestIndices.swap(simplifiedIndices);
                if (areaCapSq <= 0.0f || maxAreaSq <= areaCapSq)
                {
                    break;
                }

                ratio = std::min(1.0f, ratio * 1.6f + 0.01f);
                error = std::max(1e-4f, error * 0.6f);
            }

            interiorVertices.swap(bestVertices);
            interiorIndicesRemapped.swap(bestIndices);
        }

        std::vector<glm::vec3> mergedVertices;
        std::vector<uint32_t> mergedIndices;
        mergedVertices.reserve(borderVertices.size() + interiorVertices.size());
        mergedIndices.reserve(borderIndicesRemapped.size() + interiorIndicesRemapped.size());

        if (!borderVertices.empty() && !borderIndicesRemapped.empty())
        {
            mergedVertices.insert(mergedVertices.end(), borderVertices.begin(), borderVertices.end());
            mergedIndices.insert(mergedIndices.end(), borderIndicesRemapped.begin(), borderIndicesRemapped.end());
        }

        if (!interiorVertices.empty() && !interiorIndicesRemapped.empty())
        {
            const uint32_t offset = static_cast<uint32_t>(mergedVertices.size());
            mergedVertices.insert(mergedVertices.end(), interiorVertices.begin(), interiorVertices.end());
            mergedIndices.reserve(mergedIndices.size() + interiorIndicesRemapped.size());
            for (uint32_t idx : interiorIndicesRemapped)
            {
                mergedIndices.push_back(idx + offset);
            }
        }

        if (mergedVertices.empty() || mergedIndices.empty())
        {
            return false;
        }

        verticesInOut.swap(mergedVertices);
        indicesInOut.swap(mergedIndices);

        WeldRawMeshExact(verticesInOut, indicesInOut);
        return !verticesInOut.empty() && !indicesInOut.empty();
    }

    bool QuantizeAndBuildOutput(
        const std::vector<glm::vec3>& rawVertices,
        const std::vector<uint32_t>& rawIndices,
        float cs,
        float ch,
        bool useForcedXZBounds,
        float forcedMinX,
        float forcedMaxX,
        float forcedMinZ,
        float forcedMaxZ,
        std::vector<uint16_t>& outVertices,
        std::vector<uint16_t>& outTriangles,
        float outBoundsMin[3],
        float outBoundsMax[3],
        QuantizationFailureReason* failureReasonOut = nullptr,
        size_t* failureValueOut = nullptr)
    {
        if (failureReasonOut != nullptr)
        {
            *failureReasonOut = QuantizationFailureReason::None;
        }
        if (failureValueOut != nullptr)
        {
            *failureValueOut = 0;
        }

        outVertices.clear();
        outTriangles.clear();

        if (rawVertices.empty() || rawIndices.size() < 3)
        {
            if (failureReasonOut != nullptr)
            {
                *failureReasonOut = QuantizationFailureReason::EmptyRaw;
            }
            application::util::Logger::Error(
                "PhiveNavMesh",
                "Quantization failed: empty raw geometry (rawVertices=%zu rawIndices=%zu)",
                rawVertices.size(),
                rawIndices.size());
            return false;
        }

        float minX = rawVertices[0].x;
        float minY = rawVertices[0].y;
        float minZ = rawVertices[0].z;
        float maxX = rawVertices[0].x;
        float maxY = rawVertices[0].y;
        float maxZ = rawVertices[0].z;

        for (const glm::vec3& vertex : rawVertices)
        {
            minX = std::min(minX, vertex.x);
            minY = std::min(minY, vertex.y);
            minZ = std::min(minZ, vertex.z);
            maxX = std::max(maxX, vertex.x);
            maxY = std::max(maxY, vertex.y);
            maxZ = std::max(maxZ, vertex.z);
        }

        if (useForcedXZBounds)
        {
            minX = forcedMinX;
            maxX = forcedMaxX;
            minZ = forcedMinZ;
            maxZ = forcedMaxZ;
        }

        if (maxX - minX < cs)
        {
            maxX = minX + cs;
        }

        if (maxY - minY < ch)
        {
            maxY = minY + ch;
        }

        if (maxZ - minZ < cs)
        {
            maxZ = minZ + cs;
        }

        const float spanX = (maxX - minX) / cs;
        const float spanY = (maxY - minY) / ch;
        const float spanZ = (maxZ - minZ) / cs;
        if (spanX > 65535.0f || spanY > 65535.0f || spanZ > 65535.0f)
        {
            if (failureReasonOut != nullptr)
            {
                *failureReasonOut = QuantizationFailureReason::SpanOverflow;
            }
            application::util::Logger::Error(
                "PhiveNavMesh",
                "Quantization failed: bounds exceed uint16 quantization range (spanX=%.2f spanY=%.2f spanZ=%.2f)",
                spanX,
                spanY,
                spanZ);
            return false;
        }

        outBoundsMin[0] = minX;
        outBoundsMin[1] = minY;
        outBoundsMin[2] = minZ;
        outBoundsMax[0] = maxX;
        outBoundsMax[1] = maxY;
        outBoundsMax[2] = maxZ;

        const float boundarySnapTolerance = std::max(cs * 0.6f, 0.02f);
        const int32_t maxForcedQX = useForcedXZBounds
            ? std::max(0, static_cast<int32_t>(std::floor((forcedMaxX - forcedMinX) / cs + 1e-5f)))
            : std::numeric_limits<int32_t>::max();
        const int32_t maxForcedQZ = useForcedXZBounds
            ? std::max(0, static_cast<int32_t>(std::floor((forcedMaxZ - forcedMinZ) / cs + 1e-5f)))
            : std::numeric_limits<int32_t>::max();

        std::unordered_map<QuantizedVertexKey, uint32_t, QuantizedVertexKeyHash> vertexMap;
        vertexMap.reserve(rawVertices.size());

        std::vector<QuantizedVertexKey> compactVertices;
        compactVertices.reserve(rawVertices.size());
        std::vector<uint32_t> rawToCompact(rawVertices.size(), 0);

        for (uint32_t i = 0; i < rawVertices.size(); ++i)
        {
            glm::vec3 vertex = rawVertices[i];
            if (useForcedXZBounds)
            {
                vertex.x = SnapToBoundary(vertex.x, forcedMinX, forcedMaxX, boundarySnapTolerance);
                vertex.z = SnapToBoundary(vertex.z, forcedMinZ, forcedMaxZ, boundarySnapTolerance);
                vertex.x = ClampToRange(vertex.x, forcedMinX, forcedMaxX);
                vertex.z = ClampToRange(vertex.z, forcedMinZ, forcedMaxZ);
            }

            int32_t qx = static_cast<int32_t>(std::llround((vertex.x - minX) / cs));
            const int32_t qy = static_cast<int32_t>(std::llround((vertex.y - minY) / ch));
            int32_t qz = static_cast<int32_t>(std::llround((vertex.z - minZ) / cs));

            if (useForcedXZBounds)
            {
                qx = std::clamp(qx, 0, maxForcedQX);
                qz = std::clamp(qz, 0, maxForcedQZ);
            }

            const QuantizedVertexKey key{ ClampToU16(qx), ClampToU16(qy), ClampToU16(qz) };
            auto existing = vertexMap.find(key);
            if (existing != vertexMap.end())
            {
                rawToCompact[i] = existing->second;
                continue;
            }

            const uint32_t newIndex = static_cast<uint32_t>(compactVertices.size());
            compactVertices.push_back(key);
            vertexMap.emplace(key, newIndex);
            rawToCompact[i] = newIndex;
        }

        if (compactVertices.empty() || compactVertices.size() > 0xFFFFu)
        {
            if (failureReasonOut != nullptr)
            {
                *failureReasonOut = QuantizationFailureReason::VertexOverflow;
            }
            if (failureValueOut != nullptr)
            {
                *failureValueOut = compactVertices.size();
            }
            application::util::Logger::Error(
                "PhiveNavMesh",
                "Quantization failed: compact vertex count out of range (%zu, max 65535)",
                compactVertices.size());
            return false;
        }

        std::unordered_set<TriangleKey, TriangleKeyHash> uniqueTriangles;
        uniqueTriangles.reserve(rawIndices.size() / 3);

        std::vector<uint32_t> finalIndices;
        finalIndices.reserve(rawIndices.size());

        for (size_t i = 0; i + 2 < rawIndices.size(); i += 3)
        {
            const uint32_t ra = rawIndices[i + 0];
            const uint32_t rb = rawIndices[i + 1];
            const uint32_t rc = rawIndices[i + 2];
            if (ra >= rawToCompact.size() || rb >= rawToCompact.size() || rc >= rawToCompact.size())
            {
                continue;
            }

            const uint32_t a = rawToCompact[ra];
            const uint32_t b = rawToCompact[rb];
            const uint32_t c = rawToCompact[rc];
            if (a == b || b == c || c == a)
            {
                continue;
            }

            const TriangleKey key = MakeTriangleKey(a, b, c);
            if (!uniqueTriangles.insert(key).second)
            {
                continue;
            }

            finalIndices.push_back(a);
            finalIndices.push_back(b);
            finalIndices.push_back(c);
        }

        if (finalIndices.empty())
        {
            if (failureReasonOut != nullptr)
            {
                *failureReasonOut = QuantizationFailureReason::Collapsed;
            }
            application::util::Logger::Error("PhiveNavMesh", "Quantization failed: all triangles collapsed/removed during dedup");
            return false;
        }

        const size_t faceCount = finalIndices.size() / 3;
        if (faceCount > 0xFFFFu)
        {
            if (failureReasonOut != nullptr)
            {
                *failureReasonOut = QuantizationFailureReason::FaceOverflow;
            }
            if (failureValueOut != nullptr)
            {
                *failureValueOut = faceCount;
            }
            application::util::Logger::Error(
                "PhiveNavMesh",
                "Quantization failed: face count out of range (%zu, max 65535)",
                faceCount);
            return false;
        }

        outVertices.resize(compactVertices.size() * 3);
        for (size_t i = 0; i < compactVertices.size(); ++i)
        {
            outVertices[i * 3 + 0] = compactVertices[i].mX;
            outVertices[i * 3 + 1] = compactVertices[i].mY;
            outVertices[i * 3 + 2] = compactVertices[i].mZ;
        }

        std::unordered_map<UndirectedEdgeKey, std::vector<EdgeRef>, UndirectedEdgeKeyHash> edgeBuckets;
        edgeBuckets.reserve(faceCount * 2);

        for (uint32_t face = 0; face < static_cast<uint32_t>(faceCount); ++face)
        {
            const uint32_t a = finalIndices[face * 3 + 0];
            const uint32_t b = finalIndices[face * 3 + 1];
            const uint32_t c = finalIndices[face * 3 + 2];

            edgeBuckets[MakeUndirectedEdgeKey(a, b)].push_back(EdgeRef{ face, 0, a, b });
            edgeBuckets[MakeUndirectedEdgeKey(b, c)].push_back(EdgeRef{ face, 1, b, c });
            edgeBuckets[MakeUndirectedEdgeKey(c, a)].push_back(EdgeRef{ face, 2, c, a });
        }

        std::vector<uint16_t> oppositeFaces(faceCount * 3, INVALID_FACE_INDEX);

        for (const auto& [edge, refs] : edgeBuckets)
        {
            if (refs.size() < 2)
            {
                continue;
            }

            std::vector<uint8_t> used(refs.size(), 0);
            for (size_t i = 0; i < refs.size(); ++i)
            {
                if (used[i])
                {
                    continue;
                }

                for (size_t j = i + 1; j < refs.size(); ++j)
                {
                    if (used[j])
                    {
                        continue;
                    }

                    if (refs[i].mA != refs[j].mB || refs[i].mB != refs[j].mA)
                    {
                        continue;
                    }

                    used[i] = 1;
                    used[j] = 1;
                    oppositeFaces[refs[i].mFace * 3 + refs[i].mLocalEdge] = static_cast<uint16_t>(refs[j].mFace);
                    oppositeFaces[refs[j].mFace * 3 + refs[j].mLocalEdge] = static_cast<uint16_t>(refs[i].mFace);
                    break;
                }
            }
        }

        outTriangles.resize(faceCount * 6);
        for (size_t face = 0; face < faceCount; ++face)
        {
            outTriangles[face * 6 + 0] = static_cast<uint16_t>(finalIndices[face * 3 + 0]);
            outTriangles[face * 6 + 1] = static_cast<uint16_t>(finalIndices[face * 3 + 1]);
            outTriangles[face * 6 + 2] = static_cast<uint16_t>(finalIndices[face * 3 + 2]);
            outTriangles[face * 6 + 3] = oppositeFaces[face * 3 + 0];
            outTriangles[face * 6 + 4] = oppositeFaces[face * 3 + 1];
            outTriangles[face * 6 + 5] = oppositeFaces[face * 3 + 2];
        }

        return !outVertices.empty() && !outTriangles.empty();
    }

    bool FilterTrianglesBySlope(
        const float* inputVertices,
        int inputVertexCount,
        const int* inputIndices,
        int inputIndexCount,
        float walkableCos,
        bool useForcedXZBounds,
        float forcedMinX,
        float forcedMaxX,
        float forcedMinZ,
        float forcedMaxZ,
        float cs,
        std::vector<glm::vec3>& verticesOut,
        std::vector<uint32_t>& indicesOut)
    {
        verticesOut.clear();
        indicesOut.clear();

        if (inputVertices == nullptr || inputIndices == nullptr || inputVertexCount <= 0 || inputIndexCount < 3)
        {
            return false;
        }

        const float boundarySnapTolerance = std::max(cs * 0.6f, 0.02f);

        verticesOut.reserve(static_cast<size_t>(inputIndexCount));
        indicesOut.reserve(static_cast<size_t>(inputIndexCount));

        for (int tri = 0; tri + 2 < inputIndexCount; tri += 3)
        {
            const int ia = inputIndices[tri + 0];
            const int ib = inputIndices[tri + 1];
            const int ic = inputIndices[tri + 2];

            if (ia < 0 || ib < 0 || ic < 0 || ia >= inputVertexCount || ib >= inputVertexCount || ic >= inputVertexCount)
            {
                continue;
            }

            glm::vec3 a(
                inputVertices[ia * 3 + 0],
                inputVertices[ia * 3 + 1],
                inputVertices[ia * 3 + 2]);
            glm::vec3 b(
                inputVertices[ib * 3 + 0],
                inputVertices[ib * 3 + 1],
                inputVertices[ib * 3 + 2]);
            glm::vec3 c(
                inputVertices[ic * 3 + 0],
                inputVertices[ic * 3 + 1],
                inputVertices[ic * 3 + 2]);

            if (!IsFiniteVec3(a) || !IsFiniteVec3(b) || !IsFiniteVec3(c))
            {
                continue;
            }

            if (useForcedXZBounds)
            {
                a.x = ClampToRange(SnapToBoundary(a.x, forcedMinX, forcedMaxX, boundarySnapTolerance), forcedMinX, forcedMaxX);
                a.z = ClampToRange(SnapToBoundary(a.z, forcedMinZ, forcedMaxZ, boundarySnapTolerance), forcedMinZ, forcedMaxZ);
                b.x = ClampToRange(SnapToBoundary(b.x, forcedMinX, forcedMaxX, boundarySnapTolerance), forcedMinX, forcedMaxX);
                b.z = ClampToRange(SnapToBoundary(b.z, forcedMinZ, forcedMaxZ, boundarySnapTolerance), forcedMinZ, forcedMaxZ);
                c.x = ClampToRange(SnapToBoundary(c.x, forcedMinX, forcedMaxX, boundarySnapTolerance), forcedMinX, forcedMaxX);
                c.z = ClampToRange(SnapToBoundary(c.z, forcedMinZ, forcedMaxZ, boundarySnapTolerance), forcedMinZ, forcedMaxZ);
            }

            const glm::vec3 cross = glm::cross(b - a, c - a);
            const float crossLen = glm::length(cross);
            if (crossLen <= 1e-8f)
            {
                continue;
            }

            const float normalYAbs = std::fabs(cross.y / crossLen);
            if (normalYAbs < walkableCos)
            {
                continue;
            }

            const uint32_t base = static_cast<uint32_t>(verticesOut.size());
            verticesOut.push_back(a);
            verticesOut.push_back(b);
            verticesOut.push_back(c);
            indicesOut.push_back(base + 0);
            indicesOut.push_back(base + 1);
            indicesOut.push_back(base + 2);
        }

        RemoveDegenerateAndDuplicateTriangles(verticesOut, indicesOut);
        return !verticesOut.empty() && !indicesOut.empty();
    }

    bool VoxelizeGeometry(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        float cs,
        float ch,
        bool useForcedXZBounds,
        float forcedMinX,
        float forcedMaxX,
        float forcedMinZ,
        float forcedMaxZ,
        std::vector<VoxelCoord>& voxelsOut,
        std::unordered_set<uint64_t>& voxelSetOut,
        float& minXOut,
        float& minYOut,
        float& minZOut,
        int& nxOut,
        int& nyOut,
        int& nzOut)
    {
        voxelsOut.clear();
        voxelSetOut.clear();

        if (vertices.empty() || indices.size() < 3)
        {
            return false;
        }

        float minX = vertices[0].x;
        float minY = vertices[0].y;
        float minZ = vertices[0].z;
        float maxX = vertices[0].x;
        float maxY = vertices[0].y;
        float maxZ = vertices[0].z;

        for (const glm::vec3& v : vertices)
        {
            minX = std::min(minX, v.x);
            minY = std::min(minY, v.y);
            minZ = std::min(minZ, v.z);
            maxX = std::max(maxX, v.x);
            maxY = std::max(maxY, v.y);
            maxZ = std::max(maxZ, v.z);
        }

        if (useForcedXZBounds)
        {
            minX = forcedMinX;
            maxX = forcedMaxX;
            minZ = forcedMinZ;
            maxZ = forcedMaxZ;
        }

        if (maxX - minX < cs)
        {
            maxX = minX + cs;
        }
        if (maxY - minY < ch)
        {
            maxY = minY + ch;
        }
        if (maxZ - minZ < cs)
        {
            maxZ = minZ + cs;
        }

        const int nx = std::max(1, static_cast<int>(std::ceil((maxX - minX) / cs)));
        const int ny = std::max(1, static_cast<int>(std::ceil((maxY - minY) / ch)));
        const int nz = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / cs)));

        if (nx >= 0x1FFFFF || ny >= 0x1FFFFF || nz >= 0x1FFFFF)
        {
            application::util::Logger::Error(
                "PhiveNavMesh",
                "Voxelization aborted: voxel grid too large (%d x %d x %d)",
                nx,
                ny,
                nz);
            return false;
        }

        const float voxelRadiusSq = 0.25f * (2.0f * cs * cs + ch * ch);

        voxelSetOut.reserve(indices.size());
        voxelsOut.reserve(indices.size());

        for (size_t tri = 0; tri + 2 < indices.size(); tri += 3)
        {
            const uint32_t ia = indices[tri + 0];
            const uint32_t ib = indices[tri + 1];
            const uint32_t ic = indices[tri + 2];
            if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size())
            {
                continue;
            }

            const glm::vec3& a = vertices[ia];
            const glm::vec3& b = vertices[ib];
            const glm::vec3& c = vertices[ic];

            float triMinX = std::min({ a.x, b.x, c.x });
            float triMaxX = std::max({ a.x, b.x, c.x });
            float triMinY = std::min({ a.y, b.y, c.y });
            float triMaxY = std::max({ a.y, b.y, c.y });
            float triMinZ = std::min({ a.z, b.z, c.z });
            float triMaxZ = std::max({ a.z, b.z, c.z });

            if (useForcedXZBounds)
            {
                triMinX = ClampToRange(triMinX, minX, maxX);
                triMaxX = ClampToRange(triMaxX, minX, maxX);
                triMinZ = ClampToRange(triMinZ, minZ, maxZ);
                triMaxZ = ClampToRange(triMaxZ, minZ, maxZ);
            }

            int minVX = std::max(0, static_cast<int>(std::floor((triMinX - minX) / cs)));
            int maxVX = std::min(nx - 1, static_cast<int>(std::floor((triMaxX - minX) / cs)));
            int minVY = std::max(0, static_cast<int>(std::floor((triMinY - minY) / ch)));
            int maxVY = std::min(ny - 1, static_cast<int>(std::floor((triMaxY - minY) / ch)));
            int minVZ = std::max(0, static_cast<int>(std::floor((triMinZ - minZ) / cs)));
            int maxVZ = std::min(nz - 1, static_cast<int>(std::floor((triMaxZ - minZ) / cs)));

            if (minVX > maxVX || minVY > maxVY || minVZ > maxVZ)
            {
                continue;
            }

            for (int z = minVZ; z <= maxVZ; ++z)
            {
                const float centerZ = minZ + (static_cast<float>(z) + 0.5f) * cs;
                for (int y = minVY; y <= maxVY; ++y)
                {
                    const float centerY = minY + (static_cast<float>(y) + 0.5f) * ch;
                    for (int x = minVX; x <= maxVX; ++x)
                    {
                        const float centerX = minX + (static_cast<float>(x) + 0.5f) * cs;
                        const glm::vec3 p(centerX, centerY, centerZ);
                        if (DistancePointTriangleSquared(p, a, b, c) > voxelRadiusSq)
                        {
                            continue;
                        }

                        const uint64_t key = MakeVoxelKey(x, y, z);
                        if (!voxelSetOut.insert(key).second)
                        {
                            continue;
                        }

                        voxelsOut.push_back(VoxelCoord{ x, y, z });
                    }
                }
            }
        }

        if (voxelsOut.empty())
        {
            return false;
        }

        minXOut = minX;
        minYOut = minY;
        minZOut = minZ;
        nxOut = nx;
        nyOut = ny;
        nzOut = nz;
        return true;
    }

    void FilterWalkableVoxelsAndSmooth(
        const std::vector<VoxelCoord>& voxels,
        const std::unordered_set<uint64_t>& voxelSet,
        std::vector<VoxelCoord>& filteredVoxelsOut,
        std::unordered_set<uint64_t>& filteredSetOut,
        std::unordered_map<uint64_t, int>& columnYOut,
        std::unordered_map<uint64_t, float>& smoothedYOut,
        std::unordered_map<uint64_t, float>& smoothedVoxelYOut,
        float minLayerSeparationWorld,
        float cellHeight,
        int stitchYCells)
    {
        filteredVoxelsOut.clear();
        filteredSetOut.clear();
        columnYOut.clear();
        smoothedYOut.clear();
        smoothedVoxelYOut.clear();

        auto PackXZ = [](int x, int z) -> uint64_t
            {
                return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
                    static_cast<uint64_t>(static_cast<uint32_t>(z));
            };

        auto UnpackX = [](uint64_t key) -> int
            {
                return static_cast<int>(static_cast<uint32_t>(key >> 32));
            };
        auto UnpackZ = [](uint64_t key) -> int
            {
                return static_cast<int>(static_cast<uint32_t>(key & 0xffffffffu));
            };

        std::unordered_map<uint64_t, std::vector<int>> columnYs;
        columnYs.reserve(voxels.size() * 2 + 1);
        for (const VoxelCoord& v : voxels)
        {
            columnYs[PackXZ(v.mX, v.mZ)].push_back(v.mY);
        }

        filteredVoxelsOut.reserve(voxels.size());
        filteredSetOut.reserve(voxels.size() * 2 + 1);

        const float separationWorld = std::max(0.0f, minLayerSeparationWorld);
        const float safeCellHeight = std::max(1e-6f, cellHeight);

        for (auto& entry : columnYs)
        {
            const uint64_t key = entry.first;
            auto& ys = entry.second;
            if (ys.empty())
            {
                continue;
            }

            std::sort(ys.begin(), ys.end(), [](int a, int b) { return a > b; });

            const int x = UnpackX(key);
            const int z = UnpackZ(key);

            auto TryAddVoxel = [&](int y) -> bool
                {
                    const uint64_t vkey = MakeVoxelKey(x, y, z);
                    if (voxelSet.find(vkey) == voxelSet.end())
                    {
                        return false;
                    }

                    filteredVoxelsOut.push_back(VoxelCoord{ x, y, z });
                    filteredSetOut.insert(vkey);
                    return true;
                };

            int lastKeptY = ys.front();
            if (!TryAddVoxel(lastKeptY))
            {
                continue;
            }

            columnYOut[key] = lastKeptY;

            for (size_t i = 1; i < ys.size(); ++i)
            {
                const int candidateY = ys[i];
                if (separationWorld <= 0.0f ||
                    static_cast<float>(lastKeptY - candidateY) * safeCellHeight >= separationWorld)
                {
                    if (TryAddVoxel(candidateY))
                    {
                        lastKeptY = candidateY;
                    }
                }
            }
        }

        if (filteredVoxelsOut.empty())
        {
            return;
        }

        auto HasVoxel = [&](int x, int z, int y) -> bool
            {
                return filteredSetOut.find(MakeVoxelKey(x, y, z)) != filteredSetOut.end();
            };

        std::unordered_map<uint64_t, std::vector<int>> keptColumnYs;
        keptColumnYs.reserve(filteredVoxelsOut.size() * 2 + 1);
        for (const VoxelCoord& v : filteredVoxelsOut)
        {
            keptColumnYs[PackXZ(v.mX, v.mZ)].push_back(v.mY);
        }

        const int smoothRadius = 4;
        smoothedVoxelYOut.reserve(filteredVoxelsOut.size() * 2 + 1);

        for (const VoxelCoord& v : filteredVoxelsOut)
        {
            double sum = 0.0;
            double weightSum = 0.0;

            for (int dz = -smoothRadius; dz <= smoothRadius; ++dz)
            {
                for (int dx = -smoothRadius; dx <= smoothRadius; ++dx)
                {
                    const int nx = v.mX + dx;
                    const int nz = v.mZ + dz;
                    const uint64_t colKey = PackXZ(nx, nz);
                    auto it = keptColumnYs.find(colKey);
                    if (it == keptColumnYs.end())
                    {
                        continue;
                    }

                    int closestY = it->second.front();
                    int bestDiff = std::abs(closestY - v.mY);
                    for (int candidateY : it->second)
                    {
                        const int diff = std::abs(candidateY - v.mY);
                        if (diff < bestDiff)
                        {
                            bestDiff = diff;
                            closestY = candidateY;
                        }
                    }

                    if (bestDiff > stitchYCells)
                    {
                        continue;
                    }

                    if (!HasVoxel(nx, nz, closestY))
                    {
                        continue;
                    }

                    const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                    const double weight = 1.0 / (1.0 + static_cast<double>(dist * dist));
                    sum += static_cast<double>(closestY) * weight;
                    weightSum += weight;
                }
            }

            float smoothed = static_cast<float>(v.mY);
            if (weightSum > 0.0)
            {
                smoothed = static_cast<float>(sum / weightSum);
            }

            const float maxDelta = static_cast<float>(stitchYCells);
            smoothed = std::clamp(smoothed, static_cast<float>(v.mY) - maxDelta, static_cast<float>(v.mY) + maxDelta);
            smoothedVoxelYOut.emplace(MakeVoxelKey(v.mX, v.mY, v.mZ), smoothed);
        }

        smoothedYOut.reserve(columnYOut.size() * 2 + 1);
        for (const auto& entry : columnYOut)
        {
            const uint64_t key = entry.first;
            const int y = entry.second;
            const int x = UnpackX(key);
            const int z = UnpackZ(key);
            const uint64_t vkey = MakeVoxelKey(x, y, z);

            auto it = smoothedVoxelYOut.find(vkey);
            const float smoothed = (it != smoothedVoxelYOut.end())
                ? it->second
                : static_cast<float>(y);
            smoothedYOut.emplace(key, smoothed);
        }
    }

    bool BuildTopFaceFallback(
        const std::unordered_map<uint64_t, int>& columnY,
        const std::unordered_map<uint64_t, float>& smoothedY,
        float minX,
        float minY,
        float minZ,
        float cs,
        float ch,
        std::vector<glm::vec3>& verticesOut,
        std::vector<uint32_t>& indicesOut)
    {
        verticesOut.clear();
        indicesOut.clear();

        const bool hasSmoothed = !smoothedY.empty();
        if (!hasSmoothed && columnY.empty())
        {
            return false;
        }

        const size_t columnCount = hasSmoothed ? smoothedY.size() : columnY.size();
        verticesOut.reserve(columnCount * 4);
        indicesOut.reserve(columnCount * 6);

        auto UnpackX = [](uint64_t key) -> int
            {
                return static_cast<int>(static_cast<uint32_t>(key >> 32));
            };
        auto UnpackZ = [](uint64_t key) -> int
            {
                return static_cast<int>(static_cast<uint32_t>(key & 0xffffffffu));
            };

        auto emitQuad = [&](int x, int z, float y)
            {
                const float x0 = minX + static_cast<float>(x) * cs;
                const float z0 = minZ + static_cast<float>(z) * cs;
                const float x1 = x0 + cs;
                const float z1 = z0 + cs;
                const float yTop = minY + (y + 1.0f) * ch;

                const uint32_t base = static_cast<uint32_t>(verticesOut.size());
                verticesOut.emplace_back(x0, yTop, z0);
                verticesOut.emplace_back(x1, yTop, z0);
                verticesOut.emplace_back(x1, yTop, z1);
                verticesOut.emplace_back(x0, yTop, z1);

                indicesOut.push_back(base + 0);
                indicesOut.push_back(base + 1);
                indicesOut.push_back(base + 2);
                indicesOut.push_back(base + 0);
                indicesOut.push_back(base + 2);
                indicesOut.push_back(base + 3);
            };

        if (hasSmoothed)
        {
            for (const auto& entry : smoothedY)
            {
                emitQuad(UnpackX(entry.first), UnpackZ(entry.first), entry.second);
            }
        }
        else
        {
            for (const auto& entry : columnY)
            {
                emitQuad(UnpackX(entry.first), UnpackZ(entry.first), static_cast<float>(entry.second));
            }
        }

        if (verticesOut.empty() || indicesOut.empty())
        {
            return false;
        }

        if (!WeldRawMeshExact(verticesOut, indicesOut))
        {
            RemoveDegenerateAndDuplicateTriangles(verticesOut, indicesOut);
            return !verticesOut.empty() && !indicesOut.empty();
        }

        return true;
    }

    bool TriangulateVoxelGraph(
        const std::vector<VoxelCoord>& voxels,
        const std::unordered_map<uint64_t, int>& columnY,
        const std::unordered_map<uint64_t, float>& smoothedY,
        const std::unordered_map<uint64_t, float>& smoothedVoxelY,
        float minX,
        float minY,
        float minZ,
        float maxX,
        float maxZ,
        float cs,
        float ch,
        int maxDeltaY,
        int nx,
        int nz,
        std::vector<glm::vec3>& verticesOut,
        std::vector<uint32_t>& indicesOut)
    {
        verticesOut.clear();
        indicesOut.clear();

        if (voxels.empty())
        {
            return false;
        }

        auto PackXZ = [](int x, int z) -> uint64_t
            {
                return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
                    static_cast<uint64_t>(static_cast<uint32_t>(z));
            };
        struct VoxelEntry
        {
            int mX = 0;
            int mY = 0;
            int mZ = 0;
            float mYConnect = 0.0f;
            uint32_t mVertexIndex = 0;
        };

        std::vector<VoxelEntry> entries;
        entries.reserve(voxels.size());

        std::unordered_map<uint64_t, std::vector<size_t>> columnMap;
        columnMap.reserve(voxels.size() * 2 + 1);

        verticesOut.reserve(voxels.size());

        for (const VoxelCoord& v : voxels)
        {
            const uint64_t key = PackXZ(v.mX, v.mZ);
            float yConnect = static_cast<float>(v.mY);
            const uint64_t vkey = MakeVoxelKey(v.mX, v.mY, v.mZ);
            auto itSmooth = smoothedVoxelY.find(vkey);
            if (itSmooth != smoothedVoxelY.end())
            {
                yConnect = itSmooth->second;
            }

            float cx = minX + (static_cast<float>(v.mX) + 0.5f) * cs;
            const float cy = minY + (yConnect + 0.5f) * ch;
            float cz = minZ + (static_cast<float>(v.mZ) + 0.5f) * cs;

            if (nx > 1)
            {
                if (v.mX == 0)
                {
                    cx = minX;
                }
                else if (v.mX == nx - 1)
                {
                    cx = maxX;
                }
            }

            if (nz > 1)
            {
                if (v.mZ == 0)
                {
                    cz = minZ;
                }
                else if (v.mZ == nz - 1)
                {
                    cz = maxZ;
                }
            }

            const uint32_t vertexIndex = static_cast<uint32_t>(verticesOut.size());
            verticesOut.emplace_back(cx, cy, cz);

            const size_t entryIndex = entries.size();
            entries.push_back(VoxelEntry{ v.mX, v.mY, v.mZ, yConnect, vertexIndex });
            columnMap[key].push_back(entryIndex);
        }

        if (entries.empty())
        {
            return false;
        }

        const float connectThreshold = static_cast<float>(std::max(1, maxDeltaY));
        auto CanConnect = [&](float y0, float y1) -> bool
            {
                return std::abs(y0 - y1) <= connectThreshold;
            };

        auto FindClosestInColumn = [&](const std::vector<size_t>& column, float targetY, size_t& outIndex) -> bool
            {
                bool found = false;
                float bestDiff = connectThreshold + 1.0f;
                for (size_t idx : column)
                {
                    const float y = entries[idx].mYConnect;
                    const float diff = std::abs(y - targetY);
                    if (diff <= connectThreshold && diff < bestDiff)
                    {
                        bestDiff = diff;
                        outIndex = idx;
                        found = true;
                    }
                }
                return found;
            };

        std::vector<uint8_t> used(entries.size(), 0u);
        indicesOut.reserve(entries.size() * 6);

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const VoxelEntry& v = entries[i];

            auto it10Col = columnMap.find(PackXZ(v.mX + 1, v.mZ));
            auto it01Col = columnMap.find(PackXZ(v.mX, v.mZ + 1));
            if (it10Col == columnMap.end() || it01Col == columnMap.end())
            {
                continue;
            }

            size_t idx10 = 0;
            size_t idx01 = 0;
            if (!FindClosestInColumn(it10Col->second, v.mYConnect, idx10) ||
                !FindClosestInColumn(it01Col->second, v.mYConnect, idx01))
            {
                continue;
            }

            const float y00 = v.mYConnect;
            const float y10 = entries[idx10].mYConnect;
            const float y01 = entries[idx01].mYConnect;

            if (!CanConnect(y00, y10) || !CanConnect(y00, y01) || !CanConnect(y10, y01))
            {
                continue;
            }

            indicesOut.push_back(v.mVertexIndex);
            indicesOut.push_back(entries[idx10].mVertexIndex);
            indicesOut.push_back(entries[idx01].mVertexIndex);

            used[i] = 1u;
            used[idx10] = 1u;
            used[idx01] = 1u;

            auto it11Col = columnMap.find(PackXZ(v.mX + 1, v.mZ + 1));
            if (it11Col == columnMap.end())
            {
                continue;
            }

            const float targetDiag = 0.5f * (y10 + y01);
            size_t idx11 = 0;
            if (!FindClosestInColumn(it11Col->second, targetDiag, idx11))
            {
                continue;
            }

            const float y11 = entries[idx11].mYConnect;
            if (!CanConnect(y10, y11) || !CanConnect(y11, y01) || !CanConnect(y10, y01))
            {
                continue;
            }

            indicesOut.push_back(entries[idx10].mVertexIndex);
            indicesOut.push_back(entries[idx11].mVertexIndex);
            indicesOut.push_back(entries[idx01].mVertexIndex);

            used[idx10] = 1u;
            used[idx11] = 1u;
            used[idx01] = 1u;
        }

        auto EmitVoxelQuad = [&](const VoxelEntry& v)
            {
                float x0 = minX + static_cast<float>(v.mX) * cs;
                float z0 = minZ + static_cast<float>(v.mZ) * cs;
                float x1 = x0 + cs;
                float z1 = z0 + cs;
                const float yTop = minY + (static_cast<float>(v.mY) + 1.0f) * ch;

                if (nx > 1 && v.mX == nx - 1)
                {
                    x1 = maxX;
                }
                if (nz > 1 && v.mZ == nz - 1)
                {
                    z1 = maxZ;
                }

                const uint32_t base = static_cast<uint32_t>(verticesOut.size());
                verticesOut.emplace_back(x0, yTop, z0);
                verticesOut.emplace_back(x1, yTop, z0);
                verticesOut.emplace_back(x1, yTop, z1);
                verticesOut.emplace_back(x0, yTop, z1);

                indicesOut.push_back(base + 0);
                indicesOut.push_back(base + 1);
                indicesOut.push_back(base + 2);
                indicesOut.push_back(base + 0);
                indicesOut.push_back(base + 2);
                indicesOut.push_back(base + 3);
            };

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (used[i])
            {
                continue;
            }

            EmitVoxelQuad(entries[i]);
        }

        RemoveDegenerateAndDuplicateTriangles(verticesOut, indicesOut);
        if (!verticesOut.empty() && !indicesOut.empty())
        {
            return true;
        }

        return BuildTopFaceFallback(columnY, smoothedY, minX, minY, minZ, cs, ch, verticesOut, indicesOut);
    }

    bool BuildVoxelPipeline(
        const float* inputVertices,
        int inputVertexCount,
        const int* inputIndices,
        int inputIndexCount,
        const application::file::game::phive::starlight_physics::ai::hkaiNavMeshGeometryGenerator::BuildConfig& config,
        bool useForcedXZBounds,
        float forcedMinX,
        float forcedMaxX,
        float forcedMinZ,
        float forcedMaxZ,
        std::vector<uint16_t>& outVertices,
        std::vector<uint16_t>& outTriangles,
        float outBoundsMin[3],
        float outBoundsMax[3])
    {
        outVertices.clear();
        outTriangles.clear();

        const float cs = std::max(1e-4f, config.cs);
        const float ch = std::max(1e-4f, config.ch);
        const float slopeAngle = std::clamp(config.walkableSlopeAngle, 0.0f, 89.9f);
        const float walkableCos = std::cos(slopeAngle * DEG_TO_RAD);

        std::vector<glm::vec3> filteredVertices;
        std::vector<uint32_t> filteredIndices;
        if (!FilterTrianglesBySlope(
                inputVertices,
                inputVertexCount,
                inputIndices,
                inputIndexCount,
                walkableCos,
                useForcedXZBounds,
                forcedMinX,
                forcedMaxX,
                forcedMinZ,
                forcedMaxZ,
                cs,
                filteredVertices,
                filteredIndices))
        {
            application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: no triangles left after slope filtering");
            return false;
        }

        if (config.enableSimplification)
        {
            if (!SimplifyRawMeshByRatioWithOptions(
                    filteredVertices,
                    filteredIndices,
                    config.simplificationTargetRatio,
                    config.simplificationMaxError,
                    meshopt_SimplifyLockBorder))
            {
                application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: border-locked simplification failed");
                return false;
            }
        }

        std::vector<VoxelCoord> voxels;
        std::unordered_set<uint64_t> voxelSet;
        float minX = 0.0f;
        float minY = 0.0f;
        float minZ = 0.0f;
        int nx = 0;
        int ny = 0;
        int nz = 0;
        if (!VoxelizeGeometry(
                filteredVertices,
                filteredIndices,
                cs,
                ch,
                useForcedXZBounds,
                forcedMinX,
                forcedMaxX,
                forcedMinZ,
                forcedMaxZ,
                voxels,
                voxelSet,
                minX,
                minY,
                minZ,
                nx,
                ny,
                nz))
        {
            application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: voxelization produced no voxels");
            return false;
        }

        const float maxX = useForcedXZBounds ? forcedMaxX : (minX + static_cast<float>(nx) * cs);
        const float maxZ = useForcedXZBounds ? forcedMaxZ : (minZ + static_cast<float>(nz) * cs);

        const int stitchYCells = std::max(1, static_cast<int>(std::lround(1.0 / static_cast<double>(ch))));
        const float minLayerSeparationWorld = 2.0f;
        std::vector<VoxelCoord> walkableVoxels;
        std::unordered_set<uint64_t> walkableSet;
        std::unordered_map<uint64_t, int> columnY;
        std::unordered_map<uint64_t, float> smoothedY;
        std::unordered_map<uint64_t, float> smoothedVoxelY;
        FilterWalkableVoxelsAndSmooth(
            voxels,
            voxelSet,
            walkableVoxels,
            walkableSet,
            columnY,
            smoothedY,
            smoothedVoxelY,
            minLayerSeparationWorld,
            ch,
            stitchYCells);

        if (walkableVoxels.empty())
        {
            application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: no walkable voxels after filtering");
            return false;
        }

        std::vector<glm::vec3> voxelVertices;
        std::vector<uint32_t> voxelIndices;
        if (!TriangulateVoxelGraph(
                walkableVoxels,
                columnY,
                smoothedY,
                smoothedVoxelY,
                minX,
                minY,
                minZ,
                maxX,
                maxZ,
                cs,
                ch,
                std::max(1, stitchYCells * 2),
                nx,
                nz,
                voxelVertices,
                voxelIndices))
        {
            application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: voxel triangulation produced no geometry");
            return false;
        }

        const float borderSnapTolerance = std::max(cs * 0.6f, 0.02f);
        SnapVerticesToBounds(voxelVertices, minX, maxX, minZ, maxZ, borderSnapTolerance);

        std::vector<uint8_t> fixedMask(voxelVertices.size(), 0u);
        for (size_t i = 0; i < voxelVertices.size(); ++i)
        {
            const glm::vec3& v = voxelVertices[i];
            if (v.x <= minX + borderSnapTolerance || v.x >= maxX - borderSnapTolerance ||
                v.z <= minZ + borderSnapTolerance || v.z >= maxZ - borderSnapTolerance)
            {
                fixedMask[i] = 1u;
            }
        }

        SmoothRawMeshLaplacian(voxelVertices, voxelIndices, 20, 0.75f, &fixedMask);

        constexpr int borderBandCells = 3;
        const float borderBandWorld = std::max(cs, static_cast<float>(borderBandCells) * cs);
        const float minTriangleArea = cs * cs * 0.03f;
        const float minBatchExtent = cs * 2.5f;

        if (config.enableSimplification)
        {
            // Push interior simplification harder while still bounding triangle size to keep topology readable.
            const float interiorRatio = std::min(config.simplificationTargetRatio, 0.005f);
            const float interiorError = std::max(config.simplificationMaxError, cs * 20.0f);
            const float interiorMaxTriangleArea = std::max(cs * cs * 12.0f, 1e-6f);
            if (!SimplifyInteriorRegion(
                    voxelVertices,
                    voxelIndices,
                    minX,
                    maxX,
                    minZ,
                    maxZ,
                    borderBandWorld,
                    interiorRatio,
                    interiorError,
                    interiorMaxTriangleArea))
            {
                application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: interior simplification failed");
                return false;
            }
        }

        if (!SimplifyRawMeshToPackedLimits(voxelVertices, voxelIndices, std::max(1e-4f, config.simplificationMaxError)))
        {
            application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: unable to fit packed limits after final simplification");
            return false;
        }

        if (!CullSmallTrianglesAndBatches(
                voxelVertices,
                voxelIndices,
                minTriangleArea,
                minBatchExtent,
                minX,
                maxX,
                minZ,
                maxZ,
                borderBandWorld))
        {
            application::util::Logger::Error("PhiveNavMesh", "Simple navmesh build aborted: small batch cull removed all geometry");
            return false;
        }

        SnapVerticesToBounds(voxelVertices, minX, maxX, minZ, maxZ, borderSnapTolerance);

        return QuantizeAndBuildOutput(
            voxelVertices,
            voxelIndices,
            cs,
            ch,
            useForcedXZBounds,
            forcedMinX,
            forcedMaxX,
            forcedMinZ,
            forcedMaxZ,
            outVertices,
            outTriangles,
            outBoundsMin,
            outBoundsMax);
    }
}

namespace application::file::game::phive::starlight_physics::ai
{
    void hkaiNavMeshGeometryGenerator::setNavmeshBuildParams(const Config& config)
    {
        mConfig.cs = std::max(1e-4f, config.mCellSize);
        mConfig.ch = std::max(1e-4f, config.mCellHeight);
        mConfig.walkableSlopeAngle = std::clamp(config.mWalkableSlopeAngle, 0.0f, 89.9f);
        mConfig.walkableHeight = std::max(1, static_cast<int>(std::ceil(config.mWalkableHeight / mConfig.ch)));
        mConfig.walkableClimb = std::max(0, static_cast<int>(std::floor(config.mWalkableClimb / mConfig.ch)));
        mConfig.walkableRadius = std::max(0, static_cast<int>(std::ceil(config.mWalkableRadius / mConfig.cs)));
        mConfig.minRegionArea = std::max(0, config.mMinRegionArea * config.mMinRegionArea);
        mConfig.mergeRegionArea = std::max(mConfig.minRegionArea, mConfig.minRegionArea * 2);
        mConfig.maxVertsPerPoly = 3;
        mConfig.enableSimplification = config.mEnableSimplification;
        mConfig.simplificationTargetRatio = std::clamp(config.mSimplificationTargetRatio, 0.003f, 1.0f);
        mConfig.simplificationMaxError = std::clamp(config.mSimplificationMaxError, 0.0f, 100.0f);
    }

    void hkaiNavMeshGeometryGenerator::setForcedXZBounds(float minX, float maxX, float minZ, float maxZ)
    {
        mUseForcedXZBounds = true;
        mForcedMinX = std::min(minX, maxX);
        mForcedMaxX = std::max(minX, maxX);
        mForcedMinZ = std::min(minZ, maxZ);
        mForcedMaxZ = std::max(minZ, maxZ);
    }

    void hkaiNavMeshGeometryGenerator::clearForcedXZBounds()
    {
        mUseForcedXZBounds = false;
    }

    bool hkaiNavMeshGeometryGenerator::buildNavmeshForMesh(const float* vertices, int verticesCount, const int* indices, int indicesCount)
    {
        mMeshVertices.clear();
        mMeshTriangles.clear();

        if (vertices == nullptr || indices == nullptr || verticesCount <= 0 || indicesCount < 3)
        {
            return false;
        }

        float boundsMin[3] = { 0.0f, 0.0f, 0.0f };
        float boundsMax[3] = { 0.0f, 0.0f, 0.0f };

        if (!BuildVoxelPipeline(
                vertices,
                verticesCount,
                indices,
                indicesCount,
                mConfig,
                mUseForcedXZBounds,
                mForcedMinX,
                mForcedMaxX,
                mForcedMinZ,
                mForcedMaxZ,
                mMeshVertices,
                mMeshTriangles,
                boundsMin,
                boundsMax))
        {
            return false;
        }

        mBoundingBoxMin[0] = boundsMin[0];
        mBoundingBoxMin[1] = boundsMin[1];
        mBoundingBoxMin[2] = boundsMin[2];

        mBoundingBoxMax[0] = boundsMax[0];
        mBoundingBoxMax[1] = boundsMax[1];
        mBoundingBoxMax[2] = boundsMax[2];

        return true;
    }

    int hkaiNavMeshGeometryGenerator::getMeshVertexCount()
    {
        return static_cast<int>(mMeshVertices.size() / 3);
    }

    int hkaiNavMeshGeometryGenerator::getMeshTriangleCount()
    {
        return static_cast<int>(mMeshTriangles.size() / 6);
    }

    void hkaiNavMeshGeometryGenerator::getMeshVertices(void* buffer)
    {
        if (buffer == nullptr || mMeshVertices.empty())
        {
            return;
        }

        std::memcpy(buffer, mMeshVertices.data(), mMeshVertices.size() * sizeof(uint16_t));
    }

    void hkaiNavMeshGeometryGenerator::getMeshTriangles(void* buffer)
    {
        if (buffer == nullptr || mMeshTriangles.empty())
        {
            return;
        }

        std::memcpy(buffer, mMeshTriangles.data(), mMeshTriangles.size() * sizeof(uint16_t));
    }

    hkaiNavMeshGeometryGenerator::~hkaiNavMeshGeometryGenerator() = default;
}
