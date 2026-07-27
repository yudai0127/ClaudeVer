#pragma once

#include "Object.h"
#include "Graphics/Gltf/GltfModel.h"
#include <memory>
#include <vector>

class Ship : public Object
{
public:
    Ship();
    ~Ship() override = default;


    void update(float deltaTime) override;

    void render(ID3D11DeviceContext* dc) override;

    void renderShadow(ID3D11DeviceContext* dc) override;

    DirectX::XMFLOAT4X4 buildWorld() const;

    static void renderInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds);
    static void renderShadowInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds);

    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
private:
    static std::shared_ptr<GltfModel> sharedModel;
    std::shared_ptr<GltfModel> gltfModel;
};