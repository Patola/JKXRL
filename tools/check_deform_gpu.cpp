/* Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later. */
#include "../OpenJK/code/rd-vulkan/vk_deform.h"
#include "../OpenJK/code/rd-vulkan/vk_billboard.h"
#include <vulkan/vulkan.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(VkResult result)
{
	if (result != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(result));
}
struct Probe
{
	VkInstance instance{};
	VkPhysicalDevice physical{};
	VkDevice device{};
	VkQueue queue{};
	uint32_t family{};
	VkBuffer buffers[2]{};
	VkDeviceMemory memory[2]{};
	void* mapped[2]{};
	VkDescriptorSetLayout layouts[2]{};
	VkDescriptorPool descriptors{};
	VkDescriptorSet sets[2]{};
	VkPipelineLayout layout{};
	VkPipeline pipeline{};
	VkShaderModule shader{};
	VkCommandPool pool{};
	~Probe()
	{
		if (device)
		{
			vkDeviceWaitIdle(device);
			vkDestroyPipeline(device,pipeline,nullptr);
			vkDestroyPipelineLayout(device,layout,nullptr);
			vkDestroyShaderModule(device,shader,nullptr);
			vkDestroyCommandPool(device,pool,nullptr);
			vkDestroyDescriptorPool(device,descriptors,nullptr);
			for (int i=0;i<2;++i)
			{
				vkDestroyDescriptorSetLayout(device,layouts[i],nullptr);
				if (mapped[i]) vkUnmapMemory(device,memory[i]);
				vkDestroyBuffer(device,buffers[i],nullptr);
				vkFreeMemory(device,memory[i],nullptr);
			}
			vkDestroyDevice(device,nullptr);
		}
		if (instance) vkDestroyInstance(instance,nullptr);
	}
	bool Init()
	{
		VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_3;
		VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
		if (vkCreateInstance(&info,nullptr,&instance)!=VK_SUCCESS) return false;
		uint32_t count=0;
		Check(vkEnumeratePhysicalDevices(instance,&count,nullptr));
		std::vector<VkPhysicalDevice> devices(count);
		Check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
		for (auto candidate:devices)
		{
			vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,families.data());
			for (uint32_t i=0;i<count;++i) if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
			{ physical=candidate; family=i; break; }
			if (physical) break;
		}
		if (!physical) return false;
		float priority=1;
		VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex=family; qi.queueCount=1; qi.pQueuePriorities=&priority;
		VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi;
		Check(vkCreateDevice(physical,&di,nullptr,&device));
		vkGetDeviceQueue(device,family,0,&queue);
		return true;
	}
	void Buffer(int index, VkDeviceSize size, VkBufferUsageFlags usage)
	{
		VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=size; bi.usage=usage;
		Check(vkCreateBuffer(device,&bi,nullptr,&buffers[index]));
		VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,buffers[index],&req);
		VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(physical,&props);
		const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		for(uint32_t i=0;i<props.memoryTypeCount;++i)
			if ((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags)
			{
				VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mi.allocationSize=req.size; mi.memoryTypeIndex=i;
				Check(vkAllocateMemory(device,&mi,nullptr,&memory[index]));
				Check(vkBindBufferMemory(device,buffers[index],memory[index],0));
				Check(vkMapMemory(device,memory[index],0,size,0,&mapped[index]));
				return;
			}
		throw std::runtime_error("No host coherent memory");
	}
};

