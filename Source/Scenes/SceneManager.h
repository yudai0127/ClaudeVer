#pragma once

#include "Scene.h"

// ƒV[ƒ“ŠÇ—
class SceneManager
{
private:
    SceneManager() {}
    ~SceneManager() {}

public:
    static SceneManager* instance()
    {
        static SceneManager inst;
        return &inst;
    }

  
    void update(float elapsedTime);

    
    void render();

   
    void clear();

    
    void changeScene(Scene* scene);

private:
 
    Scene* currentScene = nullptr;

    Scene* nextScene = nullptr;
};
