#include "app/image_loader.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

std::optional<Frame> load_image(const std::filesystem::path& path, int width, int height) {
  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
  ComPtr<IWICBitmapDecoder> decoder;
  if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(
      path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
  ComPtr<IWICBitmapFrameDecode> source;
  if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &source);
  ComPtr<IWICBitmapScaler> scaler;
  if (SUCCEEDED(hr)) hr = factory->CreateBitmapScaler(&scaler);
  if (SUCCEEDED(hr)) hr = scaler->Initialize(source.Get(), static_cast<UINT>(width),
                                              static_cast<UINT>(height), WICBitmapInterpolationModeFant);
  ComPtr<IWICFormatConverter> converter;
  if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(hr)) hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
      WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) return std::nullopt;

  Frame result(width, height);
  hr = converter->CopyPixels(nullptr, static_cast<UINT>(width * sizeof(Pixel)),
      static_cast<UINT>(result.pixels.size() * sizeof(Pixel)),
      reinterpret_cast<BYTE*>(result.pixels.data()));
  if (FAILED(hr)) return std::nullopt;
  return result;
}

}  // namespace subliminalcam
