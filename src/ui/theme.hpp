#pragma once

#include "common.hpp"

#include <string>

// Tema grafico KarmaTV PostFX, nello stile di RF2 Server Manager: carbonio scuro a scacchi,
// giallo KarmaTV, linea rossa sotto l'intestazione, pulsanti obliqui, etichette rosse.
// Tutte le misure sono scalate sull'altezza dello schermo (riferimento 1440 px).
namespace ktv::theme
{
namespace color
{
inline const ImVec4 background(0.063f, 0.071f, 0.082f, 0.97f);  // #101215
inline const ImVec4 nav(0.039f, 0.043f, 0.051f, 0.94f);         // #0A0B0D
inline const ImVec4 surface(0.098f, 0.110f, 0.125f, 0.94f);     // #191C20
inline const ImVec4 surface_high(0.118f, 0.129f, 0.149f, 1.00f);// #1E2126
inline const ImVec4 hover(0.188f, 0.204f, 0.231f, 1.00f);       // #30343B
inline const ImVec4 border(0.290f, 0.310f, 0.345f, 1.00f);      // #4A4F58
inline const ImVec4 text(0.949f, 0.953f, 0.961f, 1.00f);        // #F2F3F5
inline const ImVec4 text_dim(0.706f, 0.725f, 0.761f, 1.00f);    // #B4B9C2
inline const ImVec4 accent(1.000f, 0.824f, 0.122f, 1.00f);      // #FFD21F
inline const ImVec4 accent_hover(1.000f, 0.886f, 0.380f, 1.00f);// #FFE261
inline const ImVec4 accent_soft(1.000f, 0.824f, 0.122f, 0.16f);
inline const ImVec4 on_accent(0.063f, 0.071f, 0.082f, 1.00f);   // testo sul giallo
inline const ImVec4 red(0.890f, 0.169f, 0.169f, 1.00f);         // #E32B2B linea
inline const ImVec4 label(0.941f, 0.353f, 0.310f, 1.00f);       // #F05A4F etichette
inline const ImVec4 good(0.341f, 0.851f, 0.561f, 1.00f);        // #57D98F
inline const ImVec4 go(0.184f, 0.749f, 0.443f, 1.00f);          // #2FBF71 pulsante verde
inline const ImVec4 warn(1.000f, 0.604f, 0.290f, 1.00f);        // #FF9A4A
inline const ImVec4 bad(1.000f, 0.478f, 0.459f, 1.00f);         // #FF7A75
}

enum class Look
{
    Normal,  // grigio con bordo
    Primary, // pieno giallo
    Outline, // contorno giallo
    Go,      // pieno verde (salvataggio)
    Danger   // contorno rosso
};

enum class Icon
{
    None,
    Off,
    Wheel,
    Camera,
    Headset,
    User,
    Lock,
    Keyboard,
    Star,
    Sliders,
    Save,
    Back,
    Layers,
    Bolt,
    Broadcast,
    Gauge
};

// Fattore di scala dell'interfaccia (1.0 a 1440 px di altezza).
float scale();
// Dimensione del testo normale, in pixel.
float font_size();
// Testo in maiuscolo (anche le vocali accentate italiane).
std::string upper(const char *text);

// Applica e rimuove colori e spaziature del tema (da usare in coppia).
void push();
void pop();

// Sfondo a scacchi del carbonio nel rettangolo indicato.
void checker(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max);
// Parallelogramma inclinato come i pulsanti del Server Manager.
void slant_fill(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, float skew, ImU32 color);
void slant_frame(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, float skew, ImU32 color, float thickness);
// Icona disegnata a linee, centrata in center, larga circa size.
void icon(ImDrawList *draw, Icon which, const ImVec2 &center, float size, ImU32 color);

// Pulsante obliquo con icona facoltativa e testo in maiuscolo.
float button_width(const char *label, Icon with = Icon::None);
bool button(const char *label, Look look = Look::Normal, Icon with = Icon::None, float width = 0.0f);
// Interruttore a slitta; restituisce true quando cambia valore.
bool toggle(const char *id, bool *value);
// Etichetta inclinata.
void chip(const char *text, const ImVec4 &background, const ImVec4 &foreground);
// Barra del budget GPU (gialla sotto il tetto, rossa sopra).
void budget_bar(float used_ms, float budget_ms, bool measured);
// Titolo di sezione giallo in maiuscolo.
void section(const char *title);
// Etichetta rossa piccola in maiuscolo (gruppi del menu laterale).
void group_label(const char *title);
}
