cbuffer ATMOSPHERE_CONSTANT_BUFFER : register(b3)
{
    float3 sunDirection;
    float sunIntensity;
    float3 cameraPosition;
    float nightIntensity;
    float planetRadius; 
    float atmosphereRadius;
    float rayleighScaleHeight;
    float mieScaleHeight;
    float3 rayleighScatteringCoefficient;
    float mieScatteringCoefficient;
    float mieEccentricity;
    float3 _padding2;
};