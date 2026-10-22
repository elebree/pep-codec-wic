// pep: command-line encoder and decoder for .pep images. Other image formats are read
// and written through Windows Imaging Component.

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <fcntl.h>
#include <io.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

#define PEP_IMPLEMENTATION
#include "pep.h"
#include "pep_truncated.h"
#include "quantize.h"

using Microsoft::WRL::ComPtr;

namespace
{
	constexpr int kExitOk = 0;
	constexpr int kExitFailed = 1;
	constexpr int kExitUsage = 2;

	// pep_compress keeps the first 256 colors and silently maps any others to the first one.
	constexpr size_t kPepMaxColors = 256;

	constexpr wchar_t kUsage[] =
		L"Encodes and decodes PEP images.\n"
		L"\n"
		L"usage:\n"
		L"  pep encode <input-image> <output.pep> [--bits 1|2|4|8]\n"
		L"  pep decode <input.pep> <output.png|.bmp|.tif>\n"
		L"  pep info <file.pep>...\n"
		L"\n"
		L"encode reads any image Windows can decode, such as PNG, BMP, GIF or TIFF. An\n"
		L"image with more than 256 colors is reduced to 256, which is lossy. --bits stores\n"
		L"each palette channel in fewer bits, which is also lossy; the default is 8.\n"
		L"A PEP header holds at most 4096 pixels per side. Larger images, up to 65535 per\n"
		L"side and 64 Mi pixels in all, are saved with a warning: their header shows the\n"
		L"size without its high bits, and only pep and the PEP WIC codec read them right.\n"
		L"\n"
		L"decode and info read such files at their real size, whichever encoder wrote\n"
		L"them, and say when the header shows a different one.\n";

	const wchar_t *const kFormatNames[] = {L"rgba", L"bgra", L"abgr", L"argb"};

	void PrintMessage(const wchar_t *prefix, const wchar_t *format, va_list args)
	{
		// Keep messages in order with earlier output when both streams go to one place.
		fflush(stdout);
		fwprintf(stderr, L"pep: %ls", prefix);
		vfwprintf(stderr, format, args);
		fputwc(L'\n', stderr);
	}

	// Prints "pep: <message>" to stderr and returns false, so callers can return it.
	bool Error(const wchar_t *format, ...)
	{
		va_list args;
		va_start(args, format);
		PrintMessage(L"", format, args);
		va_end(args);
		return false;
	}

	void Warning(const wchar_t *format, ...)
	{
		va_list args;
		va_start(args, format);
		PrintMessage(L"warning: ", format, args);
		va_end(args);
	}

	void Note(const wchar_t *format, ...)
	{
		va_list args;
		va_start(args, format);
		PrintMessage(L"note: ", format, args);
		va_end(args);
	}

	int UsageError(const wchar_t *format, ...)
	{
		va_list args;
		va_start(args, format);
		PrintMessage(L"", format, args);
		va_end(args);
		fputws(L"Run 'pep --help' for usage.\n", stderr);
		return kExitUsage;
	}

