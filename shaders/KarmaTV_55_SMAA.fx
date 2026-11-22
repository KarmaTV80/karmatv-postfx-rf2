// KarmaTV PostFX - Antialiasing di post-elaborazione (fase 2).
// Complementare all'MSAA di rF2: l'MSAA leviga i bordi dei poligoni, ma non le texture
// con trasparenza (reti, alberi, tribune, griglie) ne' le scalettature delle linee
// sottili. Qui si cercano i bordi nella luminanza, se ne segue l'andamento in entrambe
// le direzioni e si sfuma solo lungo il bordo. Implementazione propria, a passaggio unico.

#include "KarmaTV_Common.fxh"

uniform float KTV_Intensity < ui_type = "slider"; ui_label = "Intensità"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float KTV_AAThreshold < ui_type = "slider"; ui_label = "Sensibilità ai bordi"; ui_min = 0.0; ui_max = 1.0; > = 0.5;

#define KTV_AA_STEPS 10

float AALuma(float2 uv)
{
	// Radice della luminanza: risposta piu' uniforme tra ombre e luci
	return sqrt(KTV_Luma(saturate(tex2Dlod(KTV_Color, float4(uv, 0.0, 0.0)).rgb)));
}

float3 PS_AA(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	float3 color = tex2D(KTV_Color, uv).rgb;
	if (KTV_Bypass)
		return color;

	float2 p = KTV_PIXEL;
	float m = AALuma(uv);
	float n = AALuma(uv + float2(0.0, -p.y));
	float s = AALuma(uv + float2(0.0, p.y));
	float w = AALuma(uv + float2(-p.x, 0.0));
	float e = AALuma(uv + float2(p.x, 0.0));

	float lo = min(m, min(min(n, s), min(w, e)));
	float hi = max(m, max(max(n, s), max(w, e)));
	float range = hi - lo;
	// Soglia: 0 = solo i bordi netti, 1 = anche quelli deboli
	float threshold = lerp(0.25, 0.08, KTV_AAThreshold);
	if (range < max(0.03, hi * threshold))
		return color;

	float nw = AALuma(uv + float2(-p.x, -p.y));
	float ne = AALuma(uv + float2(p.x, -p.y));
	float sw = AALuma(uv + float2(-p.x, p.y));
	float se = AALuma(uv + float2(p.x, p.y));

	// Bordo orizzontale (variazione lungo y) o verticale (variazione lungo x)
	float horizontal = abs(nw + ne - 2.0 * n) + 2.0 * abs(w + e - 2.0 * m) + abs(sw + se - 2.0 * s);
	float vertical = abs(nw + sw - 2.0 * w) + 2.0 * abs(n + s - 2.0 * m) + abs(ne + se - 2.0 * e);
	bool is_horizontal = horizontal >= vertical;

	// Lato del bordo: il vicino con il salto di luminanza maggiore
	float l1 = is_horizontal ? n : w;
	float l2 = is_horizontal ? s : e;
	float g1 = abs(l1 - m);
	float g2 = abs(l2 - m);
	float step_length = is_horizontal ? p.y : p.x;
	float side_luma = g1 >= g2 ? l1 : l2;
	float gradient = max(g1, g2) * 0.25;
	if (g1 >= g2)
		step_length = -step_length;

	// Si parte a meta' tra il pixel e il vicino, sul bordo
	float2 edge_uv = uv;
	if (is_horizontal)
		edge_uv.y += step_length * 0.5;
	else
		edge_uv.x += step_length * 0.5;
	float edge_luma = (m + side_luma) * 0.5;
	float2 along = is_horizontal ? float2(p.x, 0.0) : float2(0.0, p.y);

	// Ricerca della fine del bordo nelle due direzioni, a passi crescenti
	float2 uv_neg = edge_uv - along;
	float2 uv_pos = edge_uv + along;
	float end_neg = AALuma(uv_neg) - edge_luma;
	float end_pos = AALuma(uv_pos) - edge_luma;
	bool done_neg = abs(end_neg) >= gradient;
	bool done_pos = abs(end_pos) >= gradient;
	[loop]
	for (int i = 1; i < KTV_AA_STEPS && !(done_neg && done_pos); ++i)
	{
		float stride = i < 4 ? 1.0 : (i < 7 ? 2.0 : 4.0);
		if (!done_neg)
		{
			uv_neg -= along * stride;
			end_neg = AALuma(uv_neg) - edge_luma;
			done_neg = abs(end_neg) >= gradient;
		}
		if (!done_pos)
		{
			uv_pos += along * stride;
			end_pos = AALuma(uv_pos) - edge_luma;
			done_pos = abs(end_pos) >= gradient;
		}
	}

	float dist_neg = is_horizontal ? uv.x - uv_neg.x : uv.y - uv_neg.y;
	float dist_pos = is_horizontal ? uv_pos.x - uv.x : uv_pos.y - uv.y;
	bool nearer_neg = dist_neg < dist_pos;
	float span = dist_neg + dist_pos;
	// Si sfuma solo se l'estremo piu' vicino va nella direzione giusta rispetto al pixel
	bool m_smaller = (m - edge_luma) < 0.0;
	bool correct = ((nearer_neg ? end_neg : end_pos) < 0.0) != m_smaller;
	float offset = correct ? 0.5 - min(dist_neg, dist_pos) / max(span, 1e-6) : 0.0;

	// Componente sotto-pixel: dettagli piu' piccoli di un pixel (reti, foglie)
	float average = (2.0 * (n + s + w + e) + (nw + ne + sw + se)) / 12.0;
	float sub = saturate(abs(average - m) / range);
	sub = smoothstep(0.0, 1.0, sub);
	sub = sub * sub * 0.75;

	float final_offset = max(offset, sub) * KTV_Intensity * 1.6;
	float2 final_uv = uv;
	if (is_horizontal)
		final_uv.y += min(final_offset, 1.0) * step_length;
	else
		final_uv.x += min(final_offset, 1.0) * step_length;

	float3 result = tex2Dlod(KTV_Color, float4(final_uv, 0.0, 0.0)).rgb;
	return KTV_Split(color, result, uv);
}

technique KarmaTV_SMAA < ui_label = "KarmaTV - Antialiasing post"; >
{
	pass
	{
		VertexShader = KTV_VS;
		PixelShader = PS_AA;
	}
}
