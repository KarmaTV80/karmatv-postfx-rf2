// KarmaTV PostFX - Nitidezza FSR 1 (RCAS).
// FSR 1 ha due parti: EASU (ingrandimento) e RCAS (nitidezza). L'ingrandimento richiede
// che il gioco disegni a risoluzione piu' bassa, cosa che un add-on non puo' imporre a rF2:
// qui c'e' quindi solo RCAS, la nitidezza "robusta" di FSR, implementata secondo
// l'algoritmo pubblicato da AMD (FidelityFX, licenza MIT). Rispetto al CAS limita meglio gli
// aloni sui bordi e ha la riduzione del rumore: usarne uno dei due, non entrambi.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Nitidezza"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_RcasDenoise < ui_type = "slider"; ui_label = "Riduzione del rumore"; ui_min = 0.0; ui_max = 1.0; > = 0.5;

// Limite del lobo di RCAS: impedisce aloni oltre una certa forza
#define KTV_RCAS_LIMIT (0.25 - (1.0 / 16.0))

float3 PS_RCAS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 e = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return e;
	float2 p = KTV_PIXEL;
	//    b
	//  d e f
	//    h
	float3 b = tex2D(KTV_Color, uv + float2(0.0, -p.y)).rgb;
	float3 d = tex2D(KTV_Color, uv + float2(-p.x, 0.0)).rgb;
	float3 f = tex2D(KTV_Color, uv + float2(p.x, 0.0)).rgb;
	float3 h = tex2D(KTV_Color, uv + float2(0.0, p.y)).rgb;

	// Riduzione del rumore: sui pixel isolati (grana, scintillii) la nitidezza si attenua
	float lb = KTV_Luma(b), ld = KTV_Luma(d), le = KTV_Luma(e), lf = KTV_Luma(f), lh = KTV_Luma(h);
	float noise = 0.25 * (lb + ld + lf + lh) - le;
	float range = max(max(max(lb, ld), max(lf, lh)), le) - min(min(min(lb, ld), min(lf, lh)), le);
	noise = saturate(abs(noise) / max(range, 1e-4));
	noise = 1.0 - 0.5 * noise * KTV_RcasDenoise * 2.0;

	// Lobo massimo che non porta nessun canale fuori da [0, 1] rispetto ai 4 vicini
	float3 mn4 = min(min(b, d), min(f, h));
	float3 mx4 = max(max(b, d), max(f, h));
	float3 hit_min = mn4 / max(4.0 * mx4, 1e-5);
	float3 hit_max = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1e-5);
	float3 lobe_rgb = max(-hit_min, hit_max);
	float lobe = max(-KTV_RCAS_LIMIT, min(max(lobe_rgb.r, max(lobe_rgb.g, lobe_rgb.b)), 0.0));
	// Intensita' 1 = nitidezza piena di FSR; 0 = nessun effetto (circa 2 stop in meno)
	lobe *= KTV_Intensity * exp2(-(1.0 - KTV_Intensity) * 2.0) * noise;

	float3 result = saturate((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0));
	return KTV_Split(e, result, uv);
}

technique KarmaTV_FSR1 < ui_label = "KarmaTV - Nitidezza FSR 1 (RCAS)"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_RCAS;
	}
}