	// Describes hr as "<system message> (0x...)".
	std::wstring Describe(const HRESULT hr)
	{
		wchar_t code[16];
		swprintf(code, 16, L"0x%08lx", static_cast<unsigned long>(hr));

		wchar_t *text = nullptr;
		const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
											nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
		std::wstring message(text != nullptr ? text : L"", length);
		LocalFree(text);
		while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L'.' || message.back() == L' '))
			message.pop_back();
		return message.empty() ? std::wstring(code) : message + L" (" + code + L")";
	}

	bool EndsWith(const wchar_t *path, const wchar_t *suffix)
	{
		const size_t pathLength = std::wcslen(path);
		const size_t suffixLength = std::wcslen(suffix);
		return pathLength >= suffixLength && _wcsicmp(path + pathLength - suffixLength, suffix) == 0;
	}

	bool ReadFileBytes(const wchar_t *path, std::vector<uint8_t> &bytes)
	{
		const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return Error(L"can't read %ls: %ls", path, Describe(HRESULT_FROM_WIN32(GetLastError())).c_str());

		LARGE_INTEGER size{};
		DWORD error = ERROR_SUCCESS;
		if (!GetFileSizeEx(file, &size))
			error = GetLastError();
		else if (size.QuadPart > std::numeric_limits<uint32_t>::max())
			error = ERROR_FILE_TOO_LARGE;
		else
		{
			bytes.resize(static_cast<size_t>(size.QuadPart));
			DWORD read = 0;
			if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
				error = GetLastError();
			else if (read != bytes.size())
				error = ERROR_HANDLE_EOF;
		}
		CloseHandle(file);

		if (error != ERROR_SUCCESS)
			return Error(L"can't read %ls: %ls", path, Describe(HRESULT_FROM_WIN32(error)).c_str());
		return true;
	}

	bool WriteFileBytes(const wchar_t *path, const uint8_t *bytes, const uint32_t size)
	{
		const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return Error(L"can't write %ls: %ls", path, Describe(HRESULT_FROM_WIN32(GetLastError())).c_str());

		DWORD written = 0;
		DWORD error = ERROR_SUCCESS;
		if (!WriteFile(file, bytes, size, &written, nullptr))
			error = GetLastError();
		else if (written != size)
			error = ERROR_WRITE_FAULT;
		if (!CloseHandle(file) && error == ERROR_SUCCESS)
			error = GetLastError();

		if (error == ERROR_SUCCESS)
			return true;
		DeleteFileW(path);
		return Error(L"can't write %ls: %ls", path, Describe(HRESULT_FROM_WIN32(error)).c_str());
	}

	// Reads the first frame of an image as 32bpp RGBA, the layout of pep_rgba.
	bool ReadImage(IWICImagingFactory *factory, const wchar_t *path, UINT *width, UINT *height, std::vector<uint32_t> &pixels)
	{
		ComPtr<IWICBitmapDecoder> decoder;
		ComPtr<IWICBitmapFrameDecode> frame;
		ComPtr<IWICFormatConverter> converter;
		UINT frameCount = 0;
		HRESULT hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
		if (SUCCEEDED(hr))
			hr = decoder->GetFrameCount(&frameCount);
		if (SUCCEEDED(hr))
			hr = decoder->GetFrame(0, &frame);
		if (SUCCEEDED(hr))
			hr = factory->CreateFormatConverter(&converter);
		if (SUCCEEDED(hr))
			hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
		if (SUCCEEDED(hr))
			hr = converter->GetSize(width, height);
		if (hr == WINCODEC_ERR_COMPONENTNOTFOUND)
			return Error(L"can't read %ls: Windows has no decoder for its format", path);
		if (FAILED(hr))
			return Error(L"can't read %ls: %ls", path, Describe(hr).c_str());

		// pep_compress takes 16-bit sizes and doesn't check them. A side over 4096 loses its
		// high bits in the header, and decoders find it again only up to a total size.
		if (*width > kPepMaxDimension || *height > kPepMaxDimension)
			return Error(L"%ls is %ux%u, but pep encodes at most %u pixels per side", path, *width, *height, kPepMaxDimension);
		if ((*width > kPepMaxHeaderDimension || *height > kPepMaxHeaderDimension) &&
			static_cast<uint64_t>(*width) * *height > kPepMaxRecoveredPixelCount)
			return Error(L"%ls is %ux%u; with a side over %u pixels, pep encodes at most %llu pixels in all",
						 path, *width, *height, kPepMaxHeaderDimension, static_cast<unsigned long long>(kPepMaxRecoveredPixelCount));
		if (frameCount > 1)
			Warning(L"%ls has %u frames; encoding the first", path, frameCount);

		pixels.resize(static_cast<size_t>(*width) * *height);
		hr = converter->CopyPixels(nullptr, *width * 4, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE *>(pixels.data()));
		if (FAILED(hr))
			return Error(L"can't read %ls: %ls", path, Describe(hr).c_str());
		return true;
	}

	// The container format for an output path's extension, or nullptr if it isn't supported.
	// Each of these encoders takes 32bpp BGRA as is.
	const GUID *ContainerFormatFor(const wchar_t *path)
	{
		if (EndsWith(path, L".png"))
			return &GUID_ContainerFormatPng;
		if (EndsWith(path, L".bmp"))
			return &GUID_ContainerFormatBmp;
		if (EndsWith(path, L".tif") || EndsWith(path, L".tiff"))
			return &GUID_ContainerFormatTiff;
		return nullptr;
	}

	bool WriteImage(IWICImagingFactory *factory, const wchar_t *path, REFGUID container, const UINT width, const UINT height, const uint32_t *bgra)
	{
		ComPtr<IWICStream> stream;
		ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame;
		ComPtr<IPropertyBag2> options;
		bool created = false;
		HRESULT hr = factory->CreateStream(&stream);
		if (SUCCEEDED(hr))
		{
			hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
			created = SUCCEEDED(hr);
		}
		if (SUCCEEDED(hr))
			hr = factory->CreateEncoder(container, nullptr, &encoder);
		if (SUCCEEDED(hr))
			hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
		if (SUCCEEDED(hr))
			hr = encoder->CreateNewFrame(&frame, &options);
		if (SUCCEEDED(hr) && IsEqualGUID(container, GUID_ContainerFormatBmp))
		{
			// Without this the BMP encoder takes only 32bpp BGR and drops alpha.
			PROPBAG2 option{};
			option.pstrName = const_cast<LPOLESTR>(L"EnableV5Header32bppBGRA");
			VARIANT value{};
			value.vt = VT_BOOL;
			value.boolVal = VARIANT_TRUE;
			hr = options->Write(1, &option, &value);
		}
		if (SUCCEEDED(hr))
			hr = frame->Initialize(options.Get());
		if (SUCCEEDED(hr))
			hr = frame->SetSize(width, height);
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(hr))
			hr = frame->SetPixelFormat(&format);
		if (SUCCEEDED(hr) && !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
			hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
		if (SUCCEEDED(hr))
			hr = frame->WritePixels(height, width * 4, static_cast<UINT>(static_cast<uint64_t>(width) * height * 4),
									reinterpret_cast<BYTE *>(const_cast<uint32_t *>(bgra)));
		if (SUCCEEDED(hr))
			hr = frame->Commit();
		if (SUCCEEDED(hr))
			hr = encoder->Commit();
		if (SUCCEEDED(hr))
			return true;

		// Close the file before deleting what was written of it.
		options.Reset();
		frame.Reset();
		encoder.Reset();
		stream.Reset();
		if (created)
			DeleteFileW(path);
		return Error(L"can't write %ls: %ls", path, Describe(hr).c_str());
	}

	// Counts distinct colors, stopping once there are more than limit.
	size_t CountColors(const std::vector<uint32_t> &pixels, const size_t limit)
	{
		std::unordered_set<uint32_t> colors;
		for (size_t index = 0; index < pixels.size() && colors.size() <= limit; ++index)
		{
			if (index == 0 || pixels[index] != pixels[index - 1])
				colors.insert(pixels[index]);
		}
		return colors.size();
	}

	// Deserializes a .pep file, or prints an error if bytes aren't one.
	bool ParsePep(const wchar_t *path, const std::vector<uint8_t> &bytes, pep *image)
	{
		*image = pep{};
		// Same header check as the codec's CouldBePepHeader.
		if (bytes.size() >= 2 && (bytes[0] & 0x80) == 0 && bytes[1] < PEP_NUM_MODELS)
			*image = pep_deserialize(bytes.data(), static_cast<uint32_t>(bytes.size()));
		if (image->bytes != nullptr && image->bytes_size != 0 && image->width != 0 && image->height != 0)
			return true;
		pep_free(image);
		return Error(L"%ls is not a PEP file", path);
	}

	// Decodes image at its header size or, failing that, at the size its header was
	// truncated from.
	uint32_t *Decompress(const pep &image, const uint8_t flags, const pep_format format, uint32_t *width, uint32_t *height)
	{
		const auto decompressAt = [format](pep candidate, const uint32_t candidateWidth, const uint32_t candidateHeight)
		{
			candidate.width = static_cast<uint16_t>(candidateWidth);
			candidate.height = static_cast<uint16_t>(candidateHeight);
			return pep_decompress(&candidate, format, 0, 0);
		};

		*width = image.width;
		*height = image.height;
		uint32_t *pixels = decompressAt(image, *width, *height);
		if (pixels == nullptr && PepHeaderCanBeTruncated(flags))
			pixels = PepDecompressTruncated(image, decompressAt, width, height);
		return pixels;
	}

	// Whether a serialized file decodes back to pixels, at width x height, the way decode reads
	// it. With fewer than 8 bits per palette channel the colors are quantized, so then only
	// check the size.
	bool DecodesTo(const uint8_t *bytes, const uint32_t size, const std::vector<uint32_t> &pixels, const UINT width, const UINT height, const pep_channel_bits bits)
	{
		pep image = pep_deserialize(bytes, size);
		uint32_t decodedWidth = 0;
		uint32_t decodedHeight = 0;
		uint32_t *decoded = image.bytes != nullptr ? Decompress(image, bytes[0], pep_rgba, &decodedWidth, &decodedHeight) : nullptr;
		pep_free(&image);
		const bool matches = decoded != nullptr && decodedWidth == width && decodedHeight == height &&
							 (bits != pep_8bit || std::memcmp(decoded, pixels.data(), pixels.size() * sizeof(uint32_t)) == 0);
		PEP_FREE(decoded);
		return matches;
	}

	// The size a 3-byte header dimension field stores for a side of this many pixels.
	uint32_t HeaderDimension(const uint32_t side)
	{
		return ((side - 1) & (kPepMaxHeaderDimension - 1)) + 1;
	}

	bool Encode(IWICImagingFactory *factory, const wchar_t *input, const wchar_t *output, const pep_channel_bits bits)
	{
		UINT width = 0;
		UINT height = 0;
		std::vector<uint32_t> pixels;
		if (!ReadImage(factory, input, &width, &height, pixels))
			return false;
		if (width > kPepMaxHeaderDimension || height > kPepMaxHeaderDimension)
			Warning(L"%ls is %ux%u, but a PEP header holds at most %u pixels per side, so the file's header will say %ux%u. "
					L"pep and the PEP WIC codec still read it at its real size; other PEP readers don't, and unpatched ones can hang on it",
					input, width, height, kPepMaxHeaderDimension, HeaderDimension(width), HeaderDimension(height));

		size_t colors = CountColors(pixels, kPepMaxColors);
		if (colors > kPepMaxColors)
		{
			const QuantizeResult reduced = QuantizeColors(pixels, kPepMaxColors);
			if (reduced.transparentOnly)
				Note(L"%ls has %zu colors; storing its fully transparent pixels as one color leaves %zu", input, reduced.inputColors, reduced.outputColors);
			else
				Warning(L"%ls has %zu colors, but PEP stores at most %zu; reduced them to %zu", input, reduced.inputColors, kPepMaxColors, reduced.outputColors);
			colors = reduced.outputColors;
		}

		pep image = pep_compress(pixels.data(), static_cast<uint16_t>(width), static_cast<uint16_t>(height), pep_rgba, bits);
		uint32_t size = 0;
		uint8_t *bytes = image.bytes != nullptr ? pep_serialize(&image, &size) : nullptr;
		const uint8_t model = image.model;
		pep_free(&image);
		if (bytes == nullptr)
			return Error(L"can't encode %ls", input);

		// Never write a file that doesn't read back.
		const bool decodes = DecodesTo(bytes, size, pixels, width, height, bits);
		const bool written = decodes && WriteFileBytes(output, bytes, size);
		PEP_FREE(bytes);
		if (!decodes)
			return Error(L"can't encode %ls: the encoded file doesn't decode back to it", input);
		if (!written)
			return false;

		wprintf(L"%ls: %ux%u, %zu colors, model %u, %u bytes\n", output, width, height, colors, model, size);
		return true;
	}

	bool Decode(IWICImagingFactory *factory, const wchar_t *input, const wchar_t *output)
	{
		const GUID *container = ContainerFormatFor(output);
		if (container == nullptr)
			return Error(L"can't write %ls: the extension must be .png, .bmp, .tif or .tiff", output);

		std::vector<uint8_t> bytes;
		pep image{};
		if (!ReadFileBytes(input, bytes) || !ParsePep(input, bytes, &image))
			return false;

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t *pixels = Decompress(image, bytes[0], pep_bgra, &width, &height);
		const uint32_t headerWidth = image.width;
		const uint32_t headerHeight = image.height;
		pep_free(&image);
		if (pixels == nullptr)
			return Error(L"can't decode %ls: its data is corrupt or doesn't match its size", input);
		if (width != headerWidth || height != headerHeight)
			Warning(L"%ls: the header says %ux%u, but the data is %ux%u; its encoder dropped the high bits of the size",
					input, headerWidth, headerHeight, width, height);

		const bool written = WriteImage(factory, output, *container, width, height, pixels);
		PEP_FREE(pixels);
		if (written)
			wprintf(L"%ls: %ux%u\n", output, width, height);
		return written;
	}

	// Prints one line per file. Returns false if any of them can't be read or decoded.
	bool Info(const std::vector<const wchar_t *> &paths)
	{
		bool ok = true;
		for (const wchar_t *path : paths)
		{
			std::vector<uint8_t> bytes;
			pep image{};
			if (!ReadFileBytes(path, bytes) || !ParsePep(path, bytes, &image))
			{
				ok = false;
				continue;
			}

			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t *pixels = Decompress(image, bytes[0], pep_bgra, &width, &height);
			const bool decoded = pixels != nullptr;
			PEP_FREE(pixels);

			wchar_t size[64];
			if (!decoded)
				swprintf(size, 64, L"can't decode (header says %ux%u)", image.width, image.height);
			else if (width != image.width || height != image.height)
				swprintf(size, 64, L"%ux%u (header says %ux%u)", width, height, image.width, image.height);
			else
				swprintf(size, 64, L"%ux%u", width, height);
			ok = ok && decoded;

			const unsigned colors = image.palette_size != 0 ? image.palette_size : 256;
			wprintf(L"%ls: %ls, %u colors, %u-bit %ls palette, model %u, %zu bytes\n", path, size, colors,
					1u << image.channel_bits, kFormatNames[image.format & 3], image.model, bytes.size());
			pep_free(&image);
		}
		return ok;
	}
} // namespace

