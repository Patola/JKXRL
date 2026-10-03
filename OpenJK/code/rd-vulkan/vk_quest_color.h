/* Experimental Quest 1.1.27 texture preparation; opt-in only. */
#ifndef VK_QUEST_COLOR_H
#define VK_QUEST_COLOR_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

inline int VK_QuestColorProfile( int requested, bool legacyColorActive )
{
	return legacyColorActive && requested >= 0 && requested <= 3 ? requested : 0;
}

inline std::array<uint8_t, 256> VK_QuestTextureLookup( bool mipmapped )
{
	std::array<uint8_t, 256> lookup = {};
	for ( int i = 0; i < 256; ++i )
	{
		const int intensity = mipmapped ? std::min( 255, static_cast<int>( i * 1.07f ) ) : i;
		lookup[i] = static_cast<uint8_t>( std::clamp(
			static_cast<int>( 255.0 * std::pow( intensity / 255.0, 1.0 / 1.15f ) + 0.5 ),
			0, 255 ) );
	}
	return lookup;
}

// Input is the existing tightly packed raw RGBA mip chain. GLES reduced picmip
// before light scaling, then generated the remaining mips from corrected bytes.
// Keep the retained higher-resolution levels usable by Vulkan's clamp sampler.
inline void VK_ApplyQuestTextureProfile(
	uint8_t *pixels, uint32_t width, uint32_t height, uint32_t levels, int picmip,
	bool mipmapped )
{
	const auto lookup = VK_QuestTextureLookup( mipmapped );
	const uint32_t base = std::min( static_cast<uint32_t>( std::max( 0, picmip ) ), levels - 1 );
	size_t offset = 0;
	size_t previousOffset = 0;
	uint32_t previousWidth = width;
	uint32_t previousHeight = height;
	for ( uint32_t level = 0; level < levels; ++level )
	{
		for ( uint32_t y = 0; y < height; ++y )
		{
			for ( uint32_t x = 0; x < width; ++x )
			{
				for ( uint32_t component = 0; component < 3; ++component )
				{
					uint8_t &value = pixels[offset + ( static_cast<size_t>( y ) * width + x ) * 4 + component];
					if ( level <= base )
					{
						value = lookup[value];
					}
					else
					{
						unsigned total = 0;
						for ( uint32_t dy = 0; dy < 2; ++dy )
						{
							for ( uint32_t dx = 0; dx < 2; ++dx )
							{
								const uint32_t sx = std::min( previousWidth - 1, x * 2 + dx );
								const uint32_t sy = std::min( previousHeight - 1, y * 2 + dy );
								total += pixels[previousOffset +
									( static_cast<size_t>( sy ) * previousWidth + sx ) * 4 + component];
							}
						}
						value = static_cast<uint8_t>( total / 4 );
					}
				}
			}
		}
		previousOffset = offset;
		offset += static_cast<size_t>( width ) * height * 4;
		previousWidth = width;
		previousHeight = height;
		width = std::max( 1u, width / 2 );
		height = std::max( 1u, height / 2 );
	}
}

#endif
