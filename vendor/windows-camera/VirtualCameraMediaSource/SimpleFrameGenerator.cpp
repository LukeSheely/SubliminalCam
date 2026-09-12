//
// Copyright (C) Microsoft Corporation. All rights reserved.
//
#include "pch.h"

#include <algorithm>
#include <cstring>

namespace
{
    HANDLE g_frameMapping = nullptr;
    const std::byte* g_frameView = nullptr;

    const std::byte* OpenSharedFrame()
    {
        if (g_frameView)
        {
            return g_frameView;
        }

        g_frameMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, subliminalcam::kFrameMappingName);
        if (!g_frameMapping)
        {
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, FALSE };
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GRGW;;;AU)(A;;GRGW;;;LS)(A;;GRGW;;;SY)", SDDL_REVISION_1,
                &descriptor, nullptr))
            {
                security.lpSecurityDescriptor = descriptor;
            }
            g_frameMapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                security.lpSecurityDescriptor ? &security : nullptr, PAGE_READWRITE,
                static_cast<DWORD>(subliminalcam::kFrameMappingBytes >> 32),
                static_cast<DWORD>(subliminalcam::kFrameMappingBytes & 0xffffffffu),
                subliminalcam::kFrameMappingName);
            LocalFree(descriptor);
        }
        if (!g_frameMapping)
        {
            return nullptr;
        }
        g_frameView = static_cast<const std::byte*>(MapViewOfFile(
            g_frameMapping, FILE_MAP_READ, 0, 0, subliminalcam::kFrameMappingBytes));
        return g_frameView;
    }

}

bool SimpleFrameGenerator::_ReadSharedFrame()
{
    const auto now = GetTickCount64();
    const auto cachedIsRecent = [this, now]
    {
        return !m_sharedPixels.empty() &&
            now - m_lastSharedFrameTick <= 2000;
    };

    const auto* view = OpenSharedFrame();
    if (!view)
    {
        return cachedIsRecent();
    }
    const auto* header = reinterpret_cast<const subliminalcam::SharedFrameHeader*>(view);
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const LONG sequenceBefore = header->sequence;
        if ((sequenceBefore & 1) != 0)
        {
            if (attempt < 4) YieldProcessor(); else SwitchToThread();
            continue;
        }
        MemoryBarrier();
        const UINT32 width = header->width;
        const UINT32 height = header->height;
        const UINT32 stride = header->stride;
        const UINT32 dataBytes = header->data_bytes;
        if (header->magic != subliminalcam::kFrameMagic ||
            header->version != subliminalcam::kFrameVersion ||
            width == 0 || height == 0 ||
            stride < width * 4 ||
            dataBytes < static_cast<ULONGLONG>(stride) * height ||
            dataBytes > subliminalcam::kMaxFrameBytes)
        {
            return cachedIsRecent();
        }

        // Avoid another multi-megabyte copy when the consumer requests more
        // frequently than the controller publishes.
        if (!m_sharedPixels.empty() && sequenceBefore == m_sharedSequence)
        {
            return cachedIsRecent();
        }

        m_sharedReadBuffer.resize(dataBytes);
        std::memcpy(m_sharedReadBuffer.data(),
            view + sizeof(subliminalcam::SharedFrameHeader), dataBytes);
        MemoryBarrier();
        if (sequenceBefore == header->sequence && (sequenceBefore & 1) == 0)
        {
            m_sharedPixels.swap(m_sharedReadBuffer);
            m_sharedWidth = width;
            m_sharedHeight = height;
            m_sharedStride = stride;
            m_sharedSequence = sequenceBefore;
            m_lastSharedFrameTick = now;
            return true;
        }
    }
    // A collision is not an offline signal. Holding the last complete frame is
    // visually stable and prevents single-frame flashes of the diagnostic slate.
    return cachedIsRecent();
}

HRESULT SimpleFrameGenerator::Initialize(_In_ IMFMediaType* pMediaType)
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pMediaType);

    RETURN_IF_FAILED(pMediaType->GetGUID(MF_MT_SUBTYPE, &m_subType));
    if (m_subType != MFVideoFormat_RGB32 && m_subType != MFVideoFormat_NV12)
    {
        RETURN_HR_MSG(MF_E_UNSUPPORTED_FORMAT, "Unsupported format: %s", winrt::to_hstring(m_subType).data());
    }
    MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m_width, &m_height);

    return S_OK;
}

/*:
   Writes to a buffer representing a 2D image.
   Writes a different constant to each line based on row number and current time.
   Assumes top down image, no negative stride and pBuf points to the begnning of the buffer of length len.
   Param:
   pBuf - pointer to beginning of buffer
   pitch - line length in bytes
   len - length of buffer in bytes
*/
HRESULT SimpleFrameGenerator::CreateFrame(
    _Inout_updates_bytes_(len) BYTE* pBuf,
    _In_ DWORD len,
    _In_ LONG pitch,
    _In_ ULONG rgbMask)
{
    if (m_subType == MFVideoFormat_RGB32)
    {
        DEBUG_MSG(L"RGB32 frames %s\n", winrt::to_hstring(MFVideoFormat_RGB32).data());

        RETURN_IF_FAILED(_CreateRGB32Frame(pBuf, len, pitch, m_width, m_height, rgbMask));
    }
    else if(m_subType == MFVideoFormat_NV12)
    {
        DEBUG_MSG(L"NV12 frames %s \n", winrt::to_hstring(MFVideoFormat_NV12).data());

        DWORD frameBuffLen = m_width * m_height * 4;
        m_rgbConversionBuffer.resize(frameBuffLen);
        RETURN_IF_FAILED(_CreateRGB32Frame(m_rgbConversionBuffer.data(), frameBuffLen,
            m_width * 4, m_width, m_height, rgbMask));
        RETURN_IF_FAILED(RGB32ToNV12Frame(m_rgbConversionBuffer.data(), frameBuffLen,
            m_width * 4, m_width, m_height, pBuf, len, pitch));
    }
    else
    {
        return MF_E_UNSUPPORTED_FORMAT;
    }

    return S_OK;
}

