#include "StageManager.h"

void StageManager::update(float elapsedTime)
{
    for (Stage* stage : stages)
    {
        stage->update(elapsedTime);
    }

}

void StageManager::render(ID3D11DeviceContext* dc)
{
    for (Stage* stage : stages)
    {
        stage->render(dc);
    }
}

void StageManager::renderShadow(ID3D11DeviceContext* dc)
{
	for (Stage* stage : stages)
	{
		stage->renderShadow(dc);
	}
}



void StageManager::clear()
{
    for (Stage* stage : stages)
    {
        delete stage;
    }
    stages.clear();
}




