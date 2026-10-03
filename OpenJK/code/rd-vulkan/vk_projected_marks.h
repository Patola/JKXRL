/* SPDX-License-Identifier: GPL-2.0-or-later
 * Projection/clipping adapted from tr_marks.cpp (id Software, Raven,
 * Activision, ioquake3 and OpenJK contributors). */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vk_marks
{
using Point = std::array<float, 3>;
inline Point Sub(const Point &a, const Point &b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
inline float Dot(const Point &a, const Point &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline Point Cross(const Point &a, const Point &b)
{
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline bool Normalize(Point &p)
{
    const float length = std::sqrt(Dot(p,p));
    if (!std::isfinite(length) || length < 0.00001f) return false;
    for (float &v : p) v /= length;
    return true;
}
inline bool Finite(const Point &p)
{
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}

struct Bounds
{
    Point mins{INFINITY, INFINITY, INFINITY}, maxs{-INFINITY, -INFINITY, -INFINITY};
    void Add(const Point &p)
    {
        for (int a=0; a<3; ++a) { mins[a]=std::min(mins[a],p[a]); maxs[a]=std::max(maxs[a],p[a]); }
    }
    bool Intersects(const Bounds &b) const
    {
        for (int a=0; a<3; ++a) if (mins[a]>b.maxs[a] || maxs[a]<b.mins[a]) return false;
        return true;
    }
    int PlaneSide(const Point &n, float d) const
    {
        float lo=0, hi=0;
        for (int a=0; a<3; ++a)
        {
            lo += n[a]*(n[a]<0 ? maxs[a] : mins[a]);
            hi += n[a]*(n[a]<0 ? mins[a] : maxs[a]);
        }
        return (hi>=d ? 1 : 0) | (lo<d ? 2 : 0);
    }
};

struct Projector
{
    static constexpr int Capacity = 64;
    struct Plane { Point normal; float dist; };
    std::array<Plane, Capacity+2> planes{};
    int planeCount = 0;
    Bounds bounds;
    Point direction{};

    bool Build(int count, const float (*points)[3], const float *projection)
    {
        *this = {};
        if (!points || !projection || count<3 || count>Capacity) return false;
        const Point delta{projection[0],projection[1],projection[2]};
        direction=delta;
        if (!Normalize(direction)) return false;
        Point center{};
        for (int i=0; i<count; ++i)
        {
            Point p{points[i][0],points[i][1],points[i][2]};
            if (!Finite(p)) return false;
            for (int a=0; a<3; ++a) center[a]+=p[a]/count;
            bounds.Add(p);
            Point end{}, front{};
            for (int a=0; a<3; ++a) { end[a]=p[a]+delta[a]; front[a]=p[a]-20*direction[a]; }
            if (!Finite(end) || !Finite(front)) return false;
            bounds.Add(end); bounds.Add(front);
        }
        for (int i=0; i<count; ++i)
        {
            const int next=(i+1)%count;
            Point p{points[i][0],points[i][1],points[i][2]};
            Point q{points[next][0],points[next][1],points[next][2]};
            Point negative{-direction[0],-direction[1],-direction[2]};
            Plane &plane=planes[i];
            plane.normal=Cross(Sub(q,p),negative);
            if (!Normalize(plane.normal)) return false;
            plane.dist=Dot(plane.normal,p);
            // Reject inverted/degenerate footprints rather than expanding them.
            if (Dot(plane.normal,center)-plane.dist <= 0.00001f) return false;
            for (int j=0; j<count; ++j)
            {
                const Point v{points[j][0],points[j][1],points[j][2]};
                if (Dot(plane.normal,v)-plane.dist < -0.5f) return false;
            }
        }
        const Point p{points[0][0],points[0][1],points[0][2]};
        planes[count]={direction,Dot(direction,p)-32};
        const Point opposite{-direction[0],-direction[1],-direction[2]};
        planes[count+1]={opposite,Dot(opposite,p)-20};
        planeCount=count+2;
        return true;
    }

    int Clip(const std::array<Point,3> &triangle, std::array<Point,Capacity> &out) const
    {
        if (!planeCount) return 0;
        std::array<Point,Capacity> a{}, b{};
        int count=3;
        std::copy(triangle.begin(),triangle.end(),a.begin());
        for (const Point &p : triangle) if (!Finite(p)) return 0;
        for (int plane=0; plane<planeCount; ++plane)
        {
            std::array<float,Capacity> distances{};
            std::array<int,Capacity> sides{};
            int front=0, back=0;
            for (int i=0; i<count; ++i)
            {
                distances[i]=Dot(a[i],planes[plane].normal)-planes[plane].dist;
                sides[i]=distances[i]>0.5f ? 1 : distances[i]<-0.5f ? -1 : 0;
                front+=sides[i]==1; back+=sides[i]==-1;
            }
            if (!front) return 0; // Legacy all-coplanar rejection.
            if (!back) continue;
            int written=0;
            auto append=[&](const Point &p) {
                if (written==Capacity) return false;
                b[written++]=p; return true;
            };
            for (int i=0; i<count; ++i)
            {
                const int next=(i+1)%count;
                if (sides[i]>=0 && !append(a[i])) return 0;
                if (!sides[i] || !sides[next] || sides[i]==sides[next]) continue;
                const float f=distances[i]/(distances[i]-distances[next]);
                Point split{};
                for (int axis=0; axis<3; ++axis) split[axis]=a[i][axis]+f*(a[next][axis]-a[i][axis]);
                if (!append(split)) return 0;
            }
            count=written; a.swap(b);
            if (count<3) return 0;
        }
        out=a;
        return count;
    }
};

template<class Fragment>
bool WriteFragment(const Point *points, int count, int maxPoints, float *pointBuffer,
                   int maxFragments, Fragment *fragments, int &writtenPoints, int &writtenFragments)
{
    if (!points || !pointBuffer || !fragments || count<3 || writtenPoints<0 || writtenFragments<0 ||
        writtenPoints>maxPoints || count>maxPoints-writtenPoints || writtenFragments>=maxFragments) return false;
    for (int i=0; i<count; ++i) if (!Finite(points[i])) return false;
    auto &fragment=fragments[writtenFragments++];
    fragment.firstPoint=writtenPoints;
    fragment.numPoints=count;
    for (int v=0; v<count; ++v)
        for (int axis=0; axis<3; ++axis) pointBuffer[(size_t(writtenPoints)+v)*3+axis]=points[v][axis];
    writtenPoints+=count;
    return true;
}
}
