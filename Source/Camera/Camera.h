#pragma once

#include <DirectXMath.h>

//　カメラ
class Camera
{
private:
	Camera() {}
	~Camera() {}

public:
	//　唯一のインスタンス取得
    static Camera* instance()
    {
        static Camera inst;
        return &inst;
    }

	//　指定方向を向く
	void setLookAt(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& forcus,
		const DirectX::XMFLOAT3& up);

	//　パーススペクティブ設定
	void setPerspectiveFov(float fovY, float aspect, float nearZ, float farZ);

	//　ビュー行列取得
	const DirectX::XMFLOAT4X4* getView() const { return &view; }

	//　プロジェクション行列取得
	const DirectX::XMFLOAT4X4* getProjection() const { return &projection; }

	float getNear() const { return nearZ; }
	float getFar() const { return farZ; }

	//　視点取得
	const DirectX::XMFLOAT3* getEye() const { return &eye; }

	//　注視点取得
	const DirectX::XMFLOAT3* getFocus() const { return &focus; }

	//　上方向取得
	const DirectX::XMFLOAT3* getUp() const { return &up; }

	//　前方向取得
	const DirectX::XMFLOAT3* getFront() const { return &front; }

	//　右方向取得
	const DirectX::XMFLOAT3* getRight() const { return &right; }

	// カメラの位置を取得
	const DirectX::XMFLOAT3* getPosition() const { return &eye; }

	void setViewMatrix(const float* viewMatrix);
	void setProjectionMatrix(const float* projectionMatrix);
private:
	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4	projection;

	float nearZ = 0.1f;
	float farZ = 1000.0f;

	DirectX::XMFLOAT3	eye;
	DirectX::XMFLOAT3	focus;

	DirectX::XMFLOAT3	up;
	DirectX::XMFLOAT3	front;
	DirectX::XMFLOAT3	right;
};