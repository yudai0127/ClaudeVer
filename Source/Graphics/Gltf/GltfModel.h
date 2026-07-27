#pragma once
#define NOMINMAX

#include <d3d11.h>
#include <wrl.h>
#include <directxmath.h>

#include <vector>
#include <unordered_map>

#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/unordered_map.hpp>

class GltfModel
{
	std::string filename;
public:
	GltfModel(ID3D11Device* device, const std::string& filename, bool static_batching);
	virtual ~GltfModel() = default;

	struct Scene
	{
		std::string name;
		std::vector<int> nodes;
	};
	std::vector<Scene> scenes;
	int default_scene = 0;

	struct Node
	{
		std::string name;
		int skin = -1;
		int mesh = -1;

		std::vector<int> children;


		DirectX::XMFLOAT4 rotation = { 0, 0, 0, 1 };
		DirectX::XMFLOAT3 scale = { 1, 1, 1 };
		DirectX::XMFLOAT3 translation = { 0, 0, 0 };

		DirectX::XMFLOAT4X4 global_transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	};
	std::vector<Node> nodes;

	struct IndexBufferView
	{
		int buffer = -1;
		UINT size_in_bytes = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	};
	struct VertexBufferView
	{
		int buffer = -1;
		UINT size_in_bytes = 0;
		UINT stride_in_bytes = 0;
	};
	struct Mesh
	{
		struct Vertex
		{
			DirectX::XMFLOAT3 position = { 0, 0, 0 };
			DirectX::XMFLOAT3 normal = { 0, 0, 1 };
			DirectX::XMFLOAT4 tangent = { 1, 0, 0, 1 };
			DirectX::XMFLOAT2 texcoord = { 0, 0 };

			DirectX::XMUINT4 joints0 = { 0, 0, 0, 0 };
			DirectX::XMUINT4 joints1 = { 0, 0, 0, 0 };
			DirectX::XMFLOAT4 weights0 = { 1, 0, 0, 0 };
			DirectX::XMFLOAT4 weights1 = { 0, 0, 0, 0 };
		};

		std::string name;

		struct Primitive
		{
			int material;


			std::vector<unsigned char> cached_indices;
			IndexBufferView index_buffer_view;

			std::vector<Vertex> cached_vertices;
			VertexBufferView vertex_buffer_view;

			std::unordered_map<std::string, DXGI_FORMAT> attributes;

			bool has(const char* attribute) const
			{
				return attributes.find(attribute) != attributes.end();
			}
		};
		std::vector<Primitive> primitives;
	};
	std::vector<Mesh> meshes;


	struct BatchMesh
	{
		struct Vertex
		{
			DirectX::XMFLOAT3 position = { 0, 0, 0 };
			DirectX::XMFLOAT3 normal = { 0, 0, 1 };
			DirectX::XMFLOAT4 tangent = { 1, 0, 0, 1 };
			DirectX::XMFLOAT2 texcoord = { 0, 0 };
		};

		int material;

		std::vector<UINT> cached_indices;
		IndexBufferView index_buffer_view;

		std::vector<Vertex> cached_vertices;
		VertexBufferView vertex_buffer_view;

		std::unordered_map<std::string, DXGI_FORMAT> attributes;

		bool has(const char* attribute) const
		{
			return attributes.find(attribute) != attributes.end();
		}
	};
	std::vector<BatchMesh> batch_meshes;
	const bool static_batching;


	std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> buffers;

