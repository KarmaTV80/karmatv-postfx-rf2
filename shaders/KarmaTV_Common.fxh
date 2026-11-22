// KarmaTV PostFX - funzioni comuni a tutti gli shader.
// Le uniform KTV_Intensity e KTV_SplitX sono impostate dall'add-on ad ogni frame:
// i valori modificati dall'interfaccia di ReShade vengono sovrascritti.

#ifndef KARMATV_COMMON_FXH
#define KARMATV_COMMON_FXH

texture KTV_ColorTex : COLOR;
sampler KTV_Color { Texture = KTV_ColorTex; };

// Profondita' fornita dall'add-on KarmaTV (funziona anche con l'MSAA di rF2, a
// differenza della semantica DEPTH di ReShade). Inversione e distanza massima sono
// impostate dall'add-on: verificarle con KarmaTV_99_Debug.fx.
texture KTV_DepthTex : KARMATV_DEPTH;
sampler KTV_Depth { Texture = KTV_DepthTex; };
uniform bool KTV_DepthReversed < hidden = true; > = false;
uniform float KTV_DepthFar < hidden = true; > = 1000.0;
// false quando nel frame non c'e' una profondita' aggiornata (menu, caricamenti):
// gli effetti che la usano lasciano l'immagine invariata.
uniform bool KTV_DepthValid < hidden = true; > = false;
#define KTV_DEPTH_FAR_PLANE KTV_DepthFar

// Confronto A/B: a sinistra di questa coordinata resta l'immagine originale (0 = spento).
uniform float KTV_SplitX < hidden = true; > = 0.0;

// Impostato dall'add-on quando gli effetti vengono eseguiti in due momenti del frame
// (esclusione dell'HUD + HDR): in quel passaggio lo shader lascia l'immagine invariata.
uniform bool KTV_Bypass < hidden = true; > = false;

#define KTV_PIXEL float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT)

void KTV_VS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD)
{
	uv.x = (id == 2) ? 2.0 : 0.0;
	uv.y = (id == 1) ? 2.0 : 0.0;
	position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float KTV_Luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Profondita' lineare 0 (vicino) .. 1 (lontano).
float KTV_LinearDepth(float2 uv)
{
	float depth = tex2Dlod(KTV_Depth, float4(uv, 0.0, 0.0)).x;
	if (KTV_DepthReversed)
		depth = 1.0 - depth;
	const float near_plane = 1.0;
	return depth / (KTV_DEPTH_FAR_PLANE - depth * (KTV_DEPTH_FAR_PLANE - near_plane));
}

float3 KTV_Split(float3 original, float3 processed, float2 uv)
{
	return (KTV_SplitX > 0.0 && uv.x < KTV_SplitX) ? original : processed;
}

#endif
