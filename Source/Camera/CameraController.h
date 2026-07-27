#pragma once
#include <DirectXMath.h>
#include "Easing/easing.h"

class CameraController
{
public:
    CameraController();
    ~CameraController();

    static CameraController* instance() { return s_instance; }

    // フレーム更新
    void update(float elapsedTime);

    
    void setRange(float r) { range = r; }

    // 行動範囲を設定
    void setMovementBounds(const DirectX::XMFLOAT3& minPos, const DirectX::XMFLOAT3& maxPos) {
        boundsMin = minPos;
        boundsMax = maxPos;
        enableBounds = true;
    }
   
    void disableMovementBounds() { enableBounds = false; }

    bool isMovementBoundsEnabled() const { return enableBounds; }
    const DirectX::XMFLOAT3& getMovementBoundsMin() const { return boundsMin; }
    const DirectX::XMFLOAT3& getMovementBoundsMax() const { return boundsMax; }

private:
   
    DirectX::XMFLOAT3 target = { 0, 0, 0 };
    DirectX::XMFLOAT3 newPosition = { 0, 0, 0 };
    DirectX::XMFLOAT3 angle = { DirectX::XMConvertToRadians(15), 0, 0 };
    float rollSpeed = DirectX::XMConvertToRadians(120);
    float range = 4.0f;
    float maxAngleX = DirectX::XMConvertToRadians(75);
    float minAngleX = DirectX::XMConvertToRadians(-60);
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 newTarget = { 0, 0, 0 };

    int skipInputFrames = 0;
    int holdFocusFrames = 0;
    DirectX::XMFLOAT3 freezeFocus = { 0, 0, 0 };
    DirectX::XMFLOAT3 activeFocusPoint = { 0, 0, 0 };
    float focusTransitionAlpha = 1.0f;

   
    float inputRampDuration = 0.08f;
    float smoothedAx = 0.0f, smoothedAy = 0.0f;
    float inputTimerX = 0.0f, inputTimerY = 0.0f;
    float inputStartX = 0.0f, inputStartY = 0.0f;
    float inputTargetX = 0.0f, inputTargetY = 0.0f;
    easingFunction inputEase = nullptr;

    // 角度補間
    bool angleTransitionActive = false;
    float angleTransitionTimer = 0.0f;
    float angleTransitionDuration = 0.18f;
    DirectX::XMFLOAT3 angleStart;
    DirectX::XMFLOAT3 angleTarget;
    easingFunction angleEase = nullptr;

    // 位置補間
    float positionBlendDuration = 0.05f;
    easingFunction positionEase = nullptr;

    // 衝突補正
    float collisionTimer = 0.0f;
    float collisionDurationIn = 0.05f;
    float collisionDurationOut = 0.4f;
    DirectX::XMFLOAT3 collisionStartPos;
    DirectX::XMFLOAT3 collisionEndPos;
    bool wasCollidingLastFrame = false;
    easingFunction collisionEaseIn = nullptr;
    easingFunction collisionEaseOut = nullptr;


    DirectX::XMFLOAT3 boundsMin = { 0, 0, 0 };
    DirectX::XMFLOAT3 boundsMax = { 0, 0, 0 };
    bool enableBounds = false;

    static CameraController* s_instance;
};