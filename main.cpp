#include "base.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

static VkInstance Instance;
static VkDevice Device;
static VkPhysicalDevice PhysicalDevice;
static VkQueue Queue;
static VkDeviceMemory DeviceMemory;

static constexpr u32 MaxSwapchainImageCount = 4;
static constexpr u32 FramesInFlight = 2;
static u32 SwapchainImageCount = 0;

static VkSemaphore ImageAvailableSemaphores[FramesInFlight];
static VkSemaphore RenderFinishedSemaphores[FramesInFlight];
static VkFence InFlightFences[FramesInFlight];

static VkPipeline Pipeline;
static VkSurfaceKHR Surface;
static VkSwapchainKHR Swapchain = 0;
static GLFWwindow *Window = 0;
static int WindowWidth = 1280, WindowHeight = 1280;

static u32 QueueFamilyIndex = -1;
static VkImage SwapchainImages[MaxSwapchainImageCount];

static VkCommandPool CommandPool;
static VkCommandBuffer CommandBuffers[FramesInFlight];

// Blit
static VkDescriptorSetLayout BlitDescriptorSetLayout;
static VkDescriptorSet BlitDescriptorSet;
static VkPipelineLayout BlitPipelineLayout;
static VkPipeline BlitPipeline;
static VkBuffer BlitUniformBuffer;
static VkImage BlitOutputImage;
static VkImageView BlitOutputImageView;

static VkDescriptorPool DescriptorPool;

static u32 BlitComputeShaderBytes[] =
	#include "blit.compute.h"
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

static memory_arena CPURenderData;
static vulkan_arena GPULocalArena;
static vulkan_arena GPUVisibleArena;

static u64 UniformBufferOffset;

static void CreateSwapchain() {
	glfwGetFramebufferSize(Window, &WindowWidth, &WindowHeight);

	{
		v2i *UniformData;
		vkMapMemory(Device, GPUVisibleArena.Memory, 0, sizeof(v2i), 0, (void **)&UniformData);
		UniformData->X = WindowWidth;
		UniformData->Y = WindowHeight;
		vkUnmapMemory(Device, GPUVisibleArena.Memory);
	}

	VkSurfaceCapabilitiesKHR Capabilities = {};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &Capabilities);

	s32 Width = S32_Clamp(WindowWidth, Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width);
	s32 Height = S32_Clamp(WindowHeight, Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height);

	vk_format_and_color FormatAndColor = VulkanGetBestAvailableFormatAndColor(PhysicalDevice, Surface);
	Swapchain = VulkanCreateSwapchain(Device, Surface, FormatAndColor, { WindowWidth, WindowHeight }, Swapchain);

	vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, NULL);
	RuntimeAssert(SwapchainImageCount <= MaxSwapchainImageCount && SwapchainImageCount > 0);
	RuntimeAssert(vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, SwapchainImages) == VK_SUCCESS);
}

s32 main() {
	Temp = CreateMemoryArena(MB(32));
	CPURenderData = CreateMemoryArena(MB(64));

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

			VkDescriptorSetLayoutBinding Bindings[] = {
				ImageBinding,
				UniformBufferBinding
			};

			bool Succeeded = true;

			VkDescriptorSetLayoutCreateInfo LayoutInfo = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = ArrayLen(Bindings),
				.pBindings = Bindings
			};
			RuntimeAssert(vkCreateDescriptorSetLayout(Device, &LayoutInfo, NULL, &BlitDescriptorSetLayout) == VK_SUCCESS);
			OnExitPush(vkDestroyDescriptorSetLayout(Device, BlitDescriptorSetLayout, NULL));

			VkDescriptorPoolSize PoolSizes[2] = {
				{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1
				},
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1
				}
			};
			VkDescriptorPoolCreateInfo PoolInfo = {};
			PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			PoolInfo.poolSizeCount = 2;
			PoolInfo.pPoolSizes = PoolSizes;
			PoolInfo.maxSets = 1;
			RuntimeAssert(vkCreateDescriptorPool(Device, &PoolInfo, NULL, &DescriptorPool) == VK_SUCCESS);
			OnExitPush(vkDestroyDescriptorPool(Device, DescriptorPool, NULL));

			VkDescriptorSetAllocateInfo DescriptorSetAllocInfo = {};
			DescriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			DescriptorSetAllocInfo.descriptorPool = DescriptorPool;
			DescriptorSetAllocInfo.descriptorSetCount = 1;
			DescriptorSetAllocInfo.pSetLayouts = &BlitDescriptorSetLayout;
			RuntimeAssert(vkAllocateDescriptorSets(Device, &DescriptorSetAllocInfo, &BlitDescriptorSet) == VK_SUCCESS);
			// OnExitPush([](){ vkFreeDescriptorSets(Device, DescriptorPool, 1, &BlitDescriptorSet); });

			constexpr u32 CodeSize = sizeof(BlitComputeShaderBytes);
			VkShaderModuleCreateInfo ShaderModuleCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
				.codeSize = CodeSize,
				.pCode = BlitComputeShaderBytes
			};
			VkShaderModule BlitShaderModule;
			RuntimeAssert(vkCreateShaderModule(Device, &ShaderModuleCreateInfo, NULL, &BlitShaderModule) == VK_SUCCESS);
			OnScopeExit(vkDestroyShaderModule(Device, BlitShaderModule, NULL));

			VkPipelineLayoutCreateInfo PipelineLayoutInfo = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &BlitDescriptorSetLayout
			};
			RuntimeAssert(vkCreatePipelineLayout(Device, &PipelineLayoutInfo, NULL, &BlitPipelineLayout) == VK_SUCCESS);
			OnExitPush(vkDestroyPipelineLayout(Device, BlitPipelineLayout, NULL));

			VkPipelineShaderStageCreateInfo ShaderStageCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = BlitShaderModule,
				.pName = "main"
			};
			VkComputePipelineCreateInfo PipelineCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage = ShaderStageCreateInfo,
				.layout = BlitPipelineLayout
			};
			RuntimeAssert(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineCreateInfo, NULL, &BlitPipeline) == VK_SUCCESS);
			OnExitPush(vkDestroyPipeline(Device, BlitPipeline, NULL));

			RuntimeAssert(Succeeded);

