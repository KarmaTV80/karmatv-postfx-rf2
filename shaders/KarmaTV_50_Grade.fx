// KarmaTV PostFX - LUT, bilanciamento del bianco, vibrance, curve.
// Tutto in un solo passaggio. Le LUT sono in un atlante (KarmaTV_LUTs.png, 1024x320)
// con 10 LUT da 32 punti impilate in verticale: 0 Neutra, 1 Broadcast TV,
// 2 Pomeriggio caldo, 3 Nuvoloso freddo, 4 Cinematica, 5 Notte, 6-9 Personale 1-4.
// L'atlante si rigenera con tools\ktv_lut (anche con LUT create da screenshot).

#include "KarmaTV_Common.fxh"

#define KTV_LUT_SIZE 32.0
#define KTV_LUT_COUNT 10.0

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Miscela con l'originale"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_Temperature < ui_type = "slider"; ui_label = "Temperatura (freddo - caldo)"; ui_min = -1.0; ui_max = 1.0; > = 0.0;
uniform float KTV_Tint < ui_type = "slider"; ui_label = "Tinta (verde - magenta)"; ui_min = -1.0; ui_max = 1.0; > = 0.0;
uniform float KTV_Vibrance < ui_type = "slider"; ui_label = "Vibrance"; ui_min = -1.0; ui_max = 1.0; > = 0.25;
uniform float KTV_Saturation < ui_type = "slider"; ui_label = "Saturazione"; ui_min = 0.0; ui_max = 2.0; > = 1.0;
uniform float KTV_Contrast < ui_type = "slider"; ui_label = "Contrasto (curva a S)"; ui_min = 0.0; ui_max = 1.0; > = 0.2;
uniform int KTV_LutIndex < ui_type = "combo"; ui_label = "LUT"; ui_items = "Neutra\0Broadcast TV\0Pomeriggio caldo\0Nuvoloso freddo\0Cinematica\0Notte\0Stile DLSS5\0Personale 2\0Personale 3\0Personale 4\0"; > = 0;
uniform float KTV_LutAmount < ui_type = "slider"; ui_label = "Intensità della LUT"; ui_min = 0.0; ui_max = 1.0; > = 1.0;

texture KTV_LutAtlasTex < source = "KarmaTV_LUTs.png"; > { Width = 1024; Height = 320; Format = RGBA8; };
sampler KTV_LutAtlas { Texture = KTV_LutAtlasTex; };

float3 ApplyLut(float3 color, float index)
{
	const float size = KTV_LUT_SIZE;
	color = saturate(color);
	float blue = color.b * (size - 1.0);
	float slice = floor(blue);
	float row = (color.g * (size - 1.0) + 0.5 + index * size) / (size * KTV_LUT_COUNT);
	float2 uv0 = float2((color.r * (size - 1.0) + 0.5) / (size * size) + slice / size, row);
	float2 uv1 = uv0 + float2(1.0 / size, 0.0);
	return lerp(tex2D(KTV_LutAtlas, uv0).rgb, tex2D(KTV_LutAtlas, uv1).rgb, blue - slice);
}

float3 PS_Grade(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;
	float3 c = color;

	// Bilanciamento del bianco
	c *= float3(1.0 + 0.1 * KTV_Temperature, 1.0 - 0.1 * KTV_Tint, 1.0 - 0.1 * KTV_Temperature);

	// Vibrance: satura di piu' i colori poco saturi
	float luma = KTV_Luma(c);
	float saturation = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
	c = lerp(luma.xxx, c, 1.0 + KTV_Vibrance * (1.0 - saturation));
	c = lerp(luma.xxx, c, KTV_Saturation);

	// Contrasto con curva a S
	c = saturate(c);
	c = lerp(c, c * c * (3.0 - 2.0 * c), KTV_Contrast);

	// LUT scelta dall'atlante (la Neutra non cambia nulla)
	if (KTV_LutIndex > 0)
		c = lerp(c, ApplyLut(c, float(KTV_LutIndex)), KTV_LutAmount);

	float3 result = lerp(color, c, KTV_Intensity);
	return KTV_Split(color, result, uv);
}

technique KarmaTV_Grade < ui_label = "KarmaTV - Color grading"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_Grade;
	}
}
