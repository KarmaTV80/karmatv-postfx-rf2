#include "ui/overlay.hpp"
#include "ui/theme.hpp"
#include "app.hpp"
#include "perf/spike_monitor.hpp"
#include "broadcast/spout_sender.hpp"
#include "core/effects_catalog.hpp"
#include "render/depth_capture.hpp"
#include "render/frame_hooks.hpp"
#include "input/hotkeys.hpp"
#include "ui/logo.hpp"
#include "core/i18n.hpp"
#include "update/updater.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace ktv
{
namespace
{
namespace col = theme::color;

enum class Page
{
    Effects,
    Profiles,
    Rules,
    Broadcast,
    Performance,
    Keys
};

Page g_page = Page::Effects;
std::unordered_set<std::string> g_expanded; // effetti con i parametri aperti
std::unordered_set<std::string> g_closed_groups; // gruppi di parametri chiusi ("<effetto>/<gruppo>")

bool param_has_groups(const EffectInfo &e)
{
    for (const ParamInfo &param : e.params)
        if (param.group != nullptr)
            return true;
    return false;
}

const char *page_label(Page page)
{
    switch (page)
    {
    case Page::Effects: return tr("Effetti");
    case Page::Profiles: return tr("Profili");
    case Page::Rules: return tr("Automatismi");
    case Page::Broadcast: return tr("Broadcast");
    case Page::Performance: return tr("Prestazioni");
    case Page::Keys: return tr("Tasti");
    }
    return "";
}

float g_header_bottom = 0.0f;   // fine dell'intestazione nel frame precedente (per la fascia scura)
bool g_advanced = false;        // menu completo (pulsante "Avanzate")
bool g_simple_keys = false;     // menu base: pagina dei tasti aperta
bool g_open_save_popup = false; // richiesta di aprire "Salva come nuovo profilo"
char g_save_name[64] = "";

const char *profile_label(const std::string &name)
{
    return name.c_str();
}

const char *profile_description(const App &a, const std::string &name)
{
    if (name == "Originale rF2") return tr("Nessun effetto, grafica di rF2");
    if (name == "Karma FX") return tr("Guida su monitor");
    if (name == "Replay Karma FX") return tr("Replay e regia");
    if (name == "VR Karma FX") return tr("Visore, effetti leggeri");
    return a.is_builtin_profile(name) ? tr("Profilo fisso") : tr("Profilo personale");
}

bool *feature_flag(Profile &profile, std::string_view id)
{
    if (id == "hud_exclusion") return &profile.features.hud_exclusion;
    if (id == "af_lod") return &profile.features.af_override;
    if (id == "mirrors") return &profile.features.mirror_grade;
    if (id == "auto_presets") return &profile.features.auto_overrides;
    return nullptr;
}

float effect_cost(App &a, reshade::api::effect_runtime *rt, const EffectInfo &e, bool *measured)
{
    const uint64_t handle = a.pipeline.technique_handle(rt, e.id);
    // Misurato solo se l'effetto e' acceso in questo momento; altrimenti la stima
    *measured = a.profiler.available() && handle != 0 && a.pipeline.is_active(rt, e.id);
    return *measured ? a.profiler.technique_ms(handle) : e.cost_max_ms;
}

float used_budget(App &a, reshade::api::effect_runtime *rt, bool *measured)
{
    *measured = a.profiler.available();
    if (*measured)
        return a.profiler.total_ms();
    float sum = 0.0f;
    for (const EffectInfo &e : effect_catalog())
        if (a.pipeline.is_active(rt, e.id))
            sum += e.cost_max_ms;
    return sum;
}

void text_dim(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, col::text_dim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor(1);
}

// Passo dei pulsanti - e +: circa un centesimo della scala, arrotondato a una potenza di 10
// (0..1 -> 0.01, 0..30 -> 0.1, 50..2000 -> 10). Sui cursori logaritmici dipende dal valore.
float fine_step(float value, float min_value, float max_value, bool logarithmic)
{
    const float span = logarithmic ? std::max(std::fabs(value), 1.0f) : (max_value - min_value);
    return std::pow(10.0f, std::floor(std::log10(std::max(span, 1e-4f) / 100.0f)));
}

// Pulsante quadrato del cursore fine; tenendolo premuto ripete
bool fine_button(const char *label)
{
    const float size = ImGui::GetFrameHeight();
    return ImGui::Button(label, ImVec2(size, size));
}

// Cursore con - e + ai lati per le correzioni fini. width < 0 = tutto lo spazio disponibile.
bool fine_slider(const char *id, float *value, float min_value, float max_value, const char *format,
                 ImGuiSliderFlags flags = 0, float width = -1.0f)
{
    const float s = theme::scale();
    const float gap = 4.0f * s;
    const float button = ImGui::GetFrameHeight();
    const float total = width < 0.0f ? ImGui::GetContentRegionAvail().x : width;
    const float step = fine_step(*value, min_value, max_value, (flags & ImGuiSliderFlags_Logarithmic) != 0);
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (fine_button("-"))
    {
        *value = std::clamp(std::round((*value - step) / step) * step, min_value, max_value);
        changed = true;
    }
    ImGui::SameLine(0.0f, gap);
    ImGui::SetNextItemWidth(std::max(total - 2.0f * (button + gap), 40.0f * s));
    changed |= ImGui::SliderFloat("##value", value, min_value, max_value, format, flags);
    ImGui::SameLine(0.0f, gap);
    if (fine_button("+"))
    {
        *value = std::clamp(std::round((*value + step) / step) * step, min_value, max_value);
        changed = true;
    }
    ImGui::PopItemFlag();
    ImGui::PopID();
    return changed;
}

bool fine_slider_int(const char *id, int *value, int min_value, int max_value, const char *format, float width = -1.0f)
{
    const float s = theme::scale();
    const float gap = 4.0f * s;
    const float button = ImGui::GetFrameHeight();
    const float total = width < 0.0f ? ImGui::GetContentRegionAvail().x : width;
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (fine_button("-") && *value > min_value)
    {
        --*value;
        changed = true;
    }
    ImGui::SameLine(0.0f, gap);
    ImGui::SetNextItemWidth(std::max(total - 2.0f * (button + gap), 40.0f * s));
    changed |= ImGui::SliderInt("##value", value, min_value, max_value, format, 0);
    ImGui::SameLine(0.0f, gap);
    if (fine_button("+") && *value < max_value)
    {
        ++*value;
        changed = true;
    }
    ImGui::PopItemFlag();
    ImGui::PopID();
    return changed;
}

// --- Intestazione -------------------------------------------------------------

theme::Icon profile_icon(const std::string &name)
{
    if (name == "Originale rF2") return theme::Icon::Off;
    if (name == "Karma FX") return theme::Icon::Wheel;
    if (name == "Replay Karma FX") return theme::Icon::Camera;
    if (name == "VR Karma FX") return theme::Icon::Headset;
    return theme::Icon::User;
}

void draw_header(App &a, reshade::api::effect_runtime *rt)
{
    const float s = theme::scale();
    const ImGuiStyle &style = ImGui::GetStyle();

    // Logo KarmaTV (se non si carica, la scritta)
    const float logo_height = 34.0f * s;
    const float row_height = std::max(logo_height, ImGui::GetFrameHeight() + 4.0f * s);
    const float row_top = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(row_top + (row_height - logo_height) * 0.5f);
    if (!logo::draw(rt, logo_height))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
        ImGui::TextUnformatted(tr("KARMA TV"));
        ImGui::PopStyleColor(1);
    }
    ImGui::SameLine(0.0f, 14.0f * s);
    ImGui::PushFont(nullptr, theme::font_size() * 1.5f);
    const float big = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(row_top + (row_height - big) * 0.5f);
    ImGui::TextUnformatted("POSTFX");
    ImGui::PopFont();

    // Stato: profilo e variante, un po' piu' grande del testo normale e sulla stessa linea
    // di base di "POSTFX" (le due righe allineate in basso)
    ImGui::SameLine(0.0f, 26.0f * s);
    ImGui::PushFont(nullptr, theme::font_size() * 1.2f);
    const float postfx_top = row_top + (row_height - big) * 0.5f;
    ImGui::SetCursorPosY(postfx_top + (big - ImGui::GetTextLineHeight()) * 0.92f);
    text_dim(tr("PROFILO"));
    ImGui::SameLine(0.0f, 6.0f * s);
    ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
    ImGui::TextUnformatted(theme::upper(tr(a.profile.name.c_str())).c_str());
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, 6.0f * s);
    text_dim((std::string("· ") + theme::upper(variant_name(a.active_variant()))).c_str());
    if (!a.loaded_override.empty())
    {
        ImGui::SameLine(0.0f, 10.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Text, col::good);
        ImGui::TextUnformatted(a.override_is_car ? tr("OVERRIDE AUTO") : tr("OVERRIDE PISTA"));
        ImGui::PopStyleColor(1);
    }
    ImGui::PopFont();

    // A destra: "Avanzate" e interruttore generale
    const char *master_label = a.master_enabled ? tr("EFFETTI ATTIVI") : tr("EFFETTI SPENTI");
    const char *advanced_label = g_advanced ? tr("Chiudi avanzate") : tr("Avanzate");
    const float label_width = ImGui::CalcTextSize(master_label, nullptr, false, -1.0f).x;
    const float toggle_width = ImGui::GetFrameHeight() * 0.66f * 2.1f;
    const float advanced_width = theme::button_width(advanced_label, theme::Icon::Sliders);
    const std::string lang_label = theme::upper(i18n::current()) + "##lang";
    const float lang_width = theme::button_width(lang_label.c_str(), theme::Icon::Broadcast);
    ImGui::SameLine();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(right - label_width - toggle_width - advanced_width - lang_width - style.ItemSpacing.x * 3.0f - 20.0f * s);
    ImGui::SetCursorPosY(row_top + (row_height - (ImGui::GetFrameHeight() + 4.0f * s)) * 0.5f);
    if (theme::button(lang_label.c_str(), theme::Look::Normal, theme::Icon::Broadcast))
        ImGui::OpenPopup("##language_menu", 0);
    if (ImGui::BeginPopup("##language_menu", 0))
    {
        int count = 0;
        const i18n::Language *list = i18n::languages(&count);
        for (int i = 0; i < count; ++i)
        {
            const bool selected = std::string_view(list[i].code) == i18n::current();
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? col::accent : col::text);
            if (ImGui::Selectable(list[i].name, selected, 0, ImVec2(220.0f * s, 0.0f)) && !selected)
                a.set_language(list[i].code);
            ImGui::PopStyleColor(1);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(row_top + (row_height - (ImGui::GetFrameHeight() + 4.0f * s)) * 0.5f);
    if (theme::button(advanced_label, g_advanced ? theme::Look::Primary : theme::Look::Outline, theme::Icon::Sliders))
        g_advanced = !g_advanced;
    ImGui::SameLine(0.0f, style.ItemSpacing.x + 20.0f * s);
    ImGui::SetCursorPosY(row_top + (row_height - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, a.master_enabled ? col::text : col::text_dim);
    ImGui::TextUnformatted(master_label);
    ImGui::PopStyleColor(1);
    ImGui::SameLine();
    ImGui::SetCursorPosY(row_top + (row_height - ImGui::GetFrameHeight()) * 0.5f);
    if (theme::toggle("##master", &a.master_enabled))
        a.toast(a.master_enabled ? tr("Effetti KarmaTV attivi") : tr("Effetti KarmaTV spenti"));
    ImGui::SetCursorPosY(row_top + row_height + style.ItemSpacing.y);

    bool measured = false;
    const float used = used_budget(a, rt, &measured);
    theme::budget_bar(used, a.profile.budget_ms, measured);

    // Linea rossa sotto l'intestazione, da bordo a bordo
    const ImVec2 win = ImGui::GetWindowPos();
    const float line_y = ImGui::GetCursorScreenPos().y + 2.0f * s;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(win.x, line_y), ImVec2(win.x + ImGui::GetWindowSize().x, line_y + 3.0f * s),
                                              ImGui::GetColorU32(col::red), 0.0f, 0);
    g_header_bottom = line_y - win.y;
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));
}

