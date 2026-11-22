// KarmaTV PostFX - Tonemapping ed esposizione automatica (fase 1).
// rF2 consegna un'immagine gia' in SDR: la si riporta in un HDR approssimato
// (tonemap inverso), si applica l'esposizione (manuale + automatica con
// adattamento nel tempo, come l'occhio all'uscita da un tunnel) e poi un
// tonemapper moderno.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Miscela con l'originale"; ui_min = 0.0; ui_max = 1.0; > = 0.8;
uniform int KTV_Mode < ui_type = "combo"; ui_label = "Tonemapper"; ui_items = "Neutro\0ACES\0AgX\0"; > = 0;
uniform float KTV_Exposure < ui_type = "slider"; ui_label = "Esposizione (EV)"; ui_min = -2.0; ui_max = 2.0; > = 0.0;
uniform float KTV_HdrRecovery < ui_type = "slider"; ui_label = "Recupero alte luci"; ui_min = 0.0; ui_max = 0.95; > = 0.6;
uniform float KTV_AutoExposure < ui_type = "slider"; ui_label = "Esposizione automatica"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_AdaptSpeed < ui_type = "slider"; ui_label = "Velocita' di adattamento"; ui_min = 0.1; ui_max = 5.0; > = 1.5;
uniform float KTV_FrameTime < source = "frametime"; >;

// Quattro zone (passaggi sfumati): cruscotto, abitacolo, pista, sfondo e cielo. Ogni zona
// aggiunge la sua esposizione e il suo recupero delle alte luci a quelli generali.
// - Abitacolo: le superfici vicine alla camera (profondita').
// - Cruscotto: le superfici vicine dentro un riquadro regolabile al centro (plancia,
//   strumenti, volante). Fianchi sotto i finestrini, pilota e gabbia restano abitacolo.
// - Cruscotto e abitacolo valgono solo nella visuale abitacolo (parte vicina che occupa una
//   buona parte dello schermo): nelle camere esterne e TV restano solo pista e sfondo.
uniform float KTV_DashEV < ui_type = "slider"; ui_label = "Cruscotto: esposizione (EV)"; ui_min = -2.0; ui_max = 2.0; > = 0.0;
uniform float KTV_DashRecovery < ui_type = "slider"; ui_label = "Cruscotto: alte luci"; ui_min = -0.6; ui_max = 0.35; > = 0.0;
uniform float KTV_CockpitEV < ui_type = "slider"; ui_label = "Abitacolo: esposizione (EV)"; ui_min = -2.0; ui_max = 2.0; > = 0.0;
uniform float KTV_CockpitRecovery < ui_type = "slider"; ui_label = "Abitacolo: alte luci"; ui_min = -0.6; ui_max = 0.35; > = 0.0;
uniform float KTV_TrackEV < ui_type = "slider"; ui_label = "Pista: esposizione (EV)"; ui_min = -2.0; ui_max = 2.0; > = 0.0;
uniform float KTV_TrackRecovery < ui_type = "slider"; ui_label = "Pista: alte luci"; ui_min = -0.6; ui_max = 0.35; > = 0.0;
uniform float KTV_FarEV < ui_type = "slider"; ui_label = "Sfondo e cielo: esposizione (EV)"; ui_min = -2.0; ui_max = 2.0; > = 0.0;
uniform float KTV_FarRecovery < ui_type = "slider"; ui_label = "Sfondo e cielo: alte luci"; ui_min = -0.6; ui_max = 0.35; > = 0.0;
uniform float KTV_DashCenter < ui_type = "slider"; ui_label = "Cruscotto: centro (orizzontale)"; ui_min = 0.2; ui_max = 0.8; > = 0.5;
uniform float KTV_DashWidth < ui_type = "slider"; ui_label = "Cruscotto: larghezza"; ui_min = 0.05; ui_max = 0.9; > = 0.26;
uniform float KTV_DashTop < ui_type = "slider"; ui_label = "Cruscotto: bordo alto"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_DashBottom < ui_type = "slider"; ui_label = "Cruscotto: bordo basso"; ui_min = 0.0; ui_max = 1.0; > = 0.85;
uniform float KTV_CockpitDistance < ui_type = "slider"; ui_label = "Fine dell'abitacolo (profondità)"; ui_min = 0.5; ui_max = 30.0; > = 10.0;
uniform float KTV_FarDistance < ui_type = "slider"; ui_label = "Inizio dello sfondo (m)"; ui_min = 50.0; ui_max = 2000.0; > = 400.0;
uniform int KTV_CameraCockpit < hidden = true; > = -1; // dall'add-on: 1 abitacolo, 0 altra telecamera, -1 non nota
uniform int KTV_ShowZones < ui_type = "combo"; ui_label = "Mostra le zone"; ui_items = "No\0Sì\0"; > = 0;

