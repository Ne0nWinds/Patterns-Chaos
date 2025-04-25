#include "base.h"

#include "shared_constants.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

static VkInstance Instance;
static VkDevice Device;
static VkPhysicalDevice PhysicalDevice;
static VkQueue Queue;
static VkDeviceMemory DeviceMemory;

static constexpr u32 MaxSwapchainImageCount = 4;
static constexpr u32 FramesInFlight = 1;
static constexpr u32 MaxParticleCount = MAX_PARTICLE_COUNT;
static u32 ParticleCount = MaxParticleCount;
static u32 FrameNumber = 0;
static bool ResetParticleState = true;

static u32 SwapchainImageCount = 0;

static VkSemaphore ImageAvailableSemaphores[FramesInFlight];
static VkSemaphore RenderFinishedSemaphores[FramesInFlight];
static VkFence InFlightFences[FramesInFlight];

static VkPipeline Pipeline;
static VkSurfaceKHR Surface;
static VkSwapchainKHR Swapchain = 0;
static GLFWwindow *Window = 0;
static int WindowWidth = 1080, WindowHeight = 1080;
static u32 DensityBufferSize = 0;
static u32 DensityBufferWidth = 0;

static u32 QueueFamilyIndex = -1;
static VkImage SwapchainImages[MaxSwapchainImageCount];

static VkCommandPool CommandPool;
static VkCommandBuffer CommandBuffers[FramesInFlight];

static VkDescriptorSetLayout DescriptorSetLayout;
static VkDescriptorSet DescriptorSet;
static VkPipelineLayout PipelineLayout;
static VkImage OutputImage;
static VkImageView OutputImageView;

enum {
	BUFFER_IDX_UNIFORM,
	BUFFER_IDX_POSITION,
	BUFFER_IDX_ANGLE,
	BUFFER_IDX_DENSITY_FIELD,
	BUFFER_IDX_COUNT
};

static VkDescriptorBufferInfo BufferHandles[BUFFER_IDX_COUNT] = {0};

struct uniform_data {
	v2i ImageSize;
	u32 ParticleCount;
	u32 FrameNumber;
	u32 DensityBufferSize;
	u32 DensityBufferWidth;
};

static VkPipeline ClearComputePipeline;
static VkPipeline ResetComputePipeline;
static VkPipeline FadeComputePipeline;
static VkPipeline SimulateComputePipeline;
static VkPipeline RenderDensityBufferComputePipeline;

static VkDescriptorPool DescriptorPool;

static u32 ResetComputeShader[] =
	#include "reset.compute.h"
;
static u32 FadeComputeShader[] =
	#include "fade.compute.h"
;
static u32 ClearComputeShader[] =
	#include "clear.compute.h"
;
static u32 SimulateComputeShader[] =
	#include "simulate.compute.h"
;

static u32 RenderDensityBufferComputeShader[] =
	#include "render_density_buffer.compute.h"
;

#define OnExitPush(...) {\
	static auto Task = [](){ __VA_ARGS__; };\
	PushCleanUpTask(&VulkanCleanupStack, Task);\
}

[[noreturn]]
static void ExitApp(u32 ErrorCode);

#define RuntimeAssert(Expression) {\
	if (!(Expression)) {\
		printf("Assertion failed: %s, file %s, line: %d", #Expression, __FILE__, __LINE__);\
		Break();\
		ExitApp(1);\
	}\
}

#include "vulkan_helpers.h"
#include "vulkan_allocator.h"

[[noreturn]]
static void ExitApp(u32 ErrorCode) {
	if (Device) vkDeviceWaitIdle(Device);
	PopAllVulkanCleanUpTasks(&VulkanCleanupStack);
	glfwTerminate();
	exit(ErrorCode);
}

static inline s32 S32_Max(s32 A, s32 B) {
	return (A > B) ? A : B;
}
static inline s32 S32_Min(s32 A, s32 B) {
	return (A > B) ? A : B;
}
static inline s32 S32_Clamp(s32 A, s32 Min, s32 Max) {
	return S32_Min(S32_Max(A, Min), Max);
}

