#include "GltfModel.h"

#include <stack>
#include <functional>

#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

#include "misc.h"

#include "Graphics/Shader/Shader.h"
#include "Graphics/Texture/Texture.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"

namespace
{
	void UpdateInstanceBuffer(
		ID3D11DeviceContext* dc,
		Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,
		UINT& capacity,
		const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds)
	{
		const UINT required = static_cast<UINT>(instanceWorlds.size());
		if (required == 0 || dc == nullptr)
		{
			return;
		}

		if (!buffer || capacity < required)
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			dc->GetDevice(device.GetAddressOf());

			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * required;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(DirectX::XMFLOAT4X4);

			buffer.Reset();
			srv.Reset();

			HRESULT hr = device->CreateBuffer(&desc, nullptr, buffer.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.NumElements = required;

			hr = device->CreateShaderResourceView(buffer.Get(), &srvDesc, srv.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

			capacity = required;
		}

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(dc->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) && mapped.pData)
		{
			const size_t bytes = sizeof(DirectX::XMFLOAT4X4) * instanceWorlds.size();
			memcpy(mapped.pData, instanceWorlds.data(), bytes);
			dc->Unmap(buffer.Get(), 0);
		}
	}
}

bool null_load_image_data(tinygltf::Image*, const int, std::string*, std::string*, int, int, const unsigned char*, int, void*)
{
	return true;
}

GltfModel::GltfModel(ID3D11Device* device, const std::string& filename, bool static_batching) : filename(filename), static_batching(static_batching)
{
	tinygltf::TinyGLTF tiny_gltf;
	tiny_gltf.SetImageLoader(null_load_image_data, nullptr);

	tinygltf::Model gltf_model;
	std::string error, warning;
	bool succeeded = false;
	if (filename.find(".glb") != std::string::npos)
	{
		succeeded = tiny_gltf.LoadBinaryFromFile(&gltf_model, &error, &warning, filename.c_str());
	}
	else if (filename.find(".gltf") != std::string::npos)
	{
		succeeded = tiny_gltf.LoadASCIIFromFile(&gltf_model, &error, &warning, filename.c_str());
	}

	_ASSERT_EXPR_A(warning.empty(), warning.c_str());
	_ASSERT_EXPR_A(error.empty(), error.c_str());
	_ASSERT_EXPR_A(succeeded, L"Failed to load glTF file");

	for (const tinygltf::Scene& gltf_scene : gltf_model.scenes)
	{
		Scene& scene = scenes.emplace_back();
		scene.name = gltf_scene.name;
		scene.nodes = gltf_scene.nodes;
	}
	default_scene = gltf_model.defaultScene < 0 ? 0 : gltf_model.defaultScene;

	fetchNodes(gltf_model);
	fetchMaterials(device, gltf_model);
	fetchTextures(device, gltf_model);

	if (static_batching)
	{
		fetchAndBatchMeshes(device, gltf_model);
	}
	else
	{
		fetchMeshes(device, gltf_model);
		fetchAnimations(gltf_model);
	}

	createAndUploadResources(device);
}

GltfModel::Node* GltfModel::findNode(const char* name)
{
	// 全てのノードを調べる
	for (Node& node : nodes)
	{
		// ノードの名前が一致した場合、そのノードへのポインタを返す
		if (strcmp(node.name.c_str(), name) == 0)
		{
			return &node;
		}
	}


	return nullptr;
}

void GltfModel::fetchNodes(const tinygltf::Model& gltf_model)
{
	for (const tinygltf::Node& gltf_node : gltf_model.nodes)
	{
		Node& node = nodes.emplace_back();
		node.name = gltf_node.name;
		node.skin = gltf_node.skin;
		node.mesh = gltf_node.mesh;
		node.children = gltf_node.children;
		if (!gltf_node.matrix.empty())
		{
			DirectX::XMFLOAT4X4 matrix;
			for (size_t row = 0; row < 4; row++)
			{
				for (size_t column = 0; column < 4; column++)
				{
					matrix(row, column) = static_cast<float>(gltf_node.matrix.at(4 * row + column));
				}
			}

			DirectX::XMVECTOR S, T, R;
			bool succeed = DirectX::XMMatrixDecompose(&S, &R, &T, DirectX::XMLoadFloat4x4(&matrix));
			_ASSERT_EXPR(succeed, L"Failed to decompose matrix.");

			DirectX::XMStoreFloat3(&node.scale, S);
			DirectX::XMStoreFloat4(&node.rotation, R);
			DirectX::XMStoreFloat3(&node.translation, T);
		}
		else
		{
			if (gltf_node.scale.size() > 0)
			{
				node.scale.x = static_cast<float>(gltf_node.scale.at(0));
				node.scale.y = static_cast<float>(gltf_node.scale.at(1));
				node.scale.z = static_cast<float>(gltf_node.scale.at(2));
			}
			if (gltf_node.translation.size() > 0)
			{
				node.translation.x = static_cast<float>(gltf_node.translation.at(0));
				node.translation.y = static_cast<float>(gltf_node.translation.at(1));
				node.translation.z = static_cast<float>(gltf_node.translation.at(2));
			}
			if (gltf_node.rotation.size() > 0)
			{
				node.rotation.x = static_cast<float>(gltf_node.rotation.at(0));
				node.rotation.y = static_cast<float>(gltf_node.rotation.at(1));
				node.rotation.z = static_cast<float>(gltf_node.rotation.at(2));
				node.rotation.w = static_cast<float>(gltf_node.rotation.at(3));
			}
		}
	}
	cumulateTransforms(nodes);
}
void GltfModel::cumulateTransforms(std::vector<Node>& nodes)
{
	using namespace DirectX;

	std::stack<XMFLOAT4X4> parent_global_transforms;
	std::function<void(int)> traverse{ [&](int node_index)->void
	{
		Node& node{nodes.at(node_index)};
		XMMATRIX S{ XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) };
		XMMATRIX R{ XMMatrixRotationQuaternion(XMVectorSet(node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w)) };
		XMMATRIX T{ XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z) };
		XMStoreFloat4x4(&node.global_transform, S * R * T * XMLoadFloat4x4(&parent_global_transforms.top()));
		for (int child_index : node.children)
		{
			parent_global_transforms.push(node.global_transform);
			traverse(child_index);
			parent_global_transforms.pop();
		}
	} };
	for (std::vector<int>::value_type node_index : scenes.at(0).nodes)
	{
		parent_global_transforms.push({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
		traverse(node_index);
		parent_global_transforms.pop();
	}
}
DXGI_FORMAT _dxgi_format(const tinygltf::Accessor& accessor)
{
	switch (accessor.type)
	{
	case TINYGLTF_TYPE_SCALAR:
		switch (accessor.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			return DXGI_FORMAT_R8_UINT;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			return DXGI_FORMAT_R16_UINT;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			return DXGI_FORMAT_R32_UINT;
		default:
			_ASSERT_EXPR(FALSE, L"This accessor component type is not supported.");
			return DXGI_FORMAT_UNKNOWN;
		}
	case TINYGLTF_TYPE_VEC2:
		switch (accessor.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			return DXGI_FORMAT_R32G32_FLOAT;
		default:
			_ASSERT_EXPR(FALSE, L"This accessor component type is not supported.");
			return DXGI_FORMAT_UNKNOWN;
		}
	case TINYGLTF_TYPE_VEC3:
		switch (accessor.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		default:
			_ASSERT_EXPR(FALSE, L"This accessor component type is not supported.");
			return DXGI_FORMAT_UNKNOWN;
		}
	case TINYGLTF_TYPE_VEC4:
		switch (accessor.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			return DXGI_FORMAT_R8G8B8A8_UINT;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			return DXGI_FORMAT_R16G16B16A16_UINT;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			return DXGI_FORMAT_R32G32B32A32_UINT;
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			_ASSERT_EXPR(FALSE, L"This accessor component type is not supported.");
			return DXGI_FORMAT_UNKNOWN;
		}
		break;
	default:
		_ASSERT_EXPR(FALSE, L"This accessor type is not supported.");
		return DXGI_FORMAT_UNKNOWN;
	}
}

UINT _sizeof_component(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8_UINT: return 1;
	case DXGI_FORMAT_R16_UINT: return 2;
	case DXGI_FORMAT_R32_UINT: return 4;
	case DXGI_FORMAT_R32G32_FLOAT: return 8;
	case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
	case DXGI_FORMAT_R8G8B8A8_UINT: return 4;
	case DXGI_FORMAT_R16G16B16A16_UINT: return 8;
	case DXGI_FORMAT_R32G32B32A32_UINT: return 16;
	case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
	}
	_ASSERT_EXPR(FALSE, L"Not supported");
	return 0;
}

