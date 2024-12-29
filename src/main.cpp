#pragma once
#include "config.h"
#include "vkutils.hpp"
#include <array>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

constexpr uint32_t width = 800;
constexpr uint32_t height = 600;

ImGui_ImplVulkanH_Window g_MainWindowData;
static int g_MinImageCount = 2;
ImGui_ImplVulkanH_Window* wd;

struct Buffer {
	vk::UniqueBuffer buffer;
	vk::UniqueDeviceMemory memory;
	vk::DeviceAddress address;

	void init(vk::PhysicalDevice physicalDevice,
		vk::Device device,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags memoryProperty,
		const void* data = nullptr) {
		// create buffer
		vk::BufferCreateInfo createInfo{};
		createInfo.setSize(size);
		createInfo.setUsage(usage);
		buffer = device.createBufferUnique(createInfo);

		//Allocate memory
		vk::MemoryAllocateFlagsInfo allocateFlags{};
		if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
			allocateFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
		}

		vk::MemoryRequirements memoryReq = 
			device.getBufferMemoryRequirements(*buffer);
		uint32_t memoryType = vkutils::getMemoryType(physicalDevice,
			memoryReq, memoryProperty);
		vk::MemoryAllocateInfo allocateInfo{};
		allocateInfo.setAllocationSize(memoryReq.size);
		allocateInfo.setMemoryTypeIndex(memoryType);
		allocateInfo.setPNext(&allocateFlags);
		memory = device.allocateMemoryUnique(allocateInfo);

		device.bindBufferMemory(*buffer, *memory, 0);

		if (data) {
			void* mappedPtr = device.mapMemory(*memory, 0, size);
			memcpy(mappedPtr, data, size);
			device.unmapMemory(*memory);
		}

		if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
			vk::BufferDeviceAddressInfo addressInfo{};
			addressInfo.setBuffer(*buffer);
			address = device.getBufferAddressKHR(&addressInfo);
		}
	}
};

struct Vertex {
	float pose[3];
};

struct AccelStruct {
	vk::UniqueAccelerationStructureKHR accel;
	Buffer buffer;

