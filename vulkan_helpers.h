#pragma once

typedef void (*VulkanCleanupCallback)();
struct vulkan_cleanup_task {
	VulkanCleanupCallback Callback;
};
struct vulkan_cleanup_stack {
	vulkan_cleanup_task Tasks[256];
	u32 Top = 0;
};
static vulkan_cleanup_stack VulkanCleanupStack = {0};
static constexpr void PopAllVulkanCleanUpTasks(vulkan_cleanup_stack *Stack) {
	for (s32 i = Stack->Top - 1; i >= 0; --i) {
		VulkanCleanupCallback Callback = Stack->Tasks[i].Callback;
		Callback();
	}
	Stack->Top = 0;
}

static inline void PushCleanUpTask(vulkan_cleanup_stack *Stack, VulkanCleanupCallback Callback) {
	vulkan_cleanup_task Task = {
		Callback
	};
	Stack->Tasks[Stack->Top] = Task;
	Stack->Top += 1;
	Assert(Stack->Top < ArrayLen(Stack->Tasks));
}

static inline uint32_t FindMemoryType(uint32_t TypeFilter, VkMemoryPropertyFlags Properties, const VkPhysicalDeviceMemoryProperties &MemProperties) {
    for (uint32_t i = 0; i < MemProperties.memoryTypeCount; i++) {
        if ((TypeFilter & (1 << i)) && (MemProperties.memoryTypes[i].propertyFlags & Properties) == Properties) {
            return i;
        }
    }
	RuntimeAssert(0);
	return -1;
}

static inline VkFence VulkanCreateFence(VkDevice Device, bool Signaled) {
	VkFenceCreateInfo FenceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = (Signaled) ? VK_FENCE_CREATE_SIGNALED_BIT : static_cast<VkFenceCreateFlags>(0)
	};
	VkFence Result = 0;
	RuntimeAssert(vkCreateFence(Device, &FenceCreateInfo, NULL, &Result) == VK_SUCCESS);
	return Result;
}

static inline VkSemaphore VulkanCreateSemaphore(VkDevice Device) {
	VkSemaphoreCreateInfo SemaphoreCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	VkSemaphore Result = 0;
	RuntimeAssert(vkCreateSemaphore(Device, &SemaphoreCreateInfo, NULL, &Result) == VK_SUCCESS);
	return Result;
}

static inline void VulkanBeginCommands(VkCommandBuffer CommandBuffer, VkCommandBufferUsageFlags Usage) {
	VkCommandBufferBeginInfo BeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = Usage
	};
	RuntimeAssert(vkBeginCommandBuffer(CommandBuffer, &BeginInfo) == VK_SUCCESS);
}

static inline void VulkanEndCommands(VkCommandBuffer CommandBuffer) {
	RuntimeAssert(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS);
}

static inline VkCommandBuffer VulkanAllocateCommandBuffers(VkDevice Device, VkCommandPool CommandPool, range<VkCommandBuffer> CommandBuffers) {
	VkCommandBuffer Result;

	VkCommandBufferAllocateInfo CommandBufferAllocateInfo = {};
	CommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	CommandBufferAllocateInfo.commandPool = CommandPool;
	CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	CommandBufferAllocateInfo.commandBufferCount = CommandBuffers.Length;
	RuntimeAssert(vkAllocateCommandBuffers(Device, &CommandBufferAllocateInfo, CommandBuffers.Data) == VK_SUCCESS);

	return Result;
}

