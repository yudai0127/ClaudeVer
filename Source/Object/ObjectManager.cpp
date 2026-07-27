#include "ObjectManager.h"

void ObjectManager::update(float elapsedTime)
{
    for (Object* object : objects)
    {
        object->update(elapsedTime);
    }
}

void ObjectManager::render(ID3D11DeviceContext* dc)
{
    for (Object* object : objects)
    {
        object->render(dc);
    }
}

void ObjectManager::renderShadow(ID3D11DeviceContext* dc)
{
    for (Object* object : objects)
    {
        object->renderShadow(dc);
    }
}

void ObjectManager::clear()
{
    for (Object* object : objects)
    {
        delete object;
    }
    objects.clear();
}