	void init(vk::PhysicalDevice physicalDevice, vk::Device device,
		VkCommandPool commandPool, vk::Queue queue,
		vk::AccelerationStructureTypeKHR type,
		vk::AccelerationStructureGeometryKHR geometry,
		uint32_t primitiveCount) {
		
		vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
		buildInfo.setType(type);
		buildInfo.setMode(vk::BuildAccelerationStructureModeKHR::eBuild);
		buildInfo.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
		buildInfo.setGeometries(geometry);

		vk::AccelerationStructureBuildSizesInfoKHR buildSizes =
			device.getAccelerationStructureBuildSizesKHR(
				vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCount);

		buffer.init(physicalDevice, device,
			buildSizes.accelerationStructureSize,
			vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		vk::AccelerationStructureCreateInfoKHR createInfo{};
		createInfo.setBuffer(*buffer.buffer);
		createInfo.setSize(buildSizes.accelerationStructureSize);
		createInfo.setType(type);
		accel = device.createAccelerationStructureKHRUnique(createInfo);

		Buffer scratchBuffer;
		scratchBuffer.init(physicalDevice, device, buildSizes.buildScratchSize,
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		buildInfo.setDstAccelerationStructure(*accel);
		buildInfo.setScratchData(scratchBuffer.address);

		vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
		buildRangeInfo.setPrimitiveCount(primitiveCount);
		buildRangeInfo.setPrimitiveOffset(0);
		buildRangeInfo.setFirstVertex(0);
		buildRangeInfo.setTransformOffset(0);

		vkutils::oneTimeSubmit(
			device, commandPool, queue,
			[&](vk::CommandBuffer commandBuffer) {
				commandBuffer.buildAccelerationStructuresKHR(buildInfo, &buildRangeInfo);
			});

		vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
		addressInfo.setAccelerationStructure(*accel);
		buffer.address = device.getAccelerationStructureAddressKHR(addressInfo);
	}
};

class Application
{
public:
	void run() {
		initWindow();
		initVulkan();

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			drawFrame();
		}

		glfwDestroyWindow(window);
		glfwTerminate();
	}

private:
	
	vk::UniqueRenderPass renderPass;
	ImDrawData* draw_data;
	ImGuiContext* imGuicontext;

	GLFWwindow* window = nullptr;

	vk::UniqueInstance instance;
	vk::UniqueDebugUtilsMessengerEXT debugMessenger;
	vk::UniqueSurfaceKHR surface;

	vk::PhysicalDevice physicalDevice;
	vk::UniqueDevice device;

	vk::Queue queue;
	uint32_t queueFamilyIndex{};

	vk::UniqueCommandPool commandPool;
	vk::UniqueCommandBuffer commandBuffer;

	vk::SurfaceFormatKHR surfaceFormat;
	vk::UniqueSwapchainKHR swapchain;
	std::vector<vk::Image> swapchainImages;
	std::vector<vk::UniqueImageView> swapchainImageViews;
	std::vector<vk::UniqueFramebuffer> swapchainFramebuffers;

	vk::Extent2D swapchainExtent;

	AccelStruct bottomAccel{};
	AccelStruct topAccel{};

	std::vector<vk::UniqueShaderModule> shaderModules;
	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;

	vk::UniqueDescriptorPool descPool;
	vk::UniqueDescriptorPool imGuiDescPool;
	vk::UniqueDescriptorSetLayout descSetLayout;
	vk::UniqueDescriptorSet descSet;

	vk::UniquePipeline pipeline;
	vk::UniquePipelineLayout pipelineLayout;

	Buffer sbt{};
	vk::StridedDeviceAddressRegionKHR raygenRegion{};
	vk::StridedDeviceAddressRegionKHR missRegion{};
	vk::StridedDeviceAddressRegionKHR hitRegion{};

	void initWindow() {
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		window = glfwCreateWindow(width, height, "vulkanRaytracing", nullptr, nullptr);

		wd = &g_MainWindowData;
		//initImGui();
		//ImGui_ImplGlfw_InitForVulkan(window, true);
		//SetUpVulkanWindow(wd, *surface, width, height);

		//
		//
	}

	void initVulkan() {
		std::vector<const char*> layers = {
			"VK_LAYER_KHRONOS_validation",
		};

		instance = vkutils::createInstance(VK_API_VERSION_1_2, layers);
		std::cout << "create vulkan instance" << std::endl;
		debugMessenger = vkutils::createDebugMessenger(*instance);
		surface = vkutils::createSurface(*instance, window);

		std::vector<const char*> deviceExtensions = {
			VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};
		physicalDevice = vkutils::pickPhysicalDevice(*instance, *surface, deviceExtensions);
		VkPhysicalDeviceProperties physProp;
		vkGetPhysicalDeviceProperties(physicalDevice, &physProp);
		std::cout << "Device Name: " << physProp.deviceName << std::endl;

		queueFamilyIndex = vkutils::findGeneralQueueFamily(physicalDevice, *surface);
		std::cout << "queue family index: " << queueFamilyIndex << std::endl;
		device = vkutils::createLogicalDevice(physicalDevice, queueFamilyIndex, deviceExtensions);
		queue = device->getQueue(queueFamilyIndex, 0);

		commandPool = vkutils::createCommandPool(*device, queueFamilyIndex);
		commandBuffer = vkutils::createCommandBuffer(*device, *commandPool);

		surfaceFormat = vkutils::chooseSurfaceFormat(physicalDevice, *surface);
		wd->Swapchain = (vkutils::createSwapchain(
			physicalDevice, *device, *surface, queueFamilyIndex,
			vk::ImageUsageFlagBits::eStorage, surfaceFormat,
			width, height, swapchainExtent)).get();

		SetUpVulkanWindow(wd, *surface, width, height);

		swapchainImages = device->getSwapchainImagesKHR(static_cast<vk::SwapchainKHR>(wd->Swapchain));
		std::cout << "Number of swapchain images: " << swapchainImages.size() << std::endl;

		createSwapchainImageViews();

		createRenderPass();
		createFramebuffers();

		createBottomLevelAS();
		createTopLevelAS();

		prepareShaders();

		createDescriptorPool();
		createDescSetLayout();
		createDescriptorSet();

		createRayTracingPipeline();

		initImGui();
		ImGui_ImplGlfw_InitForVulkan(window, true);

		createShaderBindingTable();
		
	}

	void createSwapchainImageViews() {
		for (auto image : swapchainImages) {
			vk::ImageViewCreateInfo createInfo{};
			createInfo.setImage(image);
			createInfo.setViewType(vk::ImageViewType::e2D);
			createInfo.setFormat(surfaceFormat.format);
			createInfo.setComponents({ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA });
			createInfo.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
			swapchainImageViews.push_back(device->createImageViewUnique(createInfo));
		}

		vkutils::oneTimeSubmit(*device, *commandPool, queue,
			[&](vk::CommandBuffer commandBuffer) {
				for (auto image : swapchainImages) {
					vkutils::setImageLayout(commandBuffer, image,
						vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
				}
			});
	};

	void createRenderPass() {
		vk::AttachmentDescription colorAttachment({}, vk::Format::eB8G8R8A8Unorm,
			vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare,
			vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::ePresentSrcKHR);

		vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

		vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, {}, 1, &colorAttachmentRef);

		vk::RenderPassCreateInfo renderPassInfo({}, colorAttachment, subpass);

		renderPass = device->createRenderPassUnique(renderPassInfo);
	}

	void createFramebuffers() {
		swapchainFramebuffers.reserve(swapchainImageViews.size());

		for (auto const& view : swapchainImageViews) {
			vk::FramebufferCreateInfo framebufferInfo({}, renderPass.get(), view.get(),
				swapchainExtent.width, swapchainExtent.height, 1);
			swapchainFramebuffers.push_back(device->createFramebufferUnique(framebufferInfo));
		}
	}

	void createBottomLevelAS() {
		std::cout << "Create BLAS\n";

		std::vector<Vertex> vertices = {
			{{1.0f, 1.0f, 0.0f}},
			{{-1.0f, 1.0f, 0.0f}},
			{{0.0f, -1.0f, 0.0f}},
		};
		std::vector<uint32_t> indices = { 0, 1, 2 };

		vk::BufferUsageFlags bufferUsage{
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
			vk::BufferUsageFlagBits::eShaderDeviceAddress };

		vk::MemoryPropertyFlags memoryProperty{
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent};

		Buffer vertexBuffer;
		Buffer indexBuffer;

		vertexBuffer.init(physicalDevice, *device, 
						  vertices.size() * sizeof(Vertex), bufferUsage, 
						  memoryProperty, vertices.data());

		indexBuffer.init(physicalDevice, *device, 
						 indices.size() * sizeof(uint32_t), bufferUsage, 
						 memoryProperty, indices.data());

		vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
		triangles.setVertexFormat(vk::Format::eR32G32B32Sfloat);
		triangles.setVertexData(vertexBuffer.address);
		triangles.setVertexStride(sizeof(Vertex));
		triangles.setMaxVertex(static_cast<uint32_t>(vertices.size()));
		triangles.setIndexType(vk::IndexType::eUint32);
		triangles.setIndexData(indexBuffer.address);

		vk::AccelerationStructureGeometryKHR geometry{};
		geometry.setGeometryType(vk::GeometryTypeKHR::eTriangles);
		geometry.setGeometry({ triangles });
		geometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

		uint32_t primitiveCount = static_cast<uint32_t>(indices.size() / 3);
		bottomAccel.init(physicalDevice, *device, *commandPool, queue,
			vk::AccelerationStructureTypeKHR::eBottomLevel,
			geometry, primitiveCount);

	}

	void createTopLevelAS() {
		std::cout << "Create TLAS\n";

		vk::TransformMatrixKHR transform = std::array{
			std::array{1.0f, 0.0f, 0.0f, 0.0f},
			std::array{0.0f, 1.0f, 0.0f, 0.0f},
			std::array{0.0f, 0.0f, 1.0f, 0.0f},
		};

		vk::AccelerationStructureInstanceKHR accelInstance{};
		accelInstance.setTransform(transform);
		accelInstance.setInstanceCustomIndex(0);
		accelInstance.setMask(0xFF);
		accelInstance.setInstanceShaderBindingTableRecordOffset(0);
		accelInstance.setFlags(
			vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
		accelInstance.setAccelerationStructureReference(
			bottomAccel.buffer.address);

		Buffer instanceBuffer;
		instanceBuffer.init(
			physicalDevice, *device,
			sizeof(vk::AccelerationStructureInstanceKHR),
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
			vk::BufferUsageFlagBits::eShaderDeviceAddress,
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent,
			&accelInstance);

		vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
		instancesData.setArrayOfPointers(false);
		instancesData.setData(instanceBuffer.address);

		vk::AccelerationStructureGeometryKHR geometry{};
		geometry.setGeometryType(vk::GeometryTypeKHR::eInstances);
		geometry.setGeometry({ instancesData });
		geometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

		constexpr uint32_t primitiveCount = 1;
		topAccel.init(physicalDevice, *device, *commandPool, queue,
			vk::AccelerationStructureTypeKHR::eTopLevel,
			geometry, primitiveCount);
	}

	void addShader(uint32_t shaderIndex,
		const std::string& filename,
		vk::ShaderStageFlagBits stage) {
		std::cout << "Loading shader: " << SHADER_ROOT_DIR + filename << std::endl;

		shaderModules[shaderIndex] =
			vkutils::createShaderModule(*device, SHADER_ROOT_DIR + filename);
		std::cout << "after createShader" << std::endl;
		shaderStages[shaderIndex].setStage(stage);
		shaderStages[shaderIndex].setModule(*shaderModules[shaderIndex]);
		shaderStages[shaderIndex].setPName("main");
	}

	void prepareShaders() {
		std::cout << "Prepare shaders\n";

		uint32_t raygenShader = 0;
		uint32_t missShader = 1;
		uint32_t chitShader = 2;
		shaderStages.resize(3);
		shaderModules.resize(3);

		std::cout << "before rgen" << std::endl;
		addShader(raygenShader, "raygen.rgen.spv",
			vk::ShaderStageFlagBits::eRaygenKHR);

		std::cout << "before miss" << std::endl;
		addShader(missShader, "miss.rmiss.spv",
			vk::ShaderStageFlagBits::eMissKHR);

		std::cout << "before chit" << std::endl;
		addShader(chitShader, "closesthit.rchit.spv",
			vk::ShaderStageFlagBits::eClosestHitKHR);

		uint32_t raygenGroup = 0;
		uint32_t missGroup = 1;
		uint32_t hitGroup = 2;
		shaderGroups.resize(3);

		// Raygen group
		shaderGroups[raygenGroup].setType(
			vk::RayTracingShaderGroupTypeKHR::eGeneral);
		shaderGroups[raygenGroup].setGeneralShader(raygenShader);
		shaderGroups[raygenGroup].setClosestHitShader(VK_SHADER_UNUSED_KHR);
		shaderGroups[raygenGroup].setAnyHitShader(VK_SHADER_UNUSED_KHR);
		shaderGroups[raygenGroup].setIntersectionShader(VK_SHADER_UNUSED_KHR);

		// Miss group
		shaderGroups[missGroup].setType(
			vk::RayTracingShaderGroupTypeKHR::eGeneral);
		shaderGroups[missGroup].setGeneralShader(missShader);
		shaderGroups[missGroup].setClosestHitShader(VK_SHADER_UNUSED_KHR);
		shaderGroups[missGroup].setAnyHitShader(VK_SHADER_UNUSED_KHR);
		shaderGroups[missGroup].setIntersectionShader(VK_SHADER_UNUSED_KHR);

		// Hit group
		shaderGroups[hitGroup].setType(
			vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup);
		shaderGroups[hitGroup].setGeneralShader(VK_SHADER_UNUSED_KHR);
		shaderGroups[hitGroup].setClosestHitShader(chitShader);
		shaderGroups[hitGroup].setAnyHitShader(VK_SHADER_UNUSED_KHR);
		shaderGroups[hitGroup].setIntersectionShader(VK_SHADER_UNUSED_KHR);

	}

	void createDescriptorPool() {
		std::vector<vk::DescriptorPoolSize> poolSizes = {
			{ vk::DescriptorType::eAccelerationStructureKHR, 1},
			{ vk::DescriptorType::eStorageImage, 1 },
		};

		vk::DescriptorPoolCreateInfo createInfo{};
		createInfo.setPoolSizes(poolSizes);
		createInfo.setMaxSets(1);
		createInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
		descPool = device->createDescriptorPoolUnique(createInfo);

		std::vector<vk::DescriptorPoolSize> imGuiPoolSizes = {
			{vk::DescriptorType::eSampler,1000},
			{vk::DescriptorType::eCombinedImageSampler,1000},
			{vk::DescriptorType::eSampledImage,1000},
			{vk::DescriptorType::eStorageImage,1000},
			{vk::DescriptorType::eUniformTexelBuffer,1000},
			{vk::DescriptorType::eStorageTexelBuffer,1000},
			{vk::DescriptorType::eUniformBuffer,1000},
			{vk::DescriptorType::eStorageBuffer,1000},
			{vk::DescriptorType::eUniformBufferDynamic,1000},
			{vk::DescriptorType::eStorageBufferDynamic,1000},
			{vk::DescriptorType::eInputAttachment,1000}
		};

		vk::DescriptorPoolCreateInfo imGuiPoolInfo = {};
		imGuiPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
		imGuiPoolInfo.setMaxSets(1000);
		imGuiPoolInfo.poolSizeCount = static_cast<uint32_t>(imGuiPoolSizes.size());
		imGuiPoolInfo.pPoolSizes = imGuiPoolSizes.data();

		imGuiDescPool = device->createDescriptorPoolUnique(imGuiPoolInfo);

	}

	void createDescSetLayout() {
		std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

		bindings[0].setBinding(0);
		bindings[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
		bindings[0].setDescriptorCount(1);
		bindings[0].setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);

		bindings[1].setBinding(1);
		bindings[1].setDescriptorType(vk::DescriptorType::eStorageImage);
		bindings[1].setDescriptorCount(1);
		bindings[1].setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);

		vk::DescriptorSetLayoutCreateInfo createInfo{};
		createInfo.setBindings(bindings);
		descSetLayout = device->createDescriptorSetLayoutUnique(createInfo);
	}

	void createDescriptorSet() {
		std::cout << "Create Descriptor Set\n";

		vk::DescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.setDescriptorPool(*descPool);
		allocateInfo.setSetLayouts(*descSetLayout);
		descSet = std::move(device->allocateDescriptorSetsUnique(allocateInfo).front());
	}

	void createRayTracingPipeline() {
		std::cout << "Create pipeline" << std::endl;

		vk::PipelineLayoutCreateInfo layoutCreateInfo{};
		layoutCreateInfo.setSetLayouts(*descSetLayout);
		pipelineLayout = device->createPipelineLayoutUnique(layoutCreateInfo);

		vk::RayTracingPipelineCreateInfoKHR pipelineCreateInfo{};
		pipelineCreateInfo.setLayout(*pipelineLayout);
		pipelineCreateInfo.setStages(shaderStages);
		pipelineCreateInfo.setGroups(shaderGroups);
		pipelineCreateInfo.setMaxPipelineRayRecursionDepth(1);
		auto result = device->createRayTracingPipelineKHRUnique(
			nullptr, nullptr, pipelineCreateInfo);
		if (result.result != vk::Result::eSuccess) {
			std::cerr << "Failed to create ray tracing pipeline\n";
			std::abort();
		}

		pipeline = std::move(result.value);
	}

	void createShaderBindingTable() {
		vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties = 
			vkutils::getRayTracingProps(physicalDevice);
		uint32_t handleSize = rtProperties.shaderGroupHandleSize;
		uint32_t handleAlignment = rtProperties.shaderGroupHandleAlignment;
		uint32_t baseAlignment = rtProperties.shaderGroupBaseAlignment;
		uint32_t handleSizeAligned = vkutils::alignUp(handleSize, handleAlignment);

		// Set strides and sizes
		uint32_t raygenShaderCount = 1;  // raygen count must be 1
		uint32_t missShaderCount = 1;
		uint32_t hitShaderCount = 1;

		raygenRegion.setStride(vkutils::alignUp(handleSizeAligned, baseAlignment));
		raygenRegion.setSize(raygenRegion.stride);

		missRegion.setStride(handleSizeAligned);
		missRegion.setSize(vkutils::alignUp(missShaderCount * handleSizeAligned, baseAlignment));

		hitRegion.setStride(handleSizeAligned);
		hitRegion.setSize(vkutils::alignUp(hitShaderCount * handleSizeAligned, baseAlignment));

		vk::DeviceSize sbtSize = raygenRegion.size + missRegion.size + hitRegion.size;
		sbt.init(physicalDevice, *device, sbtSize,
			vk::BufferUsageFlagBits::eShaderBindingTableKHR |
			vk::BufferUsageFlagBits::eTransferSrc |
			vk::BufferUsageFlagBits::eShaderDeviceAddress,
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent);

		uint32_t handleCount = raygenShaderCount + missShaderCount + hitShaderCount;
		uint32_t handleStorageSize = handleCount * handleSize;
		std::vector<uint8_t> handleStorage(handleStorageSize);
		auto result = device->getRayTracingShaderGroupHandlesKHR(
			*pipeline, 0, handleCount, handleStorageSize, handleStorage.data());
		if (result != vk::Result::eSuccess) {
			std::cerr << "Failed to get ray tracing shader group handles.\n";
			std::abort();
		}

		uint8_t* sbtHead = static_cast<uint8_t*>(device->mapMemory(*sbt.memory, 0, sbtSize));
		uint8_t* dstPtr = sbtHead;
		auto copyHandle = [&](uint32_t index) {
			std::memcpy(dstPtr, handleStorage.data() + handleSize * index, handleSize);
		};

		uint32_t handleIndex = 0;
		copyHandle(handleIndex++);

		dstPtr = sbtHead + raygenRegion.size;
		for (uint32_t c = 0; c < missShaderCount; c++) {
			copyHandle(handleIndex++);
			dstPtr += missRegion.stride;
		}

		dstPtr = sbtHead + raygenRegion.size + missRegion.size;
		for (uint32_t c = 0; c < hitShaderCount; c++) {
			copyHandle(handleIndex++);
			dstPtr += hitRegion.stride;
		}

		raygenRegion.setDeviceAddress(sbt.address);
		missRegion.setDeviceAddress(sbt.address + raygenRegion.size);
		hitRegion.setDeviceAddress(sbt.address + raygenRegion.size + missRegion.size);
	}

	void drawFrame() {
		deawImGui();

		vk::UniqueSemaphore imageAvailableSemaphore = device->createSemaphoreUnique({});

		auto result = device->acquireNextImageKHR(
			static_cast<vk::SwapchainKHR>(wd->Swapchain), std::numeric_limits<uint64_t>::max(), *imageAvailableSemaphore);

		if (result.result != vk::Result::eSuccess) {
			std::cerr << "Failed to acquire next image.\n";
			std::abort();
		}

		uint32_t imageIndex = result.value;

		updateDescriptorSet(*swapchainImageViews[imageIndex]);
		recordCommandBuffer(swapchainImages[imageIndex], &g_MainWindowData, draw_data);

		vk::PipelineStageFlags waitStage{ vk::PipelineStageFlagBits::eTopOfPipe };
		vk::SubmitInfo submitInfo{};
		submitInfo.setWaitDstStageMask(waitStage);
		submitInfo.setCommandBuffers(*commandBuffer);
		submitInfo.setWaitSemaphores(*imageAvailableSemaphore);
		queue.submit(submitInfo);

		queue.waitIdle();

		vk::PresentInfoKHR presentInfo{};
		presentInfo.setSwapchains(static_cast<const vk::SwapchainKHR&>(wd->Swapchain));
		presentInfo.setImageIndices(imageIndex);
		if (queue.presentKHR(presentInfo) != vk::Result::eSuccess) {
			std::cerr << "Failed to present\n";
			std::abort();
		}
	}

	void updateDescriptorSet(vk::ImageView imageView) {
		// DescriptorSetはshader実行中に各頂点,各ピクセル毎に共通して使われるリソースをまとめるもの
		// 今回はTLASと結果を書き込むためのイメージが共通リソースとして設定されてる
		// イメージに関してはスワップチェーンの~枚目みたいな指定の仕方

		std::vector<vk::WriteDescriptorSet> writes(2);

		vk::WriteDescriptorSetAccelerationStructureKHR accelInfo{};
		accelInfo.setAccelerationStructures(*topAccel.accel);

		writes[0].setDstSet(*descSet);
		writes[0].setDstBinding(0);
		writes[0].setDescriptorCount(1);
		writes[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
		writes[0].setPNext(&accelInfo);

		vk::DescriptorImageInfo imageInfo{};
		imageInfo.setImageView(imageView);
		imageInfo.setImageLayout(vk::ImageLayout::eGeneral);

		writes[1].setDstSet(*descSet);
		writes[1].setDstBinding(1);
		writes[1].setDescriptorType(vk::DescriptorType::eStorageImage);
		writes[1].setImageInfo(imageInfo);

		device->updateDescriptorSets(writes, nullptr);
	}

	void recordCommandBuffer(vk::Image image, ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data) {
		vk::Semaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;

		if (image_acquired_semaphore == VK_NULL_HANDLE) {
			throw std::runtime_error("Image acquired semaphore is invalid!");
		}

		vk::Semaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
		std::cout << wd->FrameIndex << std::endl;
		auto err = vkAcquireNextImageKHR(device.get(), wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
		//auto res = device->acquireNextImageKHR(wd->Swapchain, UINT64_MAX, image_acquired_semaphore);
		
		//if (res.result != vk::Result::eSuccess && res.result != vk::Result::eSuboptimalKHR) {
		//	throw std::runtime_error("Failed to acquire next image! Result: " + vk::to_string(res.result));
		//}

		wd->FrameIndex = err;
		std::cout << err << std::endl;
		
		if (wd->FrameIndex >= wd->ImageCount) {
			throw std::runtime_error("FrameIndex is out of range!");
		}
		
		ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

		device->waitForFences(vk::Fence(fd->Fence), VK_TRUE, UINT64_MAX);
		device->resetCommandPool(fd->CommandPool);
		vk::CommandBufferBeginInfo info = {};
		info.flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		commandBuffer->begin(vk::CommandBufferBeginInfo{});
		
		vkutils::setImageLayout(*commandBuffer, image,
			vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eGeneral);
		
		commandBuffer->bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
		
		commandBuffer->bindDescriptorSets(
			vk::PipelineBindPoint::eRayTracingKHR,
			*pipelineLayout,
			0,
			*descSet,
			nullptr);

		commandBuffer->traceRaysKHR(
			raygenRegion,
			missRegion,
			hitRegion,
			{},
			width,height, 1);

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.setRenderPass(*renderPass);
		renderPassInfo.setFramebuffer(fd->Framebuffer);
		vk::Rect2D rect({ 0,0 }, { (uint32_t)width,(uint32_t)height });

		renderPassInfo.setRenderArea(rect);

		commandBuffer->beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		ImGui::Render(); // ここで止まってる
		//for (;;);
		draw_data = ImGui::GetDrawData();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffer);

		commandBuffer->endRenderPass();

		vkutils::setImageLayout(*commandBuffer, image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR);

		//

		commandBuffer->end();
	}

	void initImGui() {
		imGuicontext = ImGui::CreateContext();
		ImGui::SetCurrentContext(imGuicontext);

		ImGui_ImplVulkan_InitInfo initInfo = {};
		initInfo.Instance = instance.get();
		initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(physicalDevice);
		initInfo.Device = device.get();
		initInfo.QueueFamily = queueFamilyIndex;
		initInfo.Queue = queue;
		initInfo.PipelineCache = VK_NULL_HANDLE;
		initInfo.DescriptorPool = *imGuiDescPool;
		initInfo.Allocator = nullptr;
		initInfo.MinImageCount = 2;
		initInfo.ImageCount = swapchainImages.size();
		initInfo.RenderPass = renderPass.get();
		initInfo.CheckVkResultFn = nullptr;

		ImGui_ImplVulkan_Init(&initInfo);
		ImGui_ImplVulkan_CreateFontsTexture();
	}

	void deawImGui() {
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Hello, world!");
		float f = 0.0f;
		ImGui::DragFloat("Drag", &f);
		bool b = false;
		ImGui::Checkbox("Check Box", &b);
		ImGui::Text("Yeah");
		ImGui::End();
	}

	void SetUpVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height) {
		wd->Surface = surface;

		// Formatを指定
		const std::vector<VkFormat> requestSurfaceImageFormat = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
		const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(physicalDevice, wd->Surface, requestSurfaceImageFormat.data(), requestSurfaceImageFormat.size(), requestSurfaceColorSpace);


		// PresentModeを指定
#ifdef IMGUI_UNLIMITED_FRAME_RATE
		std::vector<vk::PresentModeKHR> present_modes = { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eFifo };
#else
		std::vector<VkPresentModeKHR> present_modes = { VK_PRESENT_MODE_FIFO_KHR };
#endif
		wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, wd->Surface, present_modes.data(), present_modes.size());

		ImGui_ImplVulkanH_CreateOrResizeWindow(*instance, physicalDevice, *device, wd, queueFamilyIndex, nullptr, width, height, g_MinImageCount);
	}
};

int main() {
	Application app;
	app.run();
	return 0;
}