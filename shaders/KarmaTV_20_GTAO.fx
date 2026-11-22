// KarmaTV PostFX - Occlusione ambientale (fase 1).
// Occlusione basata sull'orizzonte (famiglia HBAO/GTAO), calcolata a meta'
// risoluzione e ripulita con una sfocatura che rispetta i bordi di profondita'.
// Scurisce angoli, fessure e contatti (abitacolo, gomme sull'asfalto, barriere).
// Richiede il depth buffer: controllarlo con KarmaTV_99_Debug.fx.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità"; ui_min = 0.0; ui_max = 1.0; > = 0.7;
uniform float KTV_AORadius < ui_type = "slider"; ui_label = "Raggio (m)"; ui_min = 0.2; ui_max = 4.0; > = 1.2;
uniform float KTV_AOFovY < ui_type = "slider"; ui_label = "Campo visivo verticale (gradi)"; ui_min = 20.0; ui_max = 90.0; > = 40.0;
uniform float KTV_AOMaxDistance < ui_type = "slider"; ui_label = "Distanza massima (m)"; ui_min = 20.0; ui_max = 800.0; > = 250.0;

texture KTV_AOTex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RG16F; };
sampler KTV_AO { Texture = KTV_AOTex; };
texture KTV_AOBlurTex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RG16F; };
sampler KTV_AOBlur { Texture = KTV_AOBlurTex; };

#define KTV_AO_DIRECTIONS 6
#define KTV_AO_STEPS 4
#define KTV_ASPECT (BUFFER_WIDTH * BUFFER_RCP_HEIGHT)

float TanHalfFov()
{
	return tan(radians(KTV_AOFovY) * 0.5);
}

// Posizione in spazio camera, in metri.
float3 ViewPosition(float2 uv)
{
	float z = KTV_LinearDepth(uv) * KTV_DEPTH_FAR_PLANE;
	float2 ndc = uv * 2.0 - 1.0;
	float t = TanHalfFov();
	return float3(ndc.x * t * KTV_ASPECT * z, -ndc.y * t * z, z);
}

float3 ViewNormal(float2 uv, float3 center)
{
	float2 p = KTV_PIXEL;
	float3 right = ViewPosition(uv + float2(p.x, 0.0));
	float3 left = ViewPosition(uv - float2(p.x, 0.0));
	float3 down = ViewPosition(uv + float2(0.0, p.y));
	float3 up = ViewPosition(uv - float2(0.0, p.y));
	// Differenza piu' piccola su ciascun asse: evita normali sbagliate sui bordi.
	float3 dx = abs(right.z - center.z) < abs(center.z - left.z) ? right - center : center - left;
	float3 dy = abs(down.z - center.z) < abs(center.z - up.z) ? down - center : center - up;
	float3 n = normalize(cross(dy, dx));
	return dot(n, center) > 0.0 ? -n : n; // verso la camera
}

float Noise(float2 position)
{
	return frac(52.9829189 * frac(dot(position, float2(0.06711056, 0.00583715))));
}

// R = occlusione (1 = nessuna), G = profondita' lineare per la sfocatura
float2 PS_AO(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float depth = KTV_LinearDepth(uv);
	float3 center = ViewPosition(uv);
	if (depth > 0.999 || center.z > KTV_AOMaxDistance)
		return float2(1.0, depth);

	float3 normal = ViewNormal(uv, center);
	float radius_uv = KTV_AORadius / (center.z * TanHalfFov() * 2.0);
	radius_uv = min(radius_uv, 0.15); // limita il costo a distanza ravvicinata
	float2 radius = float2(radius_uv / KTV_ASPECT, radius_uv);

	float noise = Noise(position.xy);
	float occlusion = 0.0;
	[unroll]
	for (int d = 0; d < KTV_AO_DIRECTIONS; ++d)
	{
		float angle = (d + noise) * (6.2831853 / KTV_AO_DIRECTIONS);
		float2 direction = float2(cos(angle), sin(angle)) * radius;
		[unroll]
		for (int s = 0; s < KTV_AO_STEPS; ++s)
		{
			float t = (s + frac(noise + s * 0.618)) / KTV_AO_STEPS;
			float3 sample_position = ViewPosition(uv + direction * t);
			float3 v = sample_position - center;
			float distance_sq = dot(v, v);
			float n_dot_v = dot(normal, v) * rsqrt(max(distance_sq, 1e-6));
			float falloff = saturate(1.0 - distance_sq / (KTV_AORadius * KTV_AORadius));
			occlusion += saturate(n_dot_v - 0.1) * falloff;
		}
	}
	occlusion /= KTV_AO_DIRECTIONS * KTV_AO_STEPS;

	float fade = 1.0 - smoothstep(KTV_AOMaxDistance * 0.7, KTV_AOMaxDistance, center.z);
	return float2(saturate(1.0 - occlusion * 2.0 * fade), depth);
}

// Sfocatura separabile che rispetta la profondita' (orizzontale poi verticale)
#define KTV_AO_BLUR(name, source, axis) \
	float2 name(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target \
	{ \
		float2 center = tex2D(source, uv).xy; \
		float sum = center.x; \
		float weight = 1.0; \
		[unroll] \
		for (int i = -4; i <= 4; ++i) \
		{ \
			if (i == 0) continue; \
			float2 s = tex2D(source, uv + axis * KTV_PIXEL * 2.0 * i).xy; \
			float w = exp(-abs(s.y - center.y) * 400.0) * (1.0 - abs(i) / 5.0); \
			sum += s.x * w; \
			weight += w; \
		} \
		return float2(sum / weight, center.y); \
	}

KTV_AO_BLUR(PS_BlurH, KTV_AO, float2(1.0, 0.0))
KTV_AO_BLUR(PS_BlurV, KTV_AOBlur, float2(0.0, 1.0))

float3 PS_Composite(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass || !KTV_DepthValid)
		return color;
	float ao = tex2D(KTV_AO, uv).x;
	float3 result = color * lerp(1.0, ao, KTV_Intensity);
	return KTV_Split(color, result, uv);
}

technique KarmaTV_GTAO < ui_label = "KarmaTV - Occlusione ambientale"; >
{
	pass AO { VertexShader = KTV_VS; PixelShader = PS_AO; RenderTarget = KTV_AOTex; }
	pass BlurH { VertexShader = KTV_VS; PixelShader = PS_BlurH; RenderTarget = KTV_AOBlurTex; }
	pass BlurV { VertexShader = KTV_VS; PixelShader = PS_BlurV; RenderTarget = KTV_AOTex; }
	pass Composite { VertexShader = KTV_VS; PixelShader = PS_Composite; }
}
