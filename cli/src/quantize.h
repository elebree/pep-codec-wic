#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct QuantizeResult
{
	size_t inputColors = 0;
	size_t outputColors = 0;
	// Only fully transparent pixels changed: they all became 0, which looks the same.
	bool transparentOnly = false;
};

// Reduces pixels, 32bpp RGBA in pep_rgba's layout, to at most maxColors distinct colors.
// Leaves pixels alone if they already have no more than that.
QuantizeResult QuantizeColors(std::vector<uint32_t> &pixels, size_t maxColors);
