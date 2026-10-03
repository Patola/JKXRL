/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later

Headless raster check for the production world fragment shader and lightstyle
samplers. No OpenXR or game assets required. Exit 77: no Vulkan 1.3 GPU.
*/
#include <vulkan/vulkan.h>
#include "../OpenJK/code/rd-vulkan/vk_material_blend.h"
#include "../OpenJK/code/rd-vulkan/vk_local_fog.h"

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
	VkDeviceMemory imageMemory = {}, bufferMemory = {};
	VkImageView view = {};
	VkBuffer buffer = {};
	VkCommandPool pool = {};
	VkCommandBuffer cmd = {};
	VkPipelineLayout layout = {};
	VkPipeline pipeline = {};
	VkPipeline equalPipeline = {}, equalModulatePipeline = {};
	VkPipeline decalPipeline = {};
	VkPipeline inverseAlphaPipeline = {}, inverseAlphaBothPipeline = {};
	VkPipeline fogPipeline = {}, fogEqualPipeline = {}, additivePipeline = {}, doubleModulatePipeline = {};
	VkImage depthImage = {};
	VkDeviceMemory depthMemory = {};
	VkImageView depthView = {};
	VkShaderModule vert = {}, frag = {};
	void *mapped = nullptr;
	VkDescriptorSetLayout setLayouts[3]{};
	VkDescriptorSet sets[3]{};
	VkDescriptorPool descriptors{};
	VkSampler sampler{};
	VkImage sources[4]{};
	VkImageView sourceViews[4]{};
	VkDeviceMemory sourceMemory[4]{};

	~Probe()
	{
		if ( device )
		{
			vkDeviceWaitIdle( device );
			if ( mapped ) vkUnmapMemory( device, bufferMemory );
			vkDestroyPipeline( device, pipeline, nullptr );
			vkDestroyPipeline( device, equalPipeline, nullptr );
			vkDestroyPipeline( device, equalModulatePipeline, nullptr );
			vkDestroyPipeline( device, decalPipeline, nullptr );
			vkDestroyPipeline( device, inverseAlphaPipeline, nullptr );
			vkDestroyPipeline( device, inverseAlphaBothPipeline, nullptr );
			vkDestroyPipeline( device, fogPipeline, nullptr );
			vkDestroyPipeline( device, fogEqualPipeline, nullptr );
			vkDestroyPipeline( device, additivePipeline, nullptr );
			vkDestroyPipeline( device, doubleModulatePipeline, nullptr );
			vkDestroyImageView( device, depthView, nullptr );
			vkDestroyImage( device, depthImage, nullptr );
			vkFreeMemory( device, depthMemory, nullptr );
			vkDestroyDescriptorPool(device, descriptors, nullptr);
			for (auto l : setLayouts) vkDestroyDescriptorSetLayout(device, l, nullptr);
			vkDestroySampler(device, sampler, nullptr);
			for (int i=0; i<4; ++i) {
				vkDestroyImageView(device, sourceViews[i], nullptr);
				vkDestroyImage(device, sources[i], nullptr);
				vkFreeMemory(device, sourceMemory[i], nullptr);
			}
			vkDestroyPipelineLayout( device, layout, nullptr );
			vkDestroyShaderModule( device, vert, nullptr );
			vkDestroyShaderModule( device, frag, nullptr );
			vkDestroyCommandPool( device, pool, nullptr );
			vkDestroyBuffer( device, buffer, nullptr );
			vkFreeMemory( device, bufferMemory, nullptr );
			vkDestroyImageView( device, view, nullptr );
			vkDestroyImage( device, image, nullptr );
			vkFreeMemory( device, imageMemory, nullptr );
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
			if ( !f.dynamicRendering || !f.shaderDemoteToHelperInvocation ) continue;
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
		features.shaderDemoteToHelperInvocation = VK_TRUE;
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
		VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = imageInfo.format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		Check( vkCreateImageView( device, &viewInfo, nullptr, &view ) );
		VkImageCreateInfo depthInfo = imageInfo;
		depthInfo.format = VK_FORMAT_D32_SFLOAT;
		depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		Check(vkCreateImage(device, &depthInfo, nullptr, &depthImage));
		vkGetImageMemoryRequirements(device, depthImage, &req);
		depthMemory = Allocate(req, 0);
		Check(vkBindImageMemory(device, depthImage, depthMemory, 0));
		VkImageViewCreateInfo depthViewInfo = viewInfo;
		depthViewInfo.image = depthImage; depthViewInfo.format = depthInfo.format;
		depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		Check(vkCreateImageView(device, &depthViewInfo, nullptr, &depthView));
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = 524288; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
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
		CreateSources();
		VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
		VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = 3; layoutInfo.pSetLayouts = setLayouts;
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
		rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
		VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
		depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering };
		info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &input;
		info.pInputAssemblyState = &assembly; info.pViewportState = &viewportInfo;
		info.pRasterizationState = &raster; info.pMultisampleState = &samples;
		info.pColorBlendState = &blend; info.layout = layout;
		info.pDepthStencilState = &depth;
		Check( vkCreateGraphicsPipelines( device, {}, 1, &info, nullptr, &pipeline ) );
		depth.depthWriteEnable = VK_FALSE;
		depth.depthCompareOp = VK_COMPARE_OP_EQUAL;
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &equalPipeline));
		attachment = VK_MaterialBlendAttachment(VK_BLEND_MODULATE);
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &equalModulatePipeline));
		attachment = VK_MaterialBlendAttachment(VK_BLEND_ALPHA);
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &fogEqualPipeline));
		depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &fogPipeline));
		depth.depthWriteEnable = VK_TRUE;
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &decalPipeline));
		depth.depthWriteEnable = VK_FALSE;
		attachment = VK_MaterialBlendAttachment(VK_BLEND_ADDITIVE);
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &additivePipeline));
		attachment = VK_MaterialBlendAttachment(VK_BLEND_DOUBLE_MODULATE);
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &doubleModulatePipeline));
		attachment = VK_MaterialBlendAttachment(VK_BLEND_INVERSE_ALPHA);
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &inverseAlphaPipeline));
		attachment = VK_MaterialBlendAttachment(VK_BLEND_INVERSE_ALPHA_BOTH);
		Check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &inverseAlphaBothPipeline));
		return true;
	}

	void CreateSources()
	{
		VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		Check(vkCreateSampler(device,&si,nullptr,&sampler));
		for(int i=0;i<4;++i) {
			VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			im.imageType=VK_IMAGE_TYPE_2D; im.format=VK_FORMAT_R8G8B8A8_UNORM; im.extent={2,1,1};
			im.mipLevels=im.arrayLayers=1; im.samples=VK_SAMPLE_COUNT_1_BIT;
			im.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			Check(vkCreateImage(device,&im,nullptr,&sources[i]));
			VkMemoryRequirements req; vkGetImageMemoryRequirements(device,sources[i],&req);
			sourceMemory[i]=Allocate(req,0); Check(vkBindImageMemory(device,sources[i],sourceMemory[i],0));
			VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			vi.image=sources[i]; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=im.format;
			vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
			Check(vkCreateImageView(device,&vi,nullptr,&sourceViews[i]));
		}
		VkDescriptorSetLayoutBinding binding[2]{};
		binding[0]={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
		binding[1]={1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
		VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		li.bindingCount=1; li.pBindings=binding;
		Check(vkCreateDescriptorSetLayout(device,&li,nullptr,&setLayouts[0]));
		li.bindingCount=0;
		Check(vkCreateDescriptorSetLayout(device,&li,nullptr,&setLayouts[1]));
		binding[0].descriptorCount=3; li.bindingCount=2;
		Check(vkCreateDescriptorSetLayout(device,&li,nullptr,&setLayouts[2]));
		const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1}};
		VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		pi.maxSets=3; pi.poolSizeCount=2; pi.pPoolSizes=sizes;
		Check(vkCreateDescriptorPool(device,&pi,nullptr,&descriptors));
		VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		ai.descriptorPool=descriptors; ai.descriptorSetCount=3; ai.pSetLayouts=setLayouts;
		Check(vkAllocateDescriptorSets(device,&ai,sets));
		VkDescriptorImageInfo images[4]{};
		for(int i=0;i<4;++i) images[i]={sampler,sourceViews[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkDescriptorBufferInfo bi{buffer,262144,176};
		VkWriteDescriptorSet writes[3]{};
		for(auto& w:writes) w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet=sets[0]; writes[0].descriptorCount=1;
		writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo=images;
		writes[1]=writes[0]; writes[1].dstSet=sets[2]; writes[1].descriptorCount=3; writes[1].pImageInfo=images+1;
		writes[2].dstSet=sets[2]; writes[2].dstBinding=1; writes[2].descriptorCount=1;
		writes[2].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; writes[2].pBufferInfo=&bi;
		vkUpdateDescriptorSets(device,3,writes,0,nullptr);
	}

	void Render( const std::vector<std::array<float, 32>> &rects,
		bool cutout = false, float clearDepth = 1.0f, bool brokenLightmap = false,
		const std::vector<VkPipeline>& pipelines = {}, bool patterned = false )
	{
		Check( vkResetCommandBuffer( cmd, 0 ) );
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		Check( vkBeginCommandBuffer( cmd, &begin ) );
		uint8_t pixels[32] = {
			51,77,102,255, 51,77,102,255,
			0,0,0,255, 204,0,0,255,
			0,51,0,255, 0,51,0,255,
			0,0,77,255, 0,0,77,255};
		if (cutout) pixels[3] = 0;
		if (patterned) { pixels[4]=153; pixels[5]=179; pixels[6]=204; }
		std::memcpy(mapped, pixels, sizeof(pixels));
		for (int i=0; i<4; ++i) {
			VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			b.image=sources[i]; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
			b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
			b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
			VkBufferImageCopy c{}; c.bufferOffset=i*8; c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; c.imageExtent={2,1,1};
			vkCmdCopyBufferToImage(cmd,buffer,sources[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);
			b.oldLayout=b.newLayout; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
		}
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.image = image; barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier );
		VkImageMemoryBarrier depthBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		depthBarrier.image = depthImage;
		depthBarrier.srcQueueFamilyIndex = depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
		depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, 1, &depthBarrier);
		VkRenderingAttachmentInfo attachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		attachment.imageView = view; attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (cutout) attachment.clearValue.color = {{0.6f, 0.1f, 0.8f, 1.0f}};
		VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
		depthAttachment.imageView = depthView;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.clearValue.depthStencil.depth = clearDepth;
		VkRenderingInfo render = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		render.renderArea.extent = { size, size }; render.layerCount = 1;
		render.colorAttachmentCount = 1; render.pColorAttachments = &attachment;
		render.pDepthAttachment = &depthAttachment;
		vkCmdBeginRendering( cmd, &render );
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		const uint32_t offset=0;
		vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,3,sets,1,&offset);
		size_t stageIndex = 0;
		for ( const auto &rect : rects )
		{
			const VkPipeline selected = !pipelines.empty() ? pipelines.at(stageIndex) :
				cutout && stageIndex > 0 && !(brokenLightmap && stageIndex == 1)
				? (stageIndex == 1 ? equalPipeline : equalModulatePipeline) : pipeline;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, selected);
			++stageIndex;
			vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof( rect ), rect.data() );
			vkCmdDraw( cmd, 6, 1, 0, 0 );
		}
		vkCmdEndRendering( cmd );
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier );
		VkBufferImageCopy copy = {}; copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageExtent = { size, size, 1 };
		vkCmdCopyImageToBuffer( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy );
		VkMemoryBarrier host = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
			0, 1, &host, 0, nullptr, 0, nullptr );
		Check( vkEndCommandBuffer( cmd ) );
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
		Check( vkQueueSubmit( queue, 1, &submit, {} ) ); Check( vkQueueWaitIdle( queue ) );
	}
};

