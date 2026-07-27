#include "Camera.h"
#include <cstring>

//　指定方向を向く
void Camera::setLookAt(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& focus,
	const DirectX::XMFLOAT3& up)
{
	// 視点、注視点、上方向からビュー行列を作成
	DirectX::XMVECTOR Eye = DirectX::XMLoadFloat3(&eye);
	DirectX::XMVECTOR Focus = DirectX::XMLoadFloat3(&focus);
	DirectX::XMVECTOR Up = DirectX::XMLoadFloat3(&up);
	DirectX::XMMATRIX View = DirectX::XMMatrixLookAtLH(Eye, Focus, Up);
	DirectX::XMStoreFloat4x4(&view, View);

	// ビューを逆行列化し、ワールド行列に戻す
	DirectX::XMMATRIX World = DirectX::XMMatrixInverse(nullptr, View);
	DirectX::XMFLOAT4X4 world;
	DirectX::XMStoreFloat4x4(&world, World);

	// カメラの方向を取り出す
	this->right.x = world._11;
	this->right.y = world._12;
	this->right.z = world._13;

	this->up.x = world._21;
	this->up.y = world._22;
	this->up.z = world._23;

	this->front.x = world._31;
	this->front.y = world._32;
	this->front.z = world._33;

	// 視点、注視点を保存
	this->eye = eye;
	this->focus = focus;
}

//　パーススペクティブ設定
void Camera::setPerspectiveFov(float fovY, float aspect, float nearZ, float farZ)
{
	//　画角、画面比率、クリップ距離からプロジェクション行列を作成
	DirectX::XMMATRIX Projection = DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ);
	DirectX::XMStoreFloat4x4(&projection, Projection);

	this->nearZ = nearZ;
	this->farZ = farZ;
}


void Camera::setViewMatrix(const float* viewMatrix)
{
	if (!viewMatrix) return;
	std::memcpy(&this->view, viewMatrix, sizeof(DirectX::XMFLOAT4X4));

	
	DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&this->view);
	DirectX::XMMATRIX World = DirectX::XMMatrixInverse(nullptr, V);
	DirectX::XMFLOAT4X4 world;
	DirectX::XMStoreFloat4x4(&world, World);

	this->right.x = world._11; this->right.y = world._12; this->right.z = world._13;
	this->up.x = world._21;    this->up.y = world._22;    this->up.z = world._23;
	this->front.x = world._31; this->front.y = world._32; this->front.z = world._33;
	this->eye.x = world._41;   this->eye.y = world._42;   this->eye.z = world._43;

	
	this->focus.x = this->eye.x + this->front.x;
	this->focus.y = this->eye.y + this->front.y;
	this->focus.z = this->eye.z + this->front.z;
}

void Camera::setProjectionMatrix(const float* projectionMatrix)
{
	if (!projectionMatrix) return;
	std::memcpy(&this->projection, projectionMatrix, sizeof(DirectX::XMFLOAT4X4));


}