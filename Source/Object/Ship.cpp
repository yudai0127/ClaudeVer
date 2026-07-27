#include "Ship.h"
#include "Graphics/DeviceManager/DeviceManager.h"
using namespace DirectX;

std::shared_ptr<GltfModel> Ship::sharedModel;

Ship::Ship()
{
    if (!sharedModel)
    {
        sharedModel = std::make_shared<GltfModel>(DeviceManager::instance()->getDevice(), ".\\Resources\\Model\\Ship\\Ship.glb", false);
    }

    gltfModel = sharedModel;
}

void Ship::update(float deltaTime)
{
}

DirectX::XMFLOAT4X4 Ship::buildWorld() const
{
    XMMATRIX mScale = XMMatrixScaling(scale.x, scale.y, scale.z);
    
    XMMATRIX mRot = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
    XMMATRIX mTrans = XMMatrixTranslation(position.x, position.y, position.z);
    XMMATRIX mWorld = mScale * mRot * mTrans;

    XMFLOAT4X4 world;
    XMStoreFloat4x4(&world, mWorld);
    return world;
}

void Ship::render(ID3D11DeviceContext* dc)
{
    if (gltfModel)
    {
        std::vector<GltfModel::Node> emptyNodes;
        const DirectX::XMFLOAT4X4 world = buildWorld();
        gltfModel->render(dc, world, emptyNodes);
    }
}

void Ship::renderShadow(ID3D11DeviceContext* dc)
{
    if (gltfModel)
    {
        std::vector<GltfModel::Node> emptyNodes;
        const DirectX::XMFLOAT4X4 world = buildWorld();
        gltfModel->renderShadow(dc, world, emptyNodes);
    }
}

void Ship::renderInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds)
{
    if (!sharedModel || instanceWorlds.empty())
    {
        return;
    }

    std::vector<GltfModel::Node> emptyNodes;
    sharedModel->renderInstanced(dc, instanceWorlds, emptyNodes);
}

void Ship::renderShadowInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds)
{
    if (!sharedModel || instanceWorlds.empty())
    {
        return;
    }

    std::vector<GltfModel::Node> emptyNodes;
    sharedModel->renderShadowInstanced(dc, instanceWorlds, emptyNodes);
}