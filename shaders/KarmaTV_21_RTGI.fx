// KarmaTV PostFX - Luce indiretta (SSGI, illuminazione globale in screen-space).
// Ogni superficie raccoglie la luce dei pixel vicini verso cui e' rivolta: un montante
// rivolto verso il finestrino si schiarisce e prende il colore della luce esterna,
// la plancia rivolta verso il basso no. E' la parte della "luce DLSS5" che una LUT
// non puo' riprodurre, perche' dipende da dove si trova la superficie.
// Calcolata a meta' risoluzione, ripulita con una sfocatura che rispetta la profondita'.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_GIRadius < ui_type = "slider"; ui_label = "Raggio (m)"; ui_min = 0.3; ui_max = 4.0; > = 1.5;
uniform float KTV_GIFovY < ui_type = "slider"; ui_label = "Campo visivo verticale (gradi)"; ui_min = 20.0; ui_max = 90.0; > = 40.0;
uniform float KTV_GIShadowsOnly < ui_type = "slider"; ui_label = "Solo nelle zone scure"; ui_min = 0.0; ui_max = 1.0; > = 0.7;

texture KTV_GITex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RGBA16F; };
texture KTV_GIBlurTex { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RGBA16F; };
sampler KTV_GI { Texture = KTV_GITex; };
sampler KTV_GIBlur { Texture = KTV_GIBlurTex; };

#define KTV_GI_DIRECTIONS 8
#define KTV_GI_STEPS 4
#define KTV_GI_ASPECT (BUFFER_WIDTH * BUFFER_RCP_HEIGHT)

float GITanHalfFov()
{
	return tan(radians(KTV_GIFovY) * 0.5);
}

float3 GIViewPosition(float2 uv)
{
	float z = KTV_LinearDepth(uv) * KTV_DepthFar;
	float2 ndc = uv * 2.0 - 1.0;
	float t = GITanHalfFov();
	return float3(ndc.x * t * KTV_GI_ASPECT * z, -ndc.y * t * z, z);
}

float3 GIViewNormal(float2 uv, float3 center)
{
	float2 p = KTV_PIXEL;
	float3 right = GIViewPosition(uv + float2(p.x, 0.0));
	float3 left = GIViewPosition(uv - float2(p.x, 0.0));
	float3 down = GIViewPosition(uv + float2(0.0, p.y));
	float3 up = GIViewPosition(uv - float2(0.0, p.y));
	float3 dx = abs(right.z - center.z) < abs(center.z - left.z) ? right - center : center - left;
	float3 dy = abs(down.z - center.z) < abs(center.z - up.z) ? down - center : center - up;
	float3 n = normalize(cross(dy, dx));
	return dot(n, center) > 0.0 ? -n : n;
}

float GINoise(float2 position)
{
	return frac(52.9829189 * frac(dot(position, float2(0.06711056, 0.00583715))));
}

// RGB = luce raccolta, A = profondita' lineare (per la sfocatura)
float4 PS_Gather(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float depth = KTV_LinearDepth(uv);
	if (!KTV_DepthValid || depth > 0.999)
		return float4(0.0, 0.0, 0.0, depth);

	float3 center = GIViewPosition(uv);
	float3 normal = GIViewNormal(uv, center);
	float radius_uv = min(KTV_GIRadius / (center.z * GITanHalfFov() * 2.0), 0.25);
	float2 radius = float2(radius_uv / KTV_GI_ASPECT, radius_uv);
	float max_distance = KTV_GIRadius * 2.0;

	float noise = GINoise(position.xy);
	float3 light = float3(0.0, 0.0, 0.0);
	float weight_sum = 0.0;
	// [loop]: srotolati, i 32 campioni (ognuno con lettura della profondita') superano i
	// limiti del compilatore D3D (errore X3511)
	[loop]
	for (int d = 0; d < KTV_GI_DIRECTIONS; ++d)
	{
		float angle = (d + noise) * (6.2831853 / KTV_GI_DIRECTIONS);
		float2 direction = float2(cos(angle), sin(angle)) * radius;
		[loop]
		for (int s = 1; s <= KTV_GI_STEPS; ++s)
		{
			float t = (s - 0.5 + frac(noise * 7.0 + s * 0.37)) / KTV_GI_STEPS;
			float2 sample_uv = uv + direction * t;
			if (any(sample_uv < 0.0) || any(sample_uv > 1.0))
				continue;
			float3 sample_position = GIViewPosition(sample_uv);
			float3 v = sample_position - center;
			float distance = length(v);
			// Oltre il raggio (es. l'esterno visto dal finestrino) la luce arriva comunque,
			// ma da una direzione limitata alla distanza massima: e' la luce che entra dai vetri.
			float3 to_light = v / max(distance, 1e-4);
			float facing = saturate(dot(normal, to_light));
			float falloff = distance < max_distance ? 1.0 - distance / max_distance * 0.5 : 0.5;
			float w = facing * falloff;
			light += tex2Dlod(KTV_Color, float4(sample_uv, 0.0, 0.0)).rgb * w;
			weight_sum += 1.0;
		}
	}
	return float4(light / max(weight_sum, 1.0) * 2.0, depth);
}

#define KTV_GI_BLUR(name, source, axis) \
	float4 name(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target \
	{ \
		float4 center = tex2D(source, uv); \
		float3 sum = center.rgb; \
		float weight = 1.0; \
		[unroll] \
		for (int i = -4; i <= 4; ++i) \
		{ \
			if (i == 0) continue; \
			float4 s = tex2D(source, uv + axis * KTV_PIXEL * 3.0 * i); \
			float w = exp(-abs(s.a - center.a) * 300.0) * (1.0 - abs(i) / 5.0); \
			sum += s.rgb * w; \
			weight += w; \
		} \
		return float4(sum / weight, center.a); \
	}

KTV_GI_BLUR(PS_GIBlurH, KTV_GI, float2(1.0, 0.0))
KTV_GI_BLUR(PS_GIBlurV, KTV_GIBlur, float2(0.0, 1.0))

float3 PS_GIComposite(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass || !KTV_DepthValid)
		return color;

	float3 light = tex2D(KTV_GI, uv).rgb;
	// La luce rimbalzata prende il colore della superficie (albedo stimata dal colore)
	float luma = KTV_Luma(color);
	float3 albedo = saturate(color / max(luma, 0.05) * 0.5 + 0.25);
	// Soprattutto nelle zone scure: le parti gia' illuminate cambiano poco
	float shadows = lerp(1.0, saturate(1.0 - luma * 1.4), KTV_GIShadowsOnly);
	float3 result = color + light * albedo * shadows * KTV_Intensity * 0.6;
	return KTV_Split(color, saturate(result), uv);
}

technique KarmaTV_RTGI < ui_label = "KarmaTV - Luce indiretta (SSGI)"; >
{
	pass Gather { VertexShader = KTV_VS; PixelShader = PS_Gather; RenderTarget = KTV_GITex; }
	pass BlurH { VertexShader = KTV_VS; PixelShader = PS_GIBlurH; RenderTarget = KTV_GIBlurTex; }
	pass BlurV { VertexShader = KTV_VS; PixelShader = PS_GIBlurV; RenderTarget = KTV_GITex; }
	pass Composite { VertexShader = KTV_VS; PixelShader = PS_GIComposite; }
}
