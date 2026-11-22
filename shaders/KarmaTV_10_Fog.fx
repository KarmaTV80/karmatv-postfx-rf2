// KarmaTV PostFX - Nebbia e foschia atmosferica (fase 1).
// Foschia esponenziale basata sulla distanza in metri: "Visibilita'" e' la distanza
// a cui la foschia raggiunge circa il 63% del suo massimo. Da' profondita' alla scena
// senza toccare l'abitacolo.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Foschia massima"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_Visibility < ui_type = "slider"; ui_label = "Visibilità (m)"; ui_min = 200.0; ui_max = 20000.0; > = 4000.0;
uniform float KTV_FogTint < ui_type = "slider"; ui_label = "Tinta (fredda - neutra - calda)"; ui_min = -1.0; ui_max = 1.0; > = 0.0;
uniform float KTV_FogBrightness < ui_type = "slider"; ui_label = "Luminosità della foschia"; ui_min = 0.5; ui_max = 1.2; > = 0.85;

// Colore della foschia: grigio neutro, verso l'azzurro (fredda) o verso la sabbia (calda)
float3 FogColor()
{
	const float3 cold = float3(0.80, 0.88, 1.00);
	const float3 neutral = float3(0.94, 0.94, 0.94);
	const float3 warm = float3(1.00, 0.93, 0.80);
	float3 tint = KTV_FogTint < 0.0 ? lerp(neutral, cold, -KTV_FogTint) : lerp(neutral, warm, KTV_FogTint);
	return tint * KTV_FogBrightness;
}
uniform float KTV_SkyAmount < ui_type = "slider"; ui_label = "Foschia sul cielo"; ui_min = 0.0; ui_max = 1.0; > = 0.3;

float3 PS_Fog(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass || !KTV_DepthValid)
		return color;

	float depth = KTV_LinearDepth(uv);
	float distance = depth * KTV_DepthFar;
	float fog = 1.0 - exp(-distance / KTV_Visibility);

	// Il cielo (profondita' al massimo) riceve solo una parte della foschia.
	fog = lerp(fog, KTV_SkyAmount, step(0.999, depth));

	// Al massimo la foschia copre l'85% del colore: l'immagine resta leggibile.
	float3 result = lerp(color, FogColor(), saturate(fog) * KTV_Intensity * 0.85);
	return KTV_Split(color, result, uv);
}

technique KarmaTV_Fog < ui_label = "KarmaTV - Nebbia atmosferica"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_Fog;
	}
}
