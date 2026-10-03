/* Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later. */
#include "../OpenJK/tests/md3_upload_reference.h"
#include <vulkan/vulkan.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Check( VkResult result )
{
	if ( result != VK_SUCCESS ) throw std::runtime_error( std::to_string( result ) );
}

struct Upload
{
	VkInstance instance = {};
	VkDevice device = {};
	VkBuffer buffer = {};
	VkDeviceMemory memory = {};
	Md3TestVertex *mapped = nullptr;
	~Upload()
	{
		if ( device )
		{
			if ( mapped ) vkUnmapMemory( device, memory );
			vkDestroyBuffer( device, buffer, nullptr );
			vkFreeMemory( device, memory, nullptr );
			vkDestroyDevice( device, nullptr );
		}
		if ( instance ) vkDestroyInstance( instance, nullptr );
	}
	bool Init( size_t count )
	{
		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		info.pApplicationInfo = &app;
		if ( vkCreateInstance( &info, nullptr, &instance ) != VK_SUCCESS ) return false;
		uint32_t n = 0;
		Check( vkEnumeratePhysicalDevices( instance, &n, nullptr ) );
		std::vector<VkPhysicalDevice> devices( n );
		Check( vkEnumeratePhysicalDevices( instance, &n, devices.data() ) );
		VkPhysicalDevice physical = {};
		uint32_t family = 0;
		for ( auto candidate : devices )
		{
			vkGetPhysicalDeviceQueueFamilyProperties( candidate, &n, nullptr );
			std::vector<VkQueueFamilyProperties> queues( n );
			vkGetPhysicalDeviceQueueFamilyProperties( candidate, &n, queues.data() );
			for ( uint32_t i = 0; i < n; ++i )
				if ( queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) { physical = candidate; family = i; break; }
			if ( physical ) break;
		}
		if ( !physical ) return false;
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties( physical, &properties );
		std::cout << "Device: " << properties.deviceName << '\n';
		float priority = 1;
		VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
		VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queue;
		Check( vkCreateDevice( physical, &deviceInfo, nullptr, &device ) );
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = count * sizeof( Md3TestVertex );
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		Check( vkCreateBuffer( device, &bufferInfo, nullptr, &buffer ) );
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements( device, buffer, &req );
		VkPhysicalDeviceMemoryProperties mem;
		vkGetPhysicalDeviceMemoryProperties( physical, &mem );
		const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		for ( uint32_t i = 0; i < mem.memoryTypeCount; ++i )
		{
			if ( !( req.memoryTypeBits & ( 1u << i ) ) || ( mem.memoryTypes[i].propertyFlags & flags ) != flags ) continue;
			VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocation.allocationSize = req.size; allocation.memoryTypeIndex = i;
			Check( vkAllocateMemory( device, &allocation, nullptr, &memory ) );
			std::cout << "Memory type " << i << " flags=" << mem.memoryTypes[i].propertyFlags << '\n';
			break;
		}
		if ( !memory ) return false;
		Check( vkBindBufferMemory( device, buffer, memory, 0 ) );
		void *data = nullptr;
		Check( vkMapMemory( device, memory, 0, VK_WHOLE_SIZE, 0, &data ) );
		mapped = static_cast<Md3TestVertex *>( data );
		return true;
	}
};

int main()
{
	try
	{
		constexpr size_t count = 8192;
		Upload gpu;
		if ( !gpu.Init( count ) ) return 77;
		std::vector<Md3TestVertex> base( count ), reference( count );
		for ( size_t i = 0; i < count; ++i )
			base[i] = { { float( i ), 2, 3 }, { 0.1f, 0.2f, 0.3f, 0.4f },
				{ 0.3f, 0.7f }, { 0.2f, 0.1f }, { 0.6f, 0.8f, 0 } };
		const vk_md3_pose_vertex_t current = { { 2, 4, 6 }, { 1, 0, 0 } }, previous = { { 0, 0, 0 }, { 0, 1, 0 } };
		const float ambient[3] = { 30, 90, 170 }, directed[3] = { 80, 70, 240 }, direction[3] = { 0.6f, 0.8f, 0 };
		for ( bool animated : { false, true } )
		{
			std::vector<double> oldTimes, newTimes;
			for ( int sample = 0; sample < 11; ++sample )
			for ( bool local : { false, true } )
			{
				const auto begin = std::chrono::steady_clock::now();
				if ( !local ) std::memcpy( gpu.mapped, base.data(), count * sizeof( Md3TestVertex ) );
				for ( size_t i = 0; i < count; ++i )
				{
					if ( local )
					{
						const auto vertex = VK_MD3PrepareVertex( base[i], animated ? &current : nullptr,
							animated ? &previous : nullptr, 0.375f, ambient, directed, direction );
						std::memcpy( &gpu.mapped[i], &vertex, sizeof( vertex ) );
					}
					else Md3Reference( gpu.mapped[i], animated ? &current : nullptr,
						animated ? &previous : nullptr, 0.375f, ambient, directed, direction );
				}
				const double ms = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - begin ).count();
				if ( sample >= 2 ) ( local ? newTimes : oldTimes ).push_back( ms );
				if ( !local ) std::memcpy( reference.data(), gpu.mapped, count * sizeof( Md3TestVertex ) );
				else if ( std::memcmp( reference.data(), gpu.mapped, count * sizeof( Md3TestVertex ) ) )
					throw std::runtime_error( "MD3 upload differs from original algorithm" );
			}
			std::sort( oldTimes.begin(), oldTimes.end() ); std::sort( newTimes.begin(), newTimes.end() );
			std::cout << "PASS byte-identical " << ( animated ? "animated" : "static" )
				<< " lit vertices=" << count << " median CPU ms old=" << oldTimes[4] << " local=" << newTimes[4] << '\n';
		}
		return 0;
	}
	catch ( const std::exception &error ) { std::cerr << error.what() << '\n'; return 1; }
}