template<class T>
static void _copy(unsigned char* d_data, const size_t d_stride, const unsigned char* s_data, const size_t s_stride, size_t count)
{
	while (count-- > 0)
	{
		*reinterpret_cast<T*>(d_data) = *reinterpret_cast<const T*>(s_data);
		s_data += s_stride;
		d_data += d_stride;
	}
};

void GltfModel::fetchMeshes(ID3D11Device* device, const tinygltf::Model& gltf_model)
{
	for (const tinygltf::Mesh& gltf_mesh : gltf_model.meshes)
	{
		Mesh& mesh = meshes.emplace_back();
		mesh.name = gltf_mesh.name;
		for (const tinygltf::Primitive& gltf_primitive : gltf_mesh.primitives)
		{
			Mesh::Primitive& primitive = mesh.primitives.emplace_back();
			primitive.material = gltf_primitive.material;

			// インデックスバッファビューの作成
			if (gltf_primitive.indices > -1)
			{
				const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(gltf_primitive.indices);
				const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);

				primitive.index_buffer_view.format = _dxgi_format(gltf_accessor);
				primitive.index_buffer_view.size_in_bytes = static_cast<UINT>(gltf_accessor.count) * _sizeof_component(primitive.index_buffer_view.format);
				primitive.cached_indices.resize(primitive.index_buffer_view.size_in_bytes);
				const unsigned char* data = gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset;

				memcpy_s(primitive.cached_indices.data(), primitive.cached_indices.size(), data, primitive.index_buffer_view.size_in_bytes);
			}

			// 頂点バッファビューの作成
			if (gltf_primitive.attributes.size() > 0 && gltf_primitive.attributes.find("POSITION") != gltf_primitive.attributes.end())
			{
				primitive.cached_vertices.resize(gltf_model.accessors.at(gltf_primitive.attributes.at("POSITION")).count);
			}
			else
			{
				continue;
			}


			for (std::map<std::string, int>::const_reference gltf_attribute : gltf_primitive.attributes)
			{
				const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(gltf_attribute.second);
				const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);

				const unsigned char* s_data = gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset;
				const size_t s_stride = gltf_accessor.ByteStride(gltf_buffer_view);
				const size_t d_stride = sizeof(Mesh::Vertex);

				if (gltf_attribute.first == "POSITION")
				{
					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->position);
					_copy<DirectX::XMFLOAT3>(d_data, d_stride, s_data, s_stride, count);
				}
				else if (gltf_attribute.first == "NORMAL")
				{
					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->normal);
					_copy<DirectX::XMFLOAT3>(d_data, d_stride, s_data, s_stride, count);
				}
				else if (gltf_attribute.first == "TANGENT")
				{
					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->tangent);
					_copy<DirectX::XMFLOAT4>(d_data, d_stride, s_data, s_stride, count);
				}
				else if (gltf_attribute.first == "TEXCOORD_0")
				{
					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->texcoord);
					_copy<DirectX::XMFLOAT2>(d_data, d_stride, s_data, s_stride, count);
				}
				else if (gltf_attribute.first == "JOINTS_0")
				{

					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->joints0);
						_copy<DirectX::XMINT4>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						const USHORT* data = reinterpret_cast<const USHORT*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).joints0.x = static_cast<UINT>(data[accessor_index * 4 + 0]);
							primitive.cached_vertices.at(accessor_index).joints0.y = static_cast<UINT>(data[accessor_index * 4 + 1]);
							primitive.cached_vertices.at(accessor_index).joints0.z = static_cast<UINT>(data[accessor_index * 4 + 2]);
							primitive.cached_vertices.at(accessor_index).joints0.w = static_cast<UINT>(data[accessor_index * 4 + 3]);
						}
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						const BYTE* data = reinterpret_cast<const BYTE*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).joints0.x = static_cast<UINT>(data[accessor_index * 4 + 0]);
							primitive.cached_vertices.at(accessor_index).joints0.y = static_cast<UINT>(data[accessor_index * 4 + 1]);
							primitive.cached_vertices.at(accessor_index).joints0.z = static_cast<UINT>(data[accessor_index * 4 + 2]);
							primitive.cached_vertices.at(accessor_index).joints0.w = static_cast<UINT>(data[accessor_index * 4 + 3]);
						}
					}
					else
					{
						_ASSERT_EXPR(FALSE, L"This component type is unsupported, please convert it yourself if necessary.");
					}
				}
				else if (gltf_attribute.first == "JOINTS_1")
				{

					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->joints1);
						_copy<DirectX::XMINT4>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						const USHORT* data = reinterpret_cast<const USHORT*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).joints1.x = static_cast<UINT>(data[accessor_index * 4 + 0]);
							primitive.cached_vertices.at(accessor_index).joints1.y = static_cast<UINT>(data[accessor_index * 4 + 1]);
							primitive.cached_vertices.at(accessor_index).joints1.z = static_cast<UINT>(data[accessor_index * 4 + 2]);
							primitive.cached_vertices.at(accessor_index).joints1.w = static_cast<UINT>(data[accessor_index * 4 + 3]);
						}
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						const BYTE* data = reinterpret_cast<const BYTE*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).joints1.x = static_cast<UINT>(data[accessor_index * 4 + 0]);
							primitive.cached_vertices.at(accessor_index).joints1.y = static_cast<UINT>(data[accessor_index * 4 + 1]);
							primitive.cached_vertices.at(accessor_index).joints1.z = static_cast<UINT>(data[accessor_index * 4 + 2]);
							primitive.cached_vertices.at(accessor_index).joints1.w = static_cast<UINT>(data[accessor_index * 4 + 3]);
						}
					}
					else
					{
						_ASSERT_EXPR(FALSE, L"This component type is unsupported, please convert it yourself if necessary.");
					}
				}
				else if (gltf_attribute.first == "WEIGHTS_0")
				{

					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->weights0);
						_copy<DirectX::XMFLOAT4>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						const USHORT* data = reinterpret_cast<const USHORT*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).weights1.x = static_cast<FLOAT>(data[accessor_index * 4 + 0]) / 0xFFFF;
							primitive.cached_vertices.at(accessor_index).weights1.y = static_cast<FLOAT>(data[accessor_index * 4 + 1]) / 0xFFFF;
							primitive.cached_vertices.at(accessor_index).weights1.z = static_cast<FLOAT>(data[accessor_index * 4 + 2]) / 0xFFFF;
							primitive.cached_vertices.at(accessor_index).weights1.w = static_cast<FLOAT>(data[accessor_index * 4 + 3]) / 0xFFFF;
						}
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						const BYTE* data = reinterpret_cast<const BYTE*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).weights1.x = static_cast<FLOAT>(data[accessor_index * 4 + 0]) / 0xFF;
							primitive.cached_vertices.at(accessor_index).weights1.y = static_cast<FLOAT>(data[accessor_index * 4 + 1]) / 0xFF;
							primitive.cached_vertices.at(accessor_index).weights1.z = static_cast<FLOAT>(data[accessor_index * 4 + 2]) / 0xFF;
							primitive.cached_vertices.at(accessor_index).weights1.w = static_cast<FLOAT>(data[accessor_index * 4 + 3]) / 0xFF;
						}
					}
					else
					{
						_ASSERT_EXPR(FALSE, L"This component type is unsupported, please convert it yourself if necessary.");
					}
				}
				else if (gltf_attribute.first == "WEIGHTS_1")
				{

					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == primitive.cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");

					if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&primitive.cached_vertices.data()->weights1);
						_copy<DirectX::XMFLOAT4>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						const USHORT* data = reinterpret_cast<const USHORT*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).weights1.x = static_cast<FLOAT>(data[accessor_index * 4 + 0]) / 0xFFFF;
							primitive.cached_vertices.at(accessor_index).weights1.y = static_cast<FLOAT>(data[accessor_index * 4 + 1]) / 0xFFFF;
							primitive.cached_vertices.at(accessor_index).weights1.z = static_cast<FLOAT>(data[accessor_index * 4 + 2]) / 0xFFFF;
							primitive.cached_vertices.at(accessor_index).weights1.w = static_cast<FLOAT>(data[accessor_index * 4 + 3]) / 0xFFFF;
						}
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						const BYTE* data = reinterpret_cast<const BYTE*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							primitive.cached_vertices.at(accessor_index).weights1.x = static_cast<FLOAT>(data[accessor_index * 4 + 0]) / 0xFF;
							primitive.cached_vertices.at(accessor_index).weights1.y = static_cast<FLOAT>(data[accessor_index * 4 + 1]) / 0xFF;
							primitive.cached_vertices.at(accessor_index).weights1.z = static_cast<FLOAT>(data[accessor_index * 4 + 2]) / 0xFF;
							primitive.cached_vertices.at(accessor_index).weights1.w = static_cast<FLOAT>(data[accessor_index * 4 + 3]) / 0xFF;
						}
					}
					else
					{
						_ASSERT_EXPR(FALSE, L"This component type is unsupported, please convert it yourself if necessary.");
					}
				}

				primitive.attributes.emplace(gltf_attribute.first, _dxgi_format(gltf_accessor));
			}

			// ウェイトの正規化処理
			for (Mesh::Vertex& vertex : primitive.cached_vertices)
			{

				float total_weight = vertex.weights0.x + vertex.weights0.y + vertex.weights0.z + vertex.weights0.w +
					vertex.weights1.x + vertex.weights1.y + vertex.weights1.z + vertex.weights1.w;


				if (total_weight > 0.0f)
				{
					float inv_total = 1.0f / total_weight;
					vertex.weights0.x *= inv_total;
					vertex.weights0.y *= inv_total;
					vertex.weights0.z *= inv_total;
					vertex.weights0.w *= inv_total;
					vertex.weights1.x *= inv_total;
					vertex.weights1.y *= inv_total;
					vertex.weights1.z *= inv_total;
					vertex.weights1.w *= inv_total;
				}
			}

			primitive.vertex_buffer_view.size_in_bytes = static_cast<UINT>(primitive.cached_vertices.size() * sizeof(Mesh::Vertex));
		}
	}
}



