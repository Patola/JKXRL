/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later

Headless raster check for the production console vertex shader. No OpenXR or
game assets required. Arguments: vertex.spv probe.frag.spv. Exit 77: no GPU.
*/
#include <vulkan/vulkan.h>
#include "../OpenJK/code/rd-vulkan/vk_save_preview_transfer.h"
#include "../OpenJK/code/rd-vulkan/vk_console_projection.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Check( VkResult result )
{
	if ( result != VK_SUCCESS ) throw std::runtime_error( "Vulkan result " + std::to_string( result ) );
}

struct Probe
{
	static constexpr uint32_t size = 256;
	VkInstance instance = {};
	VkPhysicalDevice physical = {};
	VkDevice device = {};
	VkQueue queue = {};
	VkImage image = {};
	VkImage thumbnail = {};
	VkDeviceMemory thumbnailMemory = {};
	VkDeviceMemory imageMemory = {}, bufferMemory = {};
	VkImageView view = {};
	VkBuffer buffer = {};
	VkCommandPool pool = {};
	VkCommandBuffer cmd = {};
	VkPipelineLayout layout = {};
	VkPipeline pipeline = {};
	VkShaderModule vert = {}, frag = {};
	void *mapped = nullptr;

	~Probe()
	{
		if ( device )
		{
			vkDeviceWaitIdle( device );
			if ( mapped ) vkUnmapMemory( device, bufferMemory );
			vkDestroyPipeline( device, pipeline, nullptr );
			vkDestroyPipelineLayout( device, layout, nullptr );
			vkDestroyShaderModule( device, vert, nullptr );
			vkDestroyShaderModule( device, frag, nullptr );
			vkDestroyCommandPool( device, pool, nullptr );
			vkDestroyBuffer( device, buffer, nullptr );
			vkFreeMemory( device, bufferMemory, nullptr );
			vkDestroyImageView( device, view, nullptr );
			vkDestroyImage( device, image, nullptr );
			vkFreeMemory( device, imageMemory, nullptr );
			vkDestroyImage( device, thumbnail, nullptr );
			vkFreeMemory( device, thumbnailMemory, nullptr );
			vkDestroyDevice( device, nullptr );
		}
		if ( instance ) vkDestroyInstance( instance, nullptr );
	}

	VkDeviceMemory Allocate( VkMemoryRequirements req, VkMemoryPropertyFlags flags )
	{
		VkPhysicalDeviceMemoryProperties props;
		vkGetPhysicalDeviceMemoryProperties( physical, &props );
		for ( uint32_t i = 0; i < props.memoryTypeCount; ++i )
		{
			if ( ( req.memoryTypeBits & ( 1u << i ) ) && ( props.memoryTypes[i].propertyFlags & flags ) == flags )
			{
				VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
				info.allocationSize = req.size;
				info.memoryTypeIndex = i;
				VkDeviceMemory memory;
				Check( vkAllocateMemory( device, &info, nullptr, &memory ) );
				return memory;
			}
		}
		throw std::runtime_error( "No matching memory type" );
	}

	VkShaderModule Shader( const char *path )
	{
		std::ifstream file( path, std::ios::binary | std::ios::ate );
		if ( !file ) throw std::runtime_error( path );
		const auto bytes = static_cast<size_t>( file.tellg() );
		if ( !bytes || bytes % 4 ) throw std::runtime_error( "Invalid SPIR-V size" );
		std::vector<uint32_t> words( bytes / 4 );
		file.seekg( 0 ); file.read( reinterpret_cast<char *>( words.data() ), bytes );
		VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		info.codeSize = bytes; info.pCode = words.data();
		VkShaderModule module;
		Check( vkCreateShaderModule( device, &info, nullptr, &module ) );
		return module;
	}

