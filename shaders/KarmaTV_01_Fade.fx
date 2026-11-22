// KarmaTV PostFX - Dissolvenza degli effetti nei passaggi menu / pista / replay.
// KarmaTV_FadeSave (prima di tutti gli effetti) salva l'immagine originale di rF2;
// KarmaTV_FadeMix (dopo l'ultimo effetto) la rimescola con quella elaborata.
// L'add-on li accende solo durante la dissolvenza (circa mezzo secondo).

#include "KarmaTV_Common.fxh"

uniform float KTV_Fade < hidden = true; > = 1.0; // 0 = immagine originale, 1 = effetti pieni

texture KTV_FadeOrigTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
sampler KTV_FadeOrig { Texture = KTV_FadeOrigTex; };

float4 PS_FadeSave(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return float4(tex2D(KTV_Color, uv).rgb, 1.0);
}

float3 PS_FadeMix(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 processed = tex2D(KTV_Color, uv).rgb;
	float3 original = tex2D(KTV_FadeOrig, uv).rgb;
	// curva morbida: la dissolvenza parte e arriva senza scatti
	float t = smoothstep(0.0, 1.0, saturate(KTV_Fade));
	return lerp(original, processed, t);
}

technique KarmaTV_FadeSave < ui_label = "KarmaTV - Dissolvenza (salva)"; hidden = true; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_FadeSave;
		RenderTarget = KTV_FadeOrigTex;
	}
}

technique KarmaTV_FadeMix < ui_label = "KarmaTV - Dissolvenza (miscela)"; hidden = true; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_FadeMix;
	}
}
