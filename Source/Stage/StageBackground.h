#pragma once

#include "Stage.h"
#include "Graphics/Gltf/GltfModel.h"


class StageBackground : public Stage
{
public:
    StageBackground();
    ~StageBackground() override;

    void update(float elapsedTime) override;
    void render(ID3D11DeviceContext* dc) override;
    void renderShadow(ID3D11DeviceContext* dc) override;

    
    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT3 position;
private:
    std::unique_ptr<GltfModel> gltfModel = nullptr;

   
};
