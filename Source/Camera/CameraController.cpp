#include "CameraController.h"
#include "Camera.h"
#include "Input/InputManager.h"
#include "Stage/StageManager.h"
#include "Easing/easing.h"
#include <cmath>

using namespace DirectX;

CameraController* CameraController::s_instance = nullptr;

namespace
{
    inline XMVECTOR normalizeXZ(XMVECTOR v)
    {
        v = XMVectorSet(XMVectorGetX(v), 0.0f, XMVectorGetZ(v), 0.0f);
        XMVECTOR len = XMVector3Length(v);
        XMVECTOR nz = XMVectorEqual(len, XMVectorZero());
        v = XMVectorMultiply(v, XMVectorReciprocal(len));
        return XMVectorSelect(v, XMVectorSet(1, 0, 0, 0), nz);
    }

    inline XMFLOAT3 LerpFloat3(const XMFLOAT3& a, const XMFLOAT3& b, float t)
    {
        return XMFLOAT3(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t);
    }

    inline float clampf(float v, float lo, float hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    inline XMFLOAT3 ClampFloat3(const XMFLOAT3& v, const XMFLOAT3& minV, const XMFLOAT3& maxV)
    {
        return XMFLOAT3(
            clampf(v.x, minV.x, maxV.x),
            clampf(v.y, minV.y, maxV.y),
            clampf(v.z, minV.z, maxV.z));
    }
}

CameraController::CameraController()
{
    s_instance = this;

    inputEase = getEasingFunction(EaseOutQuad);
    positionEase = getEasingFunction(EaseInOutSine);
    collisionEaseIn = getEasingFunction(EaseOutCubic);
    collisionEaseOut = getEasingFunction(EaseOutSine);

    if (Camera::instance())
    {
        collisionStartPos = *Camera::instance()->getEye();
        collisionEndPos = collisionStartPos;

        position = collisionStartPos;
        target = *Camera::instance()->getFocus();

        XMVECTOR e = XMLoadFloat3(&position);
        XMVECTOR f = XMLoadFloat3(&target);
        XMVECTOR dir = XMVectorSubtract(e, f);
        float dist = XMVectorGetX(XMVector3Length(dir));
        if (dist > 1e-4f)
        {
            range = dist;
            dir = XMVectorScale(dir, 1.0f / dist);
            angle.y = atan2f(XMVectorGetX(dir), XMVectorGetZ(dir)) + XM_PI;
            angle.x = asinf(XMVectorGetY(dir));
        }
    }
}

CameraController::~CameraController()
{
    if (s_instance == this) s_instance = nullptr;
}

void CameraController::update(float elapsedTime)
{
    const XMFLOAT3 prevCameraPos = position;

    GamePad* gamepad = InputManager::instance()->getGamePad();

    float rawAx = 0.0f, rawAy = 0.0f;
    if (skipInputFrames > 0) {
        --skipInputFrames;
    }
    else if (gamepad) {
        rawAx = gamepad->getAxisRX();
        rawAy = -gamepad->getAxisRY();
    }

    if (fabsf(rawAx - inputTargetX) > 1e-4f) { inputStartX = smoothedAx; inputTargetX = rawAx; inputTimerX = 0.0f; }
    if (fabsf(rawAy - inputTargetY) > 1e-4f) { inputStartY = smoothedAy; inputTargetY = rawAy; inputTimerY = 0.0f; }

    if (inputTimerX < inputRampDuration) {
        inputTimerX += elapsedTime;
        if (inputTimerX > inputRampDuration) inputTimerX = inputRampDuration;
        float t = clampf(inputTimerX / inputRampDuration, 0.0f, 1.0f);
        double e = inputEase ? inputEase(t) : t;
        smoothedAx = inputStartX + (inputTargetX - inputStartX) * static_cast<float>(e);
    }
    else {
        smoothedAx = inputTargetX;
    }

    if (inputTimerY < inputRampDuration) {
        inputTimerY += elapsedTime;
        if (inputTimerY > inputRampDuration) inputTimerY = inputRampDuration;
        float t = clampf(inputTimerY / inputRampDuration, 0.0f, 1.0f);
        double e = inputEase ? inputEase(t) : t;
        smoothedAy = inputStartY + (inputTargetY - inputStartY) * static_cast<float>(e);
    }
    else {
        smoothedAy = inputTargetY;
    }

    if (holdFocusFrames > 0)
    {
        if (fabsf(smoothedAx) > 0.1f || fabsf(smoothedAy) > 0.1f)
        {
            holdFocusFrames = 0;
        }
        else
        {
            --holdFocusFrames;
            Camera::instance()->setLookAt(position, freezeFocus, XMFLOAT3(0, 1, 0));
            return;
        }
    }

    float rotSpeed = rollSpeed * elapsedTime;

    if (angleTransitionActive)
    {
        angleTransitionTimer += elapsedTime;
        float t = clampf(angleTransitionTimer / angleTransitionDuration, 0.0f, 1.0f);
        double e = angleEase ? angleEase(t) : t;
        angle.x = angleStart.x + (angleTarget.x - angleStart.x) * static_cast<float>(e);
        angle.y = angleStart.y + (angleTarget.y - angleStart.y) * static_cast<float>(e);
        if (t >= 1.0f) angleTransitionActive = false;
    }
    else
    {
        angle.x += smoothedAy * rotSpeed;
        angle.y += smoothedAx * rotSpeed;
    }

    angle.x = clampf(angle.x, minAngleX, maxAngleX);
    if (angle.y < -XM_PI) angle.y += XM_2PI;
    if (angle.y > XM_PI)  angle.y -= XM_2PI;

    const float CenterHeight = 1.2f;
    XMVECTOR pivotVec = XMVectorAdd(XMLoadFloat3(&target), XMVectorSet(0, CenterHeight, 0, 0));

    XMMATRIX R = XMMatrixRotationRollPitchYaw(angle.x, angle.y, 0);
    XMVECTOR forward = R.r[2];
    XMVECTOR idealEyeVec = XMVectorSubtract(pivotVec, XMVectorScale(forward, range));

    XMFLOAT3 idealEye;
    XMStoreFloat3(&idealEye, idealEyeVec);
    XMFLOAT3 currentPivot;
    XMStoreFloat3(&currentPivot, pivotVec);

    XMFLOAT3 corrected = idealEye;

    float t = clampf(elapsedTime / positionBlendDuration, 0.0f, 1.0f);
    double e = positionEase ? positionEase(t) : t;
    float lerpFactor = static_cast<float>(e);

    position.x += (corrected.x - position.x) * lerpFactor;
    position.y += (corrected.y - position.y) * lerpFactor;
    position.z += (corrected.z - position.z) * lerpFactor;

    activeFocusPoint.x += (currentPivot.x - activeFocusPoint.x) * lerpFactor;
    activeFocusPoint.y += (currentPivot.y - activeFocusPoint.y) * lerpFactor;
    activeFocusPoint.z += (currentPivot.z - activeFocusPoint.z) * lerpFactor;

    if (enableBounds)
    {
        const XMFLOAT3 before = position;
        position = ClampFloat3(position, boundsMin, boundsMax);

        const XMFLOAT3 delta = {
            position.x - before.x,
            position.y - before.y,
            position.z - before.z
        };

        activeFocusPoint.x += delta.x;
        activeFocusPoint.y += delta.y;
        activeFocusPoint.z += delta.z;
    }

    Camera::instance()->setLookAt(position, activeFocusPoint, XMFLOAT3(0, 1, 0));
}