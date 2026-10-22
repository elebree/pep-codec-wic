#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

#define PEP_IMPLEMENTATION
#include "pep.h"
#include "pep_truncated.h"

namespace
{
	// {F5C347B6-8502-4FA5-8B35-875BBD966C63}
	const CLSID CLSID_PepWicCodec =
		{0xf5c347b6, 0x8502, 0x4fa5, {0x8b, 0x35, 0x87, 0x5b, 0xbd, 0x96, 0x6c, 0x63}};

	// {61D3CAEE-30D2-45AD-AA8F-EB1E9A6F44D5}
	const GUID GUID_ContainerFormatPep =
		{0x61d3caee, 0x30d2, 0x45ad, {0xaa, 0x8f, 0xeb, 0x1e, 0x9a, 0x6f, 0x44, 0xd5}};

	// {DE6A0608-2E1F-42CC-A20A-67A613667FA6}
	const GUID GUID_PepVendor =
		{0xde6a0608, 0x2e1f, 0x42cc, {0xa2, 0x0a, 0x67, 0xa6, 0x13, 0x66, 0x7f, 0xa6}};

	// WIC's CATID_WICBitmapDecoders:
	// {7ED96837-96F0-4812-B211-F13C24117ED3}
	const GUID GUID_WicBitmapDecodersCategory =
		{0x7ed96837, 0x96f0, 0x4812, {0xb2, 0x11, 0xf1, 0x3c, 0x24, 0x11, 0x7e, 0xd3}};

