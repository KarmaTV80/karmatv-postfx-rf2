#pragma once

#include <string>

// Aggiornamenti dalle versioni pubblicate su GitHub (KarmaTV80/karmatv-postfx-rf2), come in
// RF2 Server Manager: si legge l'ultima versione, si scarica l'installer con la sua impronta
// SHA-256 e lo si avvia quando rFactor 2 si chiude (l'installer vuole il gioco chiuso).
// Tutto il lavoro di rete gira su un thread a parte: il gioco non si ferma mai.
namespace ktv::update
{
enum class State
{
    Idle,        // controllo non ancora fatto
    Checking,
    UpToDate,
    Available,   // versione nuova pubblicata
    Downloading,
    Scheduled,   // scaricata e verificata: si installa alla chiusura del gioco
    Failed
};

struct Status
{
    State state = State::Idle;
    std::string version;  // versione nuova (es. "1.1.0")
    std::string notes;    // note della versione
    std::string page;     // pagina della versione su GitHub
    std::string message;  // errore, se Failed
    float progress = 0.0f;
};

// Versione in uso (da CMakeLists.txt).
const char *current_version();
// Avvia il controllo (una volta per sessione; force = rifallo adesso).
void check(bool force = false);
// Copia dello stato attuale (thread-safe).
Status status();
// Scarica la versione nuova e prepara l'installazione alla chiusura di rF2.
void download_and_schedule();
// Annulla l'installazione programmata.
void cancel_schedule();
// Apre la pagina della versione nel browser.
void open_page();
}
