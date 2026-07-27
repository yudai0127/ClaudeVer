#pragma once

#include "Object.h"
#include <vector>

class ObjectManager
{
public:

    static ObjectManager* instance()
    {
        static ObjectManager inst;
        return &inst;
    }

    ObjectManager() = default;
    ~ObjectManager() = default;
    void update(float elapsedTime);
    void render(ID3D11DeviceContext* dc);
    void renderShadow(ID3D11DeviceContext* dc);

    // ステージの登録
    void regist(Object* object) { objects.emplace_back(object); }

    void clear();

private:
    std::vector<Object*> objects;
};