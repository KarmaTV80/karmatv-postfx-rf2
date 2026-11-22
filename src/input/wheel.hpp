#pragma once

#include "common.hpp"

#include <string>

// Pulsanti di volanti, pulsantiere e altri controller di gioco via DirectInput.
// I dispositivi vengono aperti in modo non esclusivo e in background, quindi
// convivono con rF2 (che li usa in esclusiva per il force feedback).
namespace ktv::wheel
{
struct Binding
{
    GUID device = {};        // istanza del dispositivo (stabile tra un avvio e l'altro)
    int button = -1;         // indice del pulsante, -1 = nessuno
    std::string device_name; // nome mostrato nel menu
    bool valid() const { return button >= 0; }
};

// Da chiamare una volta per frame sul thread di rendering.
void poll(HWND window);
void shutdown();

// true nel frame in cui il pulsante viene premuto.
bool pressed(const Binding &binding);

// Assegnazione: restituisce true (e riempie out) appena si preme un pulsante qualsiasi.
bool capture(Binding &out);

std::string describe(const Binding &binding);
std::string to_string(const Binding &binding);
Binding from_string(const std::string &text);
int device_count();
}