#if 0
			VkBufferCreateInfo BufferCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = sizeof(v2i),
				.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE
			};
			RuntimeAssert(vkCreateBuffer(Device, &BufferCreateInfo, NULL, &BlitUniformBuffer) == VK_SUCCESS);
			OnExitPush(vkDestroyBuffer(Device, BlitUniformBuffer, NULL));

			VkPhysicalDeviceMemoryProperties MemoryProperties;
			vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);

			VkMemoryRequirements UniformBufferRequirements;
			vkGetBufferMemoryRequirements(Device, BlitUniformBuffer, &UniformBufferRequirements);

			VkMemoryAllocateInfo AllocateInfo = {};
			AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			AllocateInfo.allocationSize = UniformBufferRequirements.size;
			AllocateInfo.memoryTypeIndex = FindMemoryType(UniformBufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, MemoryProperties);
			RuntimeAssert(vkAllocateMemory(Device, &AllocateInfo, NULL, &CPUVisibleMemory) == VK_SUCCESS);
			OnExitPush(vkFreeMemory(Device, CPUVisibleMemory, NULL));

			RuntimeAssert(vkBindBufferMemory(Device, BlitUniformBuffer, CPUVisibleMemory, 0) == VK_SUCCESS);

			VkMemoryRequirements ImageRequirements;
			vkGetImageMemoryRequirements(Device, BlitOutputImage, &ImageRequirements);

			AllocateInfo = {};
			AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			AllocateInfo.allocationSize = ImageRequirements.size;
			AllocateInfo.memoryTypeIndex = FindMemoryType(ImageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryProperties);
			RuntimeAssert(vkAllocateMemory(Device, &AllocateInfo, NULL, &GPULocalMemory) == VK_SUCCESS);
			OnExitPush(vkFreeMemory(Device, GPULocalMemory, NULL));

			RuntimeAssert(vkBindImageMemory(Device, BlitOutputImage, GPULocalMemory, 0) == VK_SUCCESS);