static vulkan_arena<4> GPULocalArena;
static vulkan_arena<1> GPUVisibleArena;

static void UpdateDescriptorSets() {

	VkDescriptorImageInfo ImageInfo = {};
	ImageInfo.imageView = OutputImageView;
	ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet ImageUpdate = {};
	ImageUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	ImageUpdate.dstSet = DescriptorSet;
	ImageUpdate.dstBinding = 0;
	ImageUpdate.dstArrayElement = 0;
	ImageUpdate.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	ImageUpdate.descriptorCount = 1;
	ImageUpdate.pImageInfo = &ImageInfo;

	VkWriteDescriptorSet UniformBufferUpdate = {};
	UniformBufferUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	UniformBufferUpdate.dstSet = DescriptorSet;
	UniformBufferUpdate.dstBinding = 1;
	UniformBufferUpdate.dstArrayElement = 0;
	UniformBufferUpdate.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	UniformBufferUpdate.descriptorCount = 1;
	UniformBufferUpdate.pBufferInfo = &BufferHandles[BUFFER_IDX_UNIFORM];

	VkWriteDescriptorSet PositionBufferUpdate = {};
	PositionBufferUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	PositionBufferUpdate.dstSet = DescriptorSet;
	PositionBufferUpdate.dstBinding = 2;
	PositionBufferUpdate.dstArrayElement = 0;
	PositionBufferUpdate.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	PositionBufferUpdate.descriptorCount = 1;
	PositionBufferUpdate.pBufferInfo = &BufferHandles[BUFFER_IDX_POSITION];

	VkWriteDescriptorSet AngleBufferUpdate = {};
	AngleBufferUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	AngleBufferUpdate.dstSet = DescriptorSet;
	AngleBufferUpdate.dstBinding = 3;
	AngleBufferUpdate.dstArrayElement = 0;
	AngleBufferUpdate.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	AngleBufferUpdate.descriptorCount = 1;
	AngleBufferUpdate.pBufferInfo = &BufferHandles[BUFFER_IDX_ANGLE];

	VkWriteDescriptorSet DensityFieldBufferUpdate = {};
	DensityFieldBufferUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	DensityFieldBufferUpdate.dstSet = DescriptorSet;
	DensityFieldBufferUpdate.dstBinding = 4;
	DensityFieldBufferUpdate.dstArrayElement = 0;
	DensityFieldBufferUpdate.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	DensityFieldBufferUpdate.descriptorCount = 1;
	DensityFieldBufferUpdate.pBufferInfo = &BufferHandles[BUFFER_IDX_DENSITY_FIELD];

	VkWriteDescriptorSet DescriptorWrites[] = {
		ImageUpdate,
		UniformBufferUpdate,
		PositionBufferUpdate,
		AngleBufferUpdate,
		DensityFieldBufferUpdate,
	};
	vkUpdateDescriptorSets(Device, ArrayLen(DescriptorWrites), DescriptorWrites, 0, NULL);
}

