// KarmaTV PostFX - Viste di debug (fase 1).
// Serve a verificare che il depth buffer sia quello giusto e a tarare
// KTV_DEPTH_REVERSED / KTV_DEPTH_UPSIDE_DOWN / KTV_DEPTH_FAR_PLANE.
// Immagine corretta: auto e oggetti vicini scuri, orizzonte chiaro.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < hidden = true; > = 1.0;
uniform int KTV_DebugMode < ui_type = "combo"; ui_label = "Vista"; ui_items = "Profondità\0Normali ricostruite\0Metà immagine, metà profondità\0"; > = 0;

float3 NormalFromDepth(float2 uv)
{
	float center = KTV_LinearDepth(uv);
	float right = KTV_LinearDepth(uv + float2(KTV_PIXEL.x, 0.0));
	float down = KTV_LinearDepth(uv + float2(0.0, KTV_PIXEL.y));
	float3 dx = float3(KTV_PIXEL.x, 0.0, right - center);
	float3 dy = float3(0.0, KTV_PIXEL.y, down - center);
	return normalize(cross(dx, dy));
}

float3 PS_Debug(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	if (KTV_Bypass || !KTV_DepthValid)
		return tex2D(KTV_Color, uv).rgb;
	float depth = KTV_LinearDepth(uv);
	if (KTV_DebugMode == 1)
		return NormalFromDepth(uv) * 0.5 + 0.5;
	if (KTV_DebugMode == 2 && uv.x < 0.5)
		return tex2D(KTV_Color, uv).rgb;
	return depth.xxx;
}

technique KarmaTV_Debug < ui_label = "KarmaTV - Viste di debug"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_Debug;
	}
}
