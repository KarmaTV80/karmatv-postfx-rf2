#pragma once

#include <d3d11.h>

// Uscita "clean feed" verso OBS con il protocollo Spout2, senza librerie esterne.
//
// Spout condivide una texture GPU tra processi:
//  - la texture viene creata con D3D11_RESOURCE_MISC_SHARED e il suo handle condiviso
//    viene scritto in una memoria condivisa che porta il nome del sender
//    (SharedTextureInfo: handle, larghezza, altezza, formato DXGI...);
//  - il nome del sender viene aggiunto all'elenco "SpoutSenderNames" (blocchi da 256
//    caratteri), dove i ricevitori (plugin Spout2 di OBS) lo trovano;
//  - il mutex "<nome>_SpoutAccessMutex" protegge la texture mentre la si aggiorna.
// L'immagine viene copiata nel punto di aggancio prima dell'HUD: scena con gli effetti,
// senza HUD, specchietto virtuale ne' finestra KarmaTV.
namespace ktv::spout
{
constexpr const char *kSenderName = "KarmaTV rF2";

// Copia la texture (back buffer) nella texture condivisa e la pubblica.
// Crea o ricrea le risorse quando cambiano dimensioni o formato.
void publish(ID3D11DeviceContext *context, ID3D11Resource *source);

// Rimuove il sender dall'elenco e libera le risorse (uscita spenta o chiusura di rF2).
void shutdown();

bool active();             // sender registrato
unsigned width();
unsigned height();
unsigned long long frames(); // frame pubblicati
}
