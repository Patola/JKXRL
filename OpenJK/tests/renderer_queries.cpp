/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_lightgrid_query.h"
#include "../code/qcommon/glass_geometry.h"

namespace {
struct LightGrid
{
    float base[3] = {}, size[3] = {64, 64, 128}, sun[3] = {0, 0, 1};
    int bounds[3] = {2, 2, 2};
    std::vector<dgrid_t> data = std::vector<dgrid_t>(8);
    std::vector<uint16_t> indices = {0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::array<byte, 4>, MAX_LIGHT_STYLES> styles;
    float ambient[3] = {666, 666, 666}, directed[3] = {}, direction[3] = {};
    LightGrid()
    {
        for (auto &style : styles) style.fill(255);
        for (auto &sample : data)
        {
            std::fill(sample.styles, sample.styles+4, LS_NONE);
            sample.styles[0] = 0;
            std::fill(sample.ambientLight[0], sample.ambientLight[0]+3, 100);
            std::fill(sample.directLight[0], sample.directLight[0]+3, 200);
        }
    }
    bool Query(float x=32, float y=32, float z=64)
    {
        const float origin[3] = {x, y, z};
        return VK_QueryLightGrid(origin, base, size, bounds, data, indices,
            styles, sun, ambient, directed, direction);
    }
};

glass_face_t Pane(float z, float facing)
{
    const float normal[3] = {0, 0, facing};
    return GlassBuildFace({{0,0,z}, {100,0,z}, {100,200,z}, {0,200,z}}, normal);
}
}

BOOST_AUTO_TEST_SUITE(RendererQueries)

BOOST_AUTO_TEST_CASE(GridOutputsAreInitializedOnFailure)
{
    LightGrid g;
    g.data.clear();
    BOOST_CHECK(!g.Query());
    for (int c=0; c<3; ++c)
    {
        BOOST_CHECK_EQUAL(g.ambient[c], 255);
        BOOST_CHECK_EQUAL(g.directed[c], 255);
        BOOST_CHECK(std::isfinite(g.direction[c]));
    }
    BOOST_CHECK_EQUAL(g.direction[2], 1);
}

BOOST_AUTO_TEST_CASE(GridInterpolatesAndUsesWorldDirectionWithoutModelBoost)
{
    LightGrid g;
    g.data[7].ambientLight[0][0] = 180;
    for (auto &sample : g.data) sample.latLong[0] = 64;
    BOOST_REQUIRE(g.Query());
    BOOST_CHECK_CLOSE(g.ambient[0], 55.0f, 0.001f);
    BOOST_CHECK_EQUAL(g.ambient[1], 50.0f);
    BOOST_CHECK_EQUAL(g.directed[0], 200.0f);
    BOOST_CHECK_CLOSE(g.direction[0], 1.0f, 0.001f);
    BOOST_CHECK_SMALL(g.direction[2], 0.00001f);
}

BOOST_AUTO_TEST_CASE(GridLightStylesRemainLiveAndMissingCornersRenormalize)
{
    LightGrid g;
    for (auto &sample : g.data) sample.styles[0] = LS_NONE;
    g.data[0].styles[0] = 2;
    g.styles[2] = {255, 0, 128, 255};
    BOOST_REQUIRE(g.Query());
    BOOST_CHECK_EQUAL(g.ambient[0], 50);
    BOOST_CHECK_EQUAL(g.directed[1], 0);
    BOOST_CHECK_CLOSE(g.directed[2], 200.0f*128/255, 0.001f);
    g.styles[2].fill(0);
    BOOST_REQUIRE(g.Query());
    BOOST_CHECK_EQUAL(g.directed[0], 0);
    g.data[0].styles[0] = LS_NONE;
    BOOST_REQUIRE(g.Query());
    BOOST_CHECK_EQUAL(g.ambient[0], 0);
    BOOST_CHECK_EQUAL(g.direction[2], 1);
}

BOOST_AUTO_TEST_CASE(GridClampsEachAxisWithoutWrappingRows)
{
    LightGrid g;
    for (int i=0; i<8; ++i) g.data[i].directLight[0][0] = i*20;
    BOOST_REQUIRE(g.Query(10000, 0, 0));
    BOOST_CHECK_EQUAL(g.directed[0], 20);
    BOOST_REQUIRE(g.Query(-10000, 0, 0));
    BOOST_CHECK_EQUAL(g.directed[0], 0);
    BOOST_REQUIRE(g.Query(10000, 10000, 10000));
    BOOST_CHECK_EQUAL(g.directed[0], 140);
}

BOOST_AUTO_TEST_CASE(GridRejectsMalformedInputsWithoutLeakingPartialResults)
{
    LightGrid g;
    BOOST_CHECK(!g.Query(std::numeric_limits<float>::quiet_NaN()));
    g.indices[7] = 400;
    BOOST_CHECK(!g.Query());
    BOOST_CHECK_EQUAL(g.ambient[0], 255);
    g.indices.pop_back();
    BOOST_CHECK(!g.Query());
    g.size[0] = 0;
    BOOST_CHECK(!g.Query());
}

BOOST_AUTO_TEST_CASE(GlassRecoversCompilerInsertedVerticesAndNormalizedNormal)
{
    const float normal[3] = {0, 0, 8};
    const auto face = GlassBuildFace({{0,0,5}, {50,0,5}, {100,0,5}, {100,200,5},
        {0,200,5}, {50,80,5}, {0,0,5}}, normal);
    BOOST_REQUIRE(face.valid);
    BOOST_CHECK_CLOSE(face.area, 20000.0, 0.001);
    BOOST_CHECK_EQUAL(face.normal[2], 1);
    BOOST_CHECK(GlassQuadValid(face.vertices));
}

BOOST_AUTO_TEST_CASE(GlassSelectsOppositeBroadFacesForOppositeViews)
{
    std::array<glass_face_t, 2> faces = {};
    GlassKeepLargest(faces, Pane(0, -1));
    GlassKeepLargest(faces, Pane(2, 1));
    float vertices[4][3], normal[3];
    float forward[3] = {0,0,1};
    BOOST_REQUIRE(GlassSelectFace(faces, forward, vertices, normal));
    BOOST_CHECK_EQUAL(vertices[0][2], 0);
    BOOST_CHECK_EQUAL(normal[2], -1);
    forward[2] = -1;
    BOOST_REQUIRE(GlassSelectFace(faces, forward, vertices, normal));
    BOOST_CHECK_EQUAL(vertices[0][2], 2);
    BOOST_CHECK_EQUAL(normal[2], 1);
}

BOOST_AUTO_TEST_CASE(GlassFailureClearsAllOutputsAndDoesNotSubstituteThinSide)
{
    std::array<glass_face_t, 2> faces = {};
    GlassKeepLargest(faces, Pane(0, 1));
    const float up[3] = {0,0,1};
    const auto unsupported = GlassBuildFace({{0,0,0}, {1000,0,0}, {0,1000,0}}, up);
    BOOST_CHECK(!unsupported.valid);
    BOOST_CHECK_GT(unsupported.area, faces[0].area);
    GlassKeepLargest(faces, unsupported);
    float vertices[4][3], normal[3];
    for (auto &v : vertices) std::fill(v, v+3, 123.0f);
    BOOST_CHECK(!GlassSelectFace(faces, up, vertices, normal));
    for (auto &v : vertices) for (float x : v) BOOST_CHECK_EQUAL(x, 0);
    for (float x : normal) BOOST_CHECK_EQUAL(x, 0);
}

BOOST_AUTO_TEST_CASE(GlassRejectsInvalidTessellationSeeds)
{
    auto face = Pane(0, 1);
    BOOST_REQUIRE(face.valid);
    std::swap(face.vertices[0], face.vertices[1]);
    BOOST_CHECK(!GlassQuadValid(face.vertices));
    face = Pane(0, 1);
    face.vertices[3][2] = 100;
    BOOST_CHECK(!GlassQuadValid(face.vertices));
    face.vertices[3][2] = std::numeric_limits<float>::infinity();
    BOOST_CHECK(!GlassQuadValid(face.vertices));
    face = {};
    BOOST_CHECK(!GlassQuadValid(face.vertices));
}

BOOST_AUTO_TEST_CASE(GlassAcceptsBspCoordinateQuantization)
{
    const float normal[3] = {0, -0.2f, 1};
    const auto face = GlassBuildFace({{0,0,0}, {100,0,0}, {100,100,20.125f}, {0,100,20}}, normal);
    BOOST_REQUIRE(face.valid);
    BOOST_CHECK(GlassQuadValid(face.vertices));
    const auto warped = GlassBuildFace({{0,0,0}, {100,0,0}, {100,100,25}, {0,100,20}}, normal);
    BOOST_CHECK(!warped.valid);
}

BOOST_AUTO_TEST_CASE(GlassPolygonCapacityAndViewSelection)
{
    std::array<glass_face_t, 2> faces;
    for (int side = 0; side < 2; ++side)
    {
        const float z = side * 2.0f, normal[3] = {0, 0, side ? 1.0f : -1.0f};
        faces[side] = GlassBuildFace({{0,0,z}, {100,0,z}, {120,60,z}, {50,100,z}, {0,60,z}}, normal);
        BOOST_REQUIRE_EQUAL(faces[side].boundary.size(), 5);
        BOOST_CHECK(!faces[side].valid);
    }
    float vertices[6][3] = {}, normal[3], forward[3] = {0,0,1};
    std::fill(vertices[5], vertices[5]+3, 123.0f);
    BOOST_REQUIRE_EQUAL(GlassSelectPolygon(faces, forward, vertices, 5, normal), 5);
    BOOST_CHECK_EQUAL(normal[2], -1);
    BOOST_CHECK_EQUAL(vertices[0][2], 0);
    for (float x : vertices[5]) BOOST_CHECK_EQUAL(x, 123);
    forward[2] = -1;
    BOOST_REQUIRE_EQUAL(GlassSelectPolygon(faces, forward, vertices, 5, normal), 5);
    BOOST_CHECK_EQUAL(normal[2], 1);
    BOOST_CHECK_EQUAL(vertices[0][2], 2);
    BOOST_CHECK_EQUAL(GlassSelectPolygon(faces, forward, vertices, 4, normal), 0);
    for (int i=0; i<4; ++i) for (float x : vertices[i]) BOOST_CHECK_EQUAL(x, 0);
    for (float x : normal) BOOST_CHECK_EQUAL(x, 0);
    BOOST_CHECK_EQUAL(GlassSelectPolygon(faces, forward, nullptr, 5, normal), 0);
}

BOOST_AUTO_TEST_CASE(GlassPolygonShardsPreserveCoverageWindingAndBudget)
{
    for (float scale : {0.1f, 1.0f, 100.0f})
    for (float facing : {-1.0f, 1.0f})
    for (int axis=0; axis<3; ++axis)
    {
        const float shape[5][2] = {{0,0}, {100,0}, {120,60}, {50,100}, {0,60}};
        float vertices[5][3] = {}, normal[3] = {};
        normal[axis] = facing;
        for (int i=0; i<5; ++i)
        {
            vertices[i][(axis+1)%3] = shape[i][0]*scale;
            vertices[i][(axis+2)%3] = shape[i][1]*scale;
        }
        const auto shards = GlassBuildShards(vertices, 5, normal);
        BOOST_REQUIRE(!shards.empty());
        BOOST_CHECK_LE(shards.size(), 128);
        double total = 0;
        const int u=(axis+1)%3, v=(axis+2)%3;
        for (const auto &shard : shards)
        {
            const auto &p=shard.vertices;
            const double area=((double(p[1][u])-p[0][u])*(double(p[2][v])-p[0][v])-
                (double(p[1][v])-p[0][v])*(double(p[2][u])-p[0][u]))*facing*0.5;
            BOOST_CHECK_GT(area, 0);
            total += area;
            for (const auto &uv : shard.uv) for (float x : uv)
            {
                BOOST_CHECK(std::isfinite(x));
                BOOST_CHECK_GE(x, 0);
                BOOST_CHECK_LE(x, 1);
            }
            for (const auto &point : p)
                for (int i=0; i<5; ++i)
                {
                    const auto &a=vertices[i], &b=vertices[(i+1)%5];
                    const double edge=(double(b[u])-a[u])*(double(point[v])-a[v])-
                        (double(b[v])-a[v])*(double(point[u])-a[u]);
                    BOOST_CHECK_GE(edge, -0.01*scale*scale);
                }
        }
        BOOST_CHECK_CLOSE(total, 9000.0*scale*scale, 0.001);
    }
}

BOOST_AUTO_TEST_CASE(GlassPolygonRejectsInvalidSeeds)
{
    float p[3][3] = {{0,0,0}, {100,0,0}, {0,100,0}}, normal[3]={0,0,1};
    BOOST_CHECK(!GlassBuildShards(p, 3, normal).empty());
    BOOST_CHECK(GlassBuildShards(p, 2, normal).empty());
    BOOST_CHECK(GlassBuildShards(p, GLASS_MAX_VERTICES+1, normal).empty());
    BOOST_CHECK(GlassBuildShards(nullptr, 3, normal).empty());
    p[2][2] = 100;
    BOOST_CHECK(GlassBuildShards(p, 3, normal).empty());
    p[2][2] = std::numeric_limits<float>::quiet_NaN();
    BOOST_CHECK(GlassBuildShards(p, 3, normal).empty());
    p[2][2] = 0;
    normal[2] = 0;
    BOOST_CHECK(GlassBuildShards(p, 3, normal).empty());
}

BOOST_AUTO_TEST_SUITE_END()
