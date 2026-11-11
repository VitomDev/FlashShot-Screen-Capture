#include "core/encode.h"

#include <objbase.h>
#include <oleauto.h>
#include <wincodec.h>

#include <cwctype>
#include <fstream>

namespace flashshot {

namespace {

// Minimal owning wrapper for the few COM objects used here.
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

constexpr float kJpegQuality = 0.92f;

}  // namespace

std::vector<uint8_t> EncodeImage(const BgraImage& image, ImageFormat format) {
    if (image.empty()) return {};
    Com<IWICImagingFactory> factory;
    Com<IStream> stream;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> options;
    const GUID container = format == ImageFormat::Jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || FAILED(factory->CreateEncoder(container, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&frame, &options))) {
        return {};
    }
    if (format == ImageFormat::Jpeg && options.p) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = kJpegQuality;
        options->Write(1, &option, &value);
    }
    if (FAILED(frame->Initialize(options.p)) || FAILED(frame->SetSize(image.width, image.height))) return {};
    // Captures are opaque: 24-bit pixels, converted by WriteSource.
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&pixelFormat))) return {};
    Com<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA, image.width * 4,
                                               static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()),
                                               &bitmap))) {
        return {};
    }
    if (FAILED(frame->WriteSource(bitmap.p, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};

    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return {};
    std::vector<uint8_t> data(static_cast<size_t>(stat.cbSize.QuadPart));
    const LARGE_INTEGER start{};
    ULONG read = 0;
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) ||
        FAILED(stream->Read(data.data(), static_cast<ULONG>(data.size()), &read)) || read != data.size()) {
        return {};
    }
    return data;
}

DWORD SaveImage(const BgraImage& image, const std::filesystem::path& path, ImageFormat format) {
    const std::vector<uint8_t> data = EncodeImage(image, format);
    if (data.empty()) return ERROR_INVALID_DATA;
    std::filesystem::path temporary = path;
    temporary += L".part";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (ok && !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
    }
    if (error != ERROR_SUCCESS) DeleteFileW(temporary.c_str());
    return error;
}

ImageFormat FormatForPath(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    return extension == L".jpg" || extension == L".jpeg" ? ImageFormat::Jpeg : ImageFormat::Png;
}

}  // namespace flashshot
