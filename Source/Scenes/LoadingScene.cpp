#include "LoadingScene.h"
#include "SceneManager.h"
#include "Graphics/DeviceManager/DeviceManager.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include <DirectXMath.h> 

void LoadingScene::initialize()
{
    // ローディング画像読み込み
    loadingImage = std::make_unique<Sprite>(DeviceManager::instance()->getDevice(), L".\\Resources\\Sprite\\loading.png");

    // 別のスレッドを起動
    thread = std::thread(LoadingThread, this);
}

void LoadingScene::finalize()
{
    // スレッドが終了していない場合は待機
    if (thread.joinable())
    {
        thread.join();
    }
    if (nextScene != nullptr)
    {
        nextScene->finalize();
        delete nextScene;
        nextScene = nullptr;
    }
}

void LoadingScene::update(float elapsedTime)
{
    constexpr float speed = 180.0f; // 回転スピード
    angle += speed * elapsedTime;

    // 次のシーンの初期化完了通知が完了していたらシーン切り替える
    if (nextScene != nullptr && nextScene->isReady())
    {
        SceneManager::instance()->changeScene(nextScene);
        nextScene = nullptr;
    }
}

void LoadingScene::LoadingThread(LoadingScene* scene)
{

    HRESULT hr = CoInitialize(nullptr);

    // 次のシーンの初期化
    if (scene && scene->nextScene)
    {
        scene->nextScene->initialize();
        // 次のシーンの準備完了設定
        scene->nextScene->setReady();
    }

    // COM 終了処理
    CoUninitialize();
}

void LoadingScene::render()
{
    DeviceManager* mgr = DeviceManager::instance();
    GraphicsManager* graphics = GraphicsManager::instance();

    ID3D11DeviceContext* dc = mgr->getDeviceContext();
    ID3D11RenderTargetView* rtv = mgr->getRenderTargetView();
    ID3D11DepthStencilView* dsv = mgr->getDepthStencilView();


    const FLOAT color[] = { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA(0.0～1.0)
    dc->ClearRenderTargetView(rtv, color);
    dc->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    dc->OMSetRenderTargets(1, &rtv, dsv);

    // 2D 描画設定
    graphics->SettingRenderContext([](ID3D11DeviceContext* dc, RenderContext* rc) {
        dc->PSSetSamplers(0, 1, rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::CLAMP_LINEAR)].GetAddressOf());
        dc->OMSetBlendState(rc->blendStates[static_cast<uint32_t>(BLEND_STATE::ALPHABLENDING)].Get(), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rc->depthStencilStates[static_cast<uint32_t>(DEPTH_STENCIL_STATE::OFF_OFF)].Get(), 0);
        dc->RSSetState(rc->rasterizerStates[static_cast<uint32_t>(RASTERIZER_STATE::SOLID_CULLNONE)].Get());
        });

    // 2D 描画
    if (loadingImage)
    {
        float screenWidth = static_cast<float>(mgr->getScreenWidth());
        float screenHeight = static_cast<float>(mgr->getScreenHeight());

        
        float textureWidth = 100.0f;
        float textureHeight = 100.0f;

        

        float posX = (screenWidth - textureWidth) * 0.5f;
        float posY = (screenHeight - textureHeight) * 0.5f;

        
        loadingImage->render(dc,
            posX, posY, textureWidth, textureHeight,
            1.0f, 1.0f, 1.0f, 1.0f,
            angle, 
            0.0f, 0.0f, textureWidth, textureHeight
        );
    }
}