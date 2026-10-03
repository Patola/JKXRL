/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_projected_marks.h"

using namespace vk_marks;
BOOST_AUTO_TEST_SUITE(ProjectedMarks)
static const float footprint[4][3]={{-2,-2,0},{-2,2,0},{2,2,0},{2,-2,0}};
static const float down[3]={0,0,-20};

static float Area(const std::array<Point,Projector::Capacity> &p, int n)
{
    float area=0;
    for (int i=0;i<n;++i) area+=p[i][0]*p[(i+1)%n][1]-p[(i+1)%n][0]*p[i][1];
    return std::fabs(area)*0.5f;
}

BOOST_AUTO_TEST_CASE(FlatWallClipsToFootprintAndPreservesReceiverPlane)
{
    Projector p;
    BOOST_REQUIRE(p.Build(4,footprint,down));
    std::array<Point,Projector::Capacity> out{};
    const int n=p.Clip({Point{-10,-10,-1},Point{0,20,-1},Point{10,-10,-1}},out);
    BOOST_REQUIRE_EQUAL(n,4);
    BOOST_CHECK_CLOSE(Area(out,n),16,0.001f);
    for (int i=0;i<n;++i)
    {
        BOOST_CHECK_EQUAL(out[i][2],-1);
        BOOST_CHECK(std::fabs(out[i][0])<=2.0001f && std::fabs(out[i][1])<=2.0001f);
    }
}

BOOST_AUTO_TEST_CASE(AdjacentTrianglesHaveNoGapOrOverlap)
{
    Projector p;
    BOOST_REQUIRE(p.Build(4,footprint,down));
    const std::array<Point,3> a{Point{-10,-10,0},Point{-10,10,0},Point{10,-10,0}};
    const std::array<Point,3> b{Point{10,-10,0},Point{-10,10,0},Point{10,10,0}};
    std::array<Point,Projector::Capacity> out{};
    float area=Area(out,p.Clip(a,out));
    area+=Area(out,p.Clip(b,out));
    BOOST_CHECK_CLOSE(area,16,0.001f);
}

BOOST_AUTO_TEST_CASE(SlopedAndCurvedMeshTrianglesStayOnTheirPlane)
{
    Projector p;
    BOOST_REQUIRE(p.Build(4,footprint,down));
    for (float slope : {-0.4f,0.0f,0.4f})
    {
        std::array<Point,Projector::Capacity> out{};
        const int n=p.Clip({Point{-10,-10,-10*slope},Point{0,20,0},Point{10,-10,10*slope}},out);
        BOOST_REQUIRE(n>=3);
        for (int i=0;i<n;++i) BOOST_CHECK_SMALL(out[i][2]-out[i][0]*slope,0.0001f);
    }
}

BOOST_AUTO_TEST_CASE(DistantAndSidewaysTrianglesAreRejected)
{
    Projector p;
    BOOST_REQUIRE(p.Build(4,footprint,down));
    std::array<Point,Projector::Capacity> out{};
    for (float z : {-100.0f,100.0f})
        BOOST_CHECK_EQUAL(p.Clip({Point{-10,-10,z},Point{0,20,z},Point{10,-10,z}},out),0);
    BOOST_CHECK_EQUAL(p.Clip({Point{10,10,0},Point{20,10,0},Point{20,20,0}},out),0);
    BOOST_CHECK_EQUAL(p.bounds.PlaneSide({1,0,0},100),2);
    BOOST_CHECK_EQUAL(p.bounds.PlaneSide({1,0,0},-100),1);
    BOOST_CHECK_EQUAL(p.bounds.PlaneSide({1,0,0},0),3);
}

BOOST_AUTO_TEST_CASE(InvalidProjectorsNeverProduceGeometry)
{
    Projector p;
    const float zero[3]={};
    BOOST_CHECK(!p.Build(4,footprint,zero));
    BOOST_CHECK(!p.Build(65,footprint,down));
    BOOST_CHECK(!p.Build(2,footprint,down));
    BOOST_CHECK(!p.Build(4,nullptr,down));
    BOOST_CHECK(!p.Build(4,footprint,nullptr));
    const float invalid[3]={NAN,0,1};
    BOOST_CHECK(!p.Build(4,footprint,invalid));
    const float repeated[4][3]={{0,0,0},{0,0,0},{1,1,0},{1,0,0}};
    BOOST_CHECK(!p.Build(4,repeated,down));
    std::array<Point,Projector::Capacity> out{};
    BOOST_CHECK_EQUAL(p.Clip({Point{0,0,0},Point{0,1,0},Point{1,0,0}},out),0);
}

BOOST_AUTO_TEST_CASE(OutputBudgetsAndGuardsArePreserved)
{
    struct Fragment { int firstPoint=-1,numPoints=-1; };
    const Point triangle[3]={{1,2,3},{4,5,6},{7,8,9}};
    std::array<float,20> points; points.fill(1234);
    std::array<Fragment,3> fragments{};
    int np=0,nf=0;
    BOOST_CHECK(!WriteFragment(triangle,3,2,points.data()+1,1,fragments.data()+1,np,nf));
    BOOST_CHECK_EQUAL(np,0); BOOST_CHECK_EQUAL(nf,0);
    BOOST_CHECK(WriteFragment(triangle,3,6,points.data()+1,1,fragments.data()+1,np,nf));
    BOOST_CHECK(!WriteFragment(triangle,3,6,points.data()+1,1,fragments.data()+1,np,nf));
    BOOST_CHECK_EQUAL(np,3); BOOST_CHECK_EQUAL(nf,1);
    BOOST_CHECK_EQUAL(points[0],1234); BOOST_CHECK_EQUAL(points[10],1234);
    BOOST_CHECK_EQUAL(fragments[0].firstPoint,-1); BOOST_CHECK_EQUAL(fragments[2].firstPoint,-1);
    BOOST_CHECK_EQUAL(fragments[1].firstPoint,0); BOOST_CHECK_EQUAL(fragments[1].numPoints,3);
}

BOOST_AUTO_TEST_SUITE_END()