void GltfModel::fetchAndBatchMeshes(ID3D11Device* device, const tinygltf::Model& gltf_model)
{
	batch_meshes.resize(gltf_model.materials.size());

	std::function<void(int)> traverse = [&](int node_index)->void {
		const Node& node = nodes.at(node_index);
		if (node.mesh > -1)
		{
			const DirectX::XMMATRIX global_transform = DirectX::XMLoadFloat4x4(&node.global_transform);

			const tinygltf::Mesh& gltf_mesh = gltf_model.meshes.at(node.mesh);

			for (const tinygltf::Primitive& gltf_primitive : gltf_mesh.primitives)
			{
#if 1
				if (gltf_primitive.material < 0)
				{

					continue;
				}
#endif

				BatchMesh& batch_mesh = batch_meshes.at(gltf_primitive.material);
				batch_mesh.material = gltf_primitive.material;
				batch_mesh.index_buffer_view.format = DXGI_FORMAT_R32_UINT;
				if (gltf_primitive.indices > -1)
				{
					const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(gltf_primitive.indices);
					const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);

					std::vector<UINT> cached_indices(gltf_accessor.count);
					const size_t vertex_offset = batch_mesh.cached_vertices.size();
					if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						const BYTE* data = gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset;
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							cached_indices.at(accessor_index) = static_cast<UINT>(data[accessor_index] + vertex_offset);
						}
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						const USHORT* data = reinterpret_cast<const USHORT*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							cached_indices.at(accessor_index) = static_cast<UINT>(data[accessor_index] + vertex_offset);
						}
					}
					else if (gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
					{
						const UINT* data = reinterpret_cast<const UINT*>(gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset);
						for (size_t accessor_index = 0; accessor_index < gltf_accessor.count; ++accessor_index)
						{
							cached_indices.at(accessor_index) = static_cast<UINT>(data[accessor_index] + vertex_offset);
						}
					}
					else
					{
						_ASSERT_EXPR(false, L"This index format is not supported.");
					}

					batch_mesh.cached_indices.insert(batch_mesh.cached_indices.end(), cached_indices.begin(), cached_indices.end());
					batch_mesh.index_buffer_view.size_in_bytes += static_cast<UINT>(gltf_accessor.count * sizeof(UINT));
				}

				std::vector<BatchMesh::Vertex> cached_vertices;
				if (gltf_primitive.attributes.size() > 0 && gltf_primitive.attributes.find("POSITION") != gltf_primitive.attributes.end())
				{
					cached_vertices.resize(gltf_model.accessors.at(gltf_primitive.attributes.at("POSITION")).count);
				}
				else
				{
					continue;
				}

				for (std::map<std::string, int>::const_reference gltf_attribute : gltf_primitive.attributes)
				{
					const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(gltf_attribute.second);
					const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);

					const unsigned char* s_data = gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset;
					const size_t s_stride = gltf_accessor.ByteStride(gltf_buffer_view);
					const size_t d_stride = sizeof(BatchMesh::Vertex);
					const size_t count = gltf_accessor.count;
					_ASSERT_EXPR(count == cached_vertices.size(), L"The number of components on all vertices comprising the mesh must be the same.");
					if (gltf_attribute.first == "POSITION")
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&cached_vertices.data()->position);
						_copy<DirectX::XMFLOAT3>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_attribute.first == "NORMAL")
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&cached_vertices.data()->normal);
						_copy<DirectX::XMFLOAT3>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_attribute.first == "TANGENT")
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&cached_vertices.data()->tangent);
						_copy<DirectX::XMFLOAT4>(d_data, d_stride, s_data, s_stride, count);
					}
					else if (gltf_attribute.first == "TEXCOORD_0")
					{
						unsigned char* d_data = reinterpret_cast<unsigned char*>(&cached_vertices.data()->texcoord);
						_copy<DirectX::XMFLOAT2>(d_data, d_stride, s_data, s_stride, count);
					}
					else
					{
						_ASSERT_EXPR(FALSE, L"This attribute is unsupported.");
					}
					batch_mesh.attributes.emplace(gltf_attribute.first, _dxgi_format(gltf_accessor));
				}


				for (BatchMesh::Vertex& cached_vertex : cached_vertices)
				{
					DirectX::XMStoreFloat3(&cached_vertex.position, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&cached_vertex.position), global_transform));
					DirectX::XMStoreFloat3(&cached_vertex.normal, DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&cached_vertex.normal), global_transform)));
					float sigma = cached_vertex.tangent.w;
					cached_vertex.tangent.w = 0;
					DirectX::XMStoreFloat4(&cached_vertex.tangent, DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat4(&cached_vertex.tangent), global_transform)));
					cached_vertex.tangent.w = sigma;
				}

				batch_mesh.cached_vertices.insert(batch_mesh.cached_vertices.end(), cached_vertices.begin(), cached_vertices.end());
				batch_mesh.vertex_buffer_view.size_in_bytes += static_cast<UINT>(cached_vertices.size() * sizeof(BatchMesh::Vertex));
			}
		}
		for (std::vector<int>::value_type child_index : node.children)
		{
			traverse(child_index);
		}
		};
	for (std::vector<int>::value_type node_index : scenes.at(default_scene).nodes)
	{
		traverse(node_index);
	}
}

