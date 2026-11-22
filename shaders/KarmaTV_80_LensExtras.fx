// KarmaTV PostFX - Aberrazione cromatica, grana e sporco sulla lente (fase 2).
// Tre effetti "da telecamera" in un solo passaggio, pensati per replay e riprese:
//  - aberrazione cromatica: i canali di colore si separano verso i bordi;
//  - grana: rumore fine animato, piu' visibile nelle zone scure;
//  - sporco sulla lente: macchie che si illuminano solo dove c'e' una luce forte.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità generale"; ui_min = 0.0; ui_max = 1.0; > = 0.3;
uniform float KTV_Aberration < ui_type = "slider"; ui_label = "Aberrazione cromatica"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_Grain < ui_type = "slider"; ui_label = "Grana"; ui_min = 0.0; ui_max = 1.0; > = 0.4;
uniform float KTV_Dirt < ui_type = "slider"; ui_label = "Sporco sulla lente"; ui_min = 0.0; ui_max = 1.0; > = 0.4;
uniform float KTV_Timer < source = "timer"; >;

float Hash(float2 p)
{
	return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float ValueNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(Hash(i), Hash(i + float2(1.0, 0.0)), f.x),
	            lerp(Hash(i + float2(0.0, 1.0)), Hash(i + float2(1.0, 1.0)), f.x), f.y);
}

float3 PS_LensExtras(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;

	// Aberrazione cromatica radiale (piu' forte ai bordi)
	float2 from_center = uv - 0.5;
	float2 shift = from_center * dot(from_center, from_center) * 0.02 * KTV_Aberration * KTV_Intensity;
	float3 result = float3(tex2D(KTV_Color, uv + shift).r, color.g, tex2D(KTV_Color, uv - shift).b);

	// Sporco sulla lente: macchie statiche che si vedono solo con una luce forte vicina
	float2 aspect = float2(BUFFER_WIDTH * BUFFER_RCP_HEIGHT, 1.0);
	float dirt = smoothstep(0.55, 0.85, ValueNoise(uv * aspect * 6.0)) * 0.6 + smoothstep(0.7, 0.95, ValueNoise(uv * aspect * 22.0)) * 0.4;
	float3 local_light = (tex2Dlod(KTV_Color, float4(uv + float2(0.02, 0.0), 0.0, 0.0)).rgb +
	                      tex2Dlod(KTV_Color, float4(uv - float2(0.02, 0.0), 0.0, 0.0)).rgb +
	                      tex2Dlod(KTV_Color, float4(uv + float2(0.0, 0.03), 0.0, 0.0)).rgb +
	                      tex2Dlod(KTV_Color, float4(uv - float2(0.0, 0.03), 0.0, 0.0)).rgb) * 0.25;
	float glow = saturate((KTV_Luma(local_light) - 0.6) * 2.5);
	result += local_light * dirt * glow * KTV_Dirt * KTV_Intensity;

	// Grana animata, piu' presente nelle ombre
	float noise = Hash(position.xy + frac(KTV_Timer * 0.001) * 517.0) - 0.5;
	float shadows = 1.0 - KTV_Luma(result) * 0.7;
	result += noise * 0.08 * KTV_Grain * KTV_Intensity * shadows;

	return KTV_Split(color, saturate(result), uv);
}

technique KarmaTV_LensExtras < ui_label = "KarmaTV - Lente (aberrazione, grana, sporco)"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_LensExtras;
	}
}
