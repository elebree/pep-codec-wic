// Color reduction for images with more colors than a PEP palette holds: median cut,
// refined by a few rounds of k-means. Distances use alpha-premultiplied values, so colors
// that look alike are close, and all fully transparent colors are the same.

#include "quantize.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
	// Weights of the R, G, B and A differences in the color distance.
	constexpr int kWeights[4] = {3, 4, 2, 3};
	// k-means stops after kMaxRefineRounds, or earlier so that it assigns about
	// kRefineBudget colors in total.
	constexpr int kMaxRefineRounds = 8;
	constexpr size_t kRefineBudget = 8u * 1024 * 1024;

	using Premultiplied = std::array<uint8_t, 4>;

	uint32_t ChannelOf(const uint32_t rgba, const int channel)
	{
		return (rgba >> (8 * channel)) & 0xff;
	}

	Premultiplied Premultiply(const uint32_t rgba)
	{
		const uint32_t alpha = ChannelOf(rgba, 3);
		Premultiplied p{};
		for (int channel = 0; channel < 3; ++channel)
			p[channel] = static_cast<uint8_t>((ChannelOf(rgba, channel) * alpha + 127) / 255);
		p[3] = static_cast<uint8_t>(alpha);
		return p;
	}

	int Distance(const Premultiplied &a, const Premultiplied &b)
	{
		int distance = 0;
		for (int channel = 0; channel < 4; ++channel)
		{
			const int difference = a[channel] - b[channel];
			distance += kWeights[channel] * difference * difference;
		}
		return distance;
	}

	// A distinct input color and how many pixels have it.
	struct Color
	{
		uint32_t rgba;
		uint32_t count;
		Premultiplied p;
	};

	// Averages colors into one palette entry: alpha is the mean alpha, and RGB the mean
	// weighted by alpha, so transparent pixels don't pull it and a single color stays exact.
	class Mean
	{
	public:
		void Add(const Color &color)
		{
			const uint64_t alpha = ChannelOf(color.rgba, 3);
			count_ += color.count;
			alpha_ += color.count * alpha;
			for (int channel = 0; channel < 3; ++channel)
				rgb_[channel] += color.count * alpha * ChannelOf(color.rgba, channel);
		}

		bool Empty() const
		{
			return count_ == 0;
		}

		uint32_t Rgba() const
		{
			if (alpha_ == 0)
				return 0;
			uint32_t rgba = static_cast<uint32_t>((alpha_ + count_ / 2) / count_) << 24;
			for (int channel = 0; channel < 3; ++channel)
				rgba |= static_cast<uint32_t>((rgb_[channel] + alpha_ / 2) / alpha_) << (8 * channel);
			return rgba;
		}

	private:
		uint64_t count_ = 0;
		uint64_t alpha_ = 0;
		uint64_t rgb_[3] = {};
	};

	// A range of colors in median cut, with its weighted squared error around its mean and
	// the channel along which it varies most.
	struct Box
	{
		size_t begin;
		size_t end;
		double error;
		int channel;
	};

	void Measure(const std::vector<Color> &colors, Box &box)
	{
		double count = 0;
		double sum[4] = {};
		double squares[4] = {};
		for (size_t index = box.begin; index < box.end; ++index)
		{
			const Color &color = colors[index];
			count += color.count;
			for (int channel = 0; channel < 4; ++channel)
			{
				const double value = color.p[channel];
				sum[channel] += color.count * value;
				squares[channel] += color.count * value * value;
			}
		}

		box.error = 0;
		box.channel = 0;
		double widest = -1;
		for (int channel = 0; channel < 4; ++channel)
		{
			const double spread = kWeights[channel] * (squares[channel] - sum[channel] * sum[channel] / count);
			box.error += spread;
			if (spread > widest)
			{
				widest = spread;
				box.channel = channel;
			}
		}
	}

	// Splits box at the pixel-weighted median of its widest channel, leaving the lower part
	// in box and the upper part in upper.
	void Split(std::vector<Color> &colors, Box &box, Box &upper)
	{
		const int channel = box.channel;
		uint64_t histogram[256] = {};
		uint64_t total = 0;
		for (size_t index = box.begin; index < box.end; ++index)
		{
			histogram[colors[index].p[channel]] += colors[index].count;
			total += colors[index].count;
		}
		int median = 0;
		for (uint64_t below = histogram[0]; below * 2 < total; below += histogram[++median])
		{
		}

		// The lower part takes the values up to the median, but never all of the box.
		const auto first = colors.begin() + static_cast<ptrdiff_t>(box.begin);
		const auto last = colors.begin() + static_cast<ptrdiff_t>(box.end);
		auto middle = std::partition(first, last, [&](const Color &color) { return color.p[channel] <= median; });
		if (middle == last)
			middle = std::partition(first, last, [&](const Color &color) { return color.p[channel] < median; });
		if (middle == first) // every color has the same value there
			middle = first + (last - first) / 2;

		upper = {static_cast<size_t>(middle - colors.begin()), box.end, 0, 0};
		box.end = upper.begin;
		Measure(colors, box);
		Measure(colors, upper);
	}

	// Splits colors into at most maxColors boxes, always splitting the one with the largest error.
	std::vector<Box> MedianCut(std::vector<Color> &colors, const size_t maxColors)
	{
		std::vector<Box> boxes{{0, colors.size(), 0, 0}};
		Measure(colors, boxes[0]);
		while (boxes.size() < maxColors)
		{
			size_t worst = boxes.size();
			for (size_t index = 0; index < boxes.size(); ++index)
			{
				const bool splittable = boxes[index].end - boxes[index].begin > 1;
				if (splittable && (worst == boxes.size() || boxes[index].error > boxes[worst].error))
					worst = index;
			}
			if (worst == boxes.size())
				break;

			Box upper{};
			Split(colors, boxes[worst], upper);
			boxes.push_back(upper);
		}
		return boxes;
	}

	// Palette entries sorted by premultiplied green, so that a nearest-color search can stop
	// once the green difference alone is farther than the best match so far.
	class SearchPalette
	{
	public:
		explicit SearchPalette(const std::vector<uint32_t> &palette)
		{
			for (size_t index = 0; index < palette.size(); ++index)
				entries_.push_back({Premultiply(palette[index]), static_cast<uint32_t>(index)});
			std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) { return a.p[1] < b.p[1]; });
		}

		uint32_t Nearest(const Premultiplied &p) const
		{
			int best = INT_MAX;
			uint32_t bestIndex = 0;
			const auto consider = [&](const Entry &entry)
			{
				const int green = entry.p[1] - p[1];
				if (kWeights[1] * green * green >= best)
					return false;
				const int distance = Distance(entry.p, p);
				if (distance < best)
				{
					best = distance;
					bestIndex = entry.index;
				}
				return true;
			};

			const auto start = std::lower_bound(entries_.begin(), entries_.end(), p[1], [](const Entry &entry, const uint8_t green) { return entry.p[1] < green; });
			for (auto it = start; it != entries_.end() && consider(*it); ++it)
			{
			}
			for (auto it = start; it != entries_.begin() && consider(*--it);)
			{
			}
			return bestIndex;
		}

	private:
		struct Entry
		{
			Premultiplied p;
			uint32_t index;
		};
		std::vector<Entry> entries_;
	};

	// Points each color at its nearest palette entry. Returns whether any of them changed.
	bool Assign(const std::vector<Color> &colors, const std::vector<uint32_t> &palette, std::vector<uint32_t> &assignment)
	{
		const SearchPalette search(palette);
		bool changed = false;
		for (size_t index = 0; index < colors.size(); ++index)
		{
			const uint32_t nearest = search.Nearest(colors[index].p);
			changed = changed || nearest != assignment[index];
			assignment[index] = nearest;
		}
		return changed;
	}
} // namespace

