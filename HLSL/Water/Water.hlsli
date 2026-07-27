#ifndef WATER_COMMON_H
#define WATER_COMMON_H

// 円周率
static const float PI = 3.14159265359f;
static const float WaterIOR = 1.33f;
static const uint RefractionIterationCount = 4;
static const float RefractionErrorPixels = 1.0f;

// 1本ぶんの波パラメータ（ゲルストナー波用）
struct Wave
{
    float2 direction; // 波の進行方向
    float amplitude; // 振幅 A（波の高さ）
    float wavelength; // 波長 λ（波の間隔）
    float speed; // 位相の進み方
    float steepness; // 尖り具合
    float2 _pad; 
};

cbuffer WaterCB : register(b6)
{
    row_major float4x4 gWorld; // ローカル→ワールド
    row_major float4x4 gViewProjection; // ワールド→クリップ

    float3 gCameraPos; // カメラ位置（ワールド）
    float _pad_cam;

    float gTime; // 時間
    float gGravity; 
    uint gWaveCount; // 使用する波の本数
    float _pad0;

    Wave gWaves[8]; // 最大8本の波
};

cbuffer WaterPSParams : register(b7)
{
    float4 normal0; // xy=スクロール速度, z=タイル密度, w=重み
    float4 normal1;
    float4 normal2;
    float4 misc; // x=F0(0度反射率), y=屈折強度, z=スペキュラ強度, w=デバッグ等（未使用/スイッチ）

    float4 iblParams;
    float4 waterTint; // rgb=水の色(ティント), a=吸収強度
    float4 alphaParam; 
    float4 shadingParams;
    float4 rippleParams; //波紋高さスケール等
    
    row_major float4x4 gInvView;
    row_major float4x4 gInvProjection;
    float2 gScreenSize;
    float2 _padScreen;
};


struct PSIn
{
    float4 SVPosition : SV_POSITION; 
    float3 WorldPos : TEXCOORD0; // ワールド位置
    float3 WorldNorm : TEXCOORD1; // ワールド法線
    float2 UV : TEXCOORD2; // 水面UV
    float3 WorldTangent : TEXCOORD3; // ワールド接線
    float3 WorldBitangent : TEXCOORD4; // ワールド従法線
    float4 ClipPos : TEXCOORD5; // クリップ座標
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 SVPosition : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 WorldTangent : TEXCOORD3;
    float3 WorldBitangent : TEXCOORD4;
    float4 ClipPos : TEXCOORD5;
};

struct Ray
{
    float3 start;
    float3 dir;
};

struct Plane
{
    float3 origin;
    float3 normal;
};

inline bool IsPointValid(float2 texCoords)
{
    return all(texCoords >= 0.0f) && all(texCoords <= 1.0f);
}

inline float DistanceFromEdge(float2 texCoords)
{
    return texCoords.y;
}

inline float3 GetWorldPosFromDepth(float2 screenSpace, float ndcDepth)
{
    float3 ndcCoords = float3(screenSpace.x * 2.0f - 1.0f, 1.0f - 2.0f * screenSpace.y, ndcDepth);
    
    float4 viewPos = mul(float4(ndcCoords, 1.0f), gInvProjection);
    viewPos.xyz /= viewPos.w;
    float4 worldPos = mul(float4(viewPos.xyz, 1.0f), gInvView);
    return worldPos.xyz;
}

inline float PlaneRayIntersection(Plane plane, Ray ray)
{
    float denom = dot(plane.normal, ray.dir);
    if (abs(denom) > 0.0001f)
    {
        return dot(plane.origin - ray.start, plane.normal) / denom;
    }
    return -1.0f;
}

inline float2 FindClosestPointOnRay(float3 resultWorldPos, Ray ray)
{
    float t = dot(resultWorldPos - ray.start, ray.dir);
    float3 closestPointOnRay = ray.start + t * ray.dir;
    float4 clip = mul(float4(closestPointOnRay, 1.0f), gViewProjection);
    float2 ndcPoint = clip.xy / clip.w;
    return ndcPoint * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
}


void ApplyGerstner(
    in float3 inPos,
    out float3 outPos,
    out float3 outNormal,
    out float3 outTangent,
    out float3 outBitangent)
{
    // 元のワールド位置
    float3 P = inPos;

    // 水平変位と高さ変位
    float2 dispXZ = float2(0.0f, 0.0f);
    float dispY = 0.0f;

    // 接線ベクトルの初期値
    float3 tangentX = float3(1.0f, 0.0f, 0.0f); 
    float3 tangentZ = float3(0.0f, 0.0f, 1.0f); 

    // 波の本数ぶん合成
    [loop]
    for (uint i = 0; i < gWaveCount; ++i)
    {
        Wave w = gWaves[i];

        // 方向D（正規化）
        float2 D = normalize(w.direction);

        // 波数 k = 2π/λ
        float k = 2.0f * PI / max(w.wavelength, 1e-4f);

        // 角周波数
        float omega = (w.speed > 0.0f) ? (w.speed * k) : sqrt(gGravity * k);

        // 位相
        float theta = dot(D, P.xz) * k - omega * gTime;

        // sin/cos
        float s = sin(theta);
        float c = cos(theta);

        //水平変位の強さ
        float QA = w.steepness * w.amplitude;

        //位置の変位を加算
        // 水平
        dispXZ += QA * D * c;
        // 高さ
        dispY += w.amplitude * s;

        
        tangentX.x += -QA * k * D.x * D.x * s;
        tangentX.y += w.amplitude * k * D.x * c;
        tangentX.z += -QA * k * D.y * D.x * s;

        
        tangentZ.x += -QA * k * D.x * D.y * s;
        tangentZ.y += w.amplitude * k * D.y * c;
        tangentZ.z += -QA * k * D.y * D.y * s;
    }

    //変位後の位置
    P.xz += dispXZ;
    P.y += dispY;

    //法線
    float3 n = normalize(cross(tangentZ, tangentX));

    
    float3 t = normalize(tangentX - n * dot(tangentX, n));
    float3 b = normalize(cross(n, t));

    outPos = P;
    outNormal = n;
    outTangent = t;
    outBitangent = b;
}

#endif
