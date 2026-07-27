#include "StageBackground.h"
#include "Graphics/DeviceManager/DeviceManager.h"
using namespace DirectX;
StageBackground::StageBackground()
{
   
   gltfModel = std::make_unique<GltfModel>(DeviceManager::instance()->getDevice(), ".\\Resources\\Model\\Landescape\\scene.glb", false);

     
    scale = { 100.0,100.0,100.0 };

   
    position = { -36.0f, 0.0f, -15.0f };

   
    
}

StageBackground::~StageBackground()
{
}


void StageBackground::update(float elapsedTime)
{
}


void StageBackground::render(ID3D11DeviceContext* dc)
{
    if (gltfModel)
    {
       
       
        XMMATRIX mScale = XMMatrixScaling(scale.x, scale.y, scale.z);
        XMMATRIX mTrans = XMMatrixTranslation(position.x, position.y, position.z);
        XMMATRIX mWorld = mScale * mTrans;

        XMFLOAT4X4 world;
        XMStoreFloat4x4(&world, mWorld); 

        std::vector<GltfModel::Node> emptyNodes;
        gltfModel->render(dc, world, emptyNodes);
    }
}

void StageBackground::renderShadow(ID3D11DeviceContext* dc)
{
    if (gltfModel)
    {
        XMMATRIX mScale = XMMatrixScaling(scale.x, scale.y, scale.z);
        XMMATRIX mTrans = XMMatrixTranslation(position.x, position.y, position.z);
        XMMATRIX mWorld = mScale * mTrans;

        XMFLOAT4X4 world;
        XMStoreFloat4x4(&world, mWorld);

        std::vector<GltfModel::Node> emptyNodes;
        gltfModel->renderShadow(dc, world, emptyNodes);
    }
}