static void CreateSwapchain() {
	glfwGetFramebufferSize(Window, &WindowWidth, &WindowHeight);

	VkSurfaceCapabilitiesKHR Capabilities = {};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &Capabilities);

	s32 Width = S32_Clamp(WindowWidth, Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width);
	s32 Height = S32_Clamp(WindowHeight, Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height);

	vk_format_and_color FormatAndColor = VulkanGetBestAvailableFormatAndColor(PhysicalDevice, Surface);
	vkDestroySwapchainKHR(Device, Swapchain, NULL);
	Swapchain = VulkanCreateSwapchain(Device, Surface, FormatAndColor, { Width, Height });

	vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, NULL);
	RuntimeAssert(SwapchainImageCount <= MaxSwapchainImageCount && SwapchainImageCount > 0);
	RuntimeAssert(vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, SwapchainImages) == VK_SUCCESS);

	const VkFormat ImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	VkPhysicalDeviceMemoryProperties DeviceProperties;
	vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &DeviceProperties);

	GPULocalArena.Destroy(Device);
	{
		auto ArenaBuilder = StartBuildingMemoryArena<4>(Device);
		BufferHandles[BUFFER_IDX_POSITION].buffer = ArenaBuilder.PushBuffer(sizeof(v2) * MaxParticleCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE);
		BufferHandles[BUFFER_IDX_ANGLE].buffer = ArenaBuilder.PushBuffer(sizeof(f32) * MaxParticleCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE);

		constexpr u32 Downscale = DENSITY_BUFFER_DOWNSCALE;
		u32 Width = (WindowWidth + Downscale - 1) / Downscale;
		u32 Height = (WindowHeight + Downscale - 1) / Downscale;
		DensityBufferSize = sizeof(u32) * Width * Height;
		DensityBufferWidth = Width;
		BufferHandles[BUFFER_IDX_DENSITY_FIELD].buffer = ArenaBuilder.PushBuffer(DensityBufferSize * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE);

		OutputImage = ArenaBuilder.Push2DImage({ WindowWidth, WindowHeight }, ImageFormat, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		GPULocalArena = ArenaBuilder.CommitAndAllocateArena(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DeviceProperties);
	}

	if (OutputImageView) {
		vkDestroyImageView(Device, OutputImageView, NULL);
	}

	VkImageViewCreateInfo ImageViewCreateInfo = {};
	ImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ImageViewCreateInfo.image = OutputImage;
	ImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ImageViewCreateInfo.format = ImageFormat;
	ImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	ImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	ImageViewCreateInfo.subresourceRange.levelCount = 1;
	ImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	ImageViewCreateInfo.subresourceRange.layerCount = 1;
	RuntimeAssert(vkCreateImageView(Device, &ImageViewCreateInfo, NULL, &OutputImageView) == VK_SUCCESS);

	UpdateDescriptorSets();

	const auto TransitionImagesCmdList = [](const VkCommandBuffer TempCMD){
		CmdTransitionImageLayout(TempCMD, OutputImage,
			{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0 },
			{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT  }
		);

		for (u32 i = 0; i < SwapchainImageCount; ++i) {
			CmdTransitionImageLayout(TempCMD, SwapchainImages[i],
				{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0 },
				{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0 }
			);
		}
	};

	VulkanExecuteCommandsImmediate(Device, CommandPool, Queue, TransitionImagesCmdList);
	ResetParticleState = true;
}

void KeyCallback(GLFWwindow *Window, int Key, int ScanCode, int Action, int Mods) {
	if (Key == GLFW_KEY_R && Action == GLFW_PRESS) {
		ResetParticleState = true;
	}
}

