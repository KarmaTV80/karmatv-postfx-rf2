#pragma once

#include "common.hpp"

#include <filesystem>
#include <string>

namespace ktv
{
struct App;

// Azioni con tasto rapido.
enum class HotkeyAction : int
{
    ToggleWindow,
    ToggleEffects,
    PreviousProfile,
    NextProfile,
    Compare,
    Count
};

// Tasti predefiniti: tutti fuori dal tastierino numerico e liberi nella
// configurazione predefinita di rF2 (rF2 legge la tastiera da solo e ignora i
// modificatori: Ctrl+F10 per lui e' F10, cioe' "Auto Pit Lane").
//   Canc   apre/chiude la finestra (anche Esc la chiude)
//   +      effetti on/off
//   ,  .   profilo precedente / successivo
//   -      confronto A/B
// Si riassegnano dalla pagina "Tasti" della finestra; salvati in KarmaTV-PostFX\tasti.ini.
void load_hotkeys(const std::filesystem::path &base_dir);
void handle_hotkeys(reshade::api::effect_runtime *runtime, App &app);

// Legge UserData\player\Controller.JSON di rF2 (bin64 = cartella Bin64) e segnala
// nel log e con un avviso i tasti KarmaTV che rF2 usa gia'.
void check_rf2_key_conflicts(const std::filesystem::path &bin64, App &app);

// Per la pagina "Tasti" della finestra.
const char *hotkey_action_label(HotkeyAction action);
std::string hotkey_name(HotkeyAction action);
std::string hotkey_conflict(HotkeyAction action); // vuota se nessun conflitto con rF2
void start_hotkey_capture(HotkeyAction action);
bool hotkey_capturing(HotkeyAction action);
void reset_hotkeys();

// Pulsanti del volante (o di una pulsantiera), in aggiunta ai tasti.
std::string wheel_button_name(HotkeyAction action);
void start_wheel_capture(HotkeyAction action);
bool wheel_capturing(HotkeyAction action);
void clear_wheel_button(HotkeyAction action);
int wheel_device_count();

// Testo dei tasti per il piede della finestra.
std::string hotkey_help();
}
