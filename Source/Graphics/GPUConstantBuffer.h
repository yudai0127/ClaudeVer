#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include "misc.h"

class GPUConstantBuffer
{
public:
    GPUConstantBuffer(ID3D11Device* device, size_t byteSize)
    {
        HRESULT hr{ S_OK };

        D3D11_BUFFER_DESC bufferDesc;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.ByteWidth = static_cast<UINT>((byteSize + 15) / 16 * 16);
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = 0;
        bufferDesc.StructureByteStride = 0;

        hr = device->CreateBuffer(&bufferDesc, nullptr, cb.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }
    virtual ~GPUConstantBuffer() = default;

    template <typename T>
    void UploadData(ID3D11DeviceContext* dc, UINT startSlot, T& data, bool isSetVS = false, bool isSetHS = false, bool isSetDS = false, bool isSetGS = false, bool isSetPS = false, bool isSetCS = false)
    {
        dc->UpdateSubresource(cb.Get(), 0, 0, &data, 0, 0);
        if (isSetVS)
        {
            dc->VSSetConstantBuffers(startSlot, 1, cb.GetAddressOf());
        }
        if (isSetHS)
        {
            dc->HSSetConstantBuffers(startSlot, 1, cb.GetAddressOf());
        }
        if (isSetDS)
        {
            dc->DSSetConstantBuffers(startSlot, 1, cb.GetAddressOf());
        }
        if (isSetGS)
        {
            dc->GSSetConstantBuffers(startSlot, 1, cb.GetAddressOf());
        }
        if (isSetPS)
        {
            dc->PSSetConstantBuffers(startSlot, 1, cb.GetAddressOf());
        }
        if (isSetCS)
        {
            dc->CSSetConstantBuffers(startSlot, 1, cb.GetAddressOf());
        }
    };

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> cb;
};