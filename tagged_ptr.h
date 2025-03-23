#pragma once

template <typename pointer_type, typename enum_type>
struct tagged_ptr {
	pointer_type *Ptr;

	constexpr tagged_ptr() : Ptr(0) { }
	constexpr tagged_ptr(pointer_type *InPtr, enum_type Type) : Ptr(InPtr) {
		AddTag(Type);
	}

	static constexpr u64 PtrBitShiftAmount = 52;
	static constexpr u64 SignBit  = 1ULL << (PtrBitShiftAmount - 1);
	static constexpr u64 TagMask  = ~((1ULL << PtrBitShiftAmount) - 1);
	static constexpr u64 DataMask = ~TagMask;

	static_assert(std::is_enum_v<enum_type>);

	inline constexpr void AddTag(enum_type Type) {
		u64 Tag = static_cast<u64>(Type) << PtrBitShiftAmount;
		u64 PtrCleared = std::bit_cast<u64>(Ptr) & DataMask;
		u64 Result = Tag | PtrCleared;
		Ptr = std::bit_cast<pointer_type *>(Result);
	}

	inline constexpr enum_type GetTag() const {
		u64 N = std::bit_cast<u64>(Ptr);
		N >>= PtrBitShiftAmount;
		enum_type Result = static_cast<enum_type>(N);
		return Result;
	}

	inline constexpr pointer_type *GetPointerWithoutTag(pointer_type *Ptr) const {
		u64 N = std::bit_cast<u64>(Ptr);
		N &= DataMask;

#if defined(__x86_64__) || defined(_M_X64)
		if (N & SignBit) {
			N |= TagMask;
		}
#endif

		return std::bit_cast<pointer_type *>(N);
	}

	inline constexpr bool operator==(const tagged_ptr<pointer_type, enum_type> &B) const {
		return Ptr == B.Ptr;
	}
	inline constexpr bool operator!=(const tagged_ptr<pointer_type, enum_type> &B) const {
		return Ptr != B.Ptr;
	}

	inline constexpr pointer_type operator*() const {
		pointer_type *Pointer = GetPointerWithoutTag(this->Ptr);
		return *Pointer;
	}
	
	inline constexpr pointer_type *operator->() const {
		return GetPointerWithoutTag(this->Ptr);
	}

	explicit operator bool() const {
		return GetPointerWithoutTag(this->Ptr) != 0;
	}
};
