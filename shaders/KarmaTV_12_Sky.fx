// KarmaTV PostFX - Cielo (regolazione del solo cielo).
// Il cielo e' l'unica parte dell'immagine alla massima profondita': la profondita' fa da
// maschera, quindi luminosita', colore e contrasto delle nuvole cambiano senza toccare
// pista, auto e paesaggio. Il bordo con alberi e tribune viene ammorbidito per evitare
// aloni. Nei menu (profondita' non valida) l'immagine resta invariata.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità"; ui_min = 0.0; ui_max = 1.0; > = 1.0;
uniform float KTV_SkyExposure < ui_type = "slider"; ui_label = "Luminosità del cielo (EV)"; ui_min = -2.0; ui_max = 1.0; > = -0.5;
uniform float KTV_SkySaturation < ui_type = "slider"; ui_label = "Saturazione"; ui_min = 0.0; ui_max = 2.0; > = 1.2;
uniform float KTV_SkyBlue < ui_type = "slider"; ui_label = "Azzurro"; ui_min = -1.0; ui_max = 1.0; > = 0.2;
uniform float KTV_SkyContrast < ui_type = "slider"; ui_label = "Contrasto delle nuvole"; ui_min = 0.0; ui_max = 1.0; > = 0.3;
uniform float KTV_SkyHorizon < ui_type = "slider"; ui_label = "Sfumatura verso l'orizzonte"; ui_min = 0.0; ui_max = 1.0; > = 0.4;

bool SkyAt(float2 uv)
{
	return KTV_LinearDepth(uv) > 0.999;
}

// Copertura di cielo attorno al pixel (0..1): bordo morbido contro alberi e tribune
float SkyMask(float2 uv)
{
	float2 p = KTV_PIXEL * 1.5;
	float mask = 0.0;
	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
			mask += SkyAt(uv + float2(x, y) * p) ? 1.0 : 0.0;
	}
	return mask / 9.0;
}

float3 PS_Sky(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass || !KTV_DepthValid)
		return color;
	float mask = SkyMask(uv);
	if (mask <= 0.0)
		return color;

	float3 c = color * exp2(KTV_SkyExposure);

	// Contrasto delle nuvole attorno alla luminosita' media del cielo
	float l = KTV_Luma(c);
	float mid = 0.55 * exp2(KTV_SkyExposure);
	c = max(c + (l - mid) * KTV_SkyContrast * 0.8, 0.0);

	// Azzurro: soprattutto nelle parti chiare, dove un cielo bruciato diventa bianco
	l = KTV_Luma(c);
	float3 blue = KTV_SkyBlue >= 0.0 ? float3(0.86, 0.95, 1.12) : float3(1.08, 1.02, 0.9);
	c *= lerp(1.0.xxx, blue, abs(KTV_SkyBlue) * saturate(l * 1.5));

	l = KTV_Luma(c);
	c = lerp(l.xxx, c, KTV_SkySaturation);

	// Vicino all'orizzonte l'effetto si attenua, per non staccare il cielo dalla foschia.
	// L'orizzonte e' il punto piu' basso del cielo in questa colonna di pixel.
	float horizon = 1.0;
	if (KTV_SkyHorizon > 0.0)
	{
		float below = 0.0;
		[unroll]
		for (int i = 1; i <= 4; ++i)
			below += SkyAt(uv + float2(0.0, KTV_PIXEL.y * 24.0 * i)) ? 1.0 : 0.0;
		horizon = lerp(1.0, 0.35 + 0.65 * below / 4.0, KTV_SkyHorizon);
	}

	float3 result = lerp(color, saturate(c), mask * horizon * KTV_Intensity);
	return KTV_Split(color, result, uv);
}

technique KarmaTV_Sky < ui_label = "KarmaTV - Cielo"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_Sky;
	}
}