void GltfModel::fetchMaterials(ID3D11Device* device, const tinygltf::Model& gltf_model)
{
	for (const tinygltf::Material& gltf_material : gltf_model.materials)
	{
		std::vector<Material>::reference material = materials.emplace_back();

		material.name = gltf_material.name;

		material.data.emissive_factor[0] = static_cast<float>(gltf_material.emissiveFactor.at(0));
		material.data.emissive_factor[1] = static_cast<float>(gltf_material.emissiveFactor.at(1));
		material.data.emissive_factor[2] = static_cast<float>(gltf_material.emissiveFactor.at(2));

		material.data.alpha_mode = gltf_material.alphaMode == "OPAQUE" ? 0 : gltf_material.alphaMode == "MASK" ? 1 : gltf_material.alphaMode == "BLEND" ? 2 : 0;
		material.data.alpha_cutoff = static_cast<float>(gltf_material.alphaCutoff);
		material.data.double_sided = gltf_material.doubleSided ? 1 : 0;

		material.data.pbr_metallic_roughness.basecolor_factor[0] = static_cast<float>(gltf_material.pbrMetallicRoughness.baseColorFactor.at(0));
		material.data.pbr_metallic_roughness.basecolor_factor[1] = static_cast<float>(gltf_material.pbrMetallicRoughness.baseColorFactor.at(1));
		material.data.pbr_metallic_roughness.basecolor_factor[2] = static_cast<float>(gltf_material.pbrMetallicRoughness.baseColorFactor.at(2));
		material.data.pbr_metallic_roughness.basecolor_factor[3] = static_cast<float>(gltf_material.pbrMetallicRoughness.baseColorFactor.at(3));
		material.data.pbr_metallic_roughness.basecolor_texture.index = gltf_material.pbrMetallicRoughness.baseColorTexture.index;
		material.data.pbr_metallic_roughness.basecolor_texture.texcoord = gltf_material.pbrMetallicRoughness.baseColorTexture.texCoord;
		material.data.pbr_metallic_roughness.metallic_factor = static_cast<float>(gltf_material.pbrMetallicRoughness.metallicFactor);
		material.data.pbr_metallic_roughness.roughness_factor = static_cast<float>(gltf_material.pbrMetallicRoughness.roughnessFactor);
		material.data.pbr_metallic_roughness.metallic_roughness_texture.index = gltf_material.pbrMetallicRoughness.metallicRoughnessTexture.index;
		material.data.pbr_metallic_roughness.metallic_roughness_texture.texcoord = gltf_material.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;

		material.data.normal_texture.index = gltf_material.normalTexture.index;
		material.data.normal_texture.texcoord = gltf_material.normalTexture.texCoord;
		material.data.normal_texture.scale = static_cast<float>(gltf_material.normalTexture.scale);

		material.data.occlusion_texture.index = gltf_material.occlusionTexture.index;
		material.data.occlusion_texture.texcoord = gltf_material.occlusionTexture.texCoord;
		material.data.occlusion_texture.strength = static_cast<float>(gltf_material.occlusionTexture.strength);

		material.data.emissive_texture.index = gltf_material.emissiveTexture.index;
		material.data.emissive_texture.texcoord = gltf_material.emissiveTexture.texCoord;
	}
}
void GltfModel::fetchTextures(ID3D11Device* device, const tinygltf::Model& gltf_model)
{
	for (const tinygltf::Texture& gltf_texture : gltf_model.textures)
	{
		Texture& texture = textures.emplace_back();
		texture.name = gltf_texture.name;
		texture.source = gltf_texture.source;
	}
	for (const tinygltf::Image& gltf_image : gltf_model.images)
	{
		Image& image = images.emplace_back();
		image.name = gltf_image.name;
		image.width = gltf_image.width;
		image.height = gltf_image.height;
		image.component = gltf_image.component;
		image.bits = gltf_image.bits;
		image.pixel_type = gltf_image.pixel_type;
		image.mime_type = gltf_image.mimeType;
		image.uri = gltf_image.uri;
		image.as_is = gltf_image.as_is;

		if (gltf_image.bufferView > -1)
		{
			const tinygltf::BufferView& buffer_view = gltf_model.bufferViews.at(gltf_image.bufferView);
			const tinygltf::Buffer& buffer = gltf_model.buffers.at(buffer_view.buffer);
			const unsigned char* data = buffer.data.data() + buffer_view.byteOffset;
			image.cache_data.resize(buffer_view.byteLength);
			memcpy_s(image.cache_data.data(), image.cache_data.size(), data, buffer_view.byteLength);
		}
	}

}

void GltfModel::fetchAnimations(const tinygltf::Model& gltf_model)
{
	for (const tinygltf::Skin& transmission_skin : gltf_model.skins)
	{
		Skin& skin = skins.emplace_back();
		const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(transmission_skin.inverseBindMatrices);
		const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);
		_ASSERT_EXPR(gltf_accessor.type == TINYGLTF_TYPE_MAT4, L"");

		skin.inverse_bind_matrices.resize(gltf_accessor.count);
		memcpy(skin.inverse_bind_matrices.data(), gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset, gltf_accessor.count * sizeof(DirectX::XMFLOAT4X4));

		skin.joints = transmission_skin.joints;
	}

	for (const tinygltf::Animation& gltf_animation : gltf_model.animations)
	{
		Animation& animation = animations.emplace_back();
		animation.name = gltf_animation.name;
		for (const tinygltf::AnimationSampler& gltf_sampler : gltf_animation.samplers)
		{
			Animation::Sampler& sampler = animation.samplers.emplace_back();
			sampler.input = gltf_sampler.input;
			sampler.output = gltf_sampler.output;
			sampler.interpolation = gltf_sampler.interpolation;

			const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(gltf_sampler.input);
			const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);
			_ASSERT_EXPR(gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, L"");
			_ASSERT_EXPR(gltf_accessor.type == TINYGLTF_TYPE_SCALAR, L"");
			const std::pair<std::unordered_map<int, std::vector<float>>::iterator, bool>& timelines = animation.timelines.emplace(gltf_sampler.input, gltf_accessor.count);
			if (timelines.second)
			{
				memcpy(timelines.first->second.data(), gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset, gltf_accessor.count * sizeof(FLOAT));
			}
		}
		for (const tinygltf::AnimationChannel& gltf_channel : gltf_animation.channels)
		{
			Animation::Channel& channel = animation.channels.emplace_back();
			channel.sampler = gltf_channel.sampler;
			channel.target_node = gltf_channel.target_node;
			channel.target_path = gltf_channel.target_path;

			const tinygltf::AnimationSampler& gltf_sampler = gltf_animation.samplers.at(gltf_channel.sampler);
			const tinygltf::Accessor& gltf_accessor = gltf_model.accessors.at(gltf_sampler.output);
			const tinygltf::BufferView& gltf_buffer_view = gltf_model.bufferViews.at(gltf_accessor.bufferView);
			if (gltf_channel.target_path == "scale")
			{
				_ASSERT_EXPR(gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, L"");
				_ASSERT_EXPR(gltf_accessor.type == TINYGLTF_TYPE_VEC3, L"");

				const std::pair<std::unordered_map<int, std::vector<DirectX::XMFLOAT3>>::iterator, bool>& scales = animation.scales.emplace(gltf_sampler.output, gltf_accessor.count);
				if (scales.second)
				{
					memcpy(scales.first->second.data(), gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset, gltf_accessor.count * sizeof(DirectX::XMFLOAT3));
				}
			}
			else if (gltf_channel.target_path == "rotation")
			{
				_ASSERT_EXPR(gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, L"");
				_ASSERT_EXPR(gltf_accessor.type == TINYGLTF_TYPE_VEC4, L"");

				const std::pair<std::unordered_map<int, std::vector<DirectX::XMFLOAT4>>::iterator, bool>& rotations = animation.rotations.emplace(gltf_sampler.output, gltf_accessor.count);
				if (rotations.second)
				{
					memcpy(rotations.first->second.data(), gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset, gltf_accessor.count * sizeof(DirectX::XMFLOAT4));
				}
			}
			else if (gltf_channel.target_path == "translation")
			{
				_ASSERT_EXPR(gltf_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, L"");
				_ASSERT_EXPR(gltf_accessor.type == TINYGLTF_TYPE_VEC3, L"");
				const std::pair<std::unordered_map<int, std::vector<DirectX::XMFLOAT3>>::iterator, bool>& translations = animation.translations.emplace(gltf_sampler.output, gltf_accessor.count);
				if (translations.second)
				{
					memcpy(translations.first->second.data(), gltf_model.buffers.at(gltf_buffer_view.buffer).data.data() + gltf_buffer_view.byteOffset + gltf_accessor.byteOffset, gltf_accessor.count * sizeof(DirectX::XMFLOAT3));
				}
			}
			else if (gltf_channel.target_path == "weights")
			{
				_ASSERT_EXPR(FALSE, L"");
			}
			else
			{
				_ASSERT_EXPR(FALSE, L"");
			}
		}
	}

	for (decltype(animations)::reference animation : animations)
	{

		for (decltype(animation.timelines)::reference timelines : animation.timelines)
		{
			animation.duration = std::max<float>(animation.duration, timelines.second.back());
		}
	}

}