	bool Init( const char *vertex, const char *fragment )
	{
		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &app;
		if ( vkCreateInstance( &instanceInfo, nullptr, &instance ) != VK_SUCCESS ) return false;
		uint32_t count = 0;
		Check( vkEnumeratePhysicalDevices( instance, &count, nullptr ) );
		std::vector<VkPhysicalDevice> devices( count );
		Check( vkEnumeratePhysicalDevices( instance, &count, devices.data() ) );
		uint32_t family = 0;
		for ( const auto candidate : devices )
		{
			VkPhysicalDeviceVulkan13Features f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties( candidate, &properties );
			if ( properties.apiVersion < VK_API_VERSION_1_3 ) continue;
			VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f };
			vkGetPhysicalDeviceFeatures2( candidate, &features );
			if ( !f.dynamicRendering ) continue;
			vkGetPhysicalDeviceQueueFamilyProperties( candidate, &count, nullptr );
			std::vector<VkQueueFamilyProperties> queues( count );
			vkGetPhysicalDeviceQueueFamilyProperties( candidate, &count, queues.data() );
			for ( uint32_t i = 0; i < count; ++i )
				if ( queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) { physical = candidate; family = i; break; }
			if ( physical ) break;
		}
		if ( !physical ) return false;
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties( physical, &properties );
		std::cout << "Device: " << properties.deviceName << '\n';
		const float priority = 1;
		VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
		VkPhysicalDeviceVulkan13Features features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features.dynamicRendering = VK_TRUE;
		VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features };
		deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
		Check( vkCreateDevice( physical, &deviceInfo, nullptr, &device ) );
		vkGetDeviceQueue( device, family, 0, &queue );
		VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.extent = { size, size, 1 }; imageInfo.mipLevels = imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		Check( vkCreateImage( device, &imageInfo, nullptr, &image ) );
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements( device, image, &req ); imageMemory = Allocate( req, 0 );
		Check( vkBindImageMemory( device, image, imageMemory, 0 ) );
		imageInfo.extent = {size/2, size/2, 1};
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		Check(vkCreateImage(device, &imageInfo, nullptr, &thumbnail));
		vkGetImageMemoryRequirements(device, thumbnail, &req);
		thumbnailMemory = Allocate(req, 0);
		Check(vkBindImageMemory(device, thumbnail, thumbnailMemory, 0));
		VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = imageInfo.format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		Check( vkCreateImageView( device, &viewInfo, nullptr, &view ) );
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = size * size * 4; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		Check( vkCreateBuffer( device, &bufferInfo, nullptr, &buffer ) );
		vkGetBufferMemoryRequirements( device, buffer, &req );
		bufferMemory = Allocate( req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		Check( vkBindBufferMemory( device, buffer, bufferMemory, 0 ) );
		Check( vkMapMemory( device, bufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped ) );
		VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.queueFamilyIndex = family; poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		Check( vkCreateCommandPool( device, &poolInfo, nullptr, &pool ) );
		VkCommandBufferAllocateInfo commandInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		commandInfo.commandPool = pool; commandInfo.commandBufferCount = 1;
		Check( vkAllocateCommandBuffers( device, &commandInfo, &cmd ) );
		VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112 };
		VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &range;
		Check( vkCreatePipelineLayout( device, &layoutInfo, nullptr, &layout ) );
		vert = Shader( vertex ); frag = Shader( fragment );
		VkPipelineShaderStageCreateInfo stages[2] = {};
		for ( auto &stage : stages ) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag;
		VkPipelineVertexInputStateCreateInfo input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkPipelineInputAssemblyStateCreateInfo assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkViewport viewport = { 0, 0, float( size ), float( size ), 0, 1 };
		VkRect2D scissor = { { 0, 0 }, { size, size } };
		VkPipelineViewportStateCreateInfo viewportInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewportInfo.viewportCount = viewportInfo.scissorCount = 1;
		viewportInfo.pViewports = &viewport; viewportInfo.pScissors = &scissor;
		VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.lineWidth = 1;
		VkPipelineMultisampleStateCreateInfo samples = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineColorBlendAttachmentState attachment = {}; attachment.colorWriteMask = 15;
		VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = 1; blend.pAttachments = &attachment;
		VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &imageInfo.format;
		VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering };
		info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &input;
		info.pInputAssemblyState = &assembly; info.pViewportState = &viewportInfo;
		info.pRasterizationState = &raster; info.pMultisampleState = &samples;
		info.pColorBlendState = &blend; info.layout = layout;
		Check( vkCreateGraphicsPipelines( device, {}, 1, &info, nullptr, &pipeline ) );
		return true;
	}

	void Render( const std::vector<std::array<float, 28>> &rects, bool preview = false )
	{
		Check( vkResetCommandBuffer( cmd, 0 ) );
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		Check( vkBeginCommandBuffer( cmd, &begin ) );
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.image = image; barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier );
		VkRenderingAttachmentInfo attachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		attachment.imageView = view; attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkRenderingInfo render = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		render.renderArea.extent = { size, size }; render.layerCount = 1;
		render.colorAttachmentCount = 1; render.pColorAttachments = &attachment;
		vkCmdBeginRendering( cmd, &render );
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		for ( const auto &rect : rects )
		{
			vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof( rect ), rect.data() );
			vkCmdDraw( cmd, 6, 1, 0, 0 );
		}
		vkCmdEndRendering( cmd );
		VK_RecordSavePreviewCopy(cmd, image, buffer, size, size,
			preview ? thumbnail : VK_NULL_HANDLE, size/2, size/2);
		Check( vkEndCommandBuffer( cmd ) );
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
		Check( vkQueueSubmit( queue, 1, &submit, {} ) ); Check( vkQueueWaitIdle( queue ) );
	}
};

