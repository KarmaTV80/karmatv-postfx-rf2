// KarmaTV PostFX - Riflessi a stella sui fari (fase 2).
// Le luci piu' forti (fari, lampioni, sole sui cromati) vengono estratte a un
// quarto di risoluzione e stirate in quattro raggi (orizzontale e verticale) con
// due passaggi di lunghezza crescente. Di notte si accende da solo (telemetria).

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_GlareThreshold < ui_type = "slider"; ui_label = "Soglia (solo luci forti)"; ui_min = 0.5; ui_max = 1.0; > = 0.85;
uniform float KTV_GlareLength < ui_type = "slider"; ui_label = "Lunghezza dei raggi"; ui_min = 0.2; ui_max = 2.0; > = 1.0;

texture KTV_GlareBrightTex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
texture KTV_GlareH1Tex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
texture KTV_GlareH2Tex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
texture KTV_GlareV1Tex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
texture KTV_GlareV2Tex { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
sampler KTV_GlareBright { Texture = KTV_GlareBrightTex; };
sampler KTV_GlareH1 { Texture = KTV_GlareH1Tex; };
sampler KTV_GlareH2 { Texture = KTV_GlareH2Tex; };
sampler KTV_GlareV1 { Texture = KTV_GlareV1Tex; };
sampler KTV_GlareV2 { Texture = KTV_GlareV2Tex; };

float4 PS_Bright(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 c = tex2D(KTV_Color, uv).rgb;
	float luma = KTV_Luma(c);
	float weight = saturate((luma - KTV_GlareThreshold) / max(1.0 - KTV_GlareThreshold, 1e-3));
	return float4(c * weight * weight, 1.0);
}

// Raggio lungo una direzione: 8 campioni con attenuazione, passo in pixel del quarto di risoluzione.
#define KTV_STREAK(name, source, direction, step_pixels) \
	float4 name(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target \
	{ \
		float2 stride = direction * KTV_PIXEL * 4.0 * step_pixels * KTV_GlareLength; \
		float4 sum = float4(0.0, 0.0, 0.0, 0.0); \
		[unroll] \
		for (int i = 0; i < 8; ++i) \
		{ \
			float w = pow(0.82, float(i)); \
			sum += (tex2D(source, uv + stride * i) + tex2D(source, uv - stride * i)) * w; \
		} \
		return sum * 0.18; \
	}

KTV_STREAK(PS_H1, KTV_GlareBright, float2(1.0, 0.0), 1.0)
KTV_STREAK(PS_H2, KTV_GlareH1, float2(1.0, 0.0), 8.0)
KTV_STREAK(PS_V1, KTV_GlareBright, float2(0.0, 1.0), 1.0)
KTV_STREAK(PS_V2, KTV_GlareV1, float2(0.0, 1.0), 8.0)

float3 PS_Combine(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;
	float3 glare = tex2D(KTV_GlareH2, uv).rgb + tex2D(KTV_GlareV2, uv).rgb;
	float3 result = 1.0 - (1.0 - color) * (1.0 - saturate(glare * KTV_Intensity));
	return KTV_Split(color, result, uv);
}

technique KarmaTV_StarGlare < ui_label = "KarmaTV - Riflessi a stella"; >
{
	pass Bright { VertexShader = KTV_VS; PixelShader = PS_Bright; RenderTarget = KTV_GlareBrightTex; }
	pass H1 { VertexShader = KTV_VS; PixelShader = PS_H1; RenderTarget = KTV_GlareH1Tex; }
	pass H2 { VertexShader = KTV_VS; PixelShader = PS_H2; RenderTarget = KTV_GlareH2Tex; }
	pass V1 { VertexShader = KTV_VS; PixelShader = PS_V1; RenderTarget = KTV_GlareV1Tex; }
	pass V2 { VertexShader = KTV_VS; PixelShader = PS_V2; RenderTarget = KTV_GlareV2Tex; }
	pass Combine { VertexShader = KTV_VS; PixelShader = PS_Combine; }
}
