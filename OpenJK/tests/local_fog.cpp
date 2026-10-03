/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_local_fog.h"
#include <limits>

using namespace vk_local_fog;

static std::vector<Plane> Box()
{
	return {{-1,0,0,512},{1,0,0,512},{0,-1,0,512},
		{0,1,0,512},{0,0,-1,256},{0,0,1,0}};
}

BOOST_AUTO_TEST_CASE(local_fog_boundary_and_density)
{
	const auto fog=Build(Box(),5,{.32549f,.635294f,.0156863f},256);
	BOOST_REQUIRE(fog.valid);
	BOOST_CHECK_EQUAL(fog.mins[2],-256);
	BOOST_CHECK_EQUAL(fog.maxs[2],0);
	float outside[]{0,0,64}, inside[]{0,0,-64}, above[]{0,0,1};
	const auto draw=Parameters(fog,outside);
	BOOST_CHECK_EQUAL(draw.eye,-64);
	BOOST_CHECK_EQUAL(Evaluate(draw.plane,inside),64);
	BOOST_CHECK_CLOSE(Amount(128,64,-64,256),.5f,.001f);
	BOOST_CHECK_EQUAL(Amount(128,Evaluate(draw.plane,above),draw.eye,256),0);
	BOOST_CHECK_EQUAL(Amount(128,.5f,-64,256),0);
	BOOST_CHECK_CLOSE(Amount(64,64,64,256),.5f,.001f);
	BOOST_CHECK_EQUAL(Amount(512,64,64,256),1);
	BOOST_CHECK_EQUAL(Amount(128,-1,64,256),0);
	BOOST_CHECK_EQUAL(Amount(0,64,64,256),0);
	const auto enclosed=Build(Box(),-1,{0,0,0},256);
	BOOST_CHECK_EQUAL(Evaluate(enclosed.plane,outside),1);
}

BOOST_AUTO_TEST_CASE(local_fog_rejects_bad_brush_contracts)
{
	for (int bad=0;bad<8;++bad)
	{
		auto planes=Box(); int visible=5; float depth=256;
		if (bad==0) planes.pop_back();
		if (bad==1) visible=6;
		if (bad==2) visible=-2;
		if (bad==3) planes[0][0]=1;
		if (bad==4) planes[5][3]=-300;
		if (bad==5) planes[0][3]=std::numeric_limits<float>::infinity();
		if (bad==6) depth=std::numeric_limits<float>::quiet_NaN();
		if (bad==7) depth=0;
		BOOST_CHECK(!Build(planes,visible,{0,0,0},depth).valid);
	}
}

BOOST_AUTO_TEST_CASE(local_fog_model_plane_handles_rotation_translation_scale)
{
	auto fog=Build(Box(),5,{0,1,0},256);
	float eye[]{0,0,64};
	// Rotate local X into world Z, nonuniform scale and translation.
	float model[]{0,0,2,0, 0,3,0,0, -4,0,0,0, 20,10,-30,1};
	const auto draw=Parameters(fog,eye,model);
	for (float x : {-50.f,0.f,50.f})
		for (float z : {-30.f,0.f,30.f})
		{
			float local[]{x,4,z}, world[]{20-4*z,22,-30+2*x};
			BOOST_CHECK_CLOSE(Evaluate(draw.plane,local),Evaluate(fog.plane,world),.001f);
			BOOST_CHECK_EQUAL(draw.eye,-64);
		}
}

BOOST_AUTO_TEST_CASE(local_fog_model_selection_and_invalid_global_slots)
{
	const auto fog=Build(Box(),5,{0,1,0},256);
	float eye[]{0,0,-32};
	std::vector<Volume> volumes{{},fog};
	BOOST_CHECK_EQUAL(Select(volumes,{-10,-10,-100},{10,10,-90},eye),1);
	BOOST_CHECK_EQUAL(Select(volumes,{-10,-10,-10},{10,10,10},eye),1);
	BOOST_CHECK_EQUAL(Select(volumes,{600,600,-100},{700,700,-90},eye),-1);
	BOOST_CHECK_EQUAL(Select(volumes,{-10,-10,20},{10,10,30},eye),-1);
	// Intersection with a broad object must not depend on its two diagonal corners.
	BOOST_CHECK_EQUAL(Select(volumes,{-1000,-20,-30},{1000,20,-20},eye),1);
	BOOST_CHECK_EQUAL(Select({}, {-10,-10,-100},{10,10,-90},eye),-1);
}

BOOST_AUTO_TEST_CASE(local_fog_partial_overlap_prefers_eye_volume)
{
    auto left=Build(Box(),5,{0,1,0},256);
    auto right=left;
    right.mins[0]=512; right.maxs[0]=1536;
    const Point mins{500,-10,-20}, maxs{520,10,20};
    float eye[]{700,0,-32};
    BOOST_CHECK_EQUAL(Select({left,right},mins,maxs,eye),1);
    eye[0]=-700;
    BOOST_CHECK_EQUAL(Select({left,right},mins,maxs,eye),0);
}

BOOST_AUTO_TEST_CASE(local_fog_sloped_visible_plane)
{
    auto planes=Box();
    planes.push_back({0,3,4,0});
    const auto fog=Build(planes,6,{0,1,0},256);
    BOOST_REQUIRE(fog.valid);
    float inside[]{0,-30,-40}, outside[]{0,30,40};
    BOOST_CHECK_CLOSE(Evaluate(fog.plane,inside),50,.001f);
    BOOST_CHECK_CLOSE(Evaluate(fog.plane,outside),-50,.001f);
    BOOST_CHECK_CLOSE(Amount(128,50,-50,256),.5f,.001f);
}