#define KTV_ZONE_W (BUFFER_WIDTH / 8)
#define KTV_ZONE_H (BUFFER_HEIGHT / 8)

// Parte vicina sullo schermo; il mip piu' piccolo e' la media = quanto abitacolo si vede
texture KTV_ZoneViewTex { Width = 64; Height = 64; Format = R8; MipLevels = 7; };
sampler KTV_ZoneView { Texture = KTV_ZoneViewTex; };

bool ZoneSky(float2 uv)
{
	return KTV_LinearDepth(uv) > 0.999;
}

// Superfici vicine alla camera: abitacolo, pilota, carrozzeria vista da dentro
float ZoneNear(float2 uv)
{
	float z = KTV_LinearDepth(uv) * KTV_DepthFar;
	return 1.0 - smoothstep(KTV_CockpitDistance * 0.8, KTV_CockpitDistance * 1.25, z);
}

float PS_ZoneView(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return KTV_DepthValid ? step(0.5, ZoneNear(uv)) : 0.0;
}

// Pesi delle zone: x = cruscotto, y = abitacolo, z = pista, w = sfondo e cielo
float4 ZoneWeights(float2 uv)
{
	if (!KTV_DepthValid)
		return float4(0.0, 0.0, 1.0, 0.0);
	float z = KTV_LinearDepth(uv) * KTV_DepthFar;
	// Visuale abitacolo: almeno il 12-20% dello schermo e' vicino alla camera
	// Se rF2 dice quale telecamera e' attiva (buffer Graphics del plugin) le zone di cruscotto
	// e abitacolo valgono solo nella visuale abitacolo; altrimenti si stima dall'immagine.
	float view = tex2Dlod(KTV_ZoneView, float4(0.5, 0.5, 0.0, 6.0)).x;
	float in_cockpit = KTV_CameraCockpit < 0 ? smoothstep(0.12, 0.2, view) : float(KTV_CameraCockpit);
	float near_zone = ZoneNear(uv) * in_cockpit;
	// Cruscotto: superfici vicine dentro il riquadro regolabile (plancia, strumenti, volante);
	// il resto della parte vicina (fianchi, pilota, gabbia, tetto) e' abitacolo
	float half_width = KTV_DashWidth * 0.5;
	float inside_x = smoothstep(half_width + 0.015, half_width - 0.015, abs(uv.x - KTV_DashCenter));
	float inside_y = smoothstep(KTV_DashTop - 0.015, KTV_DashTop + 0.015, uv.y) *
	                 (1.0 - smoothstep(KTV_DashBottom - 0.015, KTV_DashBottom + 0.015, uv.y));
	float dash = near_zone * inside_x * inside_y;
	float cockpit = near_zone - dash;
	// Cielo: media di 5 punti, cosi' il bordo con alberi e tribune resta morbido
	float2 p = KTV_PIXEL * 1.5;
	float sky = (ZoneSky(uv) + ZoneSky(uv + float2(p.x, 0.0)) + ZoneSky(uv - float2(p.x, 0.0)) +
	             ZoneSky(uv + float2(0.0, p.y)) + ZoneSky(uv - float2(0.0, p.y))) / 5.0;
	float far_zone = max(sky, smoothstep(KTV_FarDistance * 0.6, KTV_FarDistance, z));
	far_zone = min(far_zone, saturate(1.0 - near_zone));
	return float4(dash, cockpit, saturate(1.0 - near_zone - far_zone), far_zone);
}

texture KTV_LumTex { Width = 256; Height = 256; Format = R16F; MipLevels = 9; };
sampler KTV_Lum { Texture = KTV_LumTex; };
texture KTV_AdaptTex { Width = 1; Height = 1; Format = R16F; };
sampler KTV_Adapt { Texture = KTV_AdaptTex; };
texture KTV_AdaptPrevTex { Width = 1; Height = 1; Format = R16F; };
sampler KTV_AdaptPrev { Texture = KTV_AdaptPrevTex; };

float3 InverseTonemap(float3 color, float strength)
{
	float3 linear_color = pow(saturate(color), 2.2);
	return linear_color / max(1.0 - linear_color * strength, 0.05);
}

