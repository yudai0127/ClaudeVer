#pragma once

#include "Graphics/Sprite/Sprite.h"
#include "Scene.h"
#include <thread>
#include <memory>

// ローディングシーン
class LoadingScene : public Scene
{
public:
    LoadingScene(Scene* nextscene) : nextScene(nextscene) {}
    ~LoadingScene() override {}

    // 初期化処理
    void initialize() override;

    // 終了処理
    void finalize() override;

    // 更新処理
    void update(float elapsedTime) override;

    // 描画処理
    void render() override;

private:
    static void LoadingThread(LoadingScene* scene);

private:
    // ローディング画像
    std::unique_ptr<Sprite> loadingImage;
    // 回転角度
    float angle = 0.0f;
    // 次のシーン
    Scene* nextScene = nullptr;
    // マルチ用スレッド
    std::thread thread;
};