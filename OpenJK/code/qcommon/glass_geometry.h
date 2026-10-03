/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

constexpr int GLASS_MAX_VERTICES = 64;

struct glass_face_t
{
    float vertices[4][3] = {};
    float normal[3] = {};
    double area = 0;
    bool valid = false;
    std::vector<std::array<float, 3>> boundary;
};

// The legacy shard tessellator requires a convex, perimeter-ordered quad.
// Reject zero, crossed, concave and non-planar inputs before any divisions.
inline bool GlassQuadValid(const float vertices[4][3])
{
    double n[3] = {}, scale = 0;
    for (int i = 0; i < 4; ++i)
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(vertices[i][axis]))
                return false;
    for (int i = 0; i < 4; ++i)
    {
        double a[3], b[3], cross[3];
        for (int axis = 0; axis < 3; ++axis)
        {
            a[axis] = double(vertices[(i+1)%4][axis]) - vertices[i][axis];
            b[axis] = double(vertices[(i+2)%4][axis]) - vertices[(i+1)%4][axis];
            scale = std::max(scale, std::abs(a[axis]));
        }
        for (int axis = 0; axis < 3; ++axis)
            cross[axis] = a[(axis+1)%3]*b[(axis+2)%3] - a[(axis+2)%3]*b[(axis+1)%3];
        if (i == 0)
            std::copy(cross, cross+3, n);
        if (cross[0]*n[0] + cross[1]*n[1] + cross[2]*n[2] <= 0.000001)
            return false;
    }
    const double length = std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    double plane = 0;
    for (int axis = 0; axis < 3; ++axis)
        plane += (double(vertices[3][axis])-vertices[0][axis])*n[axis];
    // BSP coordinates can be snapped to eighth-units independently of the
    // authored plane normal. Allow that quantization, not an arbitrary warp.
    return std::abs(plane) <= length * std::max(0.25, scale * 0.00001);
}

// Map compilers can add collinear/interior vertices to an otherwise rectangular
// pane. Recover its boundary at load time, without fabricating a bounding box
// for genuinely non-quadrilateral glass.
inline glass_face_t GlassBuildFace(std::vector<std::array<float, 3>> points, const float *normal)
{
    glass_face_t face;
    double length = 0;
    int drop = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(normal[axis])) return face;
        length += double(normal[axis])*normal[axis];
        if (std::abs(normal[axis]) > std::abs(normal[drop])) drop = axis;
    }
    if (points.size() < 3 || length < 0.000001) return face;
    length = std::sqrt(length);
    for (int axis = 0; axis < 3; ++axis) face.normal[axis] = normal[axis] / length;
    for (const auto &point : points)
    {
        double plane = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(point[axis])) return {};
            plane += (double(point[axis])-points[0][axis])*face.normal[axis];
        }
        if (std::abs(plane) > 0.25) return {};
    }
    const int u = (drop+1)%3, v = (drop+2)%3;
    std::sort(points.begin(), points.end(), [=](const auto &a, const auto &b) {
        return a[u] != b[u] ? a[u] < b[u] : a[v] < b[v];
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) return face;
    auto turn = [=](const auto &a, const auto &b, const auto &c) {
        return (double(b[u])-a[u])*(double(c[v])-a[v]) - (double(b[v])-a[v])*(double(c[u])-a[u]);
    };
    std::vector<std::array<float, 3>> hull;
    for (const auto &p : points)
    {
        while (hull.size() >= 2 && turn(hull[hull.size()-2], hull.back(), p) <= 0.001) hull.pop_back();
        hull.push_back(p);
    }
    const size_t lower = hull.size();
    for (size_t i = points.size()-1; i-- > 0;)
    {
        const auto &p = points[i];
        while (hull.size() > lower && turn(hull[hull.size()-2], hull.back(), p) <= 0.001) hull.pop_back();
        hull.push_back(p);
    }
    hull.pop_back();
    for (size_t i = 1; i+1 < hull.size(); ++i)
        face.area += turn(hull[0], hull[i], hull[i+1]) * 0.5 / std::abs(face.normal[drop]);
    if (face.normal[drop] < 0) std::reverse(hull.begin(), hull.end());
    if (hull.size() < 3 || hull.size() > GLASS_MAX_VERTICES || !std::isfinite(face.area) || face.area <= 0)
        return face;
    face.boundary = hull;
    if (hull.size() != 4) return face;
    for (int i = 0; i < 4; ++i) std::copy(hull[i].begin(), hull[i].end(), face.vertices[i]);
    face.valid = GlassQuadValid(face.vertices);
    return face;
}

inline void GlassKeepLargest(std::array<glass_face_t, 2> &faces, const glass_face_t &candidate)
{
    if (candidate.area > faces[0].area)
    {
        faces[1] = faces[0];
        faces[0] = candidate;
    }
    else if (candidate.area > faces[1].area) faces[1] = candidate;
}

