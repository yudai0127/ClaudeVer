#include "Fullscreen_Quad.h"
#include "Graphics/Shader/Shader.h"
#include "misc.h"

Fullscreen_Quad::Fullscreen_Quad(ID3D11Device* device)
{
	ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Fullscreen_quad_VS.cso", embedded_vertex_shader.ReleaseAndGetAddressOf(), nullptr, nullptr, 0);
	ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Fullscreen_Quad_PS.cso", embedded_pixel_shader.ReleaseAndGetAddressOf());
}
void Fullscreen_Quad::blit(ID3D11DeviceContext* dc, ID3D11ShaderResourceView** shader_resource_view, uint32_t start_slot, uint32_t num_views, ID3D11PixelShader* replaced_pixel_shader)
{
	dc->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	dc->IASetInputLayout(nullptr);

	dc->VSSetShader(embedded_vertex_shader.Get(), 0, 0);
	replaced_pixel_shader ? dc->PSSetShader(replaced_pixel_shader, 0, 0) : dc->PSSetShader(embedded_pixel_shader.Get(), 0, 0);

	dc->PSSetShaderResources(start_slot, num_views, shader_resource_view);

	dc->Draw(4, 0);
}