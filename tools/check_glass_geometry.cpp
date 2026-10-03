/* SPDX-License-Identifier: GPL-2.0-or-later */
// Numeric input from audit_glass_assets.py; tests the production extractor.
#include "../OpenJK/code/qcommon/glass_geometry.h"
#include <iostream>

int main()
{
    int id, count;
    while (std::cin >> id >> count)
    {
        if (count < 0 || count > 100000) return 1;
        std::array<glass_face_t, 2> faces = {};
        while (count--)
        {
            int n; float normal[3];
            if (!(std::cin >> n >> normal[0] >> normal[1] >> normal[2]) || n < 3 || n > 100000) return 1;
            std::vector<std::array<float, 3>> points(n);
            for (auto &p : points) for (auto &x : p) if (!(std::cin >> x)) return 1;
            GlassKeepLargest(faces, GlassBuildFace(std::move(points), normal));
        }
        std::cout << id << ' ' << faces[0].valid << ' ' << faces[1].valid << ' '
            << faces[0].area << ' ' << faces[1].area;
        for (const auto &face : faces)
        {
            float vertices[GLASS_MAX_VERTICES][3] = {}, normal[3], forward[3];
            for (int axis=0; axis<3; ++axis) forward[axis]=-face.normal[axis];
            const int n=GlassSelectPolygon({face, {}}, forward, vertices, GLASS_MAX_VERTICES, normal);
            const auto shards=GlassBuildShards(vertices, n, normal);
            double area=0;
            if (shards.size()>128) return 2;
            for (const auto &shard : shards)
            {
                double a[3], b[3], signedArea=0;
                for (int axis=0; axis<3; ++axis)
                {
                    a[axis]=double(shard.vertices[1][axis])-shard.vertices[0][axis];
                    b[axis]=double(shard.vertices[2][axis])-shard.vertices[0][axis];
                }
                for (int axis=0; axis<3; ++axis)
                    signedArea+=(a[(axis+1)%3]*b[(axis+2)%3]-a[(axis+2)%3]*b[(axis+1)%3])*normal[axis]*0.5;
                if (!(signedArea>0)) return 3;
                area+=signedArea;
                for (const auto &uv : shard.uv) for (float x : uv)
                    if (!std::isfinite(x) || x<0 || x>1) return 4;
            }
            // Compare the actual snapped boundary, not the area estimate based
            // on the authored BSP plane normal (which can differ slightly).
            double expected=0;
            for (int i=1; i+1<n; ++i)
                for (int axis=0; axis<3; ++axis)
                {
                    const int u=(axis+1)%3, v=(axis+2)%3;
                    expected+=((double(vertices[i][u])-vertices[0][u])*(double(vertices[i+1][v])-vertices[0][v])-
                        (double(vertices[i][v])-vertices[0][v])*(double(vertices[i+1][u])-vertices[0][u]))*normal[axis]*0.5;
                }
            if (!shards.empty() && std::abs(area-expected)>std::max(0.1, expected*0.0001))
            {
                std::cerr << "model " << id << " area " << expected << " shard area " << area << '\n';
                return 5;
            }
            std::cout << ' ' << n << ' ' << shards.size();
        }
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