//////////////////////////////////////////////////
// private

HRESULT SimpleFrameGenerator::_CreateRGB32Frame(
    _Inout_updates_bytes_(len) BYTE* pBuf,
    _In_ DWORD len,
    _In_ LONG pitch,
    _In_ DWORD width,
    _In_ DWORD height,
    _In_ ULONG rgbMask )
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pBuf);
    if (len < (abs(pitch) * height ))
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    if (_ReadSharedFrame())
    {
        if (m_sharedWidth == width && m_sharedHeight == height)
        {
            for (DWORD y = 0; y < height; ++y)
            {
                std::memcpy(pBuf + static_cast<size_t>(y) * pitch,
                    m_sharedPixels.data() + static_cast<size_t>(y) * m_sharedStride,
                    static_cast<size_t>(width) * sizeof(uint32_t));
            }
            return S_OK;
        }
        for (DWORD y = 0; y < height; ++y)
        {
            const DWORD sourceY = static_cast<DWORD>((static_cast<ULONGLONG>(y) * m_sharedHeight) / height);
            auto* destination = reinterpret_cast<uint32_t*>(pBuf + static_cast<size_t>(y) * pitch);
            const auto* source = reinterpret_cast<const uint32_t*>(
                m_sharedPixels.data() + static_cast<size_t>(sourceY) * m_sharedStride);
            for (DWORD x = 0; x < width; ++x)
            {
                const DWORD sourceX = static_cast<DWORD>((static_cast<ULONGLONG>(x) * m_sharedWidth) / width);
                destination[x] = source[sourceX];
            }
        }
        return S_OK;
    }

    // Static diagnostic slate while the controller is not publishing frames.
    for (unsigned int r = 0; r < height; r++)
    {
        uint32_t* p = (uint32_t*)(pBuf + (r * pitch));
        for (unsigned int c = 0; c < width; c++)
        {
            const BYTE gray = static_cast<BYTE>(24 + (r * 28 / std::max<DWORD>(1, height - 1)) +
                (((r / 72) & 1) ? 5 : 0));
            *p = ((uint32_t)gray << 16 | (uint32_t)gray << 8 | (uint32_t)gray) & rgbMask;
            p++;
        }
    }

    return S_OK;
}

//////////////////////////////////////////////////
// pixelFormatConverter

void SimpleFrameGenerator::RGB24ToYUY2(int R, int G, int B, BYTE* pY, BYTE* pU, BYTE* pV)
{
    *pY = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
    *pU = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
    *pV = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;
}

void SimpleFrameGenerator::RGB24ToY(int R, int G, int B, BYTE* pY)
{
    *pY = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
}

void SimpleFrameGenerator::RGB32ToNV12(BYTE RGB1[8], BYTE RGB2[8], BYTE* pY1, BYTE* pY2, BYTE* pUV)
{
    RGB24ToYUY2(RGB1[2], RGB1[1], RGB1[0], pY1, pUV, pUV + 1);
    RGB24ToY(RGB1[6], RGB1[5], RGB1[4], pY1 + 1);
    RGB24ToYUY2(RGB2[2], RGB2[1], RGB2[0], pY2, pUV, pUV + 1);
    RGB24ToY(RGB2[6], RGB2[5], RGB2[4], pY2 + 1);
};

//////////////////////////////////////////////////
// FrameFormatConverter

HRESULT SimpleFrameGenerator::RGB32ToNV12Frame(_Inout_updates_bytes_(len) BYTE* pbBuff, ULONG cbBuff, long stride, UINT width, UINT height, BYTE* pbBuffOut, ULONG cbBuffOut, long strideOut)
{
    do
    {
        RETURN_HR_IF(E_UNEXPECTED, width * 4 * height > cbBuff);
        RETURN_HR_IF(E_UNEXPECTED, width * 1.5 * height > cbBuffOut);
        RETURN_HR_IF_NULL(E_INVALIDARG, pbBuff);

        RETURN_HR_IF_NULL(E_INVALIDARG, pbBuffOut);
        for (DWORD h = 0; h < height - 1; h += 2)
        {
            BYTE* pRGB1 = h * stride + pbBuff;
            BYTE* pRGB2 = (h + 1) * stride + pbBuff;
            BYTE* pY1 = h * strideOut + pbBuffOut;
            BYTE* pY2 = (h + 1) * strideOut + pbBuffOut;
            BYTE* pUV = (h / 2 + height) * strideOut + pbBuffOut;

            for (DWORD w = 0; w < width; w += 2)
            {
                RGB32ToNV12(pRGB1, pRGB2, pY1, pY2, pUV);
                pRGB1 += 8;
                pRGB2 += 8;
                pY1 += 2;
                pY2 += 2;
                pUV += 2;
            }
        }
    } while (FALSE);

    return S_OK;
}
