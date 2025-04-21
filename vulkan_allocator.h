#pragma once
#include "vulkan_helpers.h"
#include "tagged_ptr.h"

enum class vulkan_memory_handle_type : u64 {
	Image,
	Buffer,
	Count
};

struct vulkan_memory_handle {
	vulkan_memory_handle_type Type;

	union {
		VkImage Image;
		VkBuffer Buffer;
	};
	u64 Offset;
};

template <u32 Count>
struct vulkan_arena {
	VkDeviceMemory Memory;
	VkDeviceSize Capacity;
	vulkan_memory_handle MemoryHandles[Count];

	void Destroy(VkDevice Device) {

		for (u32 i = 0; i < Count; ++i) {
			const vulkan_memory_handle &CurrentHandle = MemoryHandles[i];
			switch (CurrentHandle.Type) {
				case vulkan_memory_handle_type::Image: {
					VkImage Image = CurrentHandle.Image;
					vkDestroyImage(Device, Image, NULL);
				} break;
				case vulkan_memory_handle_type::Buffer: {
					VkBuffer Buffer = CurrentHandle.Buffer;
					vkDestroyBuffer(Device, Buffer, NULL);
				} break;
				case vulkan_memory_handle_type::Count: break;
			}
		}

		vkFreeMemory(Device, Memory, NULL);
	}
};

template <u32 Count>
struct vulkan_arena_builder {
	VkDevice Device;
	VkDeviceSize Offset;
	u32 AccumulatedMemoryTypeBits;
	u32 CurrentHandleIndex;
	vulkan_memory_handle MemoryHandles[Count];

	VkImage Push2DImage(v2i Size, VkFormat Format, VkImageUsageFlags UsageFlags) {

		VkImage Result = 0;

		VkImageCreateInfo ImageCreateInfo = {};
		ImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		ImageCreateInfo.extent.width = (u32)Size.X;
		ImageCreateInfo.extent.height = (u32)Size.Y;
		ImageCreateInfo.extent.depth = 1;
		ImageCreateInfo.mipLevels = 1;
		ImageCreateInfo.arrayLayers = 1;
		ImageCreateInfo.format = Format;
		ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		ImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ImageCreateInfo.usage = UsageFlags;
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		ImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		RuntimeAssert(vkCreateImage(Device, &ImageCreateInfo, NULL, &Result) == VK_SUCCESS);

		VkMemoryRequirements ImageRequirements;
		vkGetImageMemoryRequirements(Device, Result, &ImageRequirements);

		u32 AlignedOffset = RoundUpPowerOf2(this->Offset, ImageRequirements.alignment);
		Offset = AlignedOffset + ImageRequirements.size;
		AccumulatedMemoryTypeBits |= ImageRequirements.memoryTypeBits;

		vulkan_memory_handle &Handle = MemoryHandles[CurrentHandleIndex++];
		Handle.Type = vulkan_memory_handle_type::Image;
		Handle.Offset = AlignedOffset;
		Handle.Image = Result;

		return Result;
	}

	VkBuffer PushBuffer(u64 Size, VkBufferUsageFlags UsageFlags, VkSharingMode SharingMode) {

		VkBuffer Result = 0;

		VkBufferCreateInfo BufferCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = Size,
			.usage = UsageFlags,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};

		RuntimeAssert(vkCreateBuffer(Device, &BufferCreateInfo, NULL, &Result) == VK_SUCCESS);

		VkMemoryRequirements BufferRequirements;
		vkGetBufferMemoryRequirements(Device, Result, &BufferRequirements);

		AccumulatedMemoryTypeBits |= BufferRequirements.memoryTypeBits;

		u64 AlignedOffset = RoundUpPowerOf2(this->Offset, BufferRequirements.alignment);
		Offset = AlignedOffset + BufferRequirements.size;
		AccumulatedMemoryTypeBits |= BufferRequirements.memoryTypeBits;

		vulkan_memory_handle &Handle = MemoryHandles[CurrentHandleIndex++];
		Handle.Type = vulkan_memory_handle_type::Buffer;
		Handle.Offset = AlignedOffset;
		Handle.Buffer = Result;

		return Result;
	}

	[[nodiscard]]
	vulkan_arena<Count> CommitAndAllocateArena(VkMemoryPropertyFlags MemoryProperties, const VkPhysicalDeviceMemoryProperties &PhysicalDeviceMemoryProperties) {

		RuntimeAssert(CurrentHandleIndex == Count);

		vulkan_arena<Count> Result = {};
		Result.Capacity = Offset;

		VkMemoryAllocateInfo AllocateInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = this->Offset,
			.memoryTypeIndex = FindMemoryType(AccumulatedMemoryTypeBits, MemoryProperties, PhysicalDeviceMemoryProperties)
		};
		RuntimeAssert(vkAllocateMemory(Device, &AllocateInfo, NULL, &Result.Memory) == VK_SUCCESS);

		for (u32 i = 0; i < Count; ++i) {
			const vulkan_memory_handle &CurrentHandle = MemoryHandles[i];
			switch (CurrentHandle.Type) {
				case vulkan_memory_handle_type::Image: {
					VkImage Image = CurrentHandle.Image;
					vkBindImageMemory(Device, Image, Result.Memory, CurrentHandle.Offset);
				} break;
				case vulkan_memory_handle_type::Buffer: {
					VkBuffer Buffer = CurrentHandle.Buffer;
					vkBindBufferMemory(Device, Buffer, Result.Memory, CurrentHandle.Offset);
				} break;
				case vulkan_memory_handle_type::Count: break;
			}
			Result.MemoryHandles[i] = CurrentHandle;
		}

		return Result;
	}
};

template <u32 Count>
static vulkan_arena_builder<Count> StartBuildingMemoryArena(VkDevice Device) {
	vulkan_arena_builder<Count> ArenaBuilder = {};
	ArenaBuilder.Device = Device;

	return ArenaBuilder;
}