void GltfModel::animate(size_t animation_index, float time, std::vector<Node>& animated_nodes)
{
	using namespace std;
	using namespace DirectX;

	_ASSERT_EXPR(animations.size() > 0, L"");
	_ASSERT_EXPR(animations.size() > animation_index, L"");
	_ASSERT_EXPR(animated_nodes.size() == nodes.size(), L"");

	function<size_t(const vector<float>&, float, float&)> indexof{ [](const vector<float>& timelines, float time, float& interpolation_factor)->size_t {
		const size_t keyframe_count{ timelines.size() };
		if (time > timelines.at(keyframe_count - 1))
		{
			interpolation_factor = 1.0f;
			return keyframe_count - 2;
		}
		else if (time < timelines.at(0))
		{
			interpolation_factor = timelines.at(0);
			return 0;
		}
		size_t keyframe_index{ 0 };
		for (size_t time_index = 1; time_index < keyframe_count; ++time_index)
		{
			if (time < timelines.at(time_index))
			{
				keyframe_index = max<size_t>(0LL, time_index - 1);
				break;
			}
		}
		interpolation_factor = (time - timelines.at(keyframe_index + 0)) / (timelines.at(keyframe_index + 1) - timelines.at(keyframe_index + 0));
		return keyframe_index;
	} };

	if (animations.size() > 0)
	{
		const Animation& animation{ animations.at(animation_index) };
		for (vector<Animation::Channel>::const_reference channel : animation.channels)
		{
			const Animation::Sampler& sampler{ animation.samplers.at(channel.sampler) };
			const vector<float>& timeline{ animation.timelines.at(sampler.input) };
			if (timeline.size() <= 1)
			{
				continue;
			}
			float interpolation_factor{};
			size_t keyframe_index{ indexof(timeline, time, interpolation_factor) };
			if (channel.target_path == "scale")
			{
				const vector<XMFLOAT3>& scales{ animation.scales.at(sampler.output) };
				if (scales.size() > 1)
				{
					XMStoreFloat3(&animated_nodes.at(channel.target_node).scale, XMVectorLerp(XMLoadFloat3(&scales.at(keyframe_index + 0)), XMLoadFloat3(&scales.at(keyframe_index + 1)), interpolation_factor));
				}
			}
			else if (channel.target_path == "rotation")
			{
				const vector<XMFLOAT4>& rotations{ animation.rotations.at(sampler.output) };
				if (rotations.size() > 1)
				{
					XMStoreFloat4(&animated_nodes.at(channel.target_node).rotation, XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&rotations.at(keyframe_index + 0)), XMLoadFloat4(&rotations.at(keyframe_index + 1)), interpolation_factor)));
				}

			}
			else if (channel.target_path == "translation")
			{
				const vector<XMFLOAT3>& translations{ animation.translations.at(sampler.output) };
				if (translations.size() > 1)
				{
					XMStoreFloat3(&animated_nodes.at(channel.target_node).translation, XMVectorLerp(XMLoadFloat3(&translations.at(keyframe_index + 0)), XMLoadFloat3(&translations.at(keyframe_index + 1)), interpolation_factor));
				}
			}
			else if (channel.target_path == "weights")
			{
			}
		}
		cumulateTransforms(animated_nodes);
	}
}


void GltfModel::appendAnimation(ID3D11Device* device, const std::string& filename)
{
	tinygltf::TinyGLTF tiny_gltf;
	tiny_gltf.SetImageLoader(null_load_image_data, nullptr);

	tinygltf::Model gltf_model;
	std::string error, warning;
	bool succeeded{ false };
	if (filename.find(".glb") != std::string::npos)
	{
		succeeded = tiny_gltf.LoadBinaryFromFile(&gltf_model, &error, &warning, filename.c_str());
	}
	else if (filename.find(".gltf") != std::string::npos)
	{
		succeeded = tiny_gltf.LoadASCIIFromFile(&gltf_model, &error, &warning, filename.c_str());
	}

	_ASSERT_EXPR_A(warning.empty(), warning.c_str());
	_ASSERT_EXPR_A(error.empty(), error.c_str());
	_ASSERT_EXPR_A(succeeded, L"Failed to load glTF file");

	for (std::vector<tinygltf::Scene>::const_reference gltf_scene : gltf_model.scenes)
	{
		Scene& scene{ scenes.emplace_back() };
		scene.name = gltf_scene.name;
		scene.nodes = gltf_scene.nodes;
	}
	fetchAnimations(gltf_model);
}





