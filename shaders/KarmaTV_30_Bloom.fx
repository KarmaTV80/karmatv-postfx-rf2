// KarmaTV PostFX - Bloom (fase 1).
// Soglia morbida sulle luci forti, catena di 5 mezze risoluzioni (fino a 1/32),
// risalita additiva e fusione "screen" con l'immagine.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità"; ui_min = 0.0; ui_max = 1.0; > = 0.3;
uniform float KTV_Threshold < ui_type = "slider"; ui_label = "Soglia"; ui_min = 0.0; ui_max = 1.0; > = 0.8;
uniform float KTV_Knee < ui_type = "slider"; ui_label = "Morbidezza della soglia"; ui_min = 0.01; ui_max = 0.5; > = 0.2;

texture KTV_BloomTex1 { Width = BUFFER_WIDTH / 2;  Height = BUFFER_HEIGHT / 2;  Format = RGBA16F; };
texture KTV_BloomTex2 { Width = BUFFER_WIDTH / 4;  Height = BUFFER_HEIGHT / 4;  Format = RGBA16F; };
texture KTV_BloomTex3 { Width = BUFFER_WIDTH / 8;  Height = BUFFER_HEIGHT / 8;  Format = RGBA16F; };
texture KTV_BloomTex4 { Width = BUFFER_WIDTH / 16; Height = BUFFER_HEIGHT / 16; Format = RGBA16F; };
texture KTV_BloomTex5 { Width = BUFFER_WIDTH / 32; Height = BUFFER_HEIGHT / 32; Format = RGBA16F; };

sampler KTV_Bloom1 { Texture = KTV_BloomTex1; };
sampler KTV_Bloom2 { Texture = KTV_BloomTex2; };
sampler KTV_Bloom3 { Texture = KTV_BloomTex3; };
sampler KTV_Bloom4 { Texture = KTV_BloomTex4; };
sampler KTV_Bloom5 { Texture = KTV_BloomTex5; };

float4 PS_Prefilter(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float4 o = KTV_PIXEL.xyxy * float4(-1.0, -1.0, 1.0, 1.0);
	float3 c = 0.25 * (tex2D(KTV_Color, uv + o.xy).rgb + tex2D(KTV_Color, uv + o.zy).rgb +
	                   tex2D(KTV_Color, uv + o.xw).rgb + tex2D(KTV_Color, uv + o.zw).rgb);

	float luma = KTV_Luma(c);
	float soft = clamp(luma - KTV_Threshold + KTV_Knee, 0.0, 2.0 * KTV_Knee);
	soft = soft * soft / (4.0 * KTV_Knee + 1e-5);
	float contribution = max(soft, luma - KTV_Threshold) / max(luma, 1e-5);
	return float4(c * contribution, 1.0);
}

// Media di 4 campioni bilineari della texture sorgente (texel = KTV_PIXEL * scala).
#define KTV_BLOOM_BLUR(name, source, scale) \
	float4 name(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target \
	{ \
		float4 o = (KTV_PIXEL * scale).xyxy * float4(-1.0, -1.0, 1.0, 1.0); \
		return 0.25 * (tex2D(source, uv + o.xy) + tex2D(source, uv + o.zy) + \
		               tex2D(source, uv + o.xw) + tex2D(source, uv + o.zw)); \
	}

KTV_BLOOM_BLUR(PS_Down2, KTV_Bloom1, 2.0)
KTV_BLOOM_BLUR(PS_Down3, KTV_Bloom2, 4.0)
KTV_BLOOM_BLUR(PS_Down4, KTV_Bloom3, 8.0)
KTV_BLOOM_BLUR(PS_Down5, KTV_Bloom4, 16.0)
KTV_BLOOM_BLUR(PS_Up4, KTV_Bloom5, 32.0)
KTV_BLOOM_BLUR(PS_Up3, KTV_Bloom4, 16.0)
KTV_BLOOM_BLUR(PS_Up2, KTV_Bloom3, 8.0)
KTV_BLOOM_BLUR(PS_Up1, KTV_Bloom2, 4.0)

float3 PS_Combine(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;
	// La risalita somma 5 livelli: si normalizza prima di applicare l'intensita'.
	float3 bloom = tex2D(KTV_Bloom1, uv).rgb * 0.2;
	// Intensita' 1.0 = alone ben visibile sulle luci forti; 0.3 = alone discreto.
	// La curva e' quadratica: i valori bassi restano delicati, quelli alti salgono.
	float strength = KTV_Intensity * KTV_Intensity * 1.2 + KTV_Intensity * 0.3;
	float3 result = 1.0 - (1.0 - color) * (1.0 - saturate(bloom * strength));
	return KTV_Split(color, result, uv);
}

technique KarmaTV_Bloom < ui_label = "KarmaTV - Bloom"; >
{
	pass Prefilter { VertexShader = KTV_VS; PixelShader = PS_Prefilter; RenderTarget = KTV_BloomTex1; }
	pass Down2 { VertexShader = KTV_VS; PixelShader = PS_Down2; RenderTarget = KTV_BloomTex2; }
	pass Down3 { VertexShader = KTV_VS; PixelShader = PS_Down3; RenderTarget = KTV_BloomTex3; }
	pass Down4 { VertexShader = KTV_VS; PixelShader = PS_Down4; RenderTarget = KTV_BloomTex4; }
	pass Down5 { VertexShader = KTV_VS; PixelShader = PS_Down5; RenderTarget = KTV_BloomTex5; }
	pass Up4 { VertexShader = KTV_VS; PixelShader = PS_Up4; RenderTarget = KTV_BloomTex4; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; }
	pass Up3 { VertexShader = KTV_VS; PixelShader = PS_Up3; RenderTarget = KTV_BloomTex3; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; }
	pass Up2 { VertexShader = KTV_VS; PixelShader = PS_Up2; RenderTarget = KTV_BloomTex2; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; }
	pass Up1 { VertexShader = KTV_VS; PixelShader = PS_Up1; RenderTarget = KTV_BloomTex1; BlendEnable = true; SrcBlend = ONE; DestBlend = ONE; }
	pass Combine { VertexShader = KTV_VS; PixelShader = PS_Combine; }
}
