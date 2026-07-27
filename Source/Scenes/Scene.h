#pragma once

// シーン
class Scene
{
public:
    Scene() {}
    virtual ~Scene() {}

    // 初期化
    virtual void initialize() = 0;

    // 終了
    virtual void finalize() = 0;

    // 更新
    virtual void update(float elapsedTime) = 0;

    // 描画
    virtual void render() = 0;

    
    bool isReady() const { return readyFlag; }

   
    void setReady() { readyFlag = true; }

private:
   
    bool readyFlag = false;
};