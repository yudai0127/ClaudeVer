#include "DoF.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/Buffer.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "Graphics/FrameBuffer/FrameBuffer.h"
#include "Graphics/Fullscreen_Quad/Fullscreen_Quad.h"

#include "misc.h"
#include "imgui.h"
#include <algorithm>



void DepthOfField::UnbindSRVs(ID3D11DeviceContext* dc, UINT startSlot, UINT count)
{
	ID3D11ShaderResourceView* nulls[16] = {};
	_ASSERT(count <= 16);
	dc->PSSetShaderResources(startSlot, count, nulls);
}

bool DepthOfField::initialize(ID3D11Device* device, UINT width, UINT height)
{
	if (!device) return false;

	HRESULT hr;
	
	cbParams = std::make_unique<GPUConstantBuffer>(device, sizeof(Params));
	cbBlur = std::make_unique<GPUConstantBuffer>(device, sizeof(BlurParams));

	hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Dof_Downsamplecoc_PS.cso", psDownsample.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Dof_Blurh_PS.cso", psBlurH.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Dof_Blurv_PS.cso", psBlurV.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Dof_Composite_PS.cso", psComposite.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	blit = std::make_unique<Fullscreen_Quad>(device);

	resize(device, width, height);

	Camera* camera= Camera::instance();
	params.gNear = camera->getNear();
	params.gFar = camera->getFar();
	params.gFocusDist = 5.0f;
	params.gFocusRange = 2.0f;
	params.gMaxCoC = 0.5f;
	params.gReversedZ = 1.0f;


	return true;
}

void DepthOfField::resize(ID3D11Device* device, UINT width, UINT height)
{
	if (!device) return;
	if (width == this->width && height == this->height && halfColorCoC) return;

	this->width = width;
	this->height = height;
	halfWidth = std::max<UINT>(1, this->width / 2);
	halfHeight = std::max<UINT>(1, this->height / 2);

	halfColorCoC = std::make_unique<FrameBuffer>(device, halfWidth, halfHeight);
	halfBlurTemp = std::make_unique<FrameBuffer>(device, halfWidth, halfHeight);
	halfBlur = std::make_unique<FrameBuffer>(device, halfWidth, halfHeight);
}

void DepthOfField::SetParams(const Params& param)
{
	params = param;
}

void DepthOfField::render(ID3D11DeviceContext* dc,
	ID3D11ShaderResourceView* sceneColorSRV,
	ID3D11ShaderResourceView* depthSRV,
	ID3D11RenderTargetView* outputRTV)
{
	if (!dc || !sceneColorSRV || !depthSRV || !outputRTV) return;
	if (!halfColorCoC || !halfBlurTemp || !halfBlur || !blit) return;

	BlurParams blurParams{};
	blurParams.gInvHalfRes = DirectX::XMFLOAT2(
		1.0f / halfWidth,
		1.0f / halfHeight);

	
	
	ID3D11SamplerState* samplers[8] = {};
	samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
	samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
	samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
	samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();   // Slot 3
	samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();  // Slot 4
	samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
	samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
	samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();

	dc->PSSetSamplers(0, _countof(samplers), samplers);

	dc->OMSetBlendState((GraphicsManager::instance()->getBlendStates(BLEND_STATE::NONE)).Get(), nullptr, 0xFFFFFFFF);

	// Downsample + CoC
	halfColorCoC->activate(dc);
	halfColorCoC->clear(dc, 0, 0, 0, 0);
	
	cbParams->UploadData<Params>(dc, 0, params, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
	{
		ID3D11ShaderResourceView* srvs[2] = { sceneColorSRV, depthSRV };
		blit->blit(dc, srvs, 0, 2, psDownsample.Get());
		UnbindSRVs(dc, 0, 2);
	}
	halfColorCoC->deactivate(dc);
	 
	// Blur H
	halfBlurTemp->activate(dc);
	halfBlurTemp->clear(dc, 0, 0, 0, 0);
	
	cbBlur->UploadData<BlurParams>(dc, 1, blurParams, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
	{
		ID3D11ShaderResourceView* srvs[1] = { halfColorCoC->shader_resource_views[0].Get() };
		blit->blit(dc, srvs, 0, 1, psBlurH.Get());
		UnbindSRVs(dc, 0, 1);
	}
	halfBlurTemp->deactivate(dc);
   // Blur V
	halfBlur->activate(dc);
	halfBlur->clear(dc, 0, 0, 0, 0);
	cbBlur->UploadData<BlurParams>(dc, 1, blurParams, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
	{
		ID3D11ShaderResourceView* srvs[1] = { halfBlurTemp->shader_resource_views[0].Get() };
		blit->blit(dc, srvs, 0, 1, psBlurV.Get());
		UnbindSRVs(dc, 0, 1);
	}
	halfBlur->deactivate(dc);

	// Composite
	dc->OMSetRenderTargets(1, &outputRTV, nullptr);
	cbParams->UploadData<Params>(dc, 0, params, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
	{
		ID3D11ShaderResourceView* srvs[4] = {
			sceneColorSRV,                        
			depthSRV,                             
			halfColorCoC->shader_resource_views[0].Get(),
			halfBlur->shader_resource_views[0].Get()     
		};
		blit->blit(dc, srvs, 0, 4, psComposite.Get());
		UnbindSRVs(dc, 0, 4);
	}
}

void DepthOfField::debugGui(Params* params, bool* pEnable, ID3D11ShaderResourceView* depthSRV)
{
	if (!params)
		return;

	if (ImGui::TreeNode("Depth of Field"))
	{
		if (pEnable)
		{
			ImGui::Checkbox("Enable DoF", pEnable);
		}

		ImGui::DragFloat("Focus Dist", &params->gFocusDist, 0.1f, 0.1f, 100000.0f);
		ImGui::DragFloat("Focus Range", &params->gFocusRange, 0.1f, 0.01f, 100000.0f);
		ImGui::DragFloat("Max CoC", &params->gMaxCoC, 0.01f, 0.0f, 4.0f);

		if (ImGui::CollapsingHeader("DoF Debug Textures", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (depthSRV)
			{
				ImGui::Text("Depth SRV");
				ImGui::Image(reinterpret_cast<ImTextureID>(depthSRV), ImVec2(256, 144));
			}

			ID3D11ShaderResourceView* halfCoC = getHalfCoCSRV();
			ID3D11ShaderResourceView* halfBlur = getHalfBlurSRV();

			if (halfCoC)
			{
				ImGui::Text("Half Color+CoC (downsample)");
				ImGui::Image(reinterpret_cast<ImTextureID>(halfCoC), ImVec2(256, 144));
			}
			if (halfBlur)
			{
				ImGui::Text("Half Blur (after blur)");
				ImGui::Image(reinterpret_cast<ImTextureID>(halfBlur), ImVec2(256, 144));
			}
		}

		ImGui::TreePop();
	}
}

ID3D11ShaderResourceView* DepthOfField::getHalfCoCSRV() const
{
	return (halfColorCoC) ? halfColorCoC->shader_resource_views[0].Get() : nullptr;
}

ID3D11ShaderResourceView* DepthOfField::getHalfBlurSRV() const
{
	return (halfBlur) ? halfBlur->shader_resource_views[0].Get() : nullptr;
}