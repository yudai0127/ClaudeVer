#pragma once

#include <d3d11.h>


//ステージ
class Stage
{
public:
    Stage() {}
   virtual ~Stage() {}

    //更新
   virtual void update(float elapsedTime)=0;

    //描画
   virtual void render(ID3D11DeviceContext* dc)=0;

   //影描画
   virtual void renderShadow(ID3D11DeviceContext* dc) = 0;

   
};