static std::array<float, 28> Rectangle( float yaw, int scenario, float x0, float y0, float x1, float y1 )
{
	std::array<float, 28> push = {};
	push[8] = push[9] = push[10] = push[11] = 1;
	// Global panel UVs must agree whether rasterized as one quad or many keys.
	push[4] = ( x0 + 3 ) / 6; push[5] = ( y0 + 2 ) / 4;
	push[6] = ( x1 + 3 ) / 6; push[7] = ( y1 + 2 ) / 4; push[27] = 3;
	const float pitch = scenario == 1 ? 0.3f : scenario == 2 ? -0.25f : 0;
	const float roll = scenario == 1 ? -0.2f : scenario == 2 ? 0.15f : 0;
	const float eye = scenario == 1 ? -0.032f : scenario == 2 ? 0.032f : 0;
	const float asymmetric = scenario == 1 ? -0.1f : scenario == 2 ? 0.1f : 0;
	const float distance = scenario == 3 ? 0.4f : 6;
	for ( int i = 0; i < 4; ++i )
	{
		float clip[3];
		const float x = i & 1 ? x1 : x0;
		const float y = i & 2 ? y1 : y0;
		const float vx = x * cos( yaw ) - distance * sin( yaw ) - eye;
		const float vz = -x * sin( yaw ) - distance * cos( yaw );
		const float vy = y * cos( pitch ) - vz * sin( pitch );
		const float z = y * sin( pitch ) + vz * cos( pitch );
		VK_ConsoleViewClipPoint( vx * cos( roll ) - vy * sin( roll ),
			vx * sin( roll ) + vy * cos( roll ), z,
			-1 + asymmetric, 1 + asymmetric, -1, 1, clip );
		push[12 + i * 2] = clip[0]; push[13 + i * 2] = clip[1]; push[20 + i] = clip[2];
	}
	return push;
}

