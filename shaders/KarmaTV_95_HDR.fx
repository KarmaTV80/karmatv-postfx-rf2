// KarmaTV PostFX - AutoHDR (fase 1).
// Ultimo passaggio quando l'add-on ha convertito la swapchain in HDR10
// (R10G10B10A2, spazio colore ST.2084): riporta l'immagine SDR di rF2 in luce
// lineare, la porta al bianco carta in nit, espande le alte luci verso il
// picco del monitor, converte da Rec.709 a Rec.2020 e codifica in PQ.
// L'add-on lo accende solo se la swapchain e' davvero in HDR.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Espansione alte luci"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_PeakNits < ui_type = "slider"; ui_label = "Picco del monitor (nit)"; ui_min = 400.0; ui_max = 1600.0; > = 1000.0;
uniform float KTV_PaperWhite < ui_type = "slider"; ui_label = "Bianco carta (nit)"; ui_min = 80.0; ui_max = 400.0; > = 200.0;
uniform float KTV_Saturation < ui_type = "slider"; ui_label = "Saturazione HDR"; ui_min = 0.8; ui_max = 1.3; > = 1.0;

float3 SrgbToLinear(float3 c)
{
	c = saturate(c);
	return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}

float3 Rec709ToRec2020(float3 c)
{
	const float3x3 m = float3x3(
		0.6274040, 0.3292820, 0.0433136,
		0.0690970, 0.9195400, 0.0113612,
		0.0163916, 0.0880132, 0.8955950);
	return mul(m, c);
}

float3 PqEncode(float3 nits)
{
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;
	float3 y = pow(saturate(nits / 10000.0), m1);
	return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float3 PS_HDR(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;

	float3 lin = SrgbToLinear(color);

	// Espansione: sotto il ginocchio resta a bianco carta, sopra sale verso il picco.
	float luma = KTV_Luma(lin);
	const float knee = 0.5;
	float t = saturate((luma - knee) / (1.0 - knee));
	float boost = lerp(1.0, KTV_PeakNits / KTV_PaperWhite, t * t * KTV_Intensity);
	float3 nits = lin * KTV_PaperWhite * boost;

	nits = lerp(KTV_Luma(nits).xxx, nits, KTV_Saturation);
	nits = max(Rec709ToRec2020(nits), 0.0);
	nits = min(nits, KTV_PeakNits);
	return PqEncode(nits);
}

technique KarmaTV_HDR < ui_label = "KarmaTV - AutoHDR (HDR10)"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_HDR;
	}
}
