#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <sstream>
#include <Windows.h>


/// 定数バッファの作成
template<typename Ty>
inline HRESULT createBuffer(ID3D11Device* device, ID3D11Buffer** buffer)
{
    // シェーダ用定数バッファ
    D3D11_BUFFER_DESC desc;
    ::memset(&desc, 0, sizeof(desc));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.ByteWidth = sizeof(Ty);
    desc.StructureByteStride = 0;

    return device->CreateBuffer(&desc, 0, buffer);
}


/// 定数バッファの更新と設定
template<typename Ty>
inline void bindBuffer(ID3D11DeviceContext* dc, int slot, ID3D11Buffer** buffer, Ty* constants)
{
    // 定数バッファの更新
    dc->UpdateSubresource(*buffer, 0, 0, constants, 0, 0);
    // 定数バッファのバインド
    dc->VSSetConstantBuffers(slot, 1, buffer);
    dc->PSSetConstantBuffers(slot, 1, buffer);
}
