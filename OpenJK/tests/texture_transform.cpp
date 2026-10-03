#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_texture_transform.h"

BOOST_AUTO_TEST_SUITE(TextureTransform)
BOOST_AUTO_TEST_CASE(FootprintsMirrorWithoutChangingGeometryOrV)
{
	float uv[2] = {0.25f, 0.75f};
	const vk_texture_transform_t mirror = {-1, 0, 0, 1, 1, 0};
	VK_TransformTextureCoordinate(uv, mirror);
	BOOST_CHECK_EQUAL(uv[0], 0.75f);
	BOOST_CHECK_EQUAL(uv[1], 0.75f);
	VK_TransformTextureCoordinate(uv, mirror);
	BOOST_CHECK_EQUAL(uv[0], 0.25f);
}
BOOST_AUTO_TEST_CASE(MatrixOrderMatchesLegacyShaderContract)
{
	float uv[2] = {2, 3};
	VK_TransformTextureCoordinate(uv, {1, 2, 3, 4, 5, 6});
	BOOST_CHECK_EQUAL(uv[0], 16);
	BOOST_CHECK_EQUAL(uv[1], 22);
}
BOOST_AUTO_TEST_SUITE_END()
