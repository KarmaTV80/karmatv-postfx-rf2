// KarmaTV PostFX - Sharpening adattivo al contrasto (fase 1).
// Basato sull'algoritmo AMD FidelityFX CAS (licenza MIT), versione semplificata
// senza scaling.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Nitidezza"; ui_min = 0.0; ui_max = 1.0; > = 0.5;

float3 PS_CAS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	if (KTV_Bypass)
		return tex2D(KTV_Color, uv).rgb;
	float2 p = KTV_PIXEL;
	// a b c
	// d e f
	// g h i
	float3 a = tex2D(KTV_Color, uv + float2(-p.x, -p.y)).rgb;
	float3 b = tex2D(KTV_Color, uv + float2( 0.0, -p.y)).rgb;
	float3 c = tex2D(KTV_Color, uv + float2( p.x, -p.y)).rgb;
	float3 d = tex2D(KTV_Color, uv + float2(-p.x,  0.0)).rgb;
	float3 e = tex2D(KTV_Color, uv).rgb;
	float3 f = tex2D(KTV_Color, uv + float2( p.x,  0.0)).rgb;
	float3 g = tex2D(KTV_Color, uv + float2(-p.x,  p.y)).rgb;
	float3 h = tex2D(KTV_Color, uv + float2( 0.0,  p.y)).rgb;
	float3 i = tex2D(KTV_Color, uv + float2( p.x,  p.y)).rgb;

	float3 min_rgb = min(min(min(d, e), min(f, b)), h);
	float3 min_rgb2 = min(min_rgb, min(min(a, c), min(g, i)));
	min_rgb += min_rgb2;

	float3 max_rgb = max(max(max(d, e), max(f, b)), h);
	float3 max_rgb2 = max(max_rgb, max(max(a, c), max(g, i)));
	max_rgb += max_rgb2;

	float3 amp = saturate(min(min_rgb, 2.0 - max_rgb) / max(max_rgb, 1e-5));
	amp = rsqrt(max(amp, 1e-5));

	float peak = -3.0 * KTV_Intensity + 8.0;
	float3 weight = -1.0 / (amp * peak);
	float3 window = (b + d) + (f + h);
	float3 result = saturate((window * weight + e) / (4.0 * weight + 1.0));

	return KTV_Split(e, result, uv);
}

technique KarmaTV_CAS < ui_label = "KarmaTV - Sharpening CAS"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_CAS;
	}
}