inline bool GlassSelectFace(const std::array<glass_face_t, 2> &faces, const float *forward,
    float vertices[4][3], float *normal)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        normal[axis] = 0;
        for (int i = 0; i < 4; ++i) vertices[i][axis] = 0;
    }
    auto dot = [=](const glass_face_t &face) {
        return face.normal[0]*forward[0] + face.normal[1]*forward[1] + face.normal[2]*forward[2];
    };
    const glass_face_t &selected = faces[1].area > 0 && dot(faces[1]) < 0 && dot(faces[1]) < dot(faces[0])
        ? faces[1] : faces[0];
    if (!selected.valid) return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        normal[axis] = selected.normal[axis];
        for (int i = 0; i < 4; ++i) vertices[i][axis] = selected.vertices[i][axis];
    }
    return true;
}

inline int GlassSelectPolygon(const std::array<glass_face_t, 2> &faces, const float *forward,
    float (*vertices)[3], int capacity, float *normal)
{
    if (!vertices || !normal || !forward || capacity < 3 || capacity > GLASS_MAX_VERTICES) return 0;
    std::fill(normal, normal + 3, 0.0f);
    for (int i = 0; i < capacity; ++i) std::fill(vertices[i], vertices[i] + 3, 0.0f);
    auto dot = [=](const glass_face_t &face) {
        return face.normal[0]*forward[0] + face.normal[1]*forward[1] + face.normal[2]*forward[2];
    };
    const auto &face = faces[1].area > 0 && dot(faces[1]) < 0 && dot(faces[1]) < dot(faces[0])
        ? faces[1] : faces[0];
    if (face.boundary.size() < 3 || face.boundary.size() > size_t(capacity)) return 0;
    std::copy(face.normal, face.normal + 3, normal);
    for (size_t i = 0; i < face.boundary.size(); ++i)
        std::copy(face.boundary[i].begin(), face.boundary[i].end(), vertices[i]);
    return int(face.boundary.size());
}

struct glass_shard_t
{
    float vertices[3][3] = {};
    float uv[3][2] = {};
};

// Convex BSP face fan, uniformly subdivided into <=128 triangles. Shared edge
// midpoints preserve coverage; UVs follow the pane instead of restarting on each shard.
inline std::vector<glass_shard_t> GlassBuildShards(const float (*vertices)[3], int count, const float *normal)
{
    if (!vertices || !normal || count < 3 || count > GLASS_MAX_VERTICES) return {};
    std::vector<std::array<float, 3>> points(count);
    for (int i = 0; i < count; ++i) std::copy(vertices[i], vertices[i] + 3, points[i].begin());
    const auto face = GlassBuildFace(std::move(points), normal);
    if (face.boundary.size() < 3) return {};
    int drop = 0;
    for (int axis = 1; axis < 3; ++axis)
        if (std::abs(face.normal[axis]) > std::abs(face.normal[drop])) drop = axis;
    const int u = (drop + 1) % 3, v = (drop + 2) % 3;
    std::array<float, 3> center{}, mins = face.boundary[0], maxs = mins;
    for (const auto &point : face.boundary)
        for (int axis = 0; axis < 3; ++axis)
        {
            center[axis] += point[axis] / float(face.boundary.size());
            mins[axis] = std::min(mins[axis], point[axis]);
            maxs[axis] = std::max(maxs[axis], point[axis]);
        }
    if (maxs[u] - mins[u] <= 0.0001f || maxs[v] - mins[v] <= 0.0001f) return {};
    using triangle_t = std::array<std::array<float, 3>, 3>;
    std::vector<triangle_t> triangles;
    for (size_t i = 0; i < face.boundary.size(); ++i)
        triangles.push_back({center, face.boundary[i], face.boundary[(i+1)%face.boundary.size()]});
    while (triangles.size() * 4 <= 128 && face.area / triangles.size() > 400.0)
    {
        std::vector<triangle_t> finer;
        finer.reserve(triangles.size() * 4);
        for (const auto &t : triangles)
        {
            triangle_t mid;
            for (int i = 0; i < 3; ++i)
                for (int axis = 0; axis < 3; ++axis)
                    mid[i][axis] = t[i][axis] + (t[(i+1)%3][axis] - t[i][axis]) * 0.5f;
            finer.push_back({t[0], mid[0], mid[2]});
            finer.push_back({mid[0], t[1], mid[1]});
            finer.push_back({mid[2], mid[1], t[2]});
            finer.push_back({mid[0], mid[1], mid[2]});
        }
        triangles.swap(finer);
    }
    std::vector<glass_shard_t> shards(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i)
        for (int j = 0; j < 3; ++j)
        {
            const auto &point = triangles[i][j];
            std::copy(point.begin(), point.end(), shards[i].vertices[j]);
            shards[i].uv[j][0] = (point[u] - mins[u]) / (maxs[u] - mins[u]);
            shards[i].uv[j][1] = (point[v] - mins[v]) / (maxs[v] - mins[v]);
        }
    return shards;
}
