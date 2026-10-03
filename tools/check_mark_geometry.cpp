/* SPDX-License-Identifier: GPL-2.0-or-later */
// Read-only asset probe: each input row is normal[3], then triangle xyz[9].
#include "../OpenJK/code/rd-vulkan/vk_projected_marks.h"
#include <iostream>

int main()
{
    using namespace vk_marks;
    unsigned tests=0, hits=0;
    Point normal;
    while (std::cin >> normal[0] >> normal[1] >> normal[2])
    {
        std::array<Point,3> triangle;
        for (auto &p : triangle)
            if (!(std::cin >> p[0] >> p[1] >> p[2])) return 2;
        if (!Normalize(normal)) continue;
        const Point helper = std::fabs(normal[2])<0.9f ? Point{0,0,1} : Point{1,0,0};
        Point u=Cross(normal,helper); Normalize(u);
        const Point v=Cross(normal,u);
        Point center{};
        for (int axis=0; axis<3; ++axis)
            center[axis]=(triangle[0][axis]+triangle[1][axis]+triangle[2][axis])/3;
        float points[4][3], projection[3];
        const float signs[4][2]={{-1,-1},{-1,1},{1,1},{1,-1}};
        for (int axis=0; axis<3; ++axis)
        {
            projection[axis]=-20*normal[axis];
            for (int i=0; i<4; ++i)
                points[i][axis]=center[axis]+6*(signs[i][0]*u[axis]+signs[i][1]*v[axis]);
        }
        Projector p;
        if (!p.Build(4,points,projection)) return 3;
        std::array<Point,Projector::Capacity> out{};
        const int n=p.Clip(triangle,out);
        ++tests;
        if (n<3) continue;
        ++hits;
        for (int i=0; i<n; ++i)
            if (!Finite(out[i]) || std::fabs(Dot(normal,Sub(out[i],center)))>0.1f) return 4;
    }
    std::cout << "triangles=" << tests << " clipped=" << hits << '\n';
    return tests && hits==tests ? 0 : 1;
}