// --- Aggiornamenti -------------------------------------------------------------

void draw_update_banner()
{
    const update::Status u = update::status();
    if (u.state != update::State::Available && u.state != update::State::Downloading &&
        u.state != update::State::Scheduled && u.state != update::State::Failed)
        return;
    const float s = theme::scale();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + 22.0f * s;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(ImVec4(0.098f, 0.110f, 0.125f, 0.96f)), 0.0f, 0);
    draw->AddRectFilled(min, ImVec2(min.x + 4.0f * s, min.y + height), ImGui::GetColorU32(u.state == update::State::Failed ? col::red : col::go), 0.0f, 0);
    ImGui::SetCursorScreenPos(ImVec2(min.x + 18.0f * s, min.y + 9.0f * s));

    ImGui::AlignTextToFramePadding();
    std::string title;
    switch (u.state)
    {
    case update::State::Available: title = tr("NUOVA VERSIONE ") + u.version + tr(" DISPONIBILE"); break;
    case update::State::Downloading: title = tr("DOWNLOAD DELLA VERSIONE ") + u.version + "..."; break;
    case update::State::Scheduled: title = tr("VERSIONE ") + u.version + tr(" PRONTA"); break;
    default: title = tr("AGGIORNAMENTO NON RIUSCITO"); break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, u.state == update::State::Failed ? col::bad : col::good);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, 14.0f * s);
    ImGui::AlignTextToFramePadding();
    switch (u.state)
    {
    case update::State::Available:
        text_dim((std::string(tr("Ora usi la ")) + update::current_version() + tr(". Si installa da solo quando chiudi rF2.")).c_str());
        break;
    case update::State::Downloading:
    {
        char line[64];
        std::snprintf(line, sizeof(line), "%.0f%%", u.progress * 100.0f);
        text_dim(line);
        break;
    }
    case update::State::Scheduled:
        text_dim(tr("Si installa quando chiudi rFactor 2 (Windows chiederà il permesso)."));
        break;
    default:
        text_dim(tr(u.message.c_str()));
        break;
    }

    // Pulsanti a destra
    const char *primary = u.state == update::State::Available ? tr("Aggiorna") : (u.state == update::State::Failed ? tr("Riprova") : nullptr);
    const char *secondary = u.state == update::State::Scheduled ? tr("Annulla") : tr("Novità");
    float buttons = theme::button_width(secondary, theme::Icon::None) + 12.0f * s;
    if (primary != nullptr)
        buttons += theme::button_width(primary, theme::Icon::Save);
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(min.x + width - buttons - 12.0f * s, min.y + (height - ImGui::GetFrameHeight() - 4.0f * s) * 0.5f));
    if (primary != nullptr)
    {
        if (theme::button(primary, theme::Look::Go, theme::Icon::Save))
            update::download_and_schedule();
        ImGui::SameLine(0.0f, 12.0f * s);
    }
    if (theme::button(secondary, theme::Look::Normal))
    {
        if (u.state == update::State::Scheduled)
            update::cancel_schedule();
        else
            update::open_page();
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height + 8.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// --- Navigazione ----------------------------------------------------------------

theme::Icon page_icon(Page page)
{
    switch (page)
    {
    case Page::Effects: return theme::Icon::Layers;
    case Page::Profiles: return theme::Icon::User;
    case Page::Rules: return theme::Icon::Bolt;
    case Page::Broadcast: return theme::Icon::Broadcast;
    case Page::Performance: return theme::Icon::Gauge;
    case Page::Keys: return theme::Icon::Keyboard;
    }
    return theme::Icon::None;
}

void nav_item(Page page)
{
    const float s = theme::scale();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 40.0f * s;
    ImGui::PushID(static_cast<int>(page));
    if (ImGui::InvisibleButton("##nav", ImVec2(width, height), 0))
        g_page = page;
    const bool hovered = ImGui::IsItemHovered(0);
    ImGui::PopID();
    const bool selected = g_page == page;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + width, min.y + height);
    if (selected)
        draw->AddRectFilled(min, max, ImGui::GetColorU32(col::accent), 0.0f, 0);
    else if (hovered)
        draw->AddRectFilled(min, max, ImGui::GetColorU32(col::hover), 0.0f, 0);
    const ImU32 fore = ImGui::GetColorU32(selected ? col::on_accent : col::text);
    const float icon_size = 20.0f * s;
    theme::icon(draw, page_icon(page), ImVec2(min.x + 12.0f * s + icon_size * 0.5f, min.y + height * 0.5f), icon_size, fore);
    const std::string label = theme::upper(page_label(page));
    const float text_h = ImGui::GetTextLineHeight();
    draw->AddText(ImVec2(min.x + 22.0f * s + icon_size, min.y + (height - text_h) * 0.5f), fore, label.c_str(), nullptr);
}