#endif 
			VkPhysicalDeviceMemoryProperties DeviceProperties;
			vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &DeviceProperties);

			const VkFormat ImageFormat = VK_FORMAT_R8G8B8A8_UNORM;

			{
				vulkan_arena_builder ArenaBuilder = StartBuildingMemoryArena(Device, &CPURenderData);
				BlitOutputImage = ArenaBuilder.Push2DImage({ WindowWidth, WindowHeight }, ImageFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
				GPULocalArena = ArenaBuilder.CommitAndAllocateArena(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DeviceProperties);
			}
			{
				vulkan_arena_builder ArenaBuilder = StartBuildingMemoryArena(Device, &CPURenderData);
				BlitUniformBuffer = ArenaBuilder.PushBuffer(sizeof(v2i), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, &UniformBufferOffset);
				GPUVisibleArena = ArenaBuilder.CommitAndAllocateArena(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DeviceProperties);
			}


			VkImageViewCreateInfo ImageViewCreateInfo = {};
			ImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ImageViewCreateInfo.image = BlitOutputImage;
			ImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ImageViewCreateInfo.format = ImageFormat;
			ImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			ImageViewCreateInfo.subresourceRange.levelCount = 1;
			ImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			ImageViewCreateInfo.subresourceRange.layerCount = 1;
			RuntimeAssert(vkCreateImageView(Device, &ImageViewCreateInfo, NULL, &BlitOutputImageView) == VK_SUCCESS);
			OnExitPush(vkDestroyImageView(Device, BlitOutputImageView, NULL));
		}

		{
			VkDescriptorBufferInfo BufferInfo = {};
			BufferInfo.buffer = BlitUniformBuffer;
			BufferInfo.offset = 0;
			BufferInfo.range = sizeof(v2i);

			VkDescriptorImageInfo ImageInfo = {};
			ImageInfo.imageView = BlitOutputImageView;
			ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet UniformBufferUpdate = {};
			UniformBufferUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			UniformBufferUpdate.dstSet = BlitDescriptorSet;
			UniformBufferUpdate.dstBinding = 1;
			UniformBufferUpdate.dstArrayElement = 0;
			UniformBufferUpdate.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			UniformBufferUpdate.descriptorCount = 1;
			UniformBufferUpdate.pBufferInfo = &BufferInfo;

			VkWriteDescriptorSet ImageUpdate = {};
			ImageUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ImageUpdate.dstSet = BlitDescriptorSet;
			ImageUpdate.dstBinding = 0;
			ImageUpdate.dstArrayElement = 0;
			ImageUpdate.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			ImageUpdate.descriptorCount = 1;
			ImageUpdate.pImageInfo = &ImageInfo;

			VkWriteDescriptorSet DescriptorWrites[] = {
				UniformBufferUpdate,
				ImageUpdate,
			};
			vkUpdateDescriptorSets(Device, ArrayLen(DescriptorWrites), DescriptorWrites, 0, NULL);
		}

		CreateSwapchain();
		OnExitPush(vkDestroySwapchainKHR(Device, Swapchain, NULL));

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

		{
			VkCommandBuffer TempCMD = VulkanBeginSingleTimeCommands(Device, CommandPool);

			CmdTransitionImageLayout(TempCMD, BlitOutputImage,
				{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0 },
				{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT  }
			);

			for (u32 i = 0; i < SwapchainImageCount; ++i) {
				CmdTransitionImageLayout(TempCMD, SwapchainImages[i],
					{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0 },
					{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0 }
				);
			}

			VulkanEndSingleTimeCommands(Device, CommandPool, Queue, TempCMD);
		}

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
		glfwPollEvents();

		RuntimeAssert(vkWaitForFences(Device, 1, InFlightFences + CurrentFrame, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
		vkResetFences(Device, 1, InFlightFences + CurrentFrame);

		VkResult AcquireImageResult = vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, ImageAvailableSemaphores[CurrentFrame], VK_NULL_HANDLE, &ImageIndex);
		VkCommandBuffer CommandBuffer = CommandBuffers[CurrentFrame];

		{
			vkResetCommandBuffer(CommandBuffer, 0);
			VulkanBeginCommands(CommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			constexpr cmd_image_transition ComputeTransition =
				{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT };
			constexpr cmd_image_transition TransferSrcTransition =
				{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT };
			constexpr cmd_image_transition TransferDstTransition =
				{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
			constexpr cmd_image_transition PresentTransition =
				{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0 };

			CmdTransitionImageLayout(CommandBuffer, BlitOutputImage, TransferSrcTransition, ComputeTransition);

			vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, BlitPipeline);
			vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, BlitPipelineLayout, 0, 1, &BlitDescriptorSet, 0, NULL);
			vkCmdDispatch(CommandBuffer, (WindowWidth + 15) / 16, (WindowHeight + 15) / 16, 1);

			CmdTransitionImageLayout(CommandBuffer, BlitOutputImage, ComputeTransition, TransferSrcTransition);
			CmdTransitionImageLayout(CommandBuffer, SwapchainImages[ImageIndex], PresentTransition, TransferDstTransition);
			v2i Resolution = { WindowWidth, WindowHeight };
			CmdBlit2DImage(CommandBuffer, BlitOutputImage, SwapchainImages[ImageIndex], Resolution, Resolution);
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

		vkQueuePresentKHR(Queue, &PresentInfo);

		if (AcquireImageResult == VK_ERROR_OUT_OF_DATE_KHR || AcquireImageResult == VK_SUBOPTIMAL_KHR) {
			vkDeviceWaitIdle(Device);
			CreateSwapchain();
			CurrentFrame = 0;
		} else {
			RuntimeAssert(AcquireImageResult == VK_SUCCESS);
		}

		CurrentFrame += 1;
		CurrentFrame %= FramesInFlight;
	}

	vkDeviceWaitIdle(Device);
	ExitApp(0);

	return 0;
}