s32 main() {
	Temp = CreateMemoryArena(MB(32));

	// Init
	{
		RuntimeAssert(glfwInit());
		RuntimeAssert(glfwVulkanSupported());

		VkApplicationInfo AppInfo = {};
		AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		AppInfo.pApplicationName = "Primordial Particle System";
		AppInfo.pEngineName = "N/A";
		AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		AppInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo CreateInfo = {};
		CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		CreateInfo.pApplicationInfo = &AppInfo;

		u32 GLFWExtensionCount = 0;
		const char **GLFWExtensions = glfwGetRequiredInstanceExtensions(&GLFWExtensionCount);
		CreateInfo.enabledExtensionCount = GLFWExtensionCount;
		CreateInfo.ppEnabledExtensionNames = GLFWExtensions;

		RuntimeAssert(vkCreateInstance(&CreateInfo, NULL, &Instance) == VK_SUCCESS);
		OnExitPush(vkDestroyInstance(Instance, NULL));

		{
			u32 DeviceCount = 0;
			vkEnumeratePhysicalDevices(Instance, &DeviceCount, NULL);
			VkPhysicalDevice *PhysicalDevices = PushStruct(&Temp, VkPhysicalDevice, DeviceCount);
			vkEnumeratePhysicalDevices(Instance, &DeviceCount, PhysicalDevices);

			u32 DeviceSelection = -1;

			for (u32 i = 0; i < DeviceCount && DeviceSelection == -1; ++i) {
				VkPhysicalDeviceProperties Properties = {};
				vkGetPhysicalDeviceProperties(PhysicalDevices[i], &Properties);

				if (Properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
					Properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
					continue;
				}

				u32 QueuePropertiesCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[i], &QueuePropertiesCount, NULL);
				VkQueueFamilyProperties *QueuePropertiesList = PushStruct(&Temp, VkQueueFamilyProperties, QueuePropertiesCount);
				vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[i], &QueuePropertiesCount, QueuePropertiesList);
				
				for (u32 j = 0; j < QueuePropertiesCount; ++j) {
					bool SupportsGraphics = (QueuePropertiesList[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
					bool SupportsCompute = (QueuePropertiesList[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
					bool SupportsTransfer = (QueuePropertiesList[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
					bool SupportsPresentation = glfwGetPhysicalDevicePresentationSupport(Instance, PhysicalDevices[i], j);

					if (SupportsGraphics && SupportsCompute && SupportsTransfer && SupportsPresentation) {
						QueueFamilyIndex = j;
						DeviceSelection = i;
						break;
					}
				}

				Pop(&Temp, QueuePropertiesList);
			}

			RuntimeAssert(DeviceSelection != -1);
			RuntimeAssert(QueueFamilyIndex != -1);
			PhysicalDevice = PhysicalDevices[DeviceSelection];

			f32 Priority = 1.0f;
			VkDeviceQueueCreateInfo QueueCreateInfo = {};
			QueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			QueueCreateInfo.queueFamilyIndex = QueueFamilyIndex;
			QueueCreateInfo.queueCount = 1;
			QueueCreateInfo.pQueuePriorities = &Priority;

			const char *DeviceExtensions[] = {
				"VK_KHR_swapchain"
			};

			VkDeviceCreateInfo DeviceCreateInfo = {};
			DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			DeviceCreateInfo.queueCreateInfoCount = 1;
			DeviceCreateInfo.pQueueCreateInfos = &QueueCreateInfo;
			DeviceCreateInfo.enabledExtensionCount = ArrayLen(DeviceExtensions);
			DeviceCreateInfo.ppEnabledExtensionNames = DeviceExtensions;

			RuntimeAssert(vkCreateDevice(PhysicalDevice, &DeviceCreateInfo, NULL, &Device) == VK_SUCCESS);
			OnExitPush(vkDestroyDevice(Device, NULL));

			vkGetDeviceQueue(Device, QueueFamilyIndex, 0, &Queue);
		}

		{
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			Window = glfwCreateWindow(WindowWidth, WindowHeight, "Primordial Particle System", NULL, NULL);
			RuntimeAssert(Window);

			glfwSetKeyCallback(Window, KeyCallback);

			glfwCreateWindowSurface(Instance, Window, NULL, &Surface);
			RuntimeAssert(Surface);

			OnExitPush({
				vkDestroySurfaceKHR(Instance, Surface, NULL);
				glfwDestroyWindow(Window);
			});

		}

		// Blit Compute Shader
		{
			VkDescriptorSetLayoutBinding ImageBinding = {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			};
			VkDescriptorSetLayoutBinding UniformBufferBinding = {
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			};
			VkDescriptorSetLayoutBinding PositionBinding = {
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			};
			VkDescriptorSetLayoutBinding AngleBinding = {
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			};
			VkDescriptorSetLayoutBinding DensityFieldBinding = {
				.binding = 4,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			};

			VkDescriptorSetLayoutBinding Bindings[] = {
				ImageBinding,
				UniformBufferBinding,
				PositionBinding,
				AngleBinding,
				DensityFieldBinding
			};

			bool Succeeded = true;

			VkDescriptorSetLayoutCreateInfo LayoutInfo = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = ArrayLen(Bindings),
				.pBindings = Bindings
			};
			RuntimeAssert(vkCreateDescriptorSetLayout(Device, &LayoutInfo, NULL, &DescriptorSetLayout) == VK_SUCCESS);
			OnExitPush(vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, NULL));

			VkDescriptorPoolSize PoolSizes[3] = {
				{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1
				},
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1
				},
				{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 3
				},
			};
			VkDescriptorPoolCreateInfo PoolInfo = {};
			PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			PoolInfo.poolSizeCount = ArrayLen(PoolSizes);
			PoolInfo.pPoolSizes = PoolSizes;
			PoolInfo.maxSets = 1;
			RuntimeAssert(vkCreateDescriptorPool(Device, &PoolInfo, NULL, &DescriptorPool) == VK_SUCCESS);
			OnExitPush(vkDestroyDescriptorPool(Device, DescriptorPool, NULL));

			VkDescriptorSetAllocateInfo DescriptorSetAllocInfo = {};
			DescriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			DescriptorSetAllocInfo.descriptorPool = DescriptorPool;
			DescriptorSetAllocInfo.descriptorSetCount = 1;
			DescriptorSetAllocInfo.pSetLayouts = &DescriptorSetLayout;
			RuntimeAssert(vkAllocateDescriptorSets(Device, &DescriptorSetAllocInfo, &DescriptorSet) == VK_SUCCESS);
			// OnExitPush([](){ vkFreeDescriptorSets(Device, DescriptorPool, 1, &DescriptorSet); });

			VkPipelineLayoutCreateInfo PipelineLayoutInfo = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &DescriptorSetLayout
			};
			RuntimeAssert(vkCreatePipelineLayout(Device, &PipelineLayoutInfo, NULL, &PipelineLayout) == VK_SUCCESS);
			OnExitPush(vkDestroyPipelineLayout(Device, PipelineLayout, NULL));

			ResetComputePipeline = VulkanCreateComputeShaderPipeline(CreateRange(ResetComputeShader), PipelineLayout);
			ClearComputePipeline = VulkanCreateComputeShaderPipeline(CreateRange(ClearComputeShader), PipelineLayout);
			FadeComputePipeline = VulkanCreateComputeShaderPipeline(CreateRange(FadeComputeShader), PipelineLayout);
			SimulateComputePipeline = VulkanCreateComputeShaderPipeline(CreateRange(SimulateComputeShader), PipelineLayout);
			RenderDensityBufferComputePipeline = VulkanCreateComputeShaderPipeline(CreateRange(RenderDensityBufferComputeShader), PipelineLayout);
			OnExitPush({
				vkDestroyPipeline(Device, ResetComputePipeline, NULL);
				vkDestroyPipeline(Device, ClearComputePipeline, NULL);
				vkDestroyPipeline(Device, FadeComputePipeline, NULL);
				vkDestroyPipeline(Device, SimulateComputePipeline, NULL);
				vkDestroyPipeline(Device, RenderDensityBufferComputePipeline, NULL);
			});

			RuntimeAssert(Succeeded);

			VkPhysicalDeviceMemoryProperties DeviceProperties;
			vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &DeviceProperties);

			const VkFormat ImageFormat = VK_FORMAT_R8G8B8A8_UNORM;

			{
				vulkan_arena_builder<1> ArenaBuilder = StartBuildingMemoryArena<1>(Device);
				BufferHandles[BUFFER_IDX_UNIFORM].buffer = ArenaBuilder.PushBuffer(sizeof(uniform_data), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE);
				GPUVisibleArena = ArenaBuilder.CommitAndAllocateArena(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DeviceProperties);
			}
			OnExitPush({
				GPULocalArena.Destroy(Device);
				GPUVisibleArena.Destroy(Device);
			});

			for (u32 i = 0; i < BUFFER_IDX_COUNT; ++i) {
				BufferHandles[i].offset = 0;
				BufferHandles[i].range = VK_WHOLE_SIZE;
			}

		}

		{
			VkCommandPoolCreateInfo CommandPoolCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
				.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
				.queueFamilyIndex = QueueFamilyIndex
			};
			RuntimeAssert(vkCreateCommandPool(Device, &CommandPoolCreateInfo, NULL, &CommandPool) == VK_SUCCESS);
			OnExitPush(vkDestroyCommandPool(Device, CommandPool, NULL));

			VulkanAllocateCommandBuffers(Device, CommandPool, CreateRange(CommandBuffers));
			OnExitPush(vkFreeCommandBuffers(Device, CommandPool, ArrayLen(CommandBuffers), CommandBuffers));
		}

		CreateSwapchain();
		OnExitPush(vkDestroySwapchainKHR(Device, Swapchain, NULL));
		OnExitPush(vkDestroyImageView(Device, OutputImageView, NULL));

		Reset(&Temp);
	}

	{
		for (u32 i = 0; i < FramesInFlight; ++i) {
			ImageAvailableSemaphores[i] = VulkanCreateSemaphore(Device);
			RenderFinishedSemaphores[i] = VulkanCreateSemaphore(Device);
			InFlightFences[i] = VulkanCreateFence(Device, true);
		}

		OnExitPush({
			for (u32 i = 0; i < FramesInFlight; ++i) {
				vkDestroySemaphore(Device, ImageAvailableSemaphores[i], NULL);
				vkDestroySemaphore(Device, RenderFinishedSemaphores[i], NULL);
				vkDestroyFence(Device, InFlightFences[i], NULL);
			}
		});
	}

	u32 CurrentFrame = 0;
	u32 ImageIndex = 0;

	while (!glfwWindowShouldClose(Window)) {

		RuntimeAssert(vkWaitForFences(Device, 1, InFlightFences + CurrentFrame, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
		glfwPollEvents();

		VkResult AcquireImageResult = vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, ImageAvailableSemaphores[CurrentFrame], VK_NULL_HANDLE, &ImageIndex);

		if (AcquireImageResult == VK_ERROR_OUT_OF_DATE_KHR || AcquireImageResult == VK_SUBOPTIMAL_KHR) {
			vkDeviceWaitIdle(Device);
			CreateSwapchain();
			CurrentFrame = 0;
			FrameNumber = 0;
			continue;
		} else {
			RuntimeAssert(AcquireImageResult == VK_SUCCESS);
		}

		vkResetFences(Device, 1, InFlightFences + CurrentFrame);

		{
			uniform_data *UniformData;
			vkMapMemory(Device, GPUVisibleArena.Memory, 0, sizeof(uniform_data), 0, (void **)&UniformData);
			UniformData->ImageSize.X = WindowWidth;
			UniformData->ImageSize.Y = WindowHeight;
			UniformData->ParticleCount = ParticleCount;
			UniformData->FrameNumber = FrameNumber;
			UniformData->DensityBufferSize = DensityBufferSize;
			UniformData->DensityBufferWidth = DensityBufferWidth;
			vkUnmapMemory(Device, GPUVisibleArena.Memory);
		}

		VkCommandBuffer CommandBuffer = CommandBuffers[CurrentFrame];
		{
			vkResetCommandBuffer(CommandBuffer, 0);
			VulkanBeginCommands(CommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			constexpr cmd_image_transition ComputeRWTransition =
				{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT };
			constexpr cmd_image_transition TransferSrcTransition =
				{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT };
			constexpr cmd_image_transition TransferDstTransition =
				{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
			constexpr cmd_image_transition PresentTransition =
				{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0 };

			CmdTransitionImageLayout(CommandBuffer, OutputImage, TransferSrcTransition, ComputeRWTransition);

			vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &DescriptorSet, 0, NULL);

			// vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ClearComputePipeline);
			// vkCmdDispatch(CommandBuffer, (WindowWidth + 15) / 16, (WindowHeight + 15) / 16, 1);

			if (ResetParticleState) {
				vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ClearComputePipeline);
				vkCmdDispatch(CommandBuffer, (WindowWidth + 15) / 16, (WindowHeight + 15) / 16, 1);

				VkBufferMemoryBarrier DensityFieldBarrier = {
					.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
					.buffer = BufferHandles[BUFFER_IDX_DENSITY_FIELD].buffer,
					.offset = 0,
					.size = VK_WHOLE_SIZE
				};
				vkCmdPipelineBarrier(CommandBuffer,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0, 0, NULL, 1, &DensityFieldBarrier, 0, NULL
				);

				vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ResetComputePipeline);
				vkCmdDispatch(CommandBuffer, (ParticleCount + 127) / 128, 1, 1);

				VkBuffer Buffers[] = {
					BufferHandles[BUFFER_IDX_POSITION].buffer,
					BufferHandles[BUFFER_IDX_ANGLE].buffer,
					BufferHandles[BUFFER_IDX_DENSITY_FIELD].buffer,
				};
				VkBufferMemoryBarrier Barriers[ArrayLen(Buffers)] = {};
				for (u32 i = 0; i < ArrayLen(Barriers); ++i) {
					Barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
					Barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					Barriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
					Barriers[i].buffer = Buffers[i];
					Barriers[i].offset = 0;
					Barriers[i].size = VK_WHOLE_SIZE;
				}
				vkCmdPipelineBarrier(CommandBuffer,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0, 0, NULL, ArrayLen(Buffers), Barriers, 0, NULL
				);
				CmdTransitionImageLayout(CommandBuffer, OutputImage, ComputeRWTransition, ComputeRWTransition);
				ResetParticleState = false;
			}

			vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, FadeComputePipeline);
			vkCmdDispatch(CommandBuffer, (WindowWidth + 15) / 16, (WindowHeight + 15) / 16, 1);
			CmdTransitionImageLayout(CommandBuffer, OutputImage, ComputeRWTransition, ComputeRWTransition);
			CmdBufferMemoryBarrier(CommandBuffer, BufferHandles[BUFFER_IDX_DENSITY_FIELD].buffer,
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT },
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT }
			);
			vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, SimulateComputePipeline);
			vkCmdDispatch(CommandBuffer, (ParticleCount + 127) / 128, 1, 1);
			CmdBufferMemoryBarrier(CommandBuffer, BufferHandles[BUFFER_IDX_DENSITY_FIELD].buffer,
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT },
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT }
			);
			vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, RenderDensityBufferComputePipeline);
			vkCmdDispatch(CommandBuffer, (WindowWidth + 15) / 16, (WindowHeight + 15) / 16, 1);
			CmdBufferMemoryBarrier(CommandBuffer, BufferHandles[BUFFER_IDX_DENSITY_FIELD].buffer,
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT },
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT }
			);

			CmdTransitionImageLayout(CommandBuffer, OutputImage, ComputeRWTransition, TransferSrcTransition);
			CmdTransitionImageLayout(CommandBuffer, SwapchainImages[ImageIndex], PresentTransition, TransferDstTransition);
			v2i Resolution = { WindowWidth, WindowHeight };
			CmdBlit2DImage(CommandBuffer, OutputImage, SwapchainImages[ImageIndex], Resolution, Resolution);
			CmdTransitionImageLayout(CommandBuffer, SwapchainImages[ImageIndex], TransferDstTransition, PresentTransition);

			VulkanEndCommands(CommandBuffer);
		}

		VkPipelineStageFlags WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo SubmitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = ImageAvailableSemaphores + CurrentFrame,
			.pWaitDstStageMask = WaitStages,
			.commandBufferCount = 1,
			.pCommandBuffers = &CommandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = RenderFinishedSemaphores + CurrentFrame,
		};
		RuntimeAssert(vkQueueSubmit(Queue, 1, &SubmitInfo, InFlightFences[CurrentFrame]) == VK_SUCCESS);

		VkPresentInfoKHR PresentInfo = {};
		PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		PresentInfo.waitSemaphoreCount = 1;
		PresentInfo.pWaitSemaphores = RenderFinishedSemaphores + CurrentFrame;
		PresentInfo.swapchainCount = 1;
		PresentInfo.pSwapchains = &Swapchain;
		PresentInfo.pImageIndices = &ImageIndex;

		VkResult PresentResult = vkQueuePresentKHR(Queue, &PresentInfo);
		if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR || PresentResult == VK_SUBOPTIMAL_KHR) {
			vkDeviceWaitIdle(Device);
			CreateSwapchain();
			CurrentFrame = 0;
			FrameNumber = 0;
			continue;
		}

		CurrentFrame += 1;
		CurrentFrame %= FramesInFlight;
		FrameNumber += 1;
	}

	vkDeviceWaitIdle(Device);
	ExitApp(0);

	return 0;
}