	// WIC's GUID_WICPixelFormat32bppBGRA:
	// {6FDDC324-4E03-4BFE-B185-3D77768DC90F}
	const WICPixelFormatGUID GUID_PepPixelFormat32bppBGRA =
		{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}};

	constexpr UINT kBytesPerPixel = 4;
	constexpr wchar_t kCodecName[] = L"Prediction-Encoded Pixels WIC Codec";
	constexpr wchar_t kDecoderVersion[] = L"1.0.0.0";
	constexpr wchar_t kDecoderAuthor[] = L"PEP codec: ENDESGA and contributors; WIC wrapper: elebree";
	constexpr wchar_t kColorManagementVersion[] = L"1.0.0.0";
	constexpr wchar_t kPepProgId[] = L"pepfile";
	constexpr wchar_t kPepMimeType[] = L"image/x-pep";
	constexpr wchar_t kPepFileExtensions[] = L".pep";
	constexpr wchar_t kPepKind[] = L"picture";
	constexpr wchar_t kImagePersistentHandler[] = L"{1295340B-BF50-4A88-BB2F-287E0B9B0016}";
	// Windows' built-in IPropertyStore handler for image dimensions and metadata.
	constexpr wchar_t kImagePropertyHandler[] = L"{a38b883c-1682-497e-97b0-0a3a9e801682}";
	constexpr wchar_t kExtractImageShellExtension[] = L"{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}";
	constexpr wchar_t kThumbnailProviderShellExtension[] = L"{e357fccd-a995-4576-b01f-234630154e96}";
	constexpr wchar_t kPhotoExtractImage[] = L"{3F30C968-480A-4C6C-862D-EFC0897BB84B}";
	constexpr wchar_t kPhotoThumbnailProvider[] = L"{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}";
	constexpr wchar_t kPepExtendedTileInfo[] = L"prop:System.ItemType;*System.DateModified;*System.Image.Dimensions";
	constexpr wchar_t kPepInfoTip[] = L"prop:System.ItemType;*System.DateModified;*System.Image.Dimensions;*System.Size";
	constexpr wchar_t kPepPreviewDetails[] =
		L"prop:*System.DateModified;*System.Image.Dimensions;*System.Size;*System.OfflineAvailability;*System.OfflineStatus;*System.DateCreated;*System.SharedWith";
	constexpr wchar_t kPepFullDetails[] =
		L"prop:System.PropGroup.Image;System.Image.Dimensions;System.Image.HorizontalSize;System.Image.VerticalSize;System.Image.BitDepth;"
		L"System.PropGroup.FileSystem;System.ItemNameDisplay;System.ItemType;System.ItemFolderPathDisplay;System.DateCreated;System.DateModified;"
		L"System.Size;System.FileAttributes;System.OfflineAvailability;System.OfflineStatus;System.SharedWith;System.FileOwner;System.ComputerName";

	HMODULE g_module = nullptr;
	std::atomic<ULONG> g_objectCount{0};
	std::atomic<ULONG> g_lockCount{0};
	std::mutex g_pepMutex;

	template <typename T>
	void SafeRelease(T **object)
	{
		if (object != nullptr && *object != nullptr)
		{
			(*object)->Release();
			*object = nullptr;
		}
	}

	LARGE_INTEGER ToLargeInteger(const ULONGLONG value)
	{
		LARGE_INTEGER result{};
		result.QuadPart = static_cast<LONGLONG>(value);
		return result;
	}

	HRESULT SeekAbsolute(IStream *stream, const ULONGLONG position)
	{
		if (stream == nullptr)
			return E_INVALIDARG;
		return stream->Seek(ToLargeInteger(position), STREAM_SEEK_SET, nullptr);
	}

	HRESULT ReadStreamBytes(IStream *stream, std::vector<uint8_t> &outBytes, const bool restorePosition)
	{
		outBytes.clear();
		if (stream == nullptr)
			return E_INVALIDARG;

		ULARGE_INTEGER originalPosition{};
		HRESULT hr = stream->Seek(ToLargeInteger(0), STREAM_SEEK_CUR, &originalPosition);
		if (FAILED(hr))
			return hr;

		ULARGE_INTEGER endPosition{};
		hr = stream->Seek(ToLargeInteger(0), STREAM_SEEK_END, &endPosition);
		if (FAILED(hr))
		{
			if (restorePosition)
				SeekAbsolute(stream, originalPosition.QuadPart);
			return hr;
		}

		if (endPosition.QuadPart == 0 || endPosition.QuadPart > std::numeric_limits<uint32_t>::max())
		{
			if (restorePosition)
				SeekAbsolute(stream, originalPosition.QuadPart);
			return WINCODEC_ERR_BADIMAGE;
		}

		try
		{
			outBytes.resize(static_cast<size_t>(endPosition.QuadPart));
		}
		catch (const std::bad_alloc &)
		{
			if (restorePosition)
				SeekAbsolute(stream, originalPosition.QuadPart);
			return E_OUTOFMEMORY;
		}

		hr = SeekAbsolute(stream, 0);
		if (FAILED(hr))
		{
			if (restorePosition)
				SeekAbsolute(stream, originalPosition.QuadPart);
			return hr;
		}

		uint8_t *dst = outBytes.data();
		ULONGLONG remaining = endPosition.QuadPart;
		while (remaining > 0)
		{
			const ULONG chunk = static_cast<ULONG>(std::min<ULONGLONG>(remaining, std::numeric_limits<ULONG>::max()));
			ULONG read = 0;
			hr = stream->Read(dst, chunk, &read);
			if (FAILED(hr))
			{
				if (restorePosition)
					SeekAbsolute(stream, originalPosition.QuadPart);
				return hr;
			}
			if (read == 0)
			{
				if (restorePosition)
					SeekAbsolute(stream, originalPosition.QuadPart);
				return WINCODEC_ERR_STREAMREAD;
			}

			dst += read;
			remaining -= read;
		}

		if (restorePosition)
		{
			const HRESULT restoreHr = SeekAbsolute(stream, originalPosition.QuadPart);
			if (FAILED(restoreHr))
				return restoreHr;
		}

		return S_OK;
	}

	bool CouldBePepHeader(const std::vector<uint8_t> &bytes);

	bool IsValidPep(const std::vector<uint8_t> &bytes)
	{
		if (bytes.empty() || bytes.size() > std::numeric_limits<uint32_t>::max())
			return false;
		if (!CouldBePepHeader(bytes))
			return false;

		pep image = pep_deserialize(bytes.data(), static_cast<uint32_t>(bytes.size()));
		const bool valid = image.width != 0 && image.height != 0 && image.bytes != nullptr && image.bytes_size != 0;
		pep_free(&image);
		return valid;
	}

	std::wstring GuidToString(REFGUID guid)
	{
		wchar_t buffer[39] = {};
		const int written = StringFromGUID2(guid, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
		if (written == 0)
			return std::wstring();
		return std::wstring(buffer);
	}

	std::wstring GetModulePath()
	{
		DWORD capacity = MAX_PATH;
		for (;;)
		{
			std::vector<wchar_t> buffer(capacity);
			const DWORD length = GetModuleFileNameW(g_module, buffer.data(), capacity);
			if (length == 0)
				return std::wstring();
			if (length < capacity - 1)
				return std::wstring(buffer.data(), length);
			capacity *= 2;
		}
	}

	HRESULT SetRegistryString(HKEY root, const std::wstring &subKey, const wchar_t *valueName, const std::wstring &value)
	{
		HKEY key = nullptr;
		const LONG openResult = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
		if (openResult != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(openResult);

		const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
		const LONG setResult = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), bytes);
		RegCloseKey(key);
		return setResult == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(setResult);
	}

	HRESULT SetRegistryDword(HKEY root, const std::wstring &subKey, const wchar_t *valueName, const DWORD value)
	{
		HKEY key = nullptr;
		const LONG openResult = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
		if (openResult != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(openResult);

		const LONG setResult = RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
		RegCloseKey(key);
		return setResult == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(setResult);
	}

	HRESULT SetRegistryBinary(HKEY root, const std::wstring &subKey, const wchar_t *valueName, const BYTE *data, const DWORD size)
	{
		HKEY key = nullptr;
		const LONG openResult = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
		if (openResult != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(openResult);

		const LONG setResult = RegSetValueExW(key, valueName, 0, REG_BINARY, data, size);
		RegCloseKey(key);
		return setResult == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(setResult);
	}

	HRESULT DeleteRegistryTree(HKEY root, const std::wstring &subKey)
	{
		const LONG result = RegDeleteTreeW(root, subKey.c_str());
		if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND)
			return S_OK;
		return HRESULT_FROM_WIN32(result);
	}

	HRESULT DeleteRegistryValue(HKEY root, const std::wstring &subKey, const wchar_t *valueName)
	{
		HKEY key = nullptr;
		const LONG openResult = RegOpenKeyExW(root, subKey.c_str(), 0, KEY_SET_VALUE, &key);
		if (openResult == ERROR_FILE_NOT_FOUND)
			return S_OK;
		if (openResult != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(openResult);

		const LONG deleteResult = RegDeleteValueW(key, valueName);
		RegCloseKey(key);
		if (deleteResult == ERROR_SUCCESS || deleteResult == ERROR_FILE_NOT_FOUND)
			return S_OK;
		return HRESULT_FROM_WIN32(deleteResult);
	}

#define RETURN_IF_FAILED(expr)      \
	do                              \
	{                               \
		const HRESULT _hr = (expr); \
		if (FAILED(_hr))            \
			return _hr;             \
	} while (0)

	bool CouldBePepHeader(const std::vector<uint8_t> &bytes)
	{
		if (bytes.size() < 2)
			return false;
		if ((bytes[0] & 0x80) != 0)
			return false;
		if (bytes[1] >= PEP_NUM_MODELS)
			return false;
		return true;
	}

	HRESULT CopyStringValue(const wchar_t *value, const UINT bufferLength, WCHAR *buffer, UINT *actualLength)
	{
		if (value == nullptr || actualLength == nullptr)
			return E_INVALIDARG;

		const UINT required = static_cast<UINT>(std::wcslen(value) + 1);
		*actualLength = required;

		if (buffer == nullptr || bufferLength == 0)
			return S_OK;
		if (bufferLength < required)
			return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

		std::wmemcpy(buffer, value, required);
		return S_OK;
	}

	class PepFrame final : public IWICBitmapFrameDecode
	{
	public:
		PepFrame(const UINT width, const UINT height, std::vector<uint8_t> &&pixels)
			: refCount_(1), width_(width), height_(height), pixels_(std::move(pixels))
		{
			g_objectCount.fetch_add(1, std::memory_order_relaxed);
		}

		~PepFrame()
		{
			g_objectCount.fetch_sub(1, std::memory_order_relaxed);
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
		{
			if (object == nullptr)
				return E_POINTER;
			*object = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(IWICBitmapSource)) ||
				IsEqualIID(riid, __uuidof(IWICBitmapFrameDecode)))
			{
				*object = static_cast<IWICBitmapFrameDecode *>(this);
				AddRef();
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(InterlockedIncrement(&refCount_));
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
			if (count == 0)
				delete this;
			return count;
		}

		HRESULT STDMETHODCALLTYPE GetSize(UINT *width, UINT *height) override
		{
			if (width == nullptr || height == nullptr)
				return E_INVALIDARG;
			*width = width_;
			*height = height_;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID *pixelFormat) override
		{
			if (pixelFormat == nullptr)
				return E_INVALIDARG;
			*pixelFormat = GUID_PepPixelFormat32bppBGRA;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetResolution(double *dpiX, double *dpiY) override
		{
			if (dpiX == nullptr || dpiY == nullptr)
				return E_INVALIDARG;
			*dpiX = 96.0;
			*dpiY = 96.0;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette *palette) override
		{
			if (palette == nullptr)
				return E_INVALIDARG;
			return WINCODEC_ERR_PALETTEUNAVAILABLE;
		}

		HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect *rect, UINT stride, UINT bufferSize, BYTE *buffer) override
		{
			if (buffer == nullptr)
				return E_INVALIDARG;

			UINT x = 0;
			UINT y = 0;
			UINT copyWidth = width_;
			UINT copyHeight = height_;

			if (rect != nullptr)
			{
				if (rect->X < 0 || rect->Y < 0 || rect->Width <= 0 || rect->Height <= 0)
					return E_INVALIDARG;

				x = static_cast<UINT>(rect->X);
				y = static_cast<UINT>(rect->Y);
				copyWidth = static_cast<UINT>(rect->Width);
				copyHeight = static_cast<UINT>(rect->Height);

				if (x > width_ || y > height_)
					return E_INVALIDARG;
				if (copyWidth > width_ - x || copyHeight > height_ - y)
					return E_INVALIDARG;
			}

			const UINT rowBytes = copyWidth * kBytesPerPixel;
			if (stride < rowBytes)
				return E_INVALIDARG;

			const uint64_t requiredBytes = copyHeight == 0
											   ? 0
											   : static_cast<uint64_t>(stride) * (copyHeight - 1) + rowBytes;
			if (requiredBytes > bufferSize)
				return E_INVALIDARG;

			for (UINT row = 0; row < copyHeight; ++row)
			{
				const uint8_t *src = pixels_.data() + (static_cast<size_t>(y + row) * width_ + x) * kBytesPerPixel;
				uint8_t *dst = buffer + static_cast<size_t>(row) * stride;
				std::memcpy(dst, src, rowBytes);
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(IWICMetadataQueryReader **queryReader) override
		{
			if (queryReader == nullptr)
				return E_INVALIDARG;
			*queryReader = nullptr;
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}

		HRESULT STDMETHODCALLTYPE GetColorContexts(UINT count, IWICColorContext **colorContexts, UINT *actualCount) override
		{
			if (actualCount == nullptr)
				return E_INVALIDARG;
			if (count != 0 && colorContexts == nullptr)
				return E_INVALIDARG;
			*actualCount = 0;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetThumbnail(IWICBitmapSource **thumbnail) override
		{
			if (thumbnail == nullptr)
				return E_INVALIDARG;
			*thumbnail = nullptr;
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}

	private:
		volatile LONG refCount_;
		UINT width_;
		UINT height_;
		std::vector<uint8_t> pixels_;
	};

	// Decodes image as if its header said width x height.
	uint32_t *DecompressPep(pep image, const UINT width, const UINT height)
	{
		image.width = static_cast<uint16_t>(width);
		image.height = static_cast<uint16_t>(height);
		std::lock_guard<std::mutex> lock(g_pepMutex);
		return pep_decompress(&image, pep_bgra, 0, 0);
	}

	HRESULT DecodePepBytes(const std::vector<uint8_t> &bytes, PepFrame **outFrame)
	{
		if (outFrame == nullptr)
			return E_INVALIDARG;
		*outFrame = nullptr;

		if (bytes.empty() || bytes.size() > std::numeric_limits<uint32_t>::max())
			return WINCODEC_ERR_BADIMAGE;
		if (!CouldBePepHeader(bytes))
			return WINCODEC_ERR_BADIMAGE;

		pep image = pep_deserialize(bytes.data(), static_cast<uint32_t>(bytes.size()));
		if (image.width == 0 || image.height == 0 || image.bytes == nullptr || image.bytes_size == 0)
		{
			pep_free(&image);
			return WINCODEC_ERR_BADIMAGE;
		}

		UINT width = image.width;
		UINT height = image.height;
		uint32_t *decodedPixels = DecompressPep(image, width, height);
		if (decodedPixels == nullptr && PepHeaderCanBeTruncated(bytes[0]))
			decodedPixels = PepDecompressTruncated(image, DecompressPep, &width, &height);
		pep_free(&image);

		if (decodedPixels == nullptr)
			return WINCODEC_ERR_BADIMAGE;

		const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
		if (pixelCount > (std::numeric_limits<size_t>::max() / kBytesPerPixel))
		{
			PEP_FREE(decodedPixels);
			return WINCODEC_ERR_BADIMAGE;
		}

		try
		{
			std::vector<uint8_t> pixels(static_cast<size_t>(pixelCount) * kBytesPerPixel);
			std::memcpy(pixels.data(), decodedPixels, pixels.size());
			PEP_FREE(decodedPixels);

			PepFrame *frame = new (std::nothrow) PepFrame(width, height, std::move(pixels));
			if (frame == nullptr)
				return E_OUTOFMEMORY;
			*outFrame = frame;
			return S_OK;
		}
		catch (const std::bad_alloc &)
		{
			PEP_FREE(decodedPixels);
			return E_OUTOFMEMORY;
		}
	}

	class PepDecoder final : public IWICBitmapDecoder, public IWICBitmapDecoderInfo
	{
	public:
		PepDecoder() : refCount_(1), frame_(nullptr)
		{
			g_objectCount.fetch_add(1, std::memory_order_relaxed);
		}

		~PepDecoder()
		{
			SafeRelease(&frame_);
			g_objectCount.fetch_sub(1, std::memory_order_relaxed);
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
		{
			if (object == nullptr)
				return E_POINTER;
			*object = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(IWICBitmapDecoder)))
			{
				*object = static_cast<IWICBitmapDecoder *>(this);
				AddRef();
				return S_OK;
			}

			if (IsEqualIID(riid, __uuidof(IWICComponentInfo)) ||
				IsEqualIID(riid, __uuidof(IWICBitmapCodecInfo)) ||
				IsEqualIID(riid, __uuidof(IWICBitmapDecoderInfo)))
			{
				*object = static_cast<IWICBitmapDecoderInfo *>(this);
				AddRef();
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(InterlockedIncrement(&refCount_));
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
			if (count == 0)
				delete this;
			return count;
		}

		HRESULT STDMETHODCALLTYPE QueryCapability(IStream *stream, DWORD *capability) override
		{
			if (stream == nullptr || capability == nullptr)
				return E_INVALIDARG;
			*capability = 0;

			std::vector<uint8_t> bytes;
			const HRESULT hr = ReadStreamBytes(stream, bytes, true);
			if (FAILED(hr))
				return S_OK;

			if (IsValidPep(bytes))
			{
				*capability = WICBitmapDecoderCapabilityCanDecodeAllImages;
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE Initialize(IStream *stream, WICDecodeOptions /*cacheOptions*/) override
		{
			if (stream == nullptr)
				return E_INVALIDARG;
			if (frame_ != nullptr)
				return WINCODEC_ERR_WRONGSTATE;

			std::vector<uint8_t> bytes;
			RETURN_IF_FAILED(ReadStreamBytes(stream, bytes, false));

			PepFrame *frame = nullptr;
			RETURN_IF_FAILED(DecodePepBytes(bytes, &frame));
			frame_ = frame;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID *containerFormat) override
		{
			if (containerFormat == nullptr)
				return E_INVALIDARG;
			*containerFormat = GUID_ContainerFormatPep;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetDecoderInfo(IWICBitmapDecoderInfo **decoderInfo) override
		{
			if (decoderInfo == nullptr)
				return E_INVALIDARG;
			*decoderInfo = nullptr;

			IWICImagingFactory *factory = nullptr;
			HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
			if (FAILED(hr))
				return hr;

			IWICComponentInfo *componentInfo = nullptr;
			hr = factory->CreateComponentInfo(CLSID_PepWicCodec, &componentInfo);
			if (SUCCEEDED(hr))
			{
				hr = componentInfo->QueryInterface(IID_PPV_ARGS(decoderInfo));
			}

			SafeRelease(&componentInfo);
			SafeRelease(&factory);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette *palette) override
		{
			if (palette == nullptr)
				return E_INVALIDARG;
			return WINCODEC_ERR_PALETTEUNAVAILABLE;
		}

		HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(IWICMetadataQueryReader **queryReader) override
		{
			if (queryReader == nullptr)
				return E_INVALIDARG;
			*queryReader = nullptr;
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}

		HRESULT STDMETHODCALLTYPE GetPreview(IWICBitmapSource **preview) override
		{
			if (preview == nullptr)
				return E_INVALIDARG;
			*preview = nullptr;
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}

		HRESULT STDMETHODCALLTYPE GetColorContexts(UINT count, IWICColorContext **colorContexts, UINT *actualCount) override
		{
			if (actualCount == nullptr)
				return E_INVALIDARG;
			if (count != 0 && colorContexts == nullptr)
				return E_INVALIDARG;
			*actualCount = 0;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetThumbnail(IWICBitmapSource **thumbnail) override
		{
			if (thumbnail == nullptr)
				return E_INVALIDARG;
			*thumbnail = nullptr;
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}

		HRESULT STDMETHODCALLTYPE GetFrameCount(UINT *count) override
		{
			if (count == nullptr)
				return E_INVALIDARG;
			if (frame_ == nullptr)
				return WINCODEC_ERR_NOTINITIALIZED;
			*count = 1;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetFrame(UINT index, IWICBitmapFrameDecode **frame) override
		{
			if (frame == nullptr)
				return E_INVALIDARG;
			*frame = nullptr;
			if (frame_ == nullptr)
				return WINCODEC_ERR_NOTINITIALIZED;
			if (index != 0)
				return WINCODEC_ERR_FRAMEMISSING;

			frame_->AddRef();
			*frame = frame_;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetComponentType(WICComponentType *type) override
		{
			if (type == nullptr)
				return E_INVALIDARG;
			*type = WICDecoder;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetCLSID(CLSID *clsid) override
		{
			if (clsid == nullptr)
				return E_INVALIDARG;
			*clsid = CLSID_PepWicCodec;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetSigningStatus(DWORD *status) override
		{
			if (status == nullptr)
				return E_INVALIDARG;
			*status = WICComponentUnsigned | WICComponentSafe;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetAuthor(UINT count, WCHAR *author, UINT *actualCount) override
		{
			return CopyStringValue(kDecoderAuthor, count, author, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetVendorGUID(GUID *vendor) override
		{
			if (vendor == nullptr)
				return E_INVALIDARG;
			*vendor = GUID_PepVendor;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetVersion(UINT count, WCHAR *version, UINT *actualCount) override
		{
			return CopyStringValue(kDecoderVersion, count, version, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetSpecVersion(UINT count, WCHAR *specVersion, UINT *actualCount) override
		{
			return CopyStringValue(kDecoderVersion, count, specVersion, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetFriendlyName(UINT count, WCHAR *friendlyName, UINT *actualCount) override
		{
			return CopyStringValue(kCodecName, count, friendlyName, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetPixelFormats(UINT count, GUID *pixelFormats, UINT *actualCount) override
		{
			if (actualCount == nullptr)
				return E_INVALIDARG;
			if (count != 0 && pixelFormats == nullptr)
				return E_INVALIDARG;
			*actualCount = 1;
			if (count > 0)
				pixelFormats[0] = GUID_PepPixelFormat32bppBGRA;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetColorManagementVersion(UINT count, WCHAR *version, UINT *actualCount) override
		{
			return CopyStringValue(kColorManagementVersion, count, version, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetDeviceManufacturer(UINT count, WCHAR *manufacturer, UINT *actualCount) override
		{
			return CopyStringValue(L"", count, manufacturer, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetDeviceModels(UINT count, WCHAR *models, UINT *actualCount) override
		{
			return CopyStringValue(L"", count, models, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetMimeTypes(UINT count, WCHAR *mimeTypes, UINT *actualCount) override
		{
			return CopyStringValue(kPepMimeType, count, mimeTypes, actualCount);
		}

		HRESULT STDMETHODCALLTYPE GetFileExtensions(UINT count, WCHAR *fileExtensions, UINT *actualCount) override
		{
			return CopyStringValue(kPepFileExtensions, count, fileExtensions, actualCount);
		}

		HRESULT STDMETHODCALLTYPE DoesSupportAnimation(BOOL *supportAnimation) override
		{
			if (supportAnimation == nullptr)
				return E_INVALIDARG;
			*supportAnimation = FALSE;
			return S_OK;
		}

		// mingw-w64's wincodec.h spells the Windows SDK's DoesSupportChromakey with a capital K;
		// only the vtable slot matters.
#ifdef __MINGW32__
		HRESULT STDMETHODCALLTYPE DoesSupportChromaKey(BOOL *supportChromakey) override
#else
		HRESULT STDMETHODCALLTYPE DoesSupportChromakey(BOOL *supportChromakey) override
#endif
		{
			if (supportChromakey == nullptr)
				return E_INVALIDARG;
			*supportChromakey = FALSE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE DoesSupportLossless(BOOL *supportLossless) override
		{
			if (supportLossless == nullptr)
				return E_INVALIDARG;
			*supportLossless = TRUE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE DoesSupportMultiframe(BOOL *supportMultiframe) override
		{
			if (supportMultiframe == nullptr)
				return E_INVALIDARG;
			*supportMultiframe = FALSE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE MatchesMimeType(LPCWSTR mimeType, BOOL *matches) override
		{
			if (mimeType == nullptr || matches == nullptr)
				return E_INVALIDARG;
			*matches = _wcsicmp(mimeType, kPepMimeType) == 0;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetPatterns(UINT bufferSize, WICBitmapPattern *patterns, UINT *patternCount, UINT *actualBufferSize) override
		{
			if (patternCount == nullptr || actualBufferSize == nullptr)
				return E_INVALIDARG;

			static BYTE patternBytes[PEP_NUM_MODELS][2] =
				{
					{0x00, 0x00},
					{0x00, 0x01},
					{0x00, 0x02},
				};
			static BYTE maskBytes[PEP_NUM_MODELS][2] =
				{
					{0x80, 0xff},
					{0x80, 0xff},
					{0x80, 0xff},
				};

			const UINT required = PEP_NUM_MODELS * sizeof(WICBitmapPattern);
			*patternCount = PEP_NUM_MODELS;
			*actualBufferSize = required;

			if (patterns == nullptr || bufferSize == 0)
				return S_OK;
			if (bufferSize < required)
				return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

			for (UINT index = 0; index < PEP_NUM_MODELS; ++index)
			{
				patterns[index].Position.QuadPart = 0;
				patterns[index].Length = 2;
				patterns[index].Pattern = patternBytes[index];
				patterns[index].Mask = maskBytes[index];
				patterns[index].EndOfStream = FALSE;
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE MatchesPattern(IStream *stream, BOOL *matches) override
		{
			if (stream == nullptr || matches == nullptr)
				return E_INVALIDARG;
			*matches = FALSE;

			std::vector<uint8_t> bytes;
			const HRESULT hr = ReadStreamBytes(stream, bytes, true);
			if (FAILED(hr))
				return hr;

			*matches = IsValidPep(bytes);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE CreateInstance(IWICBitmapDecoder **decoder) override
		{
			if (decoder == nullptr)
				return E_INVALIDARG;
			*decoder = nullptr;

			PepDecoder *instance = new (std::nothrow) PepDecoder();
			if (instance == nullptr)
				return E_OUTOFMEMORY;
			*decoder = static_cast<IWICBitmapDecoder *>(instance);
			return S_OK;
		}

	private:
		volatile LONG refCount_;
		PepFrame *frame_;
	};

	class PepClassFactory final : public IClassFactory
	{
	public:
		PepClassFactory() : refCount_(1)
		{
			g_objectCount.fetch_add(1, std::memory_order_relaxed);
		}

		~PepClassFactory()
		{
			g_objectCount.fetch_sub(1, std::memory_order_relaxed);
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
		{
			if (object == nullptr)
				return E_POINTER;
			*object = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(IClassFactory)))
			{
				*object = static_cast<IClassFactory *>(this);
				AddRef();
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(InterlockedIncrement(&refCount_));
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
			if (count == 0)
				delete this;
			return count;
		}

		HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **object) override
		{
			if (object == nullptr)
				return E_POINTER;
			*object = nullptr;
			if (outer != nullptr)
				return CLASS_E_NOAGGREGATION;

			PepDecoder *decoder = new (std::nothrow) PepDecoder();
			if (decoder == nullptr)
				return E_OUTOFMEMORY;

			const HRESULT hr = decoder->QueryInterface(riid, object);
			decoder->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
		{
			if (lock)
				g_lockCount.fetch_add(1, std::memory_order_relaxed);
			else
				g_lockCount.fetch_sub(1, std::memory_order_relaxed);
			return S_OK;
		}

	private:
		volatile LONG refCount_;
	};

	HRESULT RegisterDecoder(HKEY root)
	{
		const std::wstring modulePath = GetModulePath();
		if (modulePath.empty())
			return HRESULT_FROM_WIN32(GetLastError());

		const std::wstring clsid = GuidToString(CLSID_PepWicCodec);
		const std::wstring containerFormat = GuidToString(GUID_ContainerFormatPep);
		const std::wstring vendor = GuidToString(GUID_PepVendor);
		const std::wstring decoderCategory = GuidToString(GUID_WicBitmapDecodersCategory);
		const std::wstring pixelFormat = GuidToString(GUID_PepPixelFormat32bppBGRA);
		if (clsid.empty() || containerFormat.empty() || vendor.empty() || decoderCategory.empty() || pixelFormat.empty())
			return E_FAIL;

		const std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsid;
		const std::wstring inprocKey = clsidKey + L"\\InprocServer32";
		const std::wstring categoryKey = clsidKey + L"\\Implemented Categories\\" + decoderCategory;
		const std::wstring decoderCategoryInstanceKey =
			L"Software\\Classes\\CLSID\\" + decoderCategory + L"\\Instance\\" + clsid;
		const std::wstring pixelFormatKey = clsidKey + L"\\Formats\\" + pixelFormat;
		const std::wstring extensionKey = L"Software\\Classes\\.pep";
		const std::wstring extensionOpenWithProgidsKey = extensionKey + L"\\OpenWithProgids";
		const std::wstring extensionPersistentHandlerKey = extensionKey + L"\\PersistentHandler";
		const std::wstring extensionExtractImageKey = extensionKey + L"\\ShellEx\\" + kExtractImageShellExtension;
		const std::wstring extensionThumbnailProviderKey = extensionKey + L"\\ShellEx\\" + kThumbnailProviderShellExtension;
		const std::wstring extensionPropertyHandlerKey =
			L"Software\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\.pep";
		const std::wstring extensionSystemFileAssociationKey = L"Software\\Classes\\SystemFileAssociations\\.pep";
		const std::wstring kindMapKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap";
		const std::wstring progIdKey = L"Software\\Classes\\" + std::wstring(kPepProgId);
		const std::wstring progIdExtractImageKey = progIdKey + L"\\ShellEx\\" + kExtractImageShellExtension;
		const std::wstring progIdThumbnailProviderKey = progIdKey + L"\\ShellEx\\" + kThumbnailProviderShellExtension;

		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, nullptr, kCodecName));
		RETURN_IF_FAILED(SetRegistryString(root, inprocKey, nullptr, modulePath));
		RETURN_IF_FAILED(SetRegistryString(root, inprocKey, L"ThreadingModel", L"Both"));

		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"FriendlyName", kCodecName));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"Description", kCodecName));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"Author", L"ENDESGA / PEP WIC wrapper"));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"Version", kDecoderVersion));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"SpecVersion", L"1.0.0.0"));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"ColorManagementVersion", L"1.0.0.0"));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"FileExtensions", L".pep"));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"MimeTypes", kPepMimeType));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"ContainerFormat", containerFormat));
		RETURN_IF_FAILED(SetRegistryString(root, clsidKey, L"Vendor", vendor));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"ArbitrationPriority", 10));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"Flags", WICComponentUnsigned | WICComponentSafe));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"SupportAnimation", 0));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"SupportChromaKey", 0));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"SupportLossless", 1));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"SupportMultiframe", 0));
		RETURN_IF_FAILED(SetRegistryDword(root, clsidKey, L"SupportTransparency", 1));

		RETURN_IF_FAILED(SetRegistryString(root, categoryKey, nullptr, L""));
		RETURN_IF_FAILED(SetRegistryString(root, decoderCategoryInstanceKey, L"CLSID", clsid));
		RETURN_IF_FAILED(SetRegistryString(root, decoderCategoryInstanceKey, L"FriendlyName", kCodecName));
		RETURN_IF_FAILED(SetRegistryString(root, pixelFormatKey, nullptr, L"32bpp BGRA"));

		for (DWORD model = 0; model < PEP_NUM_MODELS; ++model)
		{
			const std::wstring patternKey = clsidKey + L"\\Patterns\\" + std::to_wstring(model);
			const BYTE mask[] = {0x80, 0xff};
			const BYTE pattern[] = {0x00, static_cast<BYTE>(model)};

			RETURN_IF_FAILED(SetRegistryDword(root, patternKey, L"Length", sizeof(pattern)));
			RETURN_IF_FAILED(SetRegistryDword(root, patternKey, L"Position", 0));
			RETURN_IF_FAILED(SetRegistryBinary(root, patternKey, L"Mask", mask, sizeof(mask)));
			RETURN_IF_FAILED(SetRegistryBinary(root, patternKey, L"Pattern", pattern, sizeof(pattern)));
		}

		RETURN_IF_FAILED(SetRegistryString(root, extensionKey, nullptr, kPepProgId));
		RETURN_IF_FAILED(SetRegistryString(root, extensionKey, L"Content Type", kPepMimeType));
		RETURN_IF_FAILED(SetRegistryString(root, extensionKey, L"ContentType", kPepMimeType));
		RETURN_IF_FAILED(SetRegistryString(root, extensionKey, L"PerceivedType", L"image"));
		RETURN_IF_FAILED(SetRegistryString(root, extensionOpenWithProgidsKey, kPepProgId, L""));
		RETURN_IF_FAILED(SetRegistryString(root, extensionPersistentHandlerKey, nullptr, kImagePersistentHandler));
		RETURN_IF_FAILED(SetRegistryString(root, extensionExtractImageKey, nullptr, kPhotoExtractImage));
		RETURN_IF_FAILED(SetRegistryString(root, extensionThumbnailProviderKey, nullptr, kPhotoThumbnailProvider));
		RETURN_IF_FAILED(SetRegistryString(root, extensionPropertyHandlerKey, nullptr, kImagePropertyHandler));
		RETURN_IF_FAILED(SetRegistryString(root, extensionSystemFileAssociationKey, L"ExtendedTileInfo", kPepExtendedTileInfo));
		RETURN_IF_FAILED(SetRegistryString(root, extensionSystemFileAssociationKey, L"InfoTip", kPepInfoTip));
		RETURN_IF_FAILED(SetRegistryString(root, extensionSystemFileAssociationKey, L"PreviewDetails", kPepPreviewDetails));
		RETURN_IF_FAILED(SetRegistryString(root, extensionSystemFileAssociationKey, L"FullDetails", kPepFullDetails));
		RETURN_IF_FAILED(SetRegistryString(root, kindMapKey, L".pep", kPepKind));

		RETURN_IF_FAILED(SetRegistryString(root, progIdKey, nullptr, L"PEP Image"));
		RETURN_IF_FAILED(SetRegistryString(root, progIdKey, L"FriendlyTypeName", L"PEP Image"));
		RETURN_IF_FAILED(SetRegistryString(root, progIdExtractImageKey, nullptr, kPhotoExtractImage));
		RETURN_IF_FAILED(SetRegistryString(root, progIdThumbnailProviderKey, nullptr, kPhotoThumbnailProvider));

		return S_OK;
	}

	HRESULT UnregisterDecoder(HKEY root)
	{
		const std::wstring clsid = GuidToString(CLSID_PepWicCodec);
		const std::wstring decoderCategory = GuidToString(GUID_WicBitmapDecodersCategory);
		if (clsid.empty() || decoderCategory.empty())
			return E_FAIL;

		const std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsid;
		const std::wstring decoderCategoryInstanceKey =
			L"Software\\Classes\\CLSID\\" + decoderCategory + L"\\Instance\\" + clsid;
		const std::wstring extensionPropertyHandlerKey =
			L"Software\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\.pep";
		const std::wstring extensionSystemFileAssociationKey = L"Software\\Classes\\SystemFileAssociations\\.pep";
		const std::wstring kindMapKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap";
		RETURN_IF_FAILED(DeleteRegistryTree(root, decoderCategoryInstanceKey));
		RETURN_IF_FAILED(DeleteRegistryTree(root, clsidKey));
		RETURN_IF_FAILED(DeleteRegistryTree(root, extensionPropertyHandlerKey));
		RETURN_IF_FAILED(DeleteRegistryTree(root, extensionSystemFileAssociationKey));
		RETURN_IF_FAILED(DeleteRegistryValue(root, kindMapKey, L".pep"));
		RETURN_IF_FAILED(DeleteRegistryTree(root, L"Software\\Classes\\pepfile"));
		RETURN_IF_FAILED(DeleteRegistryTree(root, L"Software\\Classes\\.pep"));
		return S_OK;
	}

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
	return (g_objectCount.load(std::memory_order_relaxed) == 0 && g_lockCount.load(std::memory_order_relaxed) == 0)
			   ? S_OK
			   : S_FALSE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *object)
{
	if (object == nullptr)
		return E_POINTER;
	*object = nullptr;

	if (!IsEqualCLSID(clsid, CLSID_PepWicCodec))
		return CLASS_E_CLASSNOTAVAILABLE;

	PepClassFactory *factory = new (std::nothrow) PepClassFactory();
	if (factory == nullptr)
		return E_OUTOFMEMORY;

	const HRESULT hr = factory->QueryInterface(riid, object);
	factory->Release();
	return hr;
}

extern "C" HRESULT __stdcall DllRegisterServer()
{
	try
	{
		UnregisterDecoder(HKEY_CURRENT_USER);
		return RegisterDecoder(HKEY_LOCAL_MACHINE);
	}
	catch (const std::bad_alloc &)
	{
		return E_OUTOFMEMORY;
	}
}

extern "C" HRESULT __stdcall DllUnregisterServer()
{
	try
	{
		const HRESULT machineHr = UnregisterDecoder(HKEY_LOCAL_MACHINE);
		UnregisterDecoder(HKEY_CURRENT_USER);
		return machineHr;
	}
	catch (const std::bad_alloc &)
	{
		return E_OUTOFMEMORY;
	}
}
