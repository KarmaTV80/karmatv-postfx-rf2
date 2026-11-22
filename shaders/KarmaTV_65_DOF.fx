// KarmaTV PostFX - Profondita' di campo (fase 2, profilo Replay).
// Messa a fuoco automatica sul centro dello schermo (con un breve inseguimento,
// come un operatore), sfocatura a disco calcolata a meta' risoluzione e mescolata
// in base alla distanza dal piano di fuoco. Sconsigliata in gara.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Sfocatura"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_FocusRange < ui_type = "slider"; ui_label = "Profondità del fuoco"; ui_min = 0.05; ui_max = 2.0; > = 0.4;
uniform float KTV_FocusSpeed < ui_type = "slider"; ui_label = "Velocità della messa a fuoco"; ui_min = 0.5; ui_max = 10.0; > = 3.0;
uniform float KTV_FrameTime < source = "frametime"; >;

texture KTV_FocusTex { Width = 1; Height = 1; Format = R32F; };
texture KTV_FocusPrevTex { Width = 1; Height = 1; Format = R32F; };
texture KTV_DofBlurTex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RGBA16F; };
sampler KTV_Focus { Texture = KTV_FocusTex; };
sampler KTV_FocusPrev { Texture = KTV_FocusPrevTex; };
sampler KTV_DofBlur { Texture = KTV_DofBlurTex; };

// Messa a fuoco: media della profondita' al centro, inseguita nel tempo
float PS_Focus(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float target = 0.0;
	[unroll]
	for (int i = -2; i <= 2; ++i)
		target += KTV_LinearDepth(float2(0.5 + i * 0.02, 0.5)) + KTV_LinearDepth(float2(0.5, 0.5 + i * 0.03));
	target /= 10.0;

	float previous = tex2Dlod(KTV_FocusPrev, float4(0.5, 0.5, 0.0, 0.0)).x;
	if (previous <= 0.0)
		return target;
	return lerp(previous, target, saturate(1.0 - exp(-KTV_FrameTime * 0.001 * KTV_FocusSpeed)));
}

float PS_StoreFocus(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return tex2Dlod(KTV_Focus, float4(0.5, 0.5, 0.0, 0.0)).x;
}

float CircleOfConfusion(float2 uv)
{
	float focus = tex2Dlod(KTV_Focus, float4(0.5, 0.5, 0.0, 0.0)).x;
	float depth = KTV_LinearDepth(uv);
	// Differenza relativa dal piano di fuoco: oggetti al doppio della distanza = 1
	float difference = abs(depth - focus) / max(focus * KTV_FocusRange, 1e-4);
	return saturate(difference);
}

// Sfocatura a disco (16 campioni) con raggio proporzionale al cerchio di confusione
static const float2 KTV_Disk[16] = {
	float2(0.2770, 0.0), float2(0.1385, 0.2399), float2(-0.1385, 0.2399), float2(-0.2770, 0.0),
	float2(-0.1385, -0.2399), float2(0.1385, -0.2399), float2(0.6, 0.0), float2(0.4243, 0.4243),
	float2(0.0, 0.6), float2(-0.4243, 0.4243), float2(-0.6, 0.0), float2(-0.4243, -0.4243),
	float2(0.0, -0.6), float2(0.4243, -0.4243), float2(0.9, 0.3), float2(-0.9, -0.3) };

float4 PS_Blur(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float coc = CircleOfConfusion(uv);
	float2 radius = KTV_PIXEL * 24.0 * coc * KTV_Intensity;
	float3 sum = tex2D(KTV_Color, uv).rgb;
	float weight = 1.0;
	[unroll]
	for (int i = 0; i < 16; ++i)
	{
		float2 sample_uv = uv + KTV_Disk[i] * radius;
		// I campioni a fuoco non devono sbavare sullo sfondo
		float sample_weight = CircleOfConfusion(sample_uv) + 0.1;
		sum += tex2D(KTV_Color, sample_uv).rgb * sample_weight;
		weight += sample_weight;
	}
	return float4(sum / weight, coc);
}

float3 PS_DofCombine(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass || !KTV_DepthValid)
		return color;
	float coc = CircleOfConfusion(uv);
	float3 blurred = tex2D(KTV_DofBlur, uv).rgb;
	float3 result = lerp(color, blurred, smoothstep(0.05, 0.5, coc) * saturate(KTV_Intensity * 2.0));
	return KTV_Split(color, result, uv);
}

technique KarmaTV_DOF < ui_label = "KarmaTV - Profondità di campo"; >
{
	pass Focus { VertexShader = KTV_VS; PixelShader = PS_Focus; RenderTarget = KTV_FocusTex; }
	pass StoreFocus { VertexShader = KTV_VS; PixelShader = PS_StoreFocus; RenderTarget = KTV_FocusPrevTex; }
	pass Blur { VertexShader = KTV_VS; PixelShader = PS_Blur; RenderTarget = KTV_DofBlurTex; }
	pass Combine { VertexShader = KTV_VS; PixelShader = PS_DofCombine; }
}