void GltfModel::createAndUploadResources(ID3D11Device* device)
{
	HRESULT hr;
	D3D11_BUFFER_DESC buffer_desc = {};
	D3D11_SUBRESOURCE_DATA subresource_data = {};


	if (static_batching)
	{
		for (BatchMesh& batch_meshe : batch_meshes)
		{
			if (batch_meshe.index_buffer_view.size_in_bytes > 0)
			{
				batch_meshe.index_buffer_view.buffer = static_cast<int>(buffers.size());
				buffer_desc.ByteWidth = batch_meshe.index_buffer_view.size_in_bytes;
				buffer_desc.Usage = D3D11_USAGE_DEFAULT;
				buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
				buffer_desc.CPUAccessFlags = 0;
				buffer_desc.MiscFlags = 0;
				buffer_desc.StructureByteStride = 0;
				subresource_data.pSysMem = batch_meshe.cached_indices.data();
				subresource_data.SysMemPitch = 0;
				subresource_data.SysMemSlicePitch = 0;
				hr = device->CreateBuffer(&buffer_desc, &subresource_data, buffers.emplace_back().GetAddressOf());
				_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

				batch_meshe.cached_indices.clear();
			}

			if (batch_meshe.vertex_buffer_view.size_in_bytes > 0)
			{
				batch_meshe.vertex_buffer_view.buffer = static_cast<int>(buffers.size());
				buffer_desc.ByteWidth = batch_meshe.vertex_buffer_view.size_in_bytes;
				buffer_desc.Usage = D3D11_USAGE_DEFAULT;
				buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				buffer_desc.CPUAccessFlags = 0;
				buffer_desc.MiscFlags = 0;
				buffer_desc.StructureByteStride = 0;
				subresource_data.pSysMem = batch_meshe.cached_vertices.data();
				subresource_data.SysMemPitch = 0;
				subresource_data.SysMemSlicePitch = 0;
				hr = device->CreateBuffer(&buffer_desc, &subresource_data, buffers.emplace_back().GetAddressOf());
				_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

				batch_meshe.cached_vertices.clear();
			}
		}
	}
	else
	{
		for (Mesh& mesh : meshes)
		{
			for (Mesh::Primitive& primitive : mesh.primitives)
			{
				if (primitive.index_buffer_view.size_in_bytes > 0)
				{
					primitive.index_buffer_view.buffer = static_cast<int>(buffers.size());
					buffer_desc.ByteWidth = primitive.index_buffer_view.size_in_bytes;
					buffer_desc.Usage = D3D11_USAGE_DEFAULT;
					buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
					buffer_desc.CPUAccessFlags = 0;
					buffer_desc.MiscFlags = 0;
					buffer_desc.StructureByteStride = 0;
					subresource_data.pSysMem = primitive.cached_indices.data();
					subresource_data.SysMemPitch = 0;
					subresource_data.SysMemSlicePitch = 0;
					hr = device->CreateBuffer(&buffer_desc, &subresource_data, buffers.emplace_back().GetAddressOf());
					_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

					primitive.cached_indices.clear();
				}

				if (primitive.vertex_buffer_view.size_in_bytes > 0)
				{
					primitive.vertex_buffer_view.buffer = static_cast<int>(buffers.size());
					buffer_desc.ByteWidth = primitive.vertex_buffer_view.size_in_bytes;
					buffer_desc.Usage = D3D11_USAGE_DEFAULT;
					buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
					buffer_desc.CPUAccessFlags = 0;
					buffer_desc.MiscFlags = 0;
					buffer_desc.StructureByteStride = 0;
					subresource_data.pSysMem = primitive.cached_vertices.data();
					subresource_data.SysMemPitch = 0;
					subresource_data.SysMemSlicePitch = 0;
					hr = device->CreateBuffer(&buffer_desc, &subresource_data, buffers.emplace_back().GetAddressOf());
					_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

					primitive.cached_vertices.clear();
				}
			}
		}
	}


	std::vector<Material::Cbuffer> material_data;
	for (const Material& material : materials)
	{
		material_data.emplace_back(material.data);
	}
	Microsoft::WRL::ComPtr<ID3D11Buffer> material_buffer;
	buffer_desc.ByteWidth = static_cast<UINT>(sizeof(Material::Cbuffer) * material_data.size());
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	buffer_desc.StructureByteStride = sizeof(Material::Cbuffer);
	subresource_data.pSysMem = material_data.data();
	subresource_data.SysMemPitch = 0;
	subresource_data.SysMemSlicePitch = 0;
	hr = device->CreateBuffer(&buffer_desc, &subresource_data, material_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = {};
	shader_resource_view_desc.Format = DXGI_FORMAT_UNKNOWN;
	shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	shader_resource_view_desc.Buffer.NumElements = static_cast<UINT>(material_data.size());
	hr = device->CreateShaderResourceView(material_buffer.Get(), &shader_resource_view_desc, material_resource_view.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


	for (Image& image : images)
	{
		if (image.cache_data.size() > 0)
		{
			ID3D11ShaderResourceView* texture_resource_view = NULL;
			hr = TextureManager::instance()->loadTextureFromMemory(device, image.cache_data.data(), image.cache_data.size(), &texture_resource_view);
			if (hr == S_OK)
			{
				texture_resource_views.emplace_back().Attach(texture_resource_view);
			}
			image.cache_data.clear();
		}
		else
		{
			const std::filesystem::path path(filename);
			ID3D11ShaderResourceView* shader_resource_view = NULL;
			std::wstring filename{ path.parent_path().concat(L"/").wstring() + std::wstring(image.uri.begin(), image.uri.end()) };
			hr = TextureManager::instance()->loadTextureFromFile(device, filename.c_str(), &shader_resource_view, NULL);
			if (hr == S_OK)
			{
				texture_resource_views.emplace_back().Attach(shader_resource_view);
			}
		}
	}


	hr = TextureManager::instance()->makeDummyTexture(device, dummy_white_texture.GetAddressOf(), 0xFFFFFFFF, 1);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


	hr = TextureManager::instance()->makeDummyTexture(device, dummy_normal_texture.GetAddressOf(), 0xFFFF8080, 1);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	if (static_batching)
	{
		D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		// 通常の描画用VSを読み込み（input_layoutもここで作成）
		ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Gltf_Model_static_Batching_VS.cso", vertex_shader.ReleaseAndGetAddressOf(), input_layout.ReleaseAndGetAddressOf(), input_element_desc, _countof(input_element_desc));


		Microsoft::WRL::ComPtr<ID3D11InputLayout> dummy_layout;
		ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\ShadowMapVS.cso", shadow_vertex_shader.ReleaseAndGetAddressOf(), dummy_layout.ReleaseAndGetAddressOf(), input_element_desc, _countof(input_element_desc));
	}
	else
	{
		D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "JOINTS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "JOINTS", 1, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "WEIGHTS", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};

		// 通常の描画用VSを読み込み
		ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Gltf_Model_VS.cso", vertex_shader.ReleaseAndGetAddressOf(), input_layout.ReleaseAndGetAddressOf(), input_element_desc, _countof(input_element_desc));


		Microsoft::WRL::ComPtr<ID3D11InputLayout> dummy_layout;
		ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\ShadowMapVS.cso", shadow_vertex_shader.ReleaseAndGetAddressOf(), dummy_layout.ReleaseAndGetAddressOf(), input_element_desc, _countof(input_element_desc));

		Microsoft::WRL::ComPtr<ID3D11InputLayout> instanced_layout;
		ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Gltf_Model_Instanced_VS.cso", vertex_shader_instanced.ReleaseAndGetAddressOf(), instanced_layout.ReleaseAndGetAddressOf(), input_element_desc, _countof(input_element_desc));

		Microsoft::WRL::ComPtr<ID3D11InputLayout> shadow_instanced_layout;
		ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\ShadowMap_Instanced_VS.cso", shadow_vertex_shader_instanced.ReleaseAndGetAddressOf(), shadow_instanced_layout.ReleaseAndGetAddressOf(), input_element_desc, _countof(input_element_desc));
	}
	ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Gltf_Model_PS.cso", pixel_shader.ReleaseAndGetAddressOf());

	buffer_desc.ByteWidth = sizeof(Primitive_Constants);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.MiscFlags = 0;
	buffer_desc.StructureByteStride = 0;
	hr = device->CreateBuffer(&buffer_desc, nullptr, primitive_cbuffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	buffer_desc.ByteWidth = sizeof(Primitive_Joint_Constants);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.MiscFlags = 0;
	buffer_desc.StructureByteStride = 0;
	hr = device->CreateBuffer(&buffer_desc, NULL, primitive_joint_cbuffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	
}

void GltfModel::render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& world, const std::vector<Node>& animated_nodes)
{

	if (static_batching)
	{
		return batchRender(dc, world);
	}

	const std::vector<Node>& nodes = animated_nodes.size() > 0 ? animated_nodes : GltfModel::nodes;

	dc->PSSetShaderResources(11, 1, material_resource_view.GetAddressOf());

	dc->VSSetShader(vertex_shader.Get(), nullptr, 0);
	dc->PSSetShader(pixel_shader.Get(), nullptr, 0);
	dc->IASetInputLayout(input_layout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	

	std::function<void(int)> traverse = [&](int node_index)->void {
		const Node& node = nodes.at(node_index);
		if (node.skin > -1)
		{
			const Skin& skin = skins.at(node.skin);
			_ASSERT_EXPR(skin.joints.size() <= PRIMITIVE_MAX_JOINTS, L"The size of the joint array is insufficient, please expand it.");
			Primitive_Joint_Constants primitive_joint_data{};
			for (size_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index)
			{
				DirectX::XMStoreFloat4x4(&primitive_joint_data.matrices[joint_index],
					DirectX::XMLoadFloat4x4(&skin.inverse_bind_matrices.at(joint_index)) *
					DirectX::XMLoadFloat4x4(&nodes.at(skin.joints.at(joint_index)).global_transform) *
					DirectX::XMMatrixInverse(NULL, DirectX::XMLoadFloat4x4(&node.global_transform))
				);
			}
			dc->UpdateSubresource(primitive_joint_cbuffer.Get(), 0, 0, &primitive_joint_data, 0, 0);
			dc->VSSetConstantBuffers(3, 1, primitive_joint_cbuffer.GetAddressOf());
		}
		if (node.mesh > -1)
		{
			const Mesh& mesh = meshes.at(node.mesh);
			for (const Mesh::Primitive& primitive : mesh.primitives)
			{

				UINT stride = sizeof(Mesh::Vertex);
				UINT offset = 0;
				dc->IASetVertexBuffers(0, 1, buffers.at(primitive.vertex_buffer_view.buffer).GetAddressOf(), &stride, &offset);

				Primitive_Constants primitive_data = {};
				primitive_data.material = primitive.material;
				primitive_data.has_tangent = primitive.has("TANGENT");
				primitive_data.skin = node.skin;
				DirectX::XMStoreFloat4x4(&primitive_data.world, DirectX::XMLoadFloat4x4(&node.global_transform) * DirectX::XMLoadFloat4x4(&world));
				dc->UpdateSubresource(primitive_cbuffer.Get(), 0, 0, &primitive_data, 0, 0);
				dc->VSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());
				dc->PSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());

				const Material& material = materials.at(primitive.material);
				const int texture_indices[] =
				{
					material.data.pbr_metallic_roughness.basecolor_texture.index,
					material.data.pbr_metallic_roughness.metallic_roughness_texture.index,
					material.data.normal_texture.index,
					material.data.emissive_texture.index,
					material.data.occlusion_texture.index,
				};
				ID3D11ShaderResourceView* null_shader_resource_view = {};
				ID3D11ShaderResourceView* shader_resource_views[] =
				{
					// BaseColor 
					material.data.pbr_metallic_roughness.basecolor_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.pbr_metallic_roughness.basecolor_texture.index).source).Get()
						: dummy_white_texture.Get(),
					// MetallicRoughness 
					material.data.pbr_metallic_roughness.metallic_roughness_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.pbr_metallic_roughness.metallic_roughness_texture.index).source).Get()
						: dummy_white_texture.Get(),
					// Normal 
					material.data.normal_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.normal_texture.index).source).Get()
						: dummy_normal_texture.Get(),
					// Emissive 
					material.data.emissive_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.emissive_texture.index).source).Get()
						: dummy_white_texture.Get(),
					// Occlusion 
					material.data.occlusion_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.occlusion_texture.index).source).Get()
						: dummy_white_texture.Get()
				};
				dc->PSSetShaderResources(2, _countof(shader_resource_views), shader_resource_views);

				if (primitive.index_buffer_view.buffer > -1)
				{

					dc->IASetIndexBuffer(buffers.at(primitive.index_buffer_view.buffer).Get(), primitive.index_buffer_view.format, 0);
					dc->DrawIndexed(primitive.index_buffer_view.size_in_bytes / _sizeof_component(primitive.index_buffer_view.format), 0, 0);
				}
				else
				{

					dc->Draw(primitive.vertex_buffer_view.size_in_bytes / primitive.vertex_buffer_view.stride_in_bytes, 0);
				}
			}
		}
		for (std::vector<int>::value_type child_index : node.children)
		{
			traverse(child_index);
		}
		};
	for (std::vector<int>::value_type node_index : scenes.at(default_scene).nodes)
	{
		traverse(node_index);
	}

	{
		ID3D11ShaderResourceView* nullSrv = nullptr;
		dc->VSSetShaderResources(16, 1, &nullSrv);
	}
}

