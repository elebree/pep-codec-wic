// Recovery of .pep files whose header lost the high bits of their size.
// Encoder accept sizes over 4096, and drops the high bits (6784x464 is stored as
// 2688x464) while the payload stays intact.
// pep_decompress only succeeds at the size the payload was encoded with, so every size
// the header could have been truncated from can be tried until one decodes.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

// pep_serialize's flag for the 2-byte dimension form, used when both sides are at most 256.
constexpr uint8_t kPepSmallDimensionsFlag = 0x10;
// The 3-byte dimension form keeps width - 1 and height - 1 in 12 bits each.
constexpr uint32_t kPepMaxHeaderDimension = 4096;
// pep_compress takes uint16_t sizes.
constexpr uint32_t kPepMaxDimension = std::numeric_limits<uint16_t>::max();
// Largest size PepDecompressTruncated tries. Each attempt allocates 5 bytes per pixel,
// and callers usually keep another 4 for the result.
constexpr uint64_t kPepMaxRecoveredPixelCount = 64ull * 1024 * 1024;

// Whether a header with these packed flags (its first byte) uses the 3-byte dimension
// form, the only one that can have been truncated.
inline bool PepHeaderCanBeTruncated(const uint8_t flags)
{
	return (flags & kPepSmallDimensionsFlag) == 0;
}

// Calls decompressAt(image, width, height), which decodes image as if its header said
// width x height, for every size the header could have been truncated from except the
// header size itself, smallest area first. Returns the first non-null result and stores
// its size in width and height.
template <typename DecompressAt>
uint32_t *PepDecompressTruncated(const pep &image, DecompressAt decompressAt, uint32_t *width, uint32_t *height)
{
	struct Size
	{
		uint32_t width;
		uint32_t height;
		uint64_t area;
	};
	constexpr uint32_t kSizesPerSide = kPepMaxDimension / kPepMaxHeaderDimension + 1;
	std::array<Size, kSizesPerSide * kSizesPerSide> sizes{};
	size_t count = 0;
	for (uint32_t w = image.width; w <= kPepMaxDimension; w += kPepMaxHeaderDimension)
	{
		for (uint32_t h = image.height; h <= kPepMaxDimension; h += kPepMaxHeaderDimension)
		{
			const uint64_t area = static_cast<uint64_t>(w) * h;
			if (area > kPepMaxRecoveredPixelCount)
				break;
			if (w != image.width || h != image.height)
				sizes[count++] = {w, h, area};
		}
	}
	std::sort(sizes.begin(), sizes.begin() + count, [](const Size &a, const Size &b)
			  { return a.area < b.area; });

	for (size_t index = 0; index < count; ++index)
	{
		uint32_t *pixels = decompressAt(image, sizes[index].width, sizes[index].height);
		if (pixels != nullptr)
		{
			*width = sizes[index].width;
			*height = sizes[index].height;
			return pixels;
		}
	}
	return nullptr;
}