int main(int argc,char** argv)
{
	try
	{
		if(argc!=2) throw std::runtime_error("Expected probe SPIR-V path");
		Probe p;
		if(!p.Init()) return 77;
		VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(p.physical,&properties);
		std::cout<<"Device: "<<properties.deviceName<<'\n';
		std::vector<vk_deform_block_t> cases(1); // Identity, then all authored modes and chained order.
		vk_deform_t wave,move,bulge;
		wave.spread=0.02f; wave.wave={1,2.5f,0.137f,0.35f};
		move.type=VK_DEFORM_MOVE; move.vector={3,-2,1}; move.wave={0,1,0.123f,0.7f};
		bulge.type=VK_DEFORM_BULGE; bulge.vector={2,1.5f,0.8f};
		for(int f=VK_WAVE_SIN;f<=VK_WAVE_INVERSE_SAWTOOTH;++f)
			for(float time:{-1.173f,0.231f,7.371f})
			{
				wave.function=move.function=static_cast<vk_waveform_t>(f);
				for(const auto& sequence:std::vector<std::vector<vk_deform_t>>{{wave},{move},{bulge},{move,wave,bulge},{wave,move}})
					cases.push_back(VK_DeformBlock(sequence,time,0.777f));
			}
		wave.wave[3]=0;
		bulge.vector={0,12,0};
		cases.push_back(VK_DeformBlock({wave},4,3));
		cases.push_back(VK_DeformBlock({bulge},4,3));
		bulge.vector[1]=-0.2f;
		cases.push_back(VK_DeformBlock({bulge},4,3));
		for (int mode : {1,2}) for (float angle : {0.0f, 0.7f, 1.9f, 3.1f})
		{
			vk_billboard::Quad q{{{{20,-2,5},{20,2,5},{20,2,-5},{20,-2,-5}}},{{0,1,3,3,1,2}},mode};
			vk_deform_block_t block{};
			if (!vk_billboard::Frame(q,{std::cos(angle),std::sin(angle),0},
				{-std::sin(angle),std::cos(angle),0},{0,0,1},block.billboard))
				throw std::runtime_error("Invalid billboard test geometry");
			block.control[3]=1;
			cases.push_back(block);
		}
		for (const std::array<float,4> light : {std::array<float,4>{0,0,1,1},
			std::array<float,4>{0,0,-1,1}, std::array<float,4>{0.6f,0,0.8f,1}})
		{
			vk_deform_block_t block{};
			block.specularLight=light;
			cases.push_back(block);
		}
		cases.push_back({}); // Restore identity and world lighting after model cases.
		const uint32_t perCase=128;
		struct Sample { float position[4],normal[4],result[4],eye[4]; };
		const auto alignment=std::max<VkDeviceSize>(16,properties.limits.minUniformBufferOffsetAlignment);
		const uint32_t stride=static_cast<uint32_t>((sizeof(vk_deform_block_t)+alignment-1)/alignment*alignment);
		p.Buffer(0,cases.size()*perCase*sizeof(Sample),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		p.Buffer(1,cases.size()*stride,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		auto* samples=static_cast<Sample*>(p.mapped[0]);
		for(size_t c=0;c<cases.size();++c)
		{
			std::memcpy(static_cast<char*>(p.mapped[1])+c*stride,&cases[c],sizeof(cases[c]));
			for(uint32_t i=0;i<perCase;++i)
			{
				auto& s=samples[c*perCase+i];
				s={{i*0.271f-7.23f,i*-0.173f+4.37f,i*0.031f-3.21f,i*0.129f-2.74f}, {0.6f,0,0.8f,0}, {0,0,0,0}};
				// Sweep the viewing hemisphere, including both stereo-eye offsets.
				const float angle=i*6.2831853f/perCase;
				s.eye[0]=s.position[0]+20*std::sin(angle)+(i%2 ? .032f : -.032f);
				s.eye[1]=s.position[1]+2;
				s.eye[2]=s.position[2]+20*std::cos(angle);
			}
		}
		VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1}};
		for(int i=0;i<2;++i)
		{
			VkDescriptorSetLayoutBinding binding{0,sizes[i].type,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
			VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount=1;li.pBindings=&binding;
			Check(vkCreateDescriptorSetLayout(p.device,&li,nullptr,&p.layouts[i]));
		}
		VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpi.maxSets=2;dpi.poolSizeCount=2;dpi.pPoolSizes=sizes;
		Check(vkCreateDescriptorPool(p.device,&dpi,nullptr,&p.descriptors));
		VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=p.descriptors;dai.descriptorSetCount=2;dai.pSetLayouts=p.layouts;
		Check(vkAllocateDescriptorSets(p.device,&dai,p.sets));
		for(int i=0;i<2;++i)
		{
			VkDescriptorBufferInfo bi{p.buffers[i],0,i==0 ? cases.size()*perCase*sizeof(Sample):sizeof(vk_deform_block_t)};
			VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=p.sets[i];write.descriptorCount=1;write.descriptorType=sizes[i].type;write.pBufferInfo=&bi;
			vkUpdateDescriptorSets(p.device,1,&write,0,nullptr);
		}
		std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
		if(!file || file.tellg()<=0 || static_cast<size_t>(file.tellg())%4) throw std::runtime_error("Invalid SPIR-V");
		std::vector<uint32_t> words(static_cast<size_t>(file.tellg())/4);
		file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),words.size()*4);
		VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};si.codeSize=words.size()*4;si.pCode=words.data();
		Check(vkCreateShaderModule(p.device,&si,nullptr,&p.shader));
		VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,8};
		VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=2;pli.pSetLayouts=p.layouts;pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&range;
		Check(vkCreatePipelineLayout(p.device,&pli,nullptr,&p.layout));
		VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=p.layout;
		ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,p.shader,"main",nullptr};
		Check(vkCreateComputePipelines(p.device,VK_NULL_HANDLE,1,&ci,nullptr,&p.pipeline));
		VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=p.family;
		Check(vkCreateCommandPool(p.device,&cpi,nullptr,&p.pool));
		VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=p.pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
		VkCommandBuffer command;Check(vkAllocateCommandBuffers(p.device,&cai,&command));
		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(command,&begin));
		vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
		for(uint32_t c=0;c<cases.size();++c)
		{
			uint32_t offset=c*stride, push[]={c*perCase,perCase};
			vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,2,p.sets,1,&offset);
			vkCmdPushConstants(command,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);
			vkCmdDispatch(command,(perCase+63)/64,1,1);
		}
		VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
		Check(vkEndCommandBuffer(command));
		VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
		Check(vkQueueSubmit(p.queue,1,&submit,VK_NULL_HANDLE));Check(vkQueueWaitIdle(p.queue));
		float maxError=0;
		float maxSpecularError=0;
		size_t zeroHighlights=0, visibleHighlights=0;
		for(size_t c=0;c<cases.size();++c) for(uint32_t i=0;i<perCase;++i)
		{
			const auto& s=samples[c*perCase+i];
			auto expected=VK_DeformPosition(cases[c],{s.position[0],s.position[1],s.position[2]},
				{s.normal[0],s.normal[1],s.normal[2]},s.position[3]);
			for(int axis=0;axis<3;++axis)
			{
				float error=std::fabs(s.result[axis]-expected[axis]);
				maxError=std::max(maxError,error);
				if(!std::isfinite(s.result[axis]) || error>0.035f)
					throw std::runtime_error("GPU/reference mismatch case="+std::to_string(c)+" vertex="+std::to_string(i));
			}
			// Independent double-precision transcription of RB_CalcSpecularAlpha.
			double light[3],viewer[3],normal[3],lightLength=0,viewLength=0,d=0;
			const double origin[3]={-960,1980,96};
			for(int a=0;a<3;++a)
			{
				light[a]=cases[c].specularLight[3] ? cases[c].specularLight[a] : origin[a]-expected[a];
				viewer[a]=s.eye[a]-expected[a];
				normal[a]=cases[c].control[3] ? cases[c].billboard[3][a] : s.normal[a];
				lightLength+=light[a]*light[a]; viewLength+=viewer[a]*viewer[a];
			}
			for(int a=0;a<3;++a)
			{
				if(!cases[c].specularLight[3]) light[a]/=std::sqrt(std::max(lightLength,1e-20));
				d+=2*normal[a]*light[a];
			}
			double l=0;
			for(int a=0;a<3;++a) l+=(normal[a]*d-light[a])*viewer[a];
			l=std::max(0.0,l/std::sqrt(std::max(viewLength,1e-20)));
			l*=l; l*=l;
			const float alpha=std::floor(std::min(l,1.0)*255)/255;
			const float error=std::fabs(s.result[3]-alpha);
			maxSpecularError=std::max(maxSpecularError,error);
			if(!std::isfinite(s.result[3]) || error>1.01f/255)
				throw std::runtime_error("Specular GPU/legacy mismatch");
			if(alpha==0) ++zeroHighlights; else ++visibleHighlights;
		}
		if(!zeroHighlights || !visibleHighlights) throw std::runtime_error("Specular test lacks coverage");
		std::cout<<"Specular world/model/stereo samples passed; max error="<<maxSpecularError<<'\n';
		std::cout<<cases.size()*perCase<<" GPU deformation samples passed; max error="<<maxError<<'\n';
		return 0;
	}
	catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
