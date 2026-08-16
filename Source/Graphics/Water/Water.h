#pragma once
#include <d3d11.h>
#include <wrl.h>
#include <directxmath.h>
#include <cstdint>
#include "Graphics/GPUConstantBuffer.h"


class Water_Simulation;

class RippleSimulation
{
public:
    RippleSimulation() = default;
    ~RippleSimulation() = default;

    bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);
    void update(ID3D11DeviceContext* dc, float dt);
    void InjectRipple(ID3D11DeviceContext* dc, const DirectX::XMFLOAT2& center, float strength, float radius);

    ID3D11ShaderResourceView* getDisplacementMap() const {
        return srvs[currentBufferIdx].Get();
    }
    uint32_t getWidth()  const { return width; }
    uint32_t getHeight() const { return height; }
    
    float& getWaveSpeed() { return simParams.c; }
    float& getDamping() { return simParams.damping; }

private:
    struct SimCB
    {
        DirectX::XMFLOAT2 texel;
        float dt;
        float c;
        float damping;
        DirectX::XMFLOAT3 _pad;
    };

    

    struct InjectCB
    {
        DirectX::XMUINT2 center;
        float velocity;
        float radius;
        float falloff;
        DirectX::XMFLOAT3 _pad;
    };

    // シミュレーション用パラメータ（定数バッファのCPU側データ）
    SimCB simParams{};

    InjectCB cb{};

    // シミュレーション解像度とダブルバッファリング用インデックス
    uint32_t width = 0, height = 0;
    uint32_t currentBufferIdx = 0;

    // コンピュートシェーダー（波紋の計算・生成用）
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> simCS;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> injectCS;


    std::unique_ptr<GPUConstantBuffer> simcb;
    std::unique_ptr<GPUConstantBuffer> injectcb;

    // 定数バッファリソース
    Microsoft::WRL::ComPtr<ID3D11Buffer> simCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> injectCB;

    // テクスチャリソース（計算前・計算後を入れ替えるPing-Pongバッファ用）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvs[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uavs[2];
};


class Water_Simulation
{
public:
    Water_Simulation()=default;
    ~Water_Simulation()=default;

    // 初期化
    bool Initialize(ID3D11Device* device, uint32_t gridWidth, uint32_t gridHeight);
    
    bool CreateRenderTargets(ID3D11Device* device, uint32_t width, uint32_t height);

    ID3D11ShaderResourceView* getWaterNormalSRV() const { return normalSRV.Get(); }
    ID3D11ShaderResourceView* getWaterDepthSRV() const { return depthSRV.Get(); }

    ID3D11RenderTargetView* getNormalRTV() const { return normalRTV.Get(); }
    ID3D11RenderTargetView* getDepthRTV() const { return depthRTV.Get(); }
    ID3D11ShaderResourceView* getMaskSRV() const { return maskSRV.Get(); }

    // 更新
    void update(ID3D11DeviceContext* dc, float elapsedTime,
        const DirectX::XMMATRIX& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMMATRIX& viewProjection,
        const DirectX::XMFLOAT3& cameraPos);

    // 描画
    void render(ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* pSceneSRV,
        ID3D11ShaderResourceView* pSceneNormalSRV,
        ID3D11ShaderResourceView* pCausticsSRV,
        ID3D11ShaderResourceView* pSkyCubeSRV,
        ID3D11ShaderResourceView* pSSRReflectionSRV,
        ID3D11ShaderResourceView* pRippleDisplacementSRV,
        ID3D11ShaderResourceView* pDepthSRV);

    void renderGBufferPrepass(
        ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* pRippleDisplacementSRV);

    void renderCaustics(
        ID3D11DeviceContext* dc,
        ID3D11VertexShader* pCausticsVS,
        ID3D11PixelShader* pCausticsPS,
        ID3D11ShaderResourceView* pDepthSRV);

    void SetPaused(bool paused) { pauseds = paused; }