	void render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& world, const std::vector<Node>& animated_nodes);

	void renderInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds, const std::vector<Node>& animated_nodes);

	void renderShadow(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& world, const std::vector<Node>& animated_nodes);

	void renderShadowInstanced(ID3D11DeviceContext* dc, const std::vector<DirectX::XMFLOAT4X4>& instanceWorlds, const std::vector<Node>& animated_nodes);

	void batchRender(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& world);

	struct TextureInfo
	{
		int index = -1;
		int texcoord = 0;
	};
	struct NormalTextureInfo
	{
		int index = -1;
		int texcoord = 0;
		float scale = 1;
	};
	struct OcclusionTextureInfo
	{
		int index = -1;
		int texcoord = 0;
		float strength = 1;
	};

	struct PBRMetallicRoughness
	{
		float basecolor_factor[4] = { 1, 1, 1, 1 };
		TextureInfo basecolor_texture;
		float metallic_factor = 1;
		float roughness_factor = 1;
		TextureInfo metallic_roughness_texture;
	};
	struct Material {
		std::string name;
		struct Cbuffer
		{
			float emissive_factor[3] = { 0, 0, 0 };
			int alpha_mode = 0;
			float alpha_cutoff = 0.5f;
			int double_sided = 0;

			PBRMetallicRoughness pbr_metallic_roughness;

			NormalTextureInfo normal_texture;
			OcclusionTextureInfo occlusion_texture;
			TextureInfo emissive_texture;
		};
		Cbuffer data;
	};
	std::vector<Material> materials;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> material_resource_view;


	struct Texture
	{
		std::string name;
		int source = -1;
	};
	std::vector<Texture> textures;
	struct Image
	{
		std::string name;
		int width = -1;
		int height = -1;
		int component = -1;
		int bits = -1;
		int pixel_type = -1;
		std::string mime_type;
		std::string uri;


		bool as_is = false;

		std::vector<unsigned char> cache_data;
	};
	std::vector<Image> images;
	std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_resource_views;

	struct Skin
	{
		std::vector<DirectX::XMFLOAT4X4> inverse_bind_matrices;
		std::vector<int> joints;
	};
	std::vector<Skin> skins;

	struct Animation
	{
		std::string name;
		float duration = 0.0f;

		struct Channel
		{
			int sampler = -1;
			int target_node = -1;
			std::string target_path;
		};
		std::vector<Channel> channels;

		struct Sampler
		{
			int input = -1;
			int output = -1;
			std::string interpolation;
		};
		std::vector<Sampler> samplers;

		std::unordered_map<int, std::vector<float>> timelines;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT3>> scales;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT4>> rotations;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT3>> translations;
	};
	std::vector<Animation> animations;


	Node* findNode(const char* name);
public:
	void cumulateTransforms(std::vector<Node>& nodes);
private:
	void fetchNodes(const tinygltf::Model& gltf_model);

	void fetchMeshes(ID3D11Device* device, const tinygltf::Model& gltf_model);

	void fetchAndBatchMeshes(ID3D11Device* device, const tinygltf::Model& gltf_model);


	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> shadow_vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_instanced;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> shadow_vertex_shader_instanced;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
	struct Primitive_Constants
	{
		DirectX::XMFLOAT4X4 world;
		int material{ -1 };
		int has_tangent{ 0 };
		int skin{ -1 };
		int pad;
	};
	Microsoft::WRL::ComPtr<ID3D11Buffer> primitive_cbuffer;

	void fetchMaterials(ID3D11Device* device, const tinygltf::Model& gltf_model);
	void fetchTextures(ID3D11Device* device, const tinygltf::Model& gltf_model);
	void fetchAnimations(const tinygltf::Model& gltf_model);

	static const size_t PRIMITIVE_MAX_JOINTS = 512;
	struct Primitive_Joint_Constants
	{
		DirectX::XMFLOAT4X4 matrices[PRIMITIVE_MAX_JOINTS];
	};
	Microsoft::WRL::ComPtr<ID3D11Buffer> primitive_joint_cbuffer;

	Microsoft::WRL::ComPtr<ID3D11Buffer> water_depress_cbuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> instance_buffer;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> instance_srv;
	UINT instance_buffer_capacity = 0;
	void createAndUploadResources(ID3D11Device* device);

public:
	void animate(size_t animation_index, float time, std::vector<Node>& animated_nodes);

	void appendAnimation(ID3D11Device* device, const std::string& filename);
private:
	// ダミーテクスチャ用のSRV
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy_white_texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy_normal_texture;
};