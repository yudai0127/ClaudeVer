#pragma once

#include "Stage.h"
#include <vector>

class StageManager
{
public:
    StageManager() {}
    ~StageManager() {}

    //インスタンスの取得
    static StageManager* instance()
    {
        static StageManager inst;
        return &inst;
    }
    //更新
    void update(float elapsedTime);

    //描画
    void render(ID3D11DeviceContext* dc);

    void renderShadow(ID3D11DeviceContext* dc);

    // ステージの登録
    void regist(Stage* stage) { stages.emplace_back(stage); }

    
   

    //ステージ全削除
    void clear();

    
public:
    const std::vector<Stage*>& getStages() const { return stages; }

private:
    std::vector<Stage*> stages;
   
};