float3 AcesFitted(float3 x)
{
	return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 AgxContrast(float3 x)
{
	float3 x2 = x * x;
	float3 x4 = x2 * x2;
	return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// AgX "minimal": risultato gia' codificato per il display.
float3 AgX(float3 linear_color)
{
	const float3x3 agx = float3x3(
		0.842479062253094, 0.0423282422610123, 0.0423756549057051,
		0.0784335999999992, 0.878468636469772, 0.0784336,
		0.0792237451477643, 0.0791661274605434, 0.879142973793104);
	const float3x3 agx_inverse = float3x3(
		1.19687900512017, -0.0528968517574562, -0.0529716355144438,
		-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
		-0.0990297440797205, -0.0989611768448433, 1.15107367264116);
	const float min_ev = -12.47393;
	const float max_ev = 4.026069;

	float3 v = mul(linear_color, agx);
	v = clamp(log2(max(v, 1e-10)), min_ev, max_ev);
	v = (v - min_ev) / (max_ev - min_ev);
	v = AgxContrast(v);
	v = mul(v, agx_inverse);
	return saturate(v);
}

// 1. Luminanza logaritmica a 256x256 (i mip arrivano fino a 1x1 = media della scena)
float PS_Luminance(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 hdr = InverseTonemap(tex2D(KTV_Color, uv).rgb, KTV_HdrRecovery);
	return log2(max(KTV_Luma(hdr), 1e-4));
}

// 2. Adattamento nel tempo verso la luminanza media
float PS_Adapt(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float previous = tex2Dlod(KTV_AdaptPrev, float4(0.5, 0.5, 0.0, 0.0)).x;
	if (KTV_Bypass)
		return previous;
	float average = exp2(tex2Dlod(KTV_Lum, float4(0.5, 0.5, 0.0, 8.0)).x);
	if (previous <= 0.0)
		return average; // primo frame
	float blend = 1.0 - exp(-KTV_FrameTime * 0.001 * KTV_AdaptSpeed);
	return lerp(previous, average, saturate(blend));
}

// 3. Memoria del valore per il frame successivo
float PS_StorePrevious(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	return tex2Dlod(KTV_Adapt, float4(0.5, 0.5, 0.0, 0.0)).x;
}

// 4. Esposizione e tonemapping
float3 PS_Tonemap(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;

	float adapted = max(tex2Dlod(KTV_Adapt, float4(0.5, 0.5, 0.0, 0.0)).x, 1e-4);
	float auto_exposure = clamp(0.18 / adapted, 0.35, 3.0);
	float4 zones = ZoneWeights(uv);
	if (KTV_ShowZones != 0)
	{
		// Anteprima delle zone: giallo cruscotto, rosso abitacolo, verde pista, blu sfondo
		float3 tint = zones.x * float3(1.0, 0.85, 0.1) + zones.y * float3(1.0, 0.2, 0.2) +
		              zones.z * float3(0.2, 0.9, 0.3) + zones.w * float3(0.25, 0.45, 1.0);
		return lerp(color, tint * (0.35 + KTV_Luma(color)), 0.6);
	}
	float zone_ev = dot(zones, float4(KTV_DashEV, KTV_CockpitEV, KTV_TrackEV, KTV_FarEV));
	float recovery = clamp(KTV_HdrRecovery + dot(zones, float4(KTV_DashRecovery, KTV_CockpitRecovery, KTV_TrackRecovery, KTV_FarRecovery)), 0.0, 0.95);
	float exposure = exp2(KTV_Exposure + zone_ev) * lerp(1.0, auto_exposure, KTV_AutoExposure);

	float3 hdr = InverseTonemap(color, recovery) * exposure;
	float3 mapped;
	if (KTV_Mode == 0)
	{
		// Neutro: inverso esatto del tonemap inverso. A esposizione 1 l'immagine resta
		// identica; ogni EV in piu' o in meno si vede chiaramente, con le alte luci compresse.
		float3 lin = hdr / (1.0 + hdr * recovery);
		mapped = pow(saturate(lin), 1.0 / 2.2);
	}
	else if (KTV_Mode == 1)
	{
		mapped = pow(AcesFitted(hdr), 1.0 / 2.2);
	}
	else
	{
		mapped = AgX(hdr);
	}
	float3 result = lerp(color, mapped, KTV_Intensity);
	return KTV_Split(color, result, uv);
}

technique KarmaTV_Tonemap < ui_label = "KarmaTV - Tonemapping ed esposizione"; >
{
	pass ZoneView { VertexShader = KTV_VS; PixelShader = PS_ZoneView; RenderTarget = KTV_ZoneViewTex; }
	pass Luminance { VertexShader = KTV_VS; PixelShader = PS_Luminance; RenderTarget = KTV_LumTex; }
	pass Adapt { VertexShader = KTV_VS; PixelShader = PS_Adapt; RenderTarget = KTV_AdaptTex; }
	pass StorePrevious { VertexShader = KTV_VS; PixelShader = PS_StorePrevious; RenderTarget = KTV_AdaptPrevTex; }
	pass Tonemap { VertexShader = KTV_VS; PixelShader = PS_Tonemap; }
}
