#include "SceneManager.h"

void SceneManager::update(float elapsedTime)
{
   
    if (nextScene != nullptr)
    {
        
        clear();

        
        currentScene = nextScene;

        
        nextScene = nullptr;

        // シーンを初期化
        if (currentScene->isReady()==false)
        {
            currentScene->initialize();
        }
    }
    if (currentScene != nullptr)
    {
        currentScene->update(elapsedTime);
    }
}

void SceneManager::render()
{
    if (currentScene != nullptr)
    {
        currentScene->render();
    }
}

void SceneManager::clear()
{
    if (currentScene != nullptr)
    {
        currentScene->finalize();
        delete currentScene;
        currentScene = nullptr;
    }
}

void SceneManager::changeScene(Scene* scene)
{
    // 次のシーンとして設定
    nextScene = scene;
}