void nav_group(const char *title, bool first)
{
    const float s = theme::scale();
    if (!first)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 8.0f * s, p.y + 4.0f * s), ImVec2(p.x + ImGui::GetContentRegionAvail().x - 8.0f * s, p.y + 4.0f * s),
                                            ImGui::GetColorU32(ImVec4(col::red.x, col::red.y, col::red.z, 0.7f)), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 10.0f * s));
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * s);
    theme::group_label(title);
}

void draw_navigation()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f * theme::scale()));
    nav_group(tr("Grafica"), true);
    nav_item(Page::Effects);
    nav_item(Page::Profiles);
    nav_group(tr("Gara"), false);
    nav_item(Page::Rules);
    nav_item(Page::Broadcast);
    nav_group(tr("Sistema"), false);
    nav_item(Page::Performance);
    nav_item(Page::Keys);
    ImGui::PopStyleVar(1);
}

// --- Pagina Effetti -----------------------------------------------------------

void draw_effects(App &a, reshade::api::effect_runtime *rt)
{
    const bool vr = a.active_variant() == Variant::VR;
    const float s = theme::scale();

    for (int c = 0; c < static_cast<int>(Category::Count); ++c)
    {
        const Category category = static_cast<Category>(c);
        theme::section(tr(category_label(category)));

        ImGui::PushID(c);
        if (ImGui::BeginTable("effects", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f), 0.0f))
        {
            ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 56.0f * s, 0);
            ImGui::TableSetupColumn("nome", ImGuiTableColumnFlags_WidthStretch, 3.0f, 0);
            ImGui::TableSetupColumn("intensita", ImGuiTableColumnFlags_WidthStretch, 2.0f, 0);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 80.0f * s, 0);

            for (const EffectInfo &e : effect_catalog())
            {
                if (e.category != category)
                    continue;

                ImGui::PushID(e.id);
                ImGui::TableNextRow(0, 0.0f);

                if (e.fx_file != nullptr)
                {
                    EffectSetting &setting = a.profile.effects[e.id];
                    const bool found = a.pipeline.shader_found(rt, e.id);

                    ImGui::TableSetColumnIndex(0);
                    if (theme::toggle("##on", &setting.enabled))
                        a.dirty = true;

                    ImGui::TableSetColumnIndex(1);
                    const bool has_params = !e.params.empty();
                    bool expanded = g_expanded.count(e.id) != 0;
                    if (has_params)
                    {
                        if (ImGui::SmallButton(expanded ? tr("-##params") : tr("+##params")))
                        {
                            expanded = !expanded;
                            if (expanded) g_expanded.insert(e.id);
                            else g_expanded.erase(e.id);
                        }
                        ImGui::SameLine();
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(tr(e.label));
                    if (!found)
                    {
                        ImGui::SameLine();
                        char note[48];
                        std::snprintf(note, sizeof(note), tr("in arrivo, fase %d"), e.phase);
                        theme::chip(note, col::surface_high, col::text_dim);
                    }
                    else if (vr && !e.vr_allowed)
                    {
                        ImGui::SameLine();
                        theme::chip(tr("escluso in VR"), col::surface_high, col::text_dim);
                    }
                    else if (e.phase >= 3)
                    {
                        ImGui::SameLine();
                        theme::chip(tr("sperimentale"), ImVec4(col::warn.x, col::warn.y, col::warn.z, 0.18f), col::warn);
                    }
                    if (std::string_view(e.id) == "auto_hdr")
                    {
                        ImGui::SameLine();
                        if (frame::hdr_restart_needed())
                            theme::chip(tr("riavvia rF2"), ImVec4(col::warn.x, col::warn.y, col::warn.z, 0.18f), col::warn);
                        else if (frame::hdr_active())
                            theme::chip(tr("attivo"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
                    }

                    ImGui::TableSetColumnIndex(2);
                    if (fine_slider("##intensity", &setting.intensity, 0.0f, 1.0f, "%.2f"))
                        a.dirty = true;

                    ImGui::TableSetColumnIndex(3);
                    ImGui::AlignTextToFramePadding();
                    if (a.pipeline.is_active(rt, e.id))
                    {
                        bool measured = false;
                        const float ms = effect_cost(a, rt, e, &measured);
                        char value[32];
                        std::snprintf(value, sizeof(value), measured ? tr("%.2f ms") : tr("~%.1f ms"), ms);
                        ImGui::TextUnformatted(value);
                    }
                    else
                    {
                        text_dim("-");
                    }

                    // Parametri aggiuntivi, una riga ciascuno sotto l'effetto
                    if (has_params && expanded)
                    {
                        bool group_open = true;
                        for (const ParamInfo &param : e.params)
                        {
                            // Titolo di gruppo: cliccandolo si chiude o si riapre
                            if (param.group != nullptr)
                            {
                                const std::string key = std::string(e.id) + "/" + param.group;
                                group_open = g_closed_groups.count(key) == 0;
                                ImGui::PushID(key.c_str());
                                ImGui::TableNextRow(0, 0.0f);
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Indent(18.0f * s);
                                ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
                                const std::string title = std::string(group_open ? "-  " : "+  ") + tr(param.group);
                                if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, 0.0f)))
                                {
                                    if (group_open) g_closed_groups.insert(key);
                                    else g_closed_groups.erase(key);
                                    group_open = !group_open;
                                }
                                ImGui::PopStyleColor(1);
                                ImGui::Unindent(18.0f * s);
                                ImGui::PopID();
                            }
                            if (!group_open)
                                continue;

                            ImGui::PushID(param.uniform);
                            ImGui::TableNextRow(0, 0.0f);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Indent((param_has_groups(e) ? 40.0f : 28.0f) * s);
                            text_dim(tr(param.label));
                            ImGui::Unindent((param_has_groups(e) ? 40.0f : 28.0f) * s);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::SetNextItemWidth(-1.0f);
                            float value = param_value(setting, param);
                            if (param.items != nullptr)
                            {
                                int index = static_cast<int>(value + 0.5f);
                                if (ImGui::Combo("##param", &index, tr_items(param.items), -1))
                                {
                                    setting.params[param.uniform] = static_cast<float>(index);
                                    a.dirty = true;
                                }
                            }
                            else if (fine_slider("##param", &value, param.min_value, param.max_value, "%.2f"))
                            {
                                setting.params[param.uniform] = value;
                                a.dirty = true;
                            }

                            ImGui::TableSetColumnIndex(3);
                            if (ImGui::SmallButton(tr("Reset")))
                            {
                                setting.params.erase(param.uniform);
                                a.dirty = true;
                            }
                            ImGui::PopID();
                        }
                    }
                }
                else
                {
                    bool *flag = feature_flag(a.profile, e.id);

                    ImGui::TableSetColumnIndex(0);
                    if (flag != nullptr)
                    {
                        if (theme::toggle("##on", flag))
                            a.dirty = true;
                    }
                    else
                    {
                        bool off = false;
                        ImGui::BeginDisabled(true);
                        theme::toggle("##on", &off);
                        ImGui::EndDisabled();
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(tr(e.label));
                    const bool is_hud = std::string_view(e.id) == "hud_exclusion";
                    const bool is_af = std::string_view(e.id) == "af_lod";
                    if (is_af)
                    {
                        ImGui::SameLine();
                        theme::chip(tr("vale dal prossimo caricamento della pista"), col::surface_high, col::text_dim);
                    }
                    else if (std::string_view(e.id) == "auto_presets")
                    {
                        ImGui::SameLine();
                        if (!a.loaded_override.empty())
                            theme::chip(a.override_is_car ? tr("caricato: auto") : tr("caricato: pista"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
                        else
                            theme::chip(tr("si salvano dalla pagina Profili"), col::surface_high, col::text_dim);
                    }
                    else if (std::string_view(e.id) == "mirrors")
                    {
                        ImGui::SameLine();
                        if (frame::mirror_graded())
                            theme::chip(tr("applicato"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
                        else
                            theme::chip(tr("segue tonemap e LUT"), col::surface_high, col::text_dim);
                    }
                    else if (!is_hud)
                    {
                        ImGui::SameLine();
                        char note[48];
                        std::snprintf(note, sizeof(note), tr("in arrivo, fase %d"), e.phase);
                        theme::chip(note, col::surface_high, col::text_dim);
                    }
                    else if (frame::last_frame().hook_fired)
                    {
                        ImGui::SameLine();
                        theme::chip(tr("agganciato"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::AlignTextToFramePadding();
                    text_dim("-");

                    // Filtro anisotropico: livello e spostamento del LOD
                    if (is_af && *flag)
                    {
                        ImGui::TableNextRow(0, 0.0f);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Indent(28.0f * s);
                        text_dim(tr("Livello anisotropico"));
                        ImGui::Unindent(28.0f * s);
                        ImGui::TableSetColumnIndex(2);
                        if (fine_slider_int("##aflevel", &a.profile.features.af_level, 2, 16, "%dx"))
                            a.dirty = true;

                        ImGui::TableNextRow(0, 0.0f);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Indent(28.0f * s);
                        text_dim(tr("Spostamento LOD (negativo = più nitido)"));
                        ImGui::Unindent(28.0f * s);
                        ImGui::TableSetColumnIndex(2);
                        if (fine_slider("##lodbias", &a.profile.features.lod_bias, -1.0f, 1.0f, "%.2f"))
                            a.dirty = true;
                    }

                    // Esclusione dell'HUD: punto di aggancio e analisi del frame
                    if (is_hud && *flag)
                    {
                        ImGui::TableNextRow(0, 0.0f);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Indent(28.0f * s);
                        text_dim(tr("Bind del back buffer dopo la scena (consigliato 2, 0 = fine frame)"));
                        ImGui::Unindent(28.0f * s);
                        ImGui::TableSetColumnIndex(2);
                        if (fine_slider_int("##hook", &a.profile.features.hud_hook_index, 0, 12, "%d"))
                            a.dirty = true;

                        ImGui::TableNextRow(0, 0.0f);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Indent(28.0f * s);
                        char stats[128];
                        std::snprintf(stats, sizeof(stats), tr("Frame: %d bind del back buffer, %d dopo la scena, %d draw"),
                                      frame::last_frame().backbuffer_binds, frame::last_frame().backbuffer_binds_after_scene,
                                      frame::last_frame().draws);
                        text_dim(stats);
                        ImGui::Unindent(28.0f * s);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::BeginDisabled(frame::analysis_pending());
                        if (ImGui::Button(frame::analysis_pending() ? tr("Analisi in corso...") : tr("Analizza un frame"), ImVec2(-1.0f, 0.0f)))
                        {
                            frame::request_analysis();
                            a.toast(tr("Analisi del frame scritta in ReShade.log"));
                        }
                        ImGui::EndDisabled();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

// --- Scelta del profilo (menu base e pagina Profili) ---------------------------

void draw_profile_picker(App &a)
{
    const float s = theme::scale();
    const auto &names = a.profile_names();
    const std::string startup = a.startup_profile();

    theme::group_label(tr("Grafica"));
    theme::section(tr("Che profilo usi?"));
    const float gap = 12.0f * s;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int per_row = std::max(1, std::min(3, static_cast<int>((avail + gap) / (280.0f * s + gap))));
    const float width = (avail - gap * static_cast<float>(per_row - 1)) / static_cast<float>(per_row);
    const float line = ImGui::GetTextLineHeight();
    const float pad = 14.0f * s;
    const float height = line * 2.2f + pad * 2.0f;
    const float skew = height * 0.16f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const int count = static_cast<int>(names.size());
    for (int i = 0; i < count; ++i)
    {
        const ImVec2 pos(origin.x + static_cast<float>(i % per_row) * (width + gap),
                         origin.y + static_cast<float>(i / per_row) * (height + gap));
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(width, height), 0);
        const bool hovered = ImGui::IsItemHovered(0);
        ImGui::PopID();
        const bool selected = a.profile_index == i;
        const bool builtin = a.is_builtin_profile(names[i]);
        const ImVec2 end(pos.x + width, pos.y + height);
        const ImVec4 fill = selected ? col::accent : (hovered ? col::hover : col::surface_high);
        theme::slant_fill(draw, pos, end, skew, ImGui::GetColorU32(fill));
        theme::slant_frame(draw, pos, end, skew, ImGui::GetColorU32(selected ? col::accent : ImVec4(0.173f, 0.188f, 0.212f, 1.0f)), 1.0f);

        const ImVec4 title = selected ? col::on_accent : (builtin ? col::text : col::good);
        const ImVec4 sub = selected ? ImVec4(0.231f, 0.204f, 0.063f, 1.0f) : col::text_dim;
        const float icon_size = line * 1.25f;
        const float x0 = pos.x + skew + pad * 0.6f;
        theme::icon(draw, profile_icon(names[i]), ImVec2(x0 + icon_size * 0.5f, pos.y + height * 0.5f), icon_size,
                    ImGui::GetColorU32(selected ? col::on_accent : col::text_dim));
        const float tx = x0 + icon_size + 12.0f * s;
        const std::string name = theme::upper(tr(names[i].c_str()));
        ImFont *font = ImGui::GetFont();
        draw->AddText(font, ImGui::GetFontSize() * 1.12f, ImVec2(tx, pos.y + pad - 2.0f * s), ImGui::GetColorU32(title), name.c_str(), nullptr, 0.0f, nullptr);
        draw->AddText(ImVec2(tx - skew * 0.25f, pos.y + pad + line * 1.15f), ImGui::GetColorU32(sub), profile_description(a, names[i]), nullptr);

        if (names[i] == startup)
        {
            const char *tag = tr("AVVIO");
            const ImVec2 size = ImGui::CalcTextSize(tag, nullptr, false, -1.0f);
            const float th = size.y + 2.0f * s;
            const float tw = size.x + 16.0f * s;
            const ImVec2 tmin(end.x - pad - tw, pos.y + pad - 2.0f * s);
            theme::slant_fill(draw, tmin, ImVec2(tmin.x + tw, tmin.y + th), th * 0.25f,
                              ImGui::GetColorU32(selected ? col::on_accent : col::text));
            draw->AddText(ImVec2(tmin.x + 8.0f * s, tmin.y + 1.0f * s), ImGui::GetColorU32(selected ? col::accent : col::red), tag, nullptr);
        }
        else if (builtin)
        {
            const float ls = line * 0.75f;
            theme::icon(draw, theme::Icon::Lock, ImVec2(end.x - pad - skew * 0.3f - ls * 0.5f, pos.y + pad + ls * 0.4f), ls,
                        ImGui::GetColorU32(selected ? col::on_accent : col::text_dim));
        }
        if (clicked && !selected)
            a.load_profile(i);
    }
    const int rows = (count + per_row - 1) / per_row;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + static_cast<float>(rows) * (height + gap)));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    theme::section(tr("Avvio del gioco"));
    const std::string &current = names[a.profile_index];
    if (current == startup)
    {
        ImGui::AlignTextToFramePadding();
        text_dim((tr("Questo profilo si carica all'avvio di rF2 (") + std::string(variant_name(a.active_variant())) + ").").c_str());
    }
    else
    {
        if (theme::button(tr("Usa all'avvio"), theme::Look::Primary, theme::Icon::Star))
            a.set_startup_profile();
        ImGui::SameLine(0.0f, 14.0f * s);
        ImGui::AlignTextToFramePadding();
        text_dim((tr("Ora all'avvio si carica: ") + startup).c_str());
    }
    text_dim(tr("Profili fissi con il lucchetto: puoi provare modifiche, ma si salvano solo come nuovo profilo. In verde i tuoi profili."));
}

// --- Pagina Profili -------------------------------------------------------------

void draw_profiles(App &a)
{
    const float s = theme::scale();
    const auto &names = a.profile_names();

    draw_profile_picker(a);
    if (a.profile.name == "Originale rF2")
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::warn);
        ImGui::TextWrapped(tr("Originale rF2: tutti gli effetti KarmaTV sono spenti, anche quelli degli automatismi."));
        ImGui::PopStyleColor(1);
        if (effect_setting(a.profile, "auto_hdr").enabled && a.active_variant() == Variant::Monitor)
            text_dim(tr("AutoHDR resta acceso ma neutro (solo conversione in HDR, nessuna espansione). "
                     "Per la grafica originale al 100%: spegni AutoHDR in Effetti e riavvia rF2."));
        if (a.profile.features.af_override)
            text_dim(tr("Il filtro anisotropico resta forzato: per toglierlo spegnilo in Effetti e ricarica la pista."));
    }

    theme::section(tr("Nuovo profilo"));
    static char new_name[64] = "";
    ImGui::SetNextItemWidth(360.0f * s);
    ImGui::InputTextWithHint("##newprofile", tr("Nome del profilo (es. Daytona sera)"), new_name, sizeof(new_name), 0, nullptr, nullptr);
    a.ui_text_input = ImGui::IsItemActive();
    ImGui::SameLine();
    if (ImGui::Button(tr("Salva come nuovo profilo"), ImVec2(0.0f, 0.0f)))
    {
        a.save_profile_as(new_name);
        new_name[0] = '\0';
    }
    text_dim(tr("Salva tutte le regolazioni attuali con il nome scritto, per la variante attiva (Monitor o VR)."));
    if (!a.is_builtin_profile(names[a.profile_index]))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::bad);
        if (ImGui::Button(tr("Elimina questo profilo"), ImVec2(0.0f, 0.0f)))
            a.delete_current_profile();
        ImGui::PopStyleColor(1);
    }

    theme::section(tr("Variante"));
    int mode = static_cast<int>(a.variant_mode);
    bool changed = ImGui::RadioButton(tr("Automatica"), &mode, 0);
    ImGui::SameLine(0.0f, 24.0f * s);
    changed |= ImGui::RadioButton(tr("Monitor"), &mode, 1);
    ImGui::SameLine(0.0f, 24.0f * s);
    changed |= ImGui::RadioButton("VR", &mode, 2);
    if (changed)
        a.set_variant_mode(static_cast<VariantMode>(mode));
    char detected[64];
    std::snprintf(detected, sizeof(detected), tr("Rilevata automaticamente: %s"), variant_name(a.detected_variant));
    text_dim(detected);

    theme::section(tr("Salvataggio"));
    const TelemetrySnapshot &t = a.telemetry.snapshot();
    const bool has_track = t.connected && !t.track_name.empty();
    const bool has_car = t.connected && !t.vehicle_name.empty();
    if (a.is_builtin_profile(names[a.profile_index]))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::warn);
        ImGui::TextWrapped(tr("Profilo fisso: non si sovrascrive. Per tenere le modifiche salvale come nuovo profilo "
                           "(qui sopra o con il pulsante in basso a destra)."));
        ImGui::PopStyleColor(1);
        if (ImGui::Button(tr("Annulla modifiche"), ImVec2(0.0f, 0.0f)))
            a.load_profile(a.profile_index);
        if (a.modified())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::warn);
            ImGui::TextUnformatted(tr("Ci sono modifiche non salvate."));
            ImGui::PopStyleColor(1);
        }
        text_dim((tr("Impostazioni di sistema (AutoHDR, HUD, profondità...) salvate da sole in: ") + a.base_dir.string() + tr("\\sistema.ini")).c_str());
        return;
    }
    if (ImGui::Button(tr("Salva profilo"), ImVec2(0.0f, 0.0f)))
        a.save_profile(App::SaveTarget::Profile);
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_track);
    if (ImGui::Button(tr("Salva solo per questa pista"), ImVec2(0.0f, 0.0f)))
        a.save_profile(App::SaveTarget::Track);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_car);
    if (ImGui::Button(tr("Salva solo per questa auto"), ImVec2(0.0f, 0.0f)))
        a.save_profile(App::SaveTarget::Car);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Annulla modifiche"), ImVec2(0.0f, 0.0f)))
        a.load_profile(a.profile_index);
    ImGui::SameLine();
    if (ImGui::Button(tr("Ripristina valori predefiniti"), ImVec2(0.0f, 0.0f)))
        a.reset_profile_to_defaults();

    if (a.dirty)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::warn);
        ImGui::TextUnformatted(tr("Ci sono modifiche non salvate."));
        ImGui::PopStyleColor(1);
    }
    if (!a.loaded_override.empty())
    {
        const std::string text = std::string(a.override_is_car ? tr("Override auto attivo: ") : tr("Override pista attivo: ")) + a.loaded_override;
        ImGui::PushStyleColor(ImGuiCol_Text, col::good);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor(1);
    }
    if (has_track)
        text_dim((tr("Pista: ") + t.track_name).c_str());
    if (has_car)
        text_dim((tr("Auto: ") + t.vehicle_name + tr("  (l'override per pista ha la precedenza su quello per auto)")).c_str());
    text_dim((tr("Cartella dei preset: ") + a.presets_dir.string()).c_str());
}