    // ImGui描画
    void debugGui();

   
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 nrm;
        DirectX::XMFLOAT2 uv;
    };

    struct CB_Wave
    {
        DirectX::XMFLOAT2 direction;
        float amplitude;
        float wavelength;
        float speed;
        float steepness;
        float _pad[2];
    };

    struct CB_Water
    {
        DirectX::XMFLOAT4X4 gWorld;
        DirectX::XMFLOAT4X4 gViewProjection;
        DirectX::XMFLOAT3   gCameraPos;
        float      _pad_cam;

        float      gTime;
        float      gGravity;
        uint32_t   gWaveCount;
        float      _pad0;

        CB_Wave    gWaves[8];
    };


    struct PSParams
    {
        DirectX::XMFLOAT4 normal0;
        DirectX::XMFLOAT4 normal1;
        DirectX::XMFLOAT4 normal2;
        DirectX::XMFLOAT4 misc;

        DirectX::XMFLOAT4 iblParams;
        DirectX::XMFLOAT4 waterTint;
        DirectX::XMFLOAT4 alphaParam;
        DirectX::XMFLOAT4 shadingParams;
        DirectX::XMFLOAT4 rippleParams;

        DirectX::XMFLOAT4X4 gInvView;
        DirectX::XMFLOAT4X4 gInvProjection;
        DirectX::XMFLOAT2 gScreenSize;
        DirectX::XMFLOAT2 _padScreen;
    };

    // 波紋シミュレーションのインスタンスを取得
    RippleSimulation* GetRippleSimulation() { return &rippleSim; }

    DirectX::XMFLOAT2 GetWorldSize() const { return { worldSizeX, worldSizeZ }; }

    DirectX::XMFLOAT3 GetWorldCenter() const { return worldCenter; }

    // シェーダーパラメータと波の基本設定
    PSParams psParams{};
    CB_Water cb{};
    enum class Preset
    {
        ClearOcean,
        Tropical,
        MurkyHarbor,
        Storm
    };

    void ApplyPreset(Preset preset);

    // Long swells through short wind chop. The GPU buffer already supported
    // eight bands, so expose the complete spectrum on the CPU as well.
    CB_Wave waves[8]{};
    float worldOffsetY = 50.0f;   
    float waveAmpScale = 0.02f;
    float waveSteepScale = 0.50f;

private:
    // メッシュ関連
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    uint32_t indexCount;

    // シェーダー関連
    Microsoft::WRL::ComPtr<ID3D11VertexShader> watervs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> waterps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> waterinputLayout;

    Microsoft::WRL::ComPtr<ID3D11PixelShader> prepassPixelShader;


  

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBufferVS; 
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBufferPS; 
    

    // テクスチャリソース（ノーマルマップ用）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalMapSRV[3];

    // レンダリングターゲット（法線情報の描画用）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> normalRTTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> normalRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalSRV;

    // レンダリングターゲット（深度情報の描画用）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthRTTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> depthRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV;

    // マスク用テクスチャリソース
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> maskSRV;
    //メッシュ作成
    bool CreateGridMesh(ID3D11Device* device, uint32_t gridWidth, uint32_t gridHeight);

    // シミュレーションの進行時間と状態管理
    bool  pauseds = false;
    float time = 0.0f;

    // 水面のワールド座標系サイズと中心位置
    float worldSizeX = 100000.0f;
    float worldSizeZ = 100000.0f;
    DirectX::XMFLOAT3 worldCenter{ 0.0f, 0.0f, 0.0f };
    // 波紋シミュレーション
    RippleSimulation rippleSim;
    public:
        // 雨が当たったworld座標から波紋を打つ
        void InjectRippleWorld(ID3D11DeviceContext* dc,
            const DirectX::XMFLOAT3& hitWorldPos,
            float strength,
            float radiusWorld = 0.6f);

private:
    DirectX::XMFLOAT4X4 lastWorld{};     
    DirectX::XMFLOAT4X4 lastInvWorld{};
    DirectX::XMFLOAT2 screenSize{ 0.0f, 0.0f };

private:

    std::unique_ptr<GPUConstantBuffer> cbwater;
    std::unique_ptr<GPUConstantBuffer>pspara;
};