int main( int argc, char **argv )
{
	if ( argc != 3 ) { std::cerr << "Usage: check_console_gpu vertex.spv probe.frag.spv\n"; return 1; }
	try
	{
		Probe gpu;
		if ( !gpu.Init( argv[1], argv[2] ) ) return 77;
		int failures = 0;
		for ( int scenario = 0; scenario < 4; ++scenario )
		for ( int degrees = -180; degrees <= 180; degrees += 5 )
		{
			const float yaw = degrees * 3.14159265358979323846f / 180;
			gpu.Render( { Rectangle( yaw, scenario, -3, -2, 3, 2 ) } );
			const auto *bytes = static_cast<const uint8_t *>( gpu.mapped );
			const std::vector<uint8_t> whole( bytes, bytes + Probe::size * Probe::size * 4 );
			std::vector<std::array<float, 28>> tiles;
			for ( int row = 0; row < 8; ++row )
				for ( int col = 0; col < 12; ++col )
					tiles.push_back( Rectangle( yaw, scenario, -3 + col * 0.5f, -2 + row * 0.5f,
						-2.5f + col * 0.5f, -1.5f + row * 0.5f ) );
			gpu.Render( tiles );
			size_t different = 0, interiorDifferent = 0, uvDifferent = 0, visible = 0;
			for ( size_t i = 0; i < whole.size(); i += 4 )
			{
				if ( whole[i + 2] ) ++visible;
				if ( whole[i + 2] != bytes[i + 2] )
				{
					++different;
					// Clipping/tessellation can round an edge by one pixel. Only
					// accept that boundary difference, never displaced interiors.
					bool boundary = false;
					const int x = ( i / 4 ) % Probe::size, y = ( i / 4 ) / Probe::size;
					for ( int dy = -1; dy <= 1; ++dy )
						for ( int dx = -1; dx <= 1; ++dx )
							if ( x + dx >= 0 && x + dx < int( Probe::size ) &&
								y + dy >= 0 && y + dy < int( Probe::size ) )
							{
								const size_t j = ( ( y + dy ) * Probe::size + x + dx ) * 4 + 2;
								boundary |= whole[i + 2] == bytes[j];
							}
					if ( !boundary ) ++interiorDifferent;
				}
				else if ( whole[i + 2] && ( std::abs( int( whole[i] ) - bytes[i] ) > 2 ||
					std::abs( int( whole[i + 1] ) - bytes[i + 1] ) > 2 ) ) ++uvDifferent;
			}
			if ( ( degrees == 0 && visible < 1000 ) || ( std::abs( degrees ) == 180 && visible ) )
				throw std::runtime_error( "Unexpected front/behind-eye panel coverage" );
			if ( interiorDifferent || uvDifferent )
			{
				std::cerr << "FAIL scenario=" << scenario << " yaw=" << degrees
					<< " coverage=" << different << " interior=" << interiorDifferent
					<< " UV=" << uvDifferent << '\n';
				++failures;
			}
		}
		if ( failures ) return 1;
		std::cout << "PASS: panel and 96 coplanar tiles agree in coverage and UV across 292 views\n";
		// Exercise the production GPU thumbnail path, checking its 4:3 center
		// crop and linear filter against independently sampled full-size pixels.
		const auto rect = Rectangle(0, 0, -3, -2, 3, 2);
		gpu.Render({rect});
		const auto *bytes = static_cast<const uint8_t *>(gpu.mapped);
		const std::vector<uint8_t> original(bytes, bytes+Probe::size*Probe::size*4);
		gpu.Render({rect}, true);
		for (int y=0; y<128; ++y)
			for (int x=0; x<128; ++x)
				for (int c=0; c<4; ++c)
				{
					const float sy = 32+(y+.5f)*192/128-.5f;
					const int y0 = int(sy);
					const float f = sy-y0;
					const auto sample = [&](int yy) {
						return (original[(yy*256+x*2)*4+c]+original[(yy*256+x*2+1)*4+c])*.5f;
					};
					const float expected = sample(y0)*(1-f)+sample(y0+1)*f;
					if (std::abs(bytes[(y*128+x)*4+c]-expected)>2)
						throw std::runtime_error("Save preview GPU crop/filter mismatch");
				}
		std::cout << "PASS: GPU save-preview center crop, resize and readback\n";
		return 0;
	}
	catch ( const std::exception &e ) { std::cerr << e.what() << '\n'; return 1; }
}