// --- Pagina Automatismi -------------------------------------------------------

void draw_rules(App &a)
{
    const float s = theme::scale();

    theme::section(tr("Telemetria"));
    const TelemetrySnapshot &t = a.telemetry.snapshot();
    if (t.connected)
    {
        theme::chip(tr("Collegata"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
        char line[256];
        std::snprintf(line, sizeof(line), tr("Pista: %s   |   Auto: %s"), t.track_name.c_str(), t.vehicle_name.c_str());
        text_dim(line);
        const int minutes = static_cast<int>(t.time_of_day / 60.0);
        std::snprintf(line, sizeof(line), tr("Ora in pista %02d:%02d   |   Pioggia %.0f%%   |   Bagnato %.0f%%   |   Nuvole %.0f%%   |   Ai box: %s"),
                      (minutes / 60) % 24, minutes % 60,
                      t.raining * 100.0, t.wetness_avg * 100.0, t.dark_cloud * 100.0, t.in_pits ? tr("sì") : tr("no"));
        text_dim(line);
    }
    else
    {
        theme::chip(tr("Non collegata"), ImVec4(col::warn.x, col::warn.y, col::warn.z, 0.18f), col::warn);
        text_dim(tr("Attiva rF2SharedMemoryMapPlugin. I profili manuali funzionano comunque."));
    }

    theme::section(tr("Regole"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Automatismi attivi"));
    ImGui::SameLine();
    if (theme::toggle("##rules", &a.profile.features.rules_enabled))
        a.dirty = true;
    ImGui::SameLine(0.0f, 32.0f * s);
    ImGui::TextUnformatted(tr("Effetti nei replay"));
    ImGui::SameLine();
    if (theme::toggle("##replay", &a.profile.features.replay_effects))
        a.dirty = true;
    if (a.pipeline.replay_detected())
    {
        ImGui::SameLine();
        theme::chip(tr("Replay in corso"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
    }

    // Profilo per replay e monitor di gara a schermo intero
    {
        const auto &names = a.profile_names();
        const std::string current = a.profile.features.replay_profile;
        const std::string preview = current.empty() ? std::string(tr("Nessuno (resta quello attivo)")) : current;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Profilo nei replay e nel monitor di gara"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f * s);
        if (ImGui::BeginCombo("##replayprofile", preview.c_str(), 0))
        {
            if (ImGui::Selectable(tr("Nessuno (resta quello attivo)"), current.empty(), 0, ImVec2(0.0f, 0.0f)))
            {
                a.profile.features.replay_profile.clear();
                a.dirty = true;
            }
            for (const std::string &name : names)
            {
                if (name == "Originale rF2")
                    continue;
                if (ImGui::Selectable(name.c_str(), name == current, 0, ImVec2(0.0f, 0.0f)))
                {
                    a.profile.features.replay_profile = name;
                    a.dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (a.replay_profile_active())
        {
            ImGui::SameLine();
            theme::chip(tr("in uso ora"), ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
        }
        text_dim(tr("Quando parte un replay o guardi la gara a schermo intero dal monitor, KarmaTV passa a "
                 "questo profilo e, finito il replay, torna a quello di prima. Serve \"Effetti nei replay\" acceso."));
    }

    if (ImGui::BeginTable("rules", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f), 0.0f))
    {
        ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 56.0f * s, 0);
        ImGui::TableSetupColumn("regola", ImGuiTableColumnFlags_WidthStretch, 2.0f, 0);
        ImGui::TableSetupColumn("dato", ImGuiTableColumnFlags_WidthStretch, 2.0f, 0);
        ImGui::TableSetupColumn("peso", ImGuiTableColumnFlags_WidthStretch, 1.5f, 0);

        for (size_t i = 0; i < AutoRules::count; ++i)
        {
            const RuleId rule = static_cast<RuleId>(i);
            const RuleInfo &info = AutoRules::info(rule);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow(0, 0.0f);

            ImGui::TableSetColumnIndex(0);
            bool on = a.rules.enabled(rule);
            if (theme::toggle("##rule", &on))
                a.rules.set_enabled(rule, on);

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(info.label));
            if (!info.implemented)
            {
                ImGui::SameLine();
                theme::chip(tr("in arrivo, fase 2"), col::surface_high, col::text_dim);
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            text_dim(tr(info.source));

            ImGui::TableSetColumnIndex(3);
            ImGui::ProgressBar(a.rules.weight(rule), ImVec2(-1.0f, ImGui::GetFrameHeight() * 0.6f), "");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// --- Pagina Broadcast -------------------------------------------------------------

void draw_broadcast(App &a, reshade::api::effect_runtime *rt)
{
    theme::section(tr("Uscita pulita per OBS (Spout2)"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Clean feed Spout2"));
    ImGui::SameLine();
    if (theme::toggle("##clean", &a.profile.features.clean_feed))
        a.dirty = true;
    if (a.active_variant() != Variant::Monitor)
        text_dim(tr("Disponibile solo nella variante Monitor."));
    if (spout::active())
    {
        ImGui::SameLine();
        char status[96];
        std::snprintf(status, sizeof(status), tr("In onda %ux%u"), spout::width(), spout::height());
        theme::chip(status, ImVec4(col::good.x, col::good.y, col::good.z, 0.18f), col::good);
    }
    else if (a.profile.features.clean_feed)
    {
        ImGui::SameLine();
        theme::chip(tr("In attesa del primo frame"), col::surface_high, col::text_dim);
    }
    text_dim(tr("Invia a OBS la scena con gli effetti ma senza HUD, specchietto virtuale e finestra KarmaTV."));
    text_dim(tr("In OBS: plugin Spout2, sorgente \"Spout2 Capture\", sender \"KarmaTV rF2\"."));
    text_dim(tr("Senza esclusione HUD (o nei menu) viene inviata l'immagine intera."));

    theme::section(tr("Regia"));
    if (ImGui::Button(tr("Preset regia (Replay)"), ImVec2(0.0f, 0.0f)))
    {
        const auto &names = a.profile_names();
        const auto it = std::find(names.begin(), names.end(), std::string("Replay Karma FX"));
        if (it != names.end())
        {
            a.load_profile(static_cast<int>(it - names.begin()));
            a.toast(tr("Profilo Replay Karma FX: profondità di campo, motion blur, antialiasing"));
        }
        else
        {
            a.toast(tr("Il profilo Replay Karma FX esiste solo nella variante Monitor"));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Screenshot"), ImVec2(0.0f, 0.0f)) && rt != nullptr)
    {
        rt->save_screenshot("KarmaTV");
        a.toast(tr("Screenshot salvato nella cartella degli screenshot di ReShade"));
    }
    text_dim(tr("Lo screenshot viene salvato con gli effetti, senza la finestra KarmaTV."));
}

// --- Pagina Prestazioni ---------------------------------------------------------

void draw_performance(App &a, reshade::api::effect_runtime *rt)
{
    const float s = theme::scale();

    theme::section(tr("Riepilogo"));
    char line[128];
    std::snprintf(line, sizeof(line), tr("FPS: %.0f"), ImGui::GetIO().Framerate);
    ImGui::TextUnformatted(line);
    if (a.profiler.available())
    {
        std::snprintf(line, sizeof(line), tr("Tempo GPU degli effetti KarmaTV: %.2f ms"), a.profiler.total_ms());
        ImGui::TextUnformatted(line);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::warn);
        ImGui::TextUnformatted(tr("Profiler GPU non disponibile: i valori sono stime."));
        ImGui::PopStyleColor(1);
    }

    if (fine_slider("##budget", &a.profile.budget_ms, 0.5f, 20.0f, "%.1f", 0, 400.0f * s))
        a.dirty = true;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Budget (ms)"));

    theme::section(tr("Effetti attivi"));
    if (ImGui::BeginTable("perf", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f), 0.0f))
    {
        ImGui::TableSetupColumn(tr("Effetto"), ImGuiTableColumnFlags_WidthStretch, 3.0f, 0);
        ImGui::TableSetupColumn(tr("Stima"), ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
        ImGui::TableSetupColumn(tr("Misurato"), ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
        ImGui::TableHeadersRow();

        for (const EffectInfo &e : effect_catalog())
        {
            if (!a.pipeline.is_active(rt, e.id))
                continue;
            ImGui::TableNextRow(0, 0.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(tr(e.label));
            ImGui::TableSetColumnIndex(1);
            std::snprintf(line, sizeof(line), tr("%.1f - %.1f ms"), e.cost_min_ms, e.cost_max_ms);
            text_dim(line);
            ImGui::TableSetColumnIndex(2);
            bool measured = false;
            const float ms = effect_cost(a, rt, e, &measured);
            if (measured)
            {
                std::snprintf(line, sizeof(line), tr("%.2f ms"), ms);
                ImGui::TextUnformatted(line);
            }
            else
            {
                text_dim("-");
            }
        }
        ImGui::EndTable();
    }

    theme::section(tr("Picchi"));
    std::snprintf(line, sizeof(line), tr("Picchi registrati in questa sessione: %d"), spikes::count());
    ImGui::TextUnformatted(line);
    text_dim(tr("Ogni picco (frame lungo o effetti molto piu' costosi del solito) finisce in ReShade.log con "
             "il costo di ogni effetto e cosa era appena cambiato."));

    theme::section(tr("Diagnostica"));
    {
        // Vista della profondita': serve a verificare il depth buffer (nebbia e occlusione dipendono da lui)
        EffectSetting &debug = a.profile.effects["debug_views"];
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Vista profondità"));
        ImGui::SameLine();
        if (theme::toggle("##depthview", &debug.enabled))
            a.dirty = true;
        ImGui::SameLine();
        text_dim(tr("corretta se gli oggetti vicini sono scuri e l'orizzonte chiaro"));

        const depth::Status &d = depth::status();
        char depth_line[200];
        if (d.error != nullptr)
            std::snprintf(depth_line, sizeof(depth_line), tr("Profondità KarmaTV: errore, %s"), d.error);
        else if (d.found)
            std::snprintf(depth_line, sizeof(depth_line), tr("Profondità KarmaTV: %u x %u, MSAA %ux, %s, %llu copie"),
                          d.width, d.height, d.samples,
                          d.reversed_known ? (d.reversed ? tr("invertita (rilevata)") : tr("normale (rilevata)")) : tr("verso non ancora rilevato"),
                          static_cast<unsigned long long>(d.copies));
        else
            std::snprintf(depth_line, sizeof(depth_line), tr("Profondità KarmaTV: nessun depth buffer MSAA trovato (serve riavviare rF2 dopo l'installazione)"));
        ImGui::PushStyleColor(ImGuiCol_Text, d.found && d.error == nullptr ? col::good : col::warn);
        ImGui::TextUnformatted(depth_line);
        ImGui::PopStyleColor(1);

        int mode = a.profile.features.depth_mode;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Verso della profondità"));
        ImGui::SameLine();
        bool changed = ImGui::RadioButton(tr("Automatico"), &mode, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(tr("Normale"), &mode, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(tr("Invertito"), &mode, 2);
        if (changed)
        {
            a.profile.features.depth_mode = mode;
            a.dirty = true;
        }
        if (fine_slider("##depthfar", &a.profile.features.depth_far, 100.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic, 400.0f * s))
            a.dirty = true;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Distanza massima (m)"));
    }
    uint32_t width = 0, height = 0;
    rt->get_screenshot_width_and_height(&width, &height);
    std::snprintf(line, sizeof(line), tr("Immagine elaborata da ReShade: %u x %u   |   Effetti ReShade: %s"),
                  width, height, rt->get_effects_state() ? tr("attivi") : tr("SPENTI"));
    text_dim(line);
    if (ImGui::BeginTable("diag", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f), 0.0f))
    {
        ImGui::TableSetupColumn(tr("Shader"), ImGuiTableColumnFlags_WidthStretch, 3.0f, 0);
        ImGui::TableSetupColumn(tr("Trovato"), ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
        ImGui::TableSetupColumn(tr("Richiesto"), ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
        ImGui::TableSetupColumn(tr("In ReShade"), ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
        ImGui::TableHeadersRow();
        for (const EffectInfo &e : effect_catalog())
        {
            if (e.fx_file == nullptr || !a.pipeline.shader_found(rt, e.id))
                continue;
            ImGui::TableNextRow(0, 0.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e.fx_file);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(tr("sì"));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(a.pipeline.is_active(rt, e.id) ? tr("acceso") : tr("spento"));
            ImGui::TableSetColumnIndex(3);
            const bool on = a.pipeline.reshade_state(rt, e.id);
            ImGui::PushStyleColor(ImGuiCol_Text, on == a.pipeline.is_active(rt, e.id) ? col::good : col::bad);
            ImGui::TextUnformatted(on ? tr("acceso") : tr("spento"));
            ImGui::PopStyleColor(1);
        }
        ImGui::EndTable();
    }
}

// --- Pagina Tasti -----------------------------------------------------------------

void draw_keys(App &a)
{
    const float s = theme::scale();
    theme::section(tr("Tasti rapidi"));
    text_dim(tr("rF2 legge la tastiera per conto suo e ignora Ctrl, Alt e Maiusc: usa solo tasti che rF2 non ha assegnato."));

    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f), 0.0f))
    {
        ImGui::TableSetupColumn(tr("Azione"), ImGuiTableColumnFlags_WidthStretch, 2.0f, 0);
        ImGui::TableSetupColumn(tr("Tasto"), ImGuiTableColumnFlags_WidthStretch, 2.5f, 0);
        ImGui::TableSetupColumn("cambia", ImGuiTableColumnFlags_WidthFixed, 200.0f * s, 0);

        for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i)
        {
            const HotkeyAction action = static_cast<HotkeyAction>(i);
            ImGui::PushID(i);
            ImGui::TableNextRow(0, 0.0f);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(hotkey_action_label(action)));

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            const std::string name = hotkey_name(action);
            ImGui::TextUnformatted(name.c_str());
            const std::string conflict = hotkey_conflict(action);
            if (!conflict.empty())
            {
                ImGui::SameLine();
                const std::string note = tr("usato da rF2: ") + conflict;
                theme::chip(note.c_str(), ImVec4(col::bad.x, col::bad.y, col::bad.z, 0.18f), col::bad);
            }

            ImGui::TableSetColumnIndex(2);
            const bool capturing = hotkey_capturing(action);
            if (ImGui::Button(capturing ? tr("Premi un tasto...") : tr("Cambia"), ImVec2(-1.0f, 0.0f)) && !capturing)
                start_hotkey_capture(action);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::Button(tr("Ripristina tasti predefiniti"), ImVec2(0.0f, 0.0f)))
    {
        reset_hotkeys();
        a.toast(tr("Tasti predefiniti ripristinati"));
    }

    theme::section(tr("Pulsanti del volante"));
    char devices[96];
    std::snprintf(devices, sizeof(devices), tr("Controller collegati: %d. Usa pulsanti che non hai assegnato in rF2."), wheel_device_count());
    text_dim(devices);
    if (ImGui::BeginTable("wheel", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f), 0.0f))
    {
        ImGui::TableSetupColumn(tr("Azione"), ImGuiTableColumnFlags_WidthStretch, 2.0f, 0);
        ImGui::TableSetupColumn(tr("Pulsante"), ImGuiTableColumnFlags_WidthStretch, 2.5f, 0);
        ImGui::TableSetupColumn("cambia", ImGuiTableColumnFlags_WidthFixed, 200.0f * s, 0);
        ImGui::TableSetupColumn("togli", ImGuiTableColumnFlags_WidthFixed, 110.0f * s, 0);

        for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i)
        {
            const HotkeyAction action = static_cast<HotkeyAction>(i);
            ImGui::PushID(100 + i);
            ImGui::TableNextRow(0, 0.0f);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(hotkey_action_label(action)));

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            const std::string name = wheel_button_name(action);
            text_dim(name.c_str());

            ImGui::TableSetColumnIndex(2);
            const bool capturing = wheel_capturing(action);
            if (ImGui::Button(capturing ? tr("Premi un pulsante...") : tr("Cambia"), ImVec2(-1.0f, 0.0f)) && !capturing)
                start_wheel_capture(action);

            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button(tr("Togli"), ImVec2(-1.0f, 0.0f)))
                clear_wheel_button(action);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    text_dim(tr("Esc annulla la scelta di un tasto o di un pulsante. Tutto è salvato in KarmaTV-PostFX\\tasti.ini."));
}

// --- Finestra "Salva come nuovo profilo" ---------------------------------------

void draw_save_popup(App &a)
{
    const float s = theme::scale();
    if (g_open_save_popup)
    {
        g_open_save_popup = false;
        g_save_name[0] = '\0';
        ImGui::OpenPopup(tr("Salva come nuovo profilo##ktvsave"), 0);
    }
    if (!ImGui::BeginPopupModal(tr("Salva come nuovo profilo##ktvsave"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted((tr("Le modifiche a ") + a.profile.name + tr(" diventano un profilo nuovo.")).c_str());
    text_dim(tr("Il profilo fisso resta com'era."));
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere(0);
    ImGui::SetNextItemWidth(360.0f * s);
    const bool enter = ImGui::InputTextWithHint("##savename", tr("Nome del profilo (es. Daytona sera)"), g_save_name, sizeof(g_save_name),
                                                ImGuiInputTextFlags_EnterReturnsTrue, nullptr, nullptr);
    a.ui_text_input = true;
    const std::string name(g_save_name);
    const bool taken = a.is_builtin_profile(name);
    if (taken)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::bad);
        ImGui::TextUnformatted(tr("È il nome di un profilo fisso: scegline un altro."));
        ImGui::PopStyleColor(1);
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
    const bool valid = name.find_first_not_of(' ') != std::string::npos && !taken;
    const bool save = theme::button(tr("Salva"), valid ? theme::Look::Go : theme::Look::Normal, theme::Icon::Save, 150.0f * s) || enter;
    ImGui::SameLine(0.0f, 12.0f * s);
    if (theme::button(tr("Annulla"), theme::Look::Normal, theme::Icon::None, 150.0f * s) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
    if (save && valid)
    {
        a.save_profile_as(name);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// --- Piede ----------------------------------------------------------------------

void draw_footer(App &a)
{
    const float s = theme::scale();
    ImGui::AlignTextToFramePadding();
    text_dim(hotkey_help().c_str());

    // Versione e controllo manuale degli aggiornamenti
    ImGui::SameLine(0.0f, 24.0f * s);
    ImGui::AlignTextToFramePadding();
    text_dim((std::string("v") + update::current_version()).c_str());
    const update::Status u = update::status();
    ImGui::SameLine(0.0f, 10.0f * s);
    ImGui::AlignTextToFramePadding();
    const char *check_label = u.state == update::State::Checking ? tr("Controllo...") :
                              (u.state == update::State::UpToDate ? tr("Aggiornato · Controlla") : tr("Controlla aggiornamenti"));
    ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
    ImGui::TextUnformatted(check_label);
    ImGui::PopStyleColor(1);
    if (ImGui::IsItemHovered(0))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked(0))
        update::check(true);

    const bool builtin = a.is_builtin_profile(a.profile_names()[a.profile_index]);
    const bool fixed_changed = builtin && a.modified();
    if (fixed_changed || (!builtin && a.dirty))
    {
        const char *label = fixed_changed ? tr("Salva come nuovo profilo") : tr("Salva modifiche");
        const float width = theme::button_width(label, theme::Icon::Save);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width - 4.0f * s);
        if (theme::button(label, theme::Look::Go, theme::Icon::Save))
        {
            if (fixed_changed)
                g_open_save_popup = true;
            else
                a.save_profile(App::SaveTarget::Loaded);
        }
    }
}
}

void draw_main_window(reshade::api::effect_runtime *runtime)
{
    App &a = app();
    if (runtime != a.primary_runtime)
        return;
    a.ui_text_input = false; // lo riattiva il campo di testo, se e' in uso in questo frame
    if (!a.ui_open)
        return;

    // Finestra aperta: input al gioco bloccato e cursore disegnato da ImGui.
    runtime->block_input_next_frame();
    ImGui::GetIO().MouseDrawCursor = true;

    theme::push();
    const float s = theme::scale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(1100.0f * s, 780.0f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("KarmaTV PostFX##main", &a.ui_open, flags))
    {
        // Fascia scura dell'intestazione e carbonio a scacchi sotto
        {
            ImDrawList *bg = ImGui::GetWindowDrawList();
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            if (g_header_bottom > 0.0f)
            {
                bg->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + g_header_bottom), ImGui::GetColorU32(col::nav), 3.0f * s, ImDrawFlags_RoundCornersTop);
                theme::checker(bg, ImVec2(wp.x + 1.0f, wp.y + g_header_bottom + 3.0f * s), ImVec2(wp.x + ws.x - 1.0f, wp.y + ws.y - 1.0f));
            }
        }
        draw_header(a, runtime);
        draw_update_banner();

        const float footer = ImGui::GetFrameHeightWithSpacing() + 6.0f * s;
        if (!g_advanced)
        {
            // Menu base: scelta del profilo e tasti. Il resto e' sotto "Avanzate".
            if (ImGui::BeginChild("page", ImVec2(0.0f, -footer), ImGuiChildFlags_AlwaysUseWindowPadding, 0))
            {
                if (g_simple_keys)
                {
                    if (theme::button(tr("Torna ai profili"), theme::Look::Normal, theme::Icon::Back))
                        g_simple_keys = false;
                    draw_keys(a);
                }
                else
                {
                    draw_profile_picker(a);
                    theme::section(tr("Tasti"));
                    if (theme::button(tr("Personalizza i tasti"), theme::Look::Outline, theme::Icon::Keyboard))
                        g_simple_keys = true;
                    ImGui::SameLine(0.0f, 14.0f * s);
                    ImGui::AlignTextToFramePadding();
                    text_dim(tr("Tasti della tastiera e pulsanti del volante per menu, effetti e profili."));
                }
            }
            ImGui::EndChild();
        }
        else
        {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col::nav);
        if (ImGui::BeginChild("nav", ImVec2(220.0f * s, -footer), ImGuiChildFlags_AlwaysUseWindowPadding, 0))
            draw_navigation();
        ImGui::EndChild();
        ImGui::PopStyleColor(1);
        ImGui::SameLine();
        if (ImGui::BeginChild("page", ImVec2(0.0f, -footer), ImGuiChildFlags_AlwaysUseWindowPadding, 0))
        {
            switch (g_page)
            {
            case Page::Effects: draw_effects(a, runtime); break;
            case Page::Profiles: draw_profiles(a); break;
            case Page::Rules: draw_rules(a); break;
            case Page::Broadcast: draw_broadcast(a, runtime); break;
            case Page::Performance: draw_performance(a, runtime); break;
            case Page::Keys: draw_keys(a); break;
            }
        }
        ImGui::EndChild();
        }

        draw_footer(a);
        draw_save_popup(a);
    }
    ImGui::End();
    theme::pop();
}

void draw_toast(reshade::api::effect_runtime *runtime)
{
    App &a = app();
    if (runtime != a.primary_runtime || a.toast_text.empty() || now_seconds() > a.toast_until)
        return;

    theme::push();
    const float s = theme::scale();
    ImGui::SetNextWindowPos(ImVec2(28.0f * s, 28.0f * s), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##ktv_toast", nullptr, flags))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
        ImGui::TextUnformatted("KARMATV");
        ImGui::PopStyleColor(1);
        ImGui::SameLine();
        ImGui::TextUnformatted(a.toast_text.c_str());
    }
    ImGui::End();
    theme::pop();
}
}
