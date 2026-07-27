#pragma once

#include <d3d11.h>

class Object
{
    public:
    Object() = default;
    virtual ~Object() = default;
    // çXêV
    virtual void update(float deltaTime) = 0;
    // ï`âÊ
    virtual void render(ID3D11DeviceContext* dc) = 0;

    virtual void renderShadow(ID3D11DeviceContext* dc) = 0;
};