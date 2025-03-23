#pragma once
#include "vulkan_helpers.h"
#include "tagged_ptr.h"

enum class vulkan_memory_handle_type : u64 {
	Image,
	Buffer,
	Count
};

struct vulkan_memory_handle {
	union {
		VkImage Image;
		VkBuffer Buffer;
	};

	u64 Offset;
	tagged_ptr<vulkan_memory_handle, vulkan_memory_handle_type> Next;
};

using tagged_handle_ptr = tagged_ptr<vulkan_memory_handle, vulkan_memory_handle_type>;
static_assert(static_cast<u64>(vulkan_memory_handle_type::Count) <= 1 << (64 - tagged_handle_ptr::PtrBitShiftAmount));

struct vulkan_arena {
	VkDeviceMemory Memory;
	VkDeviceSize Capacity;
	tagged_handle_ptr MemoryHandles;

	void Reset(vulkan_arena *Arena, VkDevice Device) {

		tagged_handle_ptr CurrentHandle = MemoryHandles;

		while (CurrentHandle) {
			vulkan_memory_handle_type Type = CurrentHandle.GetTag();
			switch (Type) {
				case vulkan_memory_handle_type::Image: {
					VkImage Image = CurrentHandle->Image;
					vkDestroyImage(Device, Image, NULL);
				} break;
				case vulkan_memory_handle_type::Buffer: {
					VkBuffer Buffer = CurrentHandle->Buffer;
					vkDestroyBuffer(Device, Buffer, NULL);
				} break;
			}
			CurrentHandle = CurrentHandle->Next;
		};
	}
};

struct vulkan_arena_builder {
	VkDevice Device;
	VkDeviceSize Offset;
	tagged_handle_ptr MemoryHandles;
	memory_arena *CPUArena;
	u32 AccumulatedMemoryTypeBits;

	VkImage Push2DImage(v2i Size, VkFormat Format, VkImageUsageFlags UsageFlags, u64 *OutOffset = 0) {

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

		if (OutOffset) {
			*OutOffset = AlignedOffset;
		}

		{
			tagged_handle_ptr PreviousHandle = MemoryHandles;
			tagged_handle_ptr NewHandle = tagged_handle_ptr(PushStruct(CPUArena, vulkan_memory_handle), vulkan_memory_handle_type::Image);
			NewHandle->Next = PreviousHandle;
			NewHandle->Offset = AlignedOffset;
			NewHandle->Image = Result;
			MemoryHandles = NewHandle;
		}

		return Result;
	}

	VkBuffer PushBuffer(u32 Size, VkBufferUsageFlags UsageFlags, VkSharingMode SharingMode, u64 *OutOffset = 0) {

		VkBuffer Result;

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

		u32 AlignedOffset = RoundUpPowerOf2(this->Offset, BufferRequirements.alignment);
		Offset = AlignedOffset + BufferRequirements.size;
		AccumulatedMemoryTypeBits |= BufferRequirements.memoryTypeBits;

		if (OutOffset) {
			*OutOffset = AlignedOffset;
		}

		{
			tagged_handle_ptr PreviousHandle = MemoryHandles;
			tagged_handle_ptr NewHandle = tagged_handle_ptr(PushStruct(CPUArena, vulkan_memory_handle), vulkan_memory_handle_type::Buffer);
			NewHandle->Next = PreviousHandle;
			NewHandle->Offset = AlignedOffset;
			NewHandle->Buffer = Result;
			MemoryHandles = NewHandle;
		}

		return Result;
	}

	[[nodiscard]]
	vulkan_arena CommitAndAllocateArena(VkMemoryPropertyFlags MemoryProperties, const VkPhysicalDeviceMemoryProperties &PhysicalDeviceMemoryProperties) {

		vulkan_arena Result = {};
		Result.Capacity = Offset;
		Result.MemoryHandles = MemoryHandles;

		VkMemoryAllocateInfo AllocateInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = Offset,
			.memoryTypeIndex = FindMemoryType(AccumulatedMemoryTypeBits, MemoryProperties, PhysicalDeviceMemoryProperties)
		};
		vkAllocateMemory(Device, &AllocateInfo, NULL, &Result.Memory);

		tagged_handle_ptr CurrentHandle = MemoryHandles;

		while (CurrentHandle) {
			vulkan_memory_handle_type Type = CurrentHandle.GetTag();
			u64 Offset = CurrentHandle->Offset;

			switch (Type) {
				case vulkan_memory_handle_type::Image: {
					VkImage Image = CurrentHandle->Image;
					vkBindImageMemory(Device, Image, Result.Memory, Offset);
				} break;
				case vulkan_memory_handle_type::Buffer: {
					VkBuffer Buffer = CurrentHandle->Buffer;
					vkBindBufferMemory(Device, Buffer, Result.Memory, Offset);
				} break;
				case vulkan_memory_handle_type::Count: {
				} break;
			}
			CurrentHandle = CurrentHandle->Next;

		};

		return Result;
	}
};

static vulkan_arena_builder StartBuildingMemoryArena(VkDevice Device, memory_arena *CPUArena) {
	vulkan_arena_builder ArenaBuilder = {0};
	ArenaBuilder.Device = Device;
	ArenaBuilder.Offset = 0;
	ArenaBuilder.MemoryHandles = {};
	ArenaBuilder.CPUArena = CPUArena;
	ArenaBuilder.AccumulatedMemoryTypeBits = 0;
	return ArenaBuilder;
}