void GltfModel::renderInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds, const std::vector<Node>& animated_nodes)
{
	if (static_batching || instanceWorlds.empty() || !vertex_shader_instanced)
	{
		return;
	}

	UpdateInstanceBuffer(dc, instance_buffer, instance_srv, instance_buffer_capacity, instanceWorlds);

	ID3D11ShaderResourceView* instanceSrv = instance_srv.Get();
	dc->VSSetShaderResources(17, 1, &instanceSrv);

	const std::vector<Node>& nodes = animated_nodes.size() > 0 ? animated_nodes : GltfModel::nodes;

	dc->PSSetShaderResources(11, 1, material_resource_view.GetAddressOf());

	dc->VSSetShader(vertex_shader_instanced.Get(), nullptr, 0);
	dc->PSSetShader(pixel_shader.Get(), nullptr, 0);
	dc->IASetInputLayout(input_layout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT instanceCount = static_cast<UINT>(instanceWorlds.size());

	std::function<void(int)> traverse = [&](int node_index)->void {
		const Node& node = nodes.at(node_index);
		if (node.skin > -1)
		{
			const Skin& skin = skins.at(node.skin);
			_ASSERT_EXPR(skin.joints.size() <= PRIMITIVE_MAX_JOINTS, L"The size of the joint array is insufficient, please expand it.");
			Primitive_Joint_Constants primitive_joint_data{};
			for (size_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index)
			{
				DirectX::XMStoreFloat4x4(&primitive_joint_data.matrices[joint_index],
					DirectX::XMLoadFloat4x4(&skin.inverse_bind_matrices.at(joint_index)) *
					DirectX::XMLoadFloat4x4(&nodes.at(skin.joints.at(joint_index)).global_transform) *
					DirectX::XMMatrixInverse(NULL, DirectX::XMLoadFloat4x4(&node.global_transform))
				);
			}
			dc->UpdateSubresource(primitive_joint_cbuffer.Get(), 0, 0, &primitive_joint_data, 0, 0);
			dc->VSSetConstantBuffers(3, 1, primitive_joint_cbuffer.GetAddressOf());
		}
		if (node.mesh > -1)
		{
			const Mesh& mesh = meshes.at(node.mesh);
			for (const Mesh::Primitive& primitive : mesh.primitives)
			{
				UINT stride = sizeof(Mesh::Vertex);
				UINT offset = 0;
				dc->IASetVertexBuffers(0, 1, buffers.at(primitive.vertex_buffer_view.buffer).GetAddressOf(), &stride, &offset);

				Primitive_Constants primitive_data = {};
				primitive_data.material = primitive.material;
				primitive_data.has_tangent = primitive.has("TANGENT");
				primitive_data.skin = node.skin;
				DirectX::XMStoreFloat4x4(&primitive_data.world, DirectX::XMLoadFloat4x4(&node.global_transform));
				dc->UpdateSubresource(primitive_cbuffer.Get(), 0, 0, &primitive_data, 0, 0);
				dc->VSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());
				dc->PSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());

				const Material& material = materials.at(primitive.material);
				const int texture_indices[] =
				{
					material.data.pbr_metallic_roughness.basecolor_texture.index,
					material.data.pbr_metallic_roughness.metallic_roughness_texture.index,
					material.data.normal_texture.index,
					material.data.emissive_texture.index,
					material.data.occlusion_texture.index,
				};
				ID3D11ShaderResourceView* null_shader_resource_view = {};
				ID3D11ShaderResourceView* shader_resource_views[] =
				{
					material.data.pbr_metallic_roughness.basecolor_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.pbr_metallic_roughness.basecolor_texture.index).source).Get()
						: dummy_white_texture.Get(),
					material.data.pbr_metallic_roughness.metallic_roughness_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.pbr_metallic_roughness.metallic_roughness_texture.index).source).Get()
						: dummy_white_texture.Get(),
					material.data.normal_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.normal_texture.index).source).Get()
						: dummy_normal_texture.Get(),
					material.data.emissive_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.emissive_texture.index).source).Get()
						: dummy_white_texture.Get(),
					material.data.occlusion_texture.index > -1
						? texture_resource_views.at(textures.at(material.data.occlusion_texture.index).source).Get()
						: dummy_white_texture.Get()
				};
				dc->PSSetShaderResources(2, _countof(shader_resource_views), shader_resource_views);

				if (primitive.index_buffer_view.buffer > -1)
				{
					dc->IASetIndexBuffer(buffers.at(primitive.index_buffer_view.buffer).Get(), primitive.index_buffer_view.format, 0);
					dc->DrawIndexedInstanced(
						primitive.index_buffer_view.size_in_bytes / _sizeof_component(primitive.index_buffer_view.format),
						instanceCount, 0, 0, 0);
				}
				else
				{
					dc->DrawInstanced(primitive.vertex_buffer_view.size_in_bytes / primitive.vertex_buffer_view.stride_in_bytes, instanceCount, 0, 0);
				}
			}
		}
		for (std::vector<int>::value_type child_index : node.children)
		{
			traverse(child_index);
		}
		};
	for (std::vector<int>::value_type node_index : scenes.at(default_scene).nodes)
	{
		traverse(node_index);
	}

	{
		ID3D11ShaderResourceView* nullSrv = nullptr;
		dc->VSSetShaderResources(16, 1, &nullSrv);
		dc->VSSetShaderResources(17, 1, &nullSrv);
	}
}


void GltfModel::renderShadow(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& world, const std::vector<Node>& animated_nodes)
{
	

	if (static_batching)
	{

		dc->VSSetShader(shadow_vertex_shader.Get(), nullptr, 0);
		dc->PSSetShader(nullptr, nullptr, 0);
		dc->IASetInputLayout(input_layout.Get());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		for (const BatchMesh& bm : batch_meshes)
		{

			UINT stride = sizeof(BatchMesh::Vertex);
			UINT offset = 0;
			dc->IASetVertexBuffers(
				0, 1,
				buffers.at(bm.vertex_buffer_view.buffer).GetAddressOf(),
				&stride, &offset);


			Primitive_Constants pc{};
			pc.material = bm.material;
			pc.has_tangent = bm.has("TANGENT") ? 1 : 0;
			pc.skin = -1;
			pc.world = world;
			dc->UpdateSubresource(primitive_cbuffer.Get(), 0, nullptr, &pc, 0, 0);
			dc->VSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());

			// 描画
			if (bm.index_buffer_view.buffer > -1)
			{
				dc->IASetIndexBuffer(
					buffers.at(bm.index_buffer_view.buffer).Get(),
					bm.index_buffer_view.format, 0);
				dc->DrawIndexed(
					bm.index_buffer_view.size_in_bytes / _sizeof_component(bm.index_buffer_view.format),
					0, 0);
			}
			else
			{

				const UINT vertexCount = bm.vertex_buffer_view.size_in_bytes / sizeof(BatchMesh::Vertex);
				dc->Draw(vertexCount, 0);
			}
		}
		return;
	}



	const std::vector<Node>& nodes = animated_nodes.size() > 0 ? animated_nodes : GltfModel::nodes;

	dc->VSSetShader(shadow_vertex_shader.Get(), nullptr, 0);
	dc->PSSetShader(nullptr, nullptr, 0);
	dc->IASetInputLayout(input_layout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	std::function<void(int)> traverse = [&](int node_index)->void {
		const Node& node = nodes.at(node_index);


		if (node.skin > -1)
		{
			const Skin& skin = skins.at(node.skin);
			_ASSERT_EXPR(skin.joints.size() <= PRIMITIVE_MAX_JOINTS, L"The size of the joint array is insufficient, please expand it.");
			Primitive_Joint_Constants primitive_joint_data{};
			for (size_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index)
			{
				DirectX::XMStoreFloat4x4(&primitive_joint_data.matrices[joint_index],
					DirectX::XMLoadFloat4x4(&skin.inverse_bind_matrices.at(joint_index)) *
					DirectX::XMLoadFloat4x4(&nodes.at(skin.joints.at(joint_index)).global_transform) *
					DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&node.global_transform))
				);
			}
			dc->UpdateSubresource(primitive_joint_cbuffer.Get(), 0, nullptr, &primitive_joint_data, 0, 0);
			dc->VSSetConstantBuffers(3, 1, primitive_joint_cbuffer.GetAddressOf());
		}

		if (node.mesh > -1)
		{
			const Mesh& mesh = meshes.at(node.mesh);
			for (const Mesh::Primitive& primitive : mesh.primitives)
			{
				// 頂点バッファ設定
				UINT stride = sizeof(Mesh::Vertex);
				UINT offset = 0;
				dc->IASetVertexBuffers(0, 1, buffers.at(primitive.vertex_buffer_view.buffer).GetAddressOf(), &stride, &offset);


				Primitive_Constants primitive_data = {};
				primitive_data.material = primitive.material;
				primitive_data.has_tangent = primitive.has("TANGENT");
				primitive_data.skin = node.skin;
				DirectX::XMStoreFloat4x4(&primitive_data.world, DirectX::XMLoadFloat4x4(&node.global_transform) * DirectX::XMLoadFloat4x4(&world));
				dc->UpdateSubresource(primitive_cbuffer.Get(), 0, nullptr, &primitive_data, 0, 0);
				dc->VSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());

				if (primitive.index_buffer_view.buffer > -1)
				{
					dc->IASetIndexBuffer(buffers.at(primitive.index_buffer_view.buffer).Get(), primitive.index_buffer_view.format, 0);
					dc->DrawIndexed(primitive.index_buffer_view.size_in_bytes / _sizeof_component(primitive.index_buffer_view.format), 0, 0);
				}
				else
				{
					dc->Draw(primitive.vertex_buffer_view.size_in_bytes / primitive.vertex_buffer_view.stride_in_bytes, 0);
				}
			}
		}

		for (std::vector<int>::value_type child_index : node.children)
		{
			traverse(child_index);
		}
		};

	for (std::vector<int>::value_type node_index : scenes.at(default_scene).nodes)
	{
		traverse(node_index);
	}
}