int wmain(int argc, wchar_t **argv)
{
	_setmode(_fileno(stdout), _O_U8TEXT);
	_setmode(_fileno(stderr), _O_U8TEXT);

	if (argc < 2)
	{
		fputws(kUsage, stderr);
		return kExitUsage;
	}
	const std::wstring command = argv[1];
	if (command == L"-h" || command == L"--help" || command == L"help")
	{
		fputws(kUsage, stdout);
		return kExitOk;
	}
	if (command != L"encode" && command != L"decode" && command != L"info")
		return UsageError(L"unknown command '%ls'", command.c_str());

	std::vector<const wchar_t *> paths;
	pep_channel_bits bits = pep_8bit;
	for (int index = 2; index < argc; ++index)
	{
		const std::wstring argument = argv[index];
		if (command == L"encode" && argument == L"--bits")
		{
			if (++index == argc)
				return UsageError(L"--bits needs a value");
			const std::wstring value = argv[index];
			if (value == L"1")
				bits = pep_1bit;
			else if (value == L"2")
				bits = pep_2bit;
			else if (value == L"4")
				bits = pep_4bit;
			else if (value == L"8")
				bits = pep_8bit;
			else
				return UsageError(L"--bits must be 1, 2, 4 or 8, not '%ls'", value.c_str());
		}
		else if (argument.size() > 1 && argument[0] == L'-')
			return UsageError(L"unknown option '%ls'", argument.c_str());
		else
			paths.push_back(argv[index]);
	}
	if (command == L"info" ? paths.empty() : paths.size() != 2)
		return UsageError(command == L"info" ? L"info needs at least one file" : L"%ls needs an input and an output file", command.c_str());

	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	bool ok = false;
	try
	{
		ComPtr<IWICImagingFactory> factory;
		const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if (FAILED(hr))
			Error(L"can't start Windows Imaging Component: %ls", Describe(hr).c_str());
		else if (command == L"encode")
			ok = Encode(factory.Get(), paths[0], paths[1], bits);
		else if (command == L"decode")
			ok = Decode(factory.Get(), paths[0], paths[1]);
		else
			ok = Info(paths);
	}
	catch (const std::bad_alloc &)
	{
		Error(L"out of memory");
	}
	if (SUCCEEDED(initialized))
		CoUninitialize();
	return ok ? kExitOk : kExitFailed;
}
