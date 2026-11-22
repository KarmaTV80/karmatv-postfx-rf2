#pragma once

#include <string>

// Traduzione del menu, come in RF2 Server Manager: il testo sorgente e' l'italiano e fa da
// chiave; ogni lingua ha un file lang/<codice>.tsv incluso nell'add-on come risorsa.
// Un testo senza traduzione resta in italiano. I messaggi del log restano in italiano.
namespace ktv::i18n
{
struct Language
{
    const char *code;
    const char *name;
};

// Lingue disponibili, nell'ordine del menu.
const Language *languages(int *count);
// Codice della lingua in uso ("en", "it", ...).
const char *current();
// Imposta la lingua: codice, oppure vuoto per quella di Windows (inglese se non disponibile).
void set_language(const std::string &code);

// Testo tradotto (puntatore valido per tutta la vita dell'add-on). Le etichette ImGui con "##"
// traducono solo la parte visibile. Va chiamato solo dal thread del menu.
const char *tr(const char *italian);
// Voci di una lista ImGui separate da '\0' (terminate da "\0\0").
const char *tr_items(const char *italian);
}

namespace ktv
{
using i18n::tr;
using i18n::tr_items;
}