QuantizeResult QuantizeColors(std::vector<uint32_t> &pixels, const size_t maxColors)
{
	// Distinct colors and their pixel counts. Fully transparent pixels look the same whatever
	// their RGB, so they all count as 0; alpha is the top byte, so they sort to the front.
	QuantizeResult result;
	std::vector<Color> colors;
	{
		std::vector<uint32_t> sorted(pixels);
		std::sort(sorted.begin(), sorted.end());
		for (size_t index = 0; index < sorted.size();)
		{
			size_t next = index + 1;
			while (next < sorted.size() && sorted[next] == sorted[index])
				++next;
			++result.inputColors;
			const uint32_t rgba = (sorted[index] >> 24) == 0 ? 0 : sorted[index];
			if (!colors.empty() && colors.back().rgba == rgba)
				colors.back().count += static_cast<uint32_t>(next - index);
			else
				colors.push_back({rgba, static_cast<uint32_t>(next - index), Premultiply(rgba)});
			index = next;
		}
	}
	if (result.inputColors <= maxColors)
	{
		result.outputColors = result.inputColors;
		return result;
	}

	std::vector<uint32_t> palette;
	std::vector<uint32_t> assignment(colors.size(), UINT32_MAX);
	if (colors.size() <= maxColors)
	{
		result.transparentOnly = true;
		for (size_t index = 0; index < colors.size(); ++index)
		{
			palette.push_back(colors[index].rgba);
			assignment[index] = static_cast<uint32_t>(index);
		}
	}
	else
	{
		for (const Box &box : MedianCut(colors, maxColors))
		{
			Mean mean;
			for (size_t index = box.begin; index < box.end; ++index)
				mean.Add(colors[index]);
			palette.push_back(mean.Rgba());
		}

		const int rounds = static_cast<int>(std::clamp<size_t>(kRefineBudget / colors.size(), 1, kMaxRefineRounds));
		for (int round = 0;; ++round)
		{
			if (!Assign(colors, palette, assignment) || round == rounds)
				break;
			std::vector<Mean> means(palette.size());
			for (size_t index = 0; index < colors.size(); ++index)
				means[assignment[index]].Add(colors[index]);
			for (size_t entry = 0; entry < palette.size(); ++entry)
			{
				if (!means[entry].Empty())
					palette[entry] = means[entry].Rgba();
			}
		}
	}

	// Median cut reordered colors, so look the replacements up in a sorted table.
	std::vector<std::pair<uint32_t, uint32_t>> replacements(colors.size());
	std::vector<bool> used(palette.size());
	for (size_t index = 0; index < colors.size(); ++index)
	{
		replacements[index] = {colors[index].rgba, palette[assignment[index]]};
		used[assignment[index]] = true;
	}
	std::sort(replacements.begin(), replacements.end());

	// Two entries can end up with the same color.
	std::vector<uint32_t> outputColors;
	for (size_t entry = 0; entry < palette.size(); ++entry)
	{
		if (used[entry])
			outputColors.push_back(palette[entry]);
	}
	std::sort(outputColors.begin(), outputColors.end());
	result.outputColors = static_cast<size_t>(std::unique(outputColors.begin(), outputColors.end()) - outputColors.begin());

	uint32_t lastIn = 0;
	uint32_t lastOut = 0;
	bool haveLast = false;
	for (uint32_t &pixel : pixels)
	{
		const uint32_t rgba = (pixel >> 24) == 0 ? 0 : pixel;
		if (!haveLast || rgba != lastIn)
		{
			const auto found = std::lower_bound(replacements.begin(), replacements.end(), std::make_pair(rgba, 0u));
			lastIn = rgba;
			lastOut = found->second;
			haveLast = true;
		}
		pixel = lastOut;
	}
	return result;
}