void GltfModel::renderShadowInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds, const std::vector<Node>& animated_nodes)
{
	if (static_batching || instanceWorlds.empty() || !shadow_vertex_shader_instanced)
	{
		return;
	}

	UpdateInstanceBuffer(dc, instance_buffer, instance_srv, instance_buffer_capacity, instanceWorlds);

	ID3D11ShaderResourceView* instanceSrv = instance_srv.Get();
	dc->VSSetShaderResources(17, 1, &instanceSrv);


	const std::vector<Node>& nodes = animated_nodes.size() > 0 ? animated_nodes : GltfModel::nodes;

	dc->VSSetShader(shadow_vertex_shader_instanced.Get(), nullptr, 0);
	dc->PSSetShader(nullptr, nullptr, 0);
	dc->IASetInputLayout(input_layout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT instanceCount = static_cast<UINT>(instanceWorlds.size());

	std::function<void(int)> traverse = [&](int node_index)->void {
		const Node& node = nodes.at(node_index);

		if (node.skin > -1)
		{
			const Skin& skin = skins.at(node.skin);
			_ASSERT_EXPR(skin.joints.size() <= PRIMITIVE_MAX_JOINTS, L"The size of the joint array is insufficient, please expand it.");
			Primitive_Joint_Constants primitive_joint_data{};
			for (size_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index)
			{
				DirectX::XMStoreFloat4x4(&primitive_joint_data.matrices[joint_index],
					DirectX::XMLoadFloat4x4(&skin.inverse_bind_matrices.at(joint_index)) *
					DirectX::XMLoadFloat4x4(&nodes.at(skin.joints.at(joint_index)).global_transform) *
					DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&node.global_transform))
				);
			}
			dc->UpdateSubresource(primitive_joint_cbuffer.Get(), 0, nullptr, &primitive_joint_data, 0, 0);
			dc->VSSetConstantBuffers(3, 1, primitive_joint_cbuffer.GetAddressOf());
		}

		if (node.mesh > -1)
		{
			const Mesh& mesh = meshes.at(node.mesh);
			for (const Mesh::Primitive& primitive : mesh.primitives)
			{
				UINT stride = sizeof(Mesh::Vertex);
				UINT offset = 0;
				dc->IASetVertexBuffers(0, 1, buffers.at(primitive.vertex_buffer_view.buffer).GetAddressOf(), &stride, &offset);

				Primitive_Constants primitive_data = {};
				primitive_data.material = primitive.material;
				primitive_data.has_tangent = primitive.has("TANGENT");
				primitive_data.skin = node.skin;
				DirectX::XMStoreFloat4x4(&primitive_data.world, DirectX::XMLoadFloat4x4(&node.global_transform));
				dc->UpdateSubresource(primitive_cbuffer.Get(), 0, nullptr, &primitive_data, 0, 0);
				dc->VSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());

				if (primitive.index_buffer_view.buffer > -1)
				{
					dc->IASetIndexBuffer(buffers.at(primitive.index_buffer_view.buffer).Get(), primitive.index_buffer_view.format, 0);
					dc->DrawIndexedInstanced(
						primitive.index_buffer_view.size_in_bytes / _sizeof_component(primitive.index_buffer_view.format),
						instanceCount, 0, 0, 0);
				}
				else
				{
					dc->DrawInstanced(primitive.vertex_buffer_view.size_in_bytes / primitive.vertex_buffer_view.stride_in_bytes, instanceCount, 0, 0);
				}
			}
		}

		for (std::vector<int>::value_type child_index : node.children)
		{
			traverse(child_index);
		}
		};

	for (std::vector<int>::value_type node_index : scenes.at(default_scene).nodes)
	{
		traverse(node_index);
	}

	{
		ID3D11ShaderResourceView* nullSrv = nullptr;
		dc->VSSetShaderResources(16, 1, &nullSrv);
		dc->VSSetShaderResources(17, 1, &nullSrv);
	}
}


void GltfModel::batchRender(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& world)
{


	dc->PSSetShaderResources(11, 1, material_resource_view.GetAddressOf());

	dc->VSSetShader(vertex_shader.Get(), nullptr, 0);
	dc->PSSetShader(pixel_shader.Get(), nullptr, 0);
	dc->IASetInputLayout(input_layout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	

	for (const BatchMesh& batch_mesh : batch_meshes)
	{
		UINT stride = sizeof(BatchMesh::Vertex);
		UINT offset = 0;
		dc->IASetVertexBuffers(0, 1, buffers.at(batch_mesh.vertex_buffer_view.buffer).GetAddressOf(), &stride, &offset);

		Primitive_Constants primitive_data = {};
		primitive_data.material = batch_mesh.material;
		primitive_data.has_tangent = batch_mesh.has("TANGENT");
		primitive_data.skin = -1;
		primitive_data.world = world;
		dc->UpdateSubresource(primitive_cbuffer.Get(), 0, 0, &primitive_data, 0, 0);
		dc->VSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());
		dc->PSSetConstantBuffers(0, 1, primitive_cbuffer.GetAddressOf());


		const Material& material = materials.at(batch_mesh.material);
		const int texture_indices[]
		{
			material.data.pbr_metallic_roughness.basecolor_texture.index,
			material.data.pbr_metallic_roughness.metallic_roughness_texture.index,
			material.data.normal_texture.index,
			material.data.emissive_texture.index,
			material.data.occlusion_texture.index,
		};
		ID3D11ShaderResourceView* null_shader_resource_view{};
		ID3D11ShaderResourceView* shader_resource_views[] =
		{
			// BaseColor 
			material.data.pbr_metallic_roughness.basecolor_texture.index > -1
				? texture_resource_views.at(textures.at(material.data.pbr_metallic_roughness.basecolor_texture.index).source).Get()
				: dummy_white_texture.Get(),
			// MetallicRoughness
			material.data.pbr_metallic_roughness.metallic_roughness_texture.index > -1
				? texture_resource_views.at(textures.at(material.data.pbr_metallic_roughness.metallic_roughness_texture.index).source).Get()
				: dummy_white_texture.Get(),
			// Normal 
			material.data.normal_texture.index > -1
				? texture_resource_views.at(textures.at(material.data.normal_texture.index).source).Get()
				: dummy_normal_texture.Get(),
			// Emissive 
			material.data.emissive_texture.index > -1
				? texture_resource_views.at(textures.at(material.data.emissive_texture.index).source).Get()
				: dummy_white_texture.Get(),
			// Occlusion 
			material.data.occlusion_texture.index > -1
				? texture_resource_views.at(textures.at(material.data.occlusion_texture.index).source).Get()
				: dummy_white_texture.Get()
		};
		dc->PSSetShaderResources(2, _countof(shader_resource_views), shader_resource_views);

		if (batch_mesh.index_buffer_view.buffer > -1)
		{
			dc->IASetIndexBuffer(buffers.at(batch_mesh.index_buffer_view.buffer).Get(), batch_mesh.index_buffer_view.format, 0);
			dc->DrawIndexed(batch_mesh.index_buffer_view.size_in_bytes / _sizeof_component(batch_mesh.index_buffer_view.format), 0, 0);
		}
		else
		{
			dc->Draw(batch_mesh.vertex_buffer_view.size_in_bytes / batch_mesh.vertex_buffer_view.stride_in_bytes, 0);
		}
	}

	{
		ID3D11ShaderResourceView* nullSrv = nullptr;
		dc->VSSetShaderResources(16, 1, &nullSrv);
	}
}