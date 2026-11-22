// KarmaTV PostFX - Motion blur della camera per i replay (fase 2).
// rF2 non fornisce i vettori di movimento, quindi il movimento si ricava confrontando
// il frame corrente con il precedente (flusso ottico):
//  1. luminanza ridotta a 1/4 e 1/16 della risoluzione;
//  2. a 1/16 si cerca, per ogni blocco, lo spostamento che meglio fa combaciare i due
//     frame (fino a ~64 pixel per frame alla risoluzione piena);
//  3. a 1/4 lo spostamento viene rifinito attorno a quello grezzo;
//  4. l'immagine viene sfumata lungo lo spostamento (otturatore regolabile).
// Pensato per i replay e le camere TV: nell'abitacolo meglio lasciarlo spento.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Otturatore"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_MaxBlur < ui_type = "slider"; ui_label = "Scia massima (% dello schermo)"; ui_min = 0.5; ui_max = 8.0; > = 3.0;

#define KTV_MB_W4 (BUFFER_WIDTH / 4)
#define KTV_MB_H4 (BUFFER_HEIGHT / 4)
#define KTV_MB_W16 (BUFFER_WIDTH / 16)
#define KTV_MB_H16 (BUFFER_HEIGHT / 16)
#define KTV_MB_SAMPLES 12

texture KTV_MBLuma4Tex { Width = KTV_MB_W4; Height = KTV_MB_H4; Format = R16F; };
texture KTV_MBLuma16Tex { Width = KTV_MB_W16; Height = KTV_MB_H16; Format = R16F; };
texture KTV_MBPrev4Tex { Width = KTV_MB_W4; Height = KTV_MB_H4; Format = R16F; };
texture KTV_MBPrev16Tex { Width = KTV_MB_W16; Height = KTV_MB_H16; Format = R16F; };
texture KTV_MBFlow16Tex { Width = KTV_MB_W16; Height = KTV_MB_H16; Format = RG16F; };
texture KTV_MBFlow4Tex { Width = KTV_MB_W4; Height = KTV_MB_H4; Format = RG16F; };

sampler KTV_MBLuma4 { Texture = KTV_MBLuma4Tex; AddressU = CLAMP; AddressV = CLAMP; };
sampler KTV_MBLuma16 { Texture = KTV_MBLuma16Tex; AddressU = CLAMP; AddressV = CLAMP; };
sampler KTV_MBPrev4 { Texture = KTV_MBPrev4Tex; AddressU = CLAMP; AddressV = CLAMP; };
sampler KTV_MBPrev16 { Texture = KTV_MBPrev16Tex; AddressU = CLAMP; AddressV = CLAMP; };
sampler KTV_MBFlow16 { Texture = KTV_MBFlow16Tex; AddressU = CLAMP; AddressV = CLAMP; };
sampler KTV_MBFlow4 { Texture = KTV_MBFlow4Tex; AddressU = CLAMP; AddressV = CLAMP; };

float PS_Luma4(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	// 4 letture bilineari = media di 4x4 pixel
	float2 p = KTV_PIXEL;
	float3 c = tex2D(KTV_Color, uv + float2(-p.x, -p.y)).rgb + tex2D(KTV_Color, uv + float2(p.x, -p.y)).rgb +
	           tex2D(KTV_Color, uv + float2(-p.x, p.y)).rgb + tex2D(KTV_Color, uv + float2(p.x, p.y)).rgb;
	return KTV_Luma(c * 0.25);
}

float PS_Luma16(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float2 p = 1.0 / float2(KTV_MB_W4, KTV_MB_H4);
	return (tex2D(KTV_MBLuma4, uv + float2(-p.x, -p.y)).x + tex2D(KTV_MBLuma4, uv + float2(p.x, -p.y)).x +
	        tex2D(KTV_MBLuma4, uv + float2(-p.x, p.y)).x + tex2D(KTV_MBLuma4, uv + float2(p.x, p.y)).x) * 0.25;
}

// Differenza tra un blocco 3x3 del frame corrente e lo stesso blocco spostato nel precedente
#define KTV_MB_COST(cur, prev, uv, offset, texel, result) \
	{ \
		result = 0.0; \
		[unroll] for (int ky = -1; ky <= 1; ++ky) \
		[unroll] for (int kx = -1; kx <= 1; ++kx) \
		{ \
			float2 k = float2(kx, ky) * texel; \
			result += abs(tex2Dlod(cur, float4(uv + k, 0.0, 0.0)).x - tex2Dlod(prev, float4(uv + k + offset, 0.0, 0.0)).x); \
		} \
	}

// Spostamento (in uv) del punto del frame precedente che corrisponde a questo pixel
float2 PS_FlowCoarse(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float2 texel = 1.0 / float2(KTV_MB_W16, KTV_MB_H16);
	float zero_cost;
	KTV_MB_COST(KTV_MBLuma16, KTV_MBPrev16, uv, float2(0.0, 0.0), texel, zero_cost);
	float best_cost = zero_cost;
	float2 best = float2(0.0, 0.0);
	[loop]
	for (int y = -4; y <= 4; ++y)
	{
		[loop]
		for (int x = -4; x <= 4; ++x)
		{
			float2 offset = float2(x, y) * texel;
			float cost;
			KTV_MB_COST(KTV_MBLuma16, KTV_MBPrev16, uv, offset, texel, cost);
			cost += 0.002 * length(float2(x, y)); // a parita', lo spostamento piu' piccolo
			if (cost < best_cost)
			{
				best_cost = cost;
				best = offset;
			}
		}
	}
	// Zone uniformi (cielo, asfalto liscio): il confronto non dice nulla, nessun movimento
	if (zero_cost - best_cost < 0.01)
		best = float2(0.0, 0.0);
	return best;
}

float2 PS_FlowFine(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float2 texel = 1.0 / float2(KTV_MB_W4, KTV_MB_H4);
	float2 start = tex2Dlod(KTV_MBFlow16, float4(uv, 0.0, 0.0)).xy;
	float best_cost;
	KTV_MB_COST(KTV_MBLuma4, KTV_MBPrev4, uv, start, texel, best_cost);
	float2 best = start;
	[loop]
	for (int y = -2; y <= 2; ++y)
	{
		[loop]
		for (int x = -2; x <= 2; ++x)
		{
			float2 offset = start + float2(x, y) * texel;
			float cost;
			KTV_MB_COST(KTV_MBLuma4, KTV_MBPrev4, uv, offset, texel, cost);
			if (cost < best_cost)
			{
				best_cost = cost;
				best = offset;
			}
		}
	}
	return best;
}

// Il frame corrente diventa il "precedente" del prossimo
float PS_Store4(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return tex2D(KTV_MBLuma4, uv).x;
}

float PS_Store16(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return tex2D(KTV_MBLuma16, uv).x;
}

float3 PS_Blur(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;

	// Il pixel arriva da uv + flow: la scia va nella direzione opposta
	float2 velocity = -tex2D(KTV_MBFlow4, uv).xy * KTV_Intensity;
	float max_length = KTV_MaxBlur * 0.01;
	float len = length(velocity * float2(BUFFER_WIDTH * BUFFER_RCP_HEIGHT, 1.0));
	if (len > max_length)
		velocity *= max_length / len;
	if (len * BUFFER_HEIGHT < 1.0)
		return KTV_Split(color, color, uv);

	float noise = frac(52.9829189 * frac(dot(position.xy, float2(0.06711056, 0.00583715))));
	float3 sum = color;
	float weight = 1.0;
	float center_depth = KTV_DepthValid ? KTV_LinearDepth(uv) : 0.0;
	[loop]
	for (int i = 0; i < KTV_MB_SAMPLES; ++i)
	{
		float t = (i + noise) / KTV_MB_SAMPLES - 0.5;
		float2 sample_uv = uv + velocity * t;
		float w = 1.0;
		// Gli oggetti molto piu' vicini (auto in primo piano) non sbavano sullo sfondo
		if (KTV_DepthValid)
			w = KTV_LinearDepth(sample_uv) + 0.002 < center_depth * 0.5 ? 0.2 : 1.0;
		sum += tex2Dlod(KTV_Color, float4(sample_uv, 0.0, 0.0)).rgb * w;
		weight += w;
	}
	return KTV_Split(color, sum / weight, uv);
}

technique KarmaTV_MotionBlur < ui_label = "KarmaTV - Motion blur (replay)"; >
{
	pass Luma4 { VertexShader = KTV_VS; PixelShader = PS_Luma4; RenderTarget = KTV_MBLuma4Tex; }
	pass Luma16 { VertexShader = KTV_VS; PixelShader = PS_Luma16; RenderTarget = KTV_MBLuma16Tex; }
	pass FlowCoarse { VertexShader = KTV_VS; PixelShader = PS_FlowCoarse; RenderTarget = KTV_MBFlow16Tex; }
	pass FlowFine { VertexShader = KTV_VS; PixelShader = PS_FlowFine; RenderTarget = KTV_MBFlow4Tex; }
	pass Store4 { VertexShader = KTV_VS; PixelShader = PS_Store4; RenderTarget = KTV_MBPrev4Tex; }
	pass Store16 { VertexShader = KTV_VS; PixelShader = PS_Store16; RenderTarget = KTV_MBPrev16Tex; }
	pass Blur { VertexShader = KTV_VS; PixelShader = PS_Blur; }
}