int main(int argc, char** argv)
{
	try {
		if(argc!=3 && argc!=4) throw std::runtime_error("Expected probe vertex and production fragment SPIR-V, optional triangle fixture");
		Probe p; if(!p.Init(argv[1],argv[2])) return 77;
		if (argc == 4)
		{
			std::memset(static_cast<uint8_t*>(p.mapped)+262144,0,176);
			std::ifstream input(argv[3]);
			unsigned count;
			while (input >> count)
			{
				std::vector<std::array<float,32>> triangles(count);
				for (auto& triangle : triangles)
				{
					for (int i=0;i<12;++i) input >> triangle[i];
					triangle[15]=-1;
					triangle[18]=triangle[20]=triangle[21]=triangle[22]=triangle[23]=1;
					triangle[28]=triangle[29]=triangle[30]=1;
				}
				if (!input) throw std::runtime_error("Incomplete triangle fixture");
				p.Render(triangles);
				const auto* pixels=static_cast<const uint8_t*>(p.mapped);
				unsigned lit=0;
				for (unsigned i=0;i<256*256;++i) lit += pixels[i*4] != 0;
				unsigned clear=0;
				for (unsigned y=32;y<224;++y)
					for (unsigned x=120;x<136;++x)
						if(pixels[(y*256+x)*4]==0) ++clear;
				std::cout << "uncovered=" << clear << '\n';
				if (!lit) throw std::runtime_error("Empty geometry raster");
			}
			return 0;
		}
		std::array<float,32> push{};
		push[18]=push[19]=1;
		push[20]=push[21]=push[22]=push[23]=1;
		push[28]=push[29]=push[30]=1;
		float block[44]{};
		block[0]=block[1]=block[2]=1;
		block[19]=1; block[20]=0.5f; // Secondary style lives in the other atlas cell.
		const auto run=[&](const char* name, std::array<float,3> expected) {
			std::memcpy(static_cast<uint8_t*>(p.mapped)+262144,block,sizeof(block));
			p.Render({push});
			const auto* out=static_cast<const uint8_t*>(p.mapped)+(128*256+128)*4;
			for(int c=0;c<3;++c)
				if(std::abs(int(out[c])-int(std::lround(expected[c]*255)))>2)
					throw std::runtime_error(std::string(name)+" channel "+std::to_string(c)+" got "+std::to_string(out[c]));
			std::cout<<name<<" passed\n";
		};
		const float r=51/255.f,g=77/255.f,b=102/255.f;
		run("Secondary pulse off",{r,g,b});
		block[4]=0.5f; run("Red pulse, translated secondary atlas UV",{r+0.4f,g,b});
		block[9]=1; block[14]=1;
		run("Four independent styles/textures",{r+0.4f,g+0.2f,b+77/255.f});
		block[19]=0; run("Default disabled descriptor preserves ordinary lighting",{r,g,b});
		block[19]=1; push[19]=0;
		run("Non-lightmap stage ignores extra layers",{r,g,b});
		push[19]=1; push[30]=2;
		run("Gamma applies after lighting sum",{std::sqrt(r+0.4f),std::sqrt(g+0.2f),std::sqrt(b+77/255.f)});
		push[30]=1; block[4]=2;
		run("Light sum clamps before material modulation",{1,g+0.2f,b+77/255.f});
		push[20]=push[21]=push[22]=0.5f;
		run("Material modulation after light sum",{0.5f,(g+0.2f)*0.5f,(b+77/255.f)*0.5f});

		// Reproduce the vines' coverage -> replace -> modulate contract with a
		// synthetic half-transparent texture and the production fragment shader.
		std::array<float,32> mask{};
		mask[0] = mask[18] = mask[20] = mask[21] = mask[22] = mask[23] = 1;
		mask[27] = 3; // GE128; clear half must write neither color nor depth.
		auto light = mask; light[27] = 0;
		auto color = light;
		for (float depth : {1.0f, 0.25f})
		{
			p.Render({mask, light, color}, true, depth);
			for (int x : {64,192})
			{
				const std::array<float,3> expected = x < 128 || depth < 0.5f
					? std::array<float,3>{0.6f,0.1f,0.8f} : std::array<float,3>{r*r,g*g,b*b};
				const auto* out = static_cast<const uint8_t*>(p.mapped)+(128*256+x)*4;
				for (int c=0; c<3; ++c)
					if (std::abs(int(out[c])-int(std::lround(expected[c]*255)))>2)
						throw std::runtime_error("Cutout finishing pass damaged coverage/occlusion");
			}
		}
		std::cout << "Cutout holes, lit texels and foreground occlusion passed\n";
		p.Render({mask, light, color}, true, 1.0f, true);
		const auto* broken = static_cast<const uint8_t*>(p.mapped)+(128*256+64)*4;
		if (std::abs(int(broken[0])-153) <= 2)
			throw std::runtime_error("Negative control failed to reproduce filled cutout");
		std::cout << "Negative control reproduces original filled cutout\n";
		// A depth-writing alpha decal must not prevent the wall's second stage.
		auto wallLight = light;
		wallLight[0] = 0; // Sample an opaque texel for both wall stages.
		wallLight[16] = 0.5f;
		auto wallColor = wallLight;
		auto decal = wallLight;
		decal[0] = 1; decal[16] = 0;
		decal[1] = -0.001f; // Polygon offset towards the eye.
		decal[23] = 0.6f;
		for (float foreground : {1.f, 0.25f})
		{
			p.Render({wallLight, wallColor, decal}, true, foreground, false,
				{p.pipeline, p.equalModulatePipeline, p.decalPipeline});
			for (int x : {64, 192})
			{
				const float tex[3]{r, g, b};
				const float clear[3]{0.6f, 0.1f, 0.8f};
				const auto* out = static_cast<const uint8_t*>(p.mapped)+(128*256+x)*4;
				for (int c=0; c<3; ++c)
				{
					const float expected = foreground < 0.5f ? clear[c] :
						x < 128 ? tex[c]*tex[c] : 0.6f*tex[c]+0.4f*tex[c]*tex[c];
					if (std::abs(int(out[c])-int(std::lround(expected*255)))>2)
						throw std::runtime_error("Decal alpha/finished-wall/foreground coverage failed");
				}
			}
		}
		p.Render({wallLight, decal, wallColor}, true, 1.f, false,
			{p.pipeline, p.decalPipeline, p.equalModulatePipeline});
		const auto* pale = static_cast<const uint8_t*>(p.mapped)+(128*256+64)*4;
		if (std::abs(int(pale[0])-int(std::lround(r*255)))>2)
			throw std::runtime_error("Negative control did not reproduce pale decal rectangle");
		std::cout << "Alpha decal over finished wall, depth occlusion and pale-rectangle negative control passed\n";
		for (bool both : {false,true})
			for (float alpha : {0.f,0.25f,0.75f,1.f})
			{
				auto energy=light;
				energy[0]=0; energy[20]=.5f; energy[21]=.25f; energy[22]=.75f;
				auto frame=light;
				frame[0]=0; frame[23]=alpha;
				p.Render({energy,frame},false,1.f,false,
					{p.pipeline,both ? p.inverseAlphaBothPipeline : p.inverseAlphaPipeline});
				const auto* out=static_cast<const uint8_t*>(p.mapped)+(128*256+64)*4;
				const float tex[4]{r,g,b,1};
				for (int c=0; c<4; ++c)
				{
					const float src=c==3 ? alpha : tex[c];
					const float dst=tex[c]*energy[20+c];
					const float expected=std::min(1.f,src*(1-alpha)+dst*(both ? 1-alpha : alpha));
					if (std::abs(int(out[c])-int(std::lround(expected*255)))>2)
						throw std::runtime_error("Inverse-alpha energy composition failed");
				}
			}
		std::cout << "Inverse-alpha energy mask and dual inverse-alpha layers passed\n";
		for (float flags : {0.f,1.f,2.f,3.f,4.f,5.f})
		{
			auto stage=light;
			stage[0]=0; stage[24]=flags;
			p.Render({stage});
			const auto* out=static_cast<const uint8_t*>(p.mapped)+(128*256+64)*4;
			const float expected[4]{r,g,b,flags>=4 ? .25f : 1.f};
			for(int c=0;c<4;++c)
				if(std::abs(int(out[c])-int(std::lround(expected[c]*255)))>2)
					throw std::runtime_error("Specular alpha/RGB generation isolation failed");
		}
		std::cout << "Specular alpha replaces coverage without changing RGB/wave/ordinary stages\n";
		// Real production fragment branch, including its plane/alpha-test encoding.
		unsigned fogCases=0;
		for (float eye : {-64.f,0.f,64.f})
			for (float point : {-64.f,0.f,.5f,1.f,64.f})
				for (float depth : {1.f,4.f,256.f})
				{
					std::array<float,32> fog{};
					fog[18]=depth;
					fog[19]=1; // plane Z also exercises lightmap-branch isolation
					fog[20]=.32549f; fog[21]=.635294f; fog[22]=.0156863f; fog[23]=1;
					fog[25]=eye; fog[27]=15; fog[28]=fog[29]=1; fog[31]=point;
					p.Render({fog});
					const auto* out=static_cast<const uint8_t*>(p.mapped)+(128*256+128)*4;
					const float amount=vk_local_fog::Amount(1,point,eye,depth);
					if(std::abs(int(out[3])-int(std::lround(amount*255)))>1)
						throw std::runtime_error("Local fog clipping/density mismatch");
					for(int c=0;c<3;++c)
						if(std::abs(int(out[c])-int(std::lround(fog[20+c]*255)))>1)
							throw std::runtime_error("Local fog color contaminated by lightmaps");
					++fogCases;
				}
		std::array<float,32> fogMask{};
		fogMask[0]=1; fogMask[18]=1; fogMask[20]=fogMask[21]=fogMask[22]=fogMask[23]=1;
		fogMask[27]=18; fogMask[31]=64; fogMask[28]=fogMask[29]=1;
		p.Render({fogMask},true);
		const auto* clearFog=static_cast<const uint8_t*>(p.mapped)+(128*256+64)*4;
		const auto* solidFog=static_cast<const uint8_t*>(p.mapped)+(128*256+192)*4;
		if(std::abs(int(clearFog[0])-153)>2 || solidFog[0]!=255)
			throw std::runtime_error("Local fog filled alpha-tested holes");
		std::cout << "Local fog: " << fogCases << " clipping/density cases and cutout coverage passed\n";
		// Generated vegetation: one-pass result equals leaf blend then legacy fog blend.
		unsigned plantCases = 0;
		for (bool opaque : {false,true})
		for (float eye : {-64.f,64.f})
		for (float point : {-64.f,0.f,64.f})
		for (float depth : {1.f,256.f})
		{
			float plants[44]{};
			plants[35]=point;
			plants[36]=.113725f; plants[37]=.121569f; plants[38]=.0431373f; plants[39]=depth;
			plants[40]=eye; plants[41]=opaque ? 1.f : 0.f;
			std::memcpy(static_cast<uint8_t*>(p.mapped)+262144,plants,sizeof(plants));
			std::array<float,32> leaf{};
			leaf[0]=1; leaf[18]=leaf[20]=leaf[21]=leaf[22]=leaf[28]=leaf[29]=leaf[30]=1;
			leaf[23]=.8f; leaf[27]=6; // Alpha-cutout plus existing plant coverage mode.
			p.Render({leaf},true,1.f,false,{opaque?p.pipeline:p.decalPipeline});
			const auto* pixels=static_cast<const uint8_t*>(p.mapped);
			const float background[3]{.6f,.1f,.8f}, texture[3]{r,g,b};
			const float amount=vk_local_fog::Amount(1,point,eye,depth);
			for(int x : {64,192}) for(int c=0;c<3;++c)
			{
				float expected=background[c];
				if(x==192) {
					const float alpha=opaque?1.f:.8f;
					expected=(texture[c]*alpha+background[c]*(1-alpha))*(1-amount)+plants[36+c]*amount;
				}
				if(std::abs(int(pixels[(128*256+x)*4+c])-int(std::lround(expected*255)))>2)
					throw std::runtime_error("Vegetation single-pass fog composition/cutout mismatch");
			}
			// Distance coverage rejection must not leave a fog-colored rectangle behind.
			leaf[3]=77; leaf[2]=0; leaf[24]=1;
			p.Render({leaf},true,1.f,false,{p.decalPipeline});
			for(int c=0;c<3;++c)
				if(std::abs(int(pixels[(128*256+192)*4+c])-int(std::lround(background[c]*255)))>2)
					throw std::runtime_error("Vegetation fog revived distance-discarded texel");
			++plantCases;
		}
		std::memset(static_cast<uint8_t*>(p.mapped)+262144,0,176);
		std::cout << "Vegetation fog: " << plantCases << " boundary/blend/coverage cases passed\n";
		// A non-depth-writing liquid must retain detail above a fully fogged bed.
		std::array<float,32> bed{};
		bed[1]=.2f;
		bed[18]=bed[23]=bed[28]=bed[29]=bed[30]=1;
		auto fog=bed;
		fog[20]=.32549f; fog[21]=.635294f; fog[22]=.0156863f;
		fog[25]=1; fog[27]=15; fog[31]=64;
		auto boundary=fog; boundary[1]=0;
		auto liquid=bed; liquid[0]=1; liquid[1]=0;
		liquid[20]=liquid[21]=liquid[22]=.2f;
		auto liquidLight=bed; liquidLight[1]=0;
		liquidLight[20]=liquidLight[21]=liquidLight[22]=1;
		p.Render({bed,fog,boundary,liquid,liquidLight},false,1,false,
			{p.pipeline,p.fogPipeline,p.fogEqualPipeline,p.additivePipeline,p.doubleModulatePipeline},true);
		const auto* correct=static_cast<const uint8_t*>(p.mapped);
		const int left=(128*256+64)*4, right=(128*256+192)*4;
		for (int c=0;c<3;++c)
		{
			const float tex[3]{r,g,b};
			const float texRight[3]{153/255.f,179/255.f,204/255.f};
			for (int side=0;side<2;++side)
			{
				const float expected=(fog[20+c]+.2f*(side ? texRight[c] : tex[c]))*2*tex[c];
				if (std::abs(int(correct[(side ? right : left)+c])-int(std::lround(expected*255)))>3)
					throw std::runtime_error("Fog/liquid composition or equal-depth boundary failed");
			}
		}
		if (correct[right+1]-correct[left+1]<5)
			throw std::runtime_error("Liquid detail lost above fog");
		p.Render({bed,liquid,liquidLight,fog,boundary},false,1,false,
			{p.pipeline,p.additivePipeline,p.doubleModulatePipeline,p.fogPipeline,p.fogPipeline},true);
		const auto* wrong=static_cast<const uint8_t*>(p.mapped);
		if (std::abs(int(wrong[right+1])-int(wrong[left+1]))>1 ||
			std::abs(int(wrong[left+1])-int(std::lround(.635294f*255)))>2)
			throw std::runtime_error("Negative control did not reproduce uniform fog over liquid");
		std::cout << "Fogged bed beneath liquid detail, equal-depth boundary and old-order negative control passed\n";
		return 0;
	} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