static inline VkCommandBuffer VulkanBeginSingleTimeCommands(VkDevice Device, VkCommandPool CommandPool) {

	VkCommandBuffer CommandBuffer = 0;

	VkCommandBufferAllocateInfo CommandBufferAllocateInfo = {};
	CommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	CommandBufferAllocateInfo.commandPool = CommandPool;
	CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	CommandBufferAllocateInfo.commandBufferCount = 1;
	RuntimeAssert(vkAllocateCommandBuffers(Device, &CommandBufferAllocateInfo, &CommandBuffer) == VK_SUCCESS);

	VulkanBeginCommands(CommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	return CommandBuffer;
}

static inline void VulkanEndSingleTimeCommands(VkDevice Device, VkCommandPool CommandPool, VkQueue Queue, VkCommandBuffer CommandBuffer) {
	VulkanEndCommands(CommandBuffer);

	VkFence CompletionFence = VulkanCreateFence(Device, false);
	OnScopeExit(vkDestroyFence(Device, CompletionFence, NULL));

	VkSubmitInfo SubmitInfo = {};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers = &CommandBuffer;

	RuntimeAssert(vkQueueSubmit(Queue, 1, &SubmitInfo, CompletionFence) == VK_SUCCESS);
	RuntimeAssert(vkWaitForFences(Device, 1, &CompletionFence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

	vkFreeCommandBuffers(Device, CommandPool, 1, &CommandBuffer);
}

struct cmd_image_transition {
	VkImageLayout ImageLayout;
	VkPipelineStageFlags PipelineStage;
	VkAccessFlags AccessFlags;
};

static inline void CmdTransitionImageLayout(VkCommandBuffer CommandBuffer, VkImage Image, const cmd_image_transition &Src, const cmd_image_transition &Dst) {
    VkImageMemoryBarrier Barrier = {};
    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout = Src.ImageLayout;
    Barrier.newLayout = Dst.ImageLayout;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = Image;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel = 0;
    Barrier.subresourceRange.levelCount = 1;
    Barrier.subresourceRange.baseArrayLayer = 0;
    Barrier.subresourceRange.layerCount = 1;
	Barrier.srcAccessMask = Src.AccessFlags;
	Barrier.dstAccessMask = Dst.AccessFlags;

    vkCmdPipelineBarrier(CommandBuffer, Src.PipelineStage, Dst.PipelineStage, 0, 0, NULL, 0, NULL, 1, &Barrier);
}

static void CmdBlit2DImage(VkCommandBuffer CommandBuffer, VkImage SrcImage, VkImage DstImage, const v2i &SrcResolution, const v2i &DstResolution, VkFilter Filter = VK_FILTER_NEAREST) {
    VkImageBlit BlitRegion = {};

    BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    BlitRegion.srcSubresource.mipLevel = 0;
    BlitRegion.srcSubresource.baseArrayLayer = 0;
    BlitRegion.srcSubresource.layerCount = 1;

    BlitRegion.dstSubresource = BlitRegion.srcSubresource;

    BlitRegion.srcOffsets[0] = { 0, 0, 0 };
    BlitRegion.srcOffsets[1] = { SrcResolution.X, SrcResolution.Y, 1 };

    BlitRegion.dstOffsets[0] = { 0, 0, 0 };
    BlitRegion.dstOffsets[1] = { DstResolution.X, DstResolution.Y, 1 };

    vkCmdBlitImage(CommandBuffer,
		SrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		DstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &BlitRegion, Filter
	);
}

struct vk_format_and_color {
	VkFormat Format;
	VkColorSpaceKHR ColorSpace;
};

static vk_format_and_color VulkanGetBestAvailableFormatAndColor(VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface) {

	u32 SurfaceFormatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, NULL);
	VkSurfaceFormatKHR *SurfaceFormats = PushStruct(&Temp, VkSurfaceFormatKHR, SurfaceFormatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats);

	VkFormat Format = SurfaceFormats[0].format;
	VkColorSpaceKHR ColorSpace = SurfaceFormats[0].colorSpace;
	for (u32 i = 1; i < SurfaceFormatCount; ++i) {
		VkSurfaceFormatKHR SurfaceFormat = SurfaceFormats[i];
		if (SurfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
			SurfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			Format = SurfaceFormat.format;
			ColorSpace = SurfaceFormat.colorSpace;
			break;
		}
	}

	return { Format, ColorSpace };
}

static VkSwapchainKHR VulkanCreateSwapchain(VkDevice Device, VkSurfaceKHR Surface, vk_format_and_color FormatAndColor, v2i WindowResolution, VkSwapchainKHR OldSwapchain = 0) {

	VkFormat Format = FormatAndColor.Format;
	VkColorSpaceKHR ColorSpace = FormatAndColor.ColorSpace;
	VkSwapchainCreateInfoKHR CreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = Surface,
		.minImageCount = FramesInFlight,
		.imageFormat = Format,
		.imageColorSpace = ColorSpace,
		.imageExtent = { (u32)WindowWidth, (u32)WindowHeight },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = OldSwapchain
	};

	VkSwapchainKHR Result = 0;
	RuntimeAssert(vkCreateSwapchainKHR(Device, &CreateInfo, NULL, &Result) == VK_SUCCESS);
	return Result;
}
