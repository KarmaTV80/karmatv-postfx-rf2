#include "ui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ktv::theme
{
namespace
{
int g_colors_pushed = 0;
int g_vars_pushed = 0;
constexpr float kPi = 3.14159265f;

void push_color(ImGuiCol index, const ImVec4 &value)
{
    ImGui::PushStyleColor(index, value);
    ++g_colors_pushed;
}

void push_var(ImGuiStyleVar index, float value)
{
    ImGui::PushStyleVar(index, value);
    ++g_vars_pushed;
}

void push_var(ImGuiStyleVar index, const ImVec2 &value)
{
    ImGui::PushStyleVar(index, value);
    ++g_vars_pushed;
}

ImVec4 with_alpha(ImVec4 c, float alpha)
{
    c.w = alpha;
    return c;
}

ImU32 u32(const ImVec4 &c)
{
    return ImGui::GetColorU32(c);
}

// Arco come spezzata (angoli in radianti, 0 = destra, senso orario sullo schermo)
void arc(ImDrawList *draw, const ImVec2 &c, float r, float a0, float a1, ImU32 col, float t, bool closed = false)
{
    ImVec2 points[24];
    const int n = 24;
    for (int i = 0; i < n; ++i)
    {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n - 1);
        points[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    draw->AddPolyline(points, n, col, closed ? ImDrawFlags_Closed : 0, t);
}

// Testo visibile di un'etichetta ImGui (senza la parte dopo "##")
std::string visible(const char *label)
{
    const char *hash = std::strstr(label, "##");
    return hash != nullptr ? std::string(label, hash) : std::string(label);
}
}

float scale()
{
    const float height = ImGui::GetIO().DisplaySize.y;
    return std::clamp(height / 1440.0f, 0.75f, 2.0f);
}

float font_size()
{
    // ReShade moltiplica la dimensione per il suo FontScale (STYLE/FontScale in ReShade.ini):
    // lo si compensa per avere 21 px a 1440 px di altezza indipendentemente da quel valore.
    // 21 px perche' il carattere del tema (Barlow Condensed) e' stretto.
    const float reshade_scale = std::max(ImGui::GetStyle().FontScaleMain, 0.1f);
    return 21.0f * scale() / reshade_scale;
}

std::string upper(const char *text)
{
    std::string out(text);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'a' && c <= 'z')
            out[i] = static_cast<char>(c - 32);
        // UTF-8 latino: à è é ì ò ù (C3 A0..BE) -> À È É Ì Ò Ù (C3 80..9E)
        else if (c == 0xC3 && i + 1 < out.size())
        {
            const unsigned char next = static_cast<unsigned char>(out[i + 1]);
            if (next >= 0xA0 && next <= 0xBE && next != 0xB7)
                out[i + 1] = static_cast<char>(next - 0x20);
            ++i;
        }
    }
    return out;
}

void push()
{
    const float s = scale();
    using namespace color;

    push_var(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * s, 18.0f * s));
    push_var(ImGuiStyleVar_FramePadding, ImVec2(12.0f * s, 7.0f * s));
    push_var(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f * s, 10.0f * s));
    push_var(ImGuiStyleVar_CellPadding, ImVec2(8.0f * s, 6.0f * s));
    push_var(ImGuiStyleVar_WindowRounding, 3.0f * s);
    push_var(ImGuiStyleVar_ChildRounding, 0.0f);
    push_var(ImGuiStyleVar_FrameRounding, 0.0f);
    push_var(ImGuiStyleVar_GrabRounding, 0.0f);
    push_var(ImGuiStyleVar_PopupRounding, 0.0f);
    push_var(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    push_var(ImGuiStyleVar_ScrollbarSize, 10.0f * s);
    push_var(ImGuiStyleVar_GrabMinSize, 14.0f * s);
    push_var(ImGuiStyleVar_WindowBorderSize, 1.0f);
    push_var(ImGuiStyleVar_ChildBorderSize, 0.0f);
    push_var(ImGuiStyleVar_FrameBorderSize, 0.0f);
    push_var(ImGuiStyleVar_PopupBorderSize, 1.0f);

    push_color(ImGuiCol_WindowBg, background);
    push_color(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // si vedono gli scacchi
    push_color(ImGuiCol_PopupBg, ImVec4(0.075f, 0.082f, 0.094f, 0.99f));
    push_color(ImGuiCol_Border, ImVec4(0.173f, 0.188f, 0.212f, 1.0f));
    push_color(ImGuiCol_Text, text);
    push_color(ImGuiCol_TextDisabled, text_dim);
    push_color(ImGuiCol_TextSelectedBg, with_alpha(accent, 0.35f));
    push_color(ImGuiCol_FrameBg, ImVec4(0.035f, 0.039f, 0.047f, 0.95f));
    push_color(ImGuiCol_FrameBgHovered, hover);
    push_color(ImGuiCol_FrameBgActive, with_alpha(accent, 0.25f));
    push_color(ImGuiCol_SliderGrab, accent);
    push_color(ImGuiCol_SliderGrabActive, accent_hover);
    push_color(ImGuiCol_CheckMark, accent);
    push_color(ImGuiCol_Button, surface_high);
    push_color(ImGuiCol_ButtonHovered, hover);
    push_color(ImGuiCol_ButtonActive, border);
    push_color(ImGuiCol_Header, accent_soft);
    push_color(ImGuiCol_HeaderHovered, hover);
    push_color(ImGuiCol_HeaderActive, with_alpha(accent, 0.30f));
    push_color(ImGuiCol_Separator, ImVec4(0.173f, 0.188f, 0.212f, 1.0f));
    push_color(ImGuiCol_TitleBg, nav);
    push_color(ImGuiCol_TitleBgActive, nav);
    push_color(ImGuiCol_TableHeaderBg, surface_high);
    push_color(ImGuiCol_TableRowBg, ImVec4(0.098f, 0.110f, 0.125f, 0.55f));
    push_color(ImGuiCol_TableRowBgAlt, ImVec4(0.118f, 0.129f, 0.149f, 0.55f));
    push_color(ImGuiCol_TableBorderLight, ImVec4(0.173f, 0.188f, 0.212f, 1.0f));
    push_color(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    push_color(ImGuiCol_ScrollbarGrab, border);
    push_color(ImGuiCol_ScrollbarGrabHovered, text_dim);
    push_color(ImGuiCol_ScrollbarGrabActive, accent);
    push_color(ImGuiCol_PlotHistogram, accent);

    ImGui::PushFont(nullptr, font_size());
}

void pop()
{
    ImGui::PopFont();
    ImGui::PopStyleColor(g_colors_pushed);
    ImGui::PopStyleVar(g_vars_pushed);
    g_colors_pushed = 0;
    g_vars_pushed = 0;
}

void checker(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max)
{
    const float cell = 56.0f * scale();
    const ImU32 col = u32(ImVec4(0.086f, 0.094f, 0.110f, 0.85f));
    int row = 0;
    for (float y = min.y; y < max.y; y += cell, ++row)
    {
        int column = 0;
        for (float x = min.x; x < max.x; x += cell, ++column)
            if (((row + column) & 1) == 0)
                draw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), col, 0.0f, 0);
    }
}

void slant_fill(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, float skew, ImU32 color)
{
    draw->AddQuadFilled(ImVec2(min.x + skew, min.y), ImVec2(max.x, min.y), ImVec2(max.x - skew, max.y), ImVec2(min.x, max.y), color);
}

void slant_frame(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, float skew, ImU32 color, float thickness)
{
    draw->AddQuad(ImVec2(min.x + skew, min.y), ImVec2(max.x, min.y), ImVec2(max.x - skew, max.y), ImVec2(min.x, max.y), color, thickness);
}

void icon(ImDrawList *draw, Icon which, const ImVec2 &c, float size, ImU32 col)
{
    const float r = size * 0.5f;
    const float t = std::max(1.4f, size * 0.085f);
    const auto P = [&c, r](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    switch (which)
    {
    case Icon::None:
        break;
    case Icon::Off:
        draw->AddCircle(c, r * 0.85f, col, 0, t);
        draw->AddLine(P(-0.6f, -0.6f), P(0.6f, 0.6f), col, t);
        break;
    case Icon::Wheel:
        draw->AddCircle(c, r * 0.88f, col, 0, t);
        draw->AddCircle(c, r * 0.22f, col, 0, t);
        draw->AddLine(P(-0.86f, -0.1f), P(-0.22f, -0.05f), col, t);
        draw->AddLine(P(0.22f, -0.05f), P(0.86f, -0.1f), col, t);
        draw->AddLine(P(0.0f, 0.22f), P(0.0f, 0.86f), col, t);
        break;
    case Icon::Camera:
        draw->AddRect(P(-0.9f, -0.5f), P(0.25f, 0.5f), col, 0.0f, 0, t);
        draw->AddQuad(P(0.25f, -0.15f), P(0.9f, -0.5f), P(0.9f, 0.5f), P(0.25f, 0.15f), col, t);
        break;
    case Icon::Headset:
    {
        const ImVec2 pts[] = {P(-0.9f, -0.45f), P(0.9f, -0.45f), P(0.9f, 0.45f), P(0.25f, 0.45f), P(0.1f, 0.15f),
                              P(-0.1f, 0.15f), P(-0.25f, 0.45f), P(-0.9f, 0.45f)};
        draw->AddPolyline(pts, 8, col, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::User:
        draw->AddCircle(P(0.0f, -0.35f), r * 0.32f, col, 0, t);
        arc(draw, P(0.0f, 0.85f), r * 0.65f, kPi, 2.0f * kPi, col, t);
        break;
    case Icon::Lock:
        draw->AddRect(P(-0.6f, -0.05f), P(0.6f, 0.8f), col, 0.0f, 0, t);
        arc(draw, P(0.0f, -0.05f), r * 0.38f, kPi, 2.0f * kPi, col, t);
        break;
    case Icon::Keyboard:
        draw->AddRect(P(-0.95f, -0.55f), P(0.95f, 0.55f), col, 0.0f, 0, t);
        for (int i = 0; i < 4; ++i)
            draw->AddRectFilled(P(-0.62f + 0.4f * i - 0.07f, -0.22f), P(-0.62f + 0.4f * i + 0.07f, -0.08f), col, 0.0f, 0);
        draw->AddLine(P(-0.45f, 0.25f), P(0.45f, 0.25f), col, t);
        break;
    case Icon::Star:
    {
        ImVec2 pts[10];
        for (int i = 0; i < 10; ++i)
        {
            const float a = -kPi * 0.5f + static_cast<float>(i) * kPi / 5.0f;
            const float rr = (i % 2 == 0) ? r * 0.95f : r * 0.42f;
            pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
        }
        draw->AddPolyline(pts, 10, col, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::Sliders:
        draw->AddLine(P(-0.9f, -0.45f), P(0.9f, -0.45f), col, t);
        draw->AddLine(P(-0.9f, 0.45f), P(0.9f, 0.45f), col, t);
        draw->AddCircleFilled(P(0.35f, -0.45f), r * 0.2f, col, 0);
        draw->AddCircleFilled(P(-0.35f, 0.45f), r * 0.2f, col, 0);
        break;
    case Icon::Save:
        draw->AddRect(P(-0.8f, -0.8f), P(0.8f, 0.8f), col, 0.0f, 0, t);
        draw->AddRect(P(-0.45f, -0.8f), P(0.4f, -0.2f), col, 0.0f, 0, t);
        draw->AddRect(P(-0.5f, 0.2f), P(0.5f, 0.8f), col, 0.0f, 0, t);
        break;
    case Icon::Back:
        draw->AddLine(P(0.85f, 0.0f), P(-0.8f, 0.0f), col, t);
        draw->AddLine(P(-0.8f, 0.0f), P(-0.2f, -0.6f), col, t);
        draw->AddLine(P(-0.8f, 0.0f), P(-0.2f, 0.6f), col, t);
        break;
    case Icon::Layers:
        draw->AddQuad(P(0.0f, -0.85f), P(0.9f, -0.35f), P(0.0f, 0.15f), P(-0.9f, -0.35f), col, t);
        draw->AddLine(P(-0.9f, 0.15f), P(0.0f, 0.65f), col, t);
        draw->AddLine(P(0.0f, 0.65f), P(0.9f, 0.15f), col, t);
        break;
    case Icon::Bolt:
    {
        const ImVec2 pts[] = {P(0.2f, -0.95f), P(-0.55f, 0.1f), P(0.0f, 0.1f), P(-0.2f, 0.95f), P(0.55f, -0.15f), P(0.0f, -0.15f)};
        draw->AddPolyline(pts, 6, col, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::Broadcast:
        draw->AddCircleFilled(c, r * 0.16f, col, 0);
        arc(draw, c, r * 0.5f, -kPi * 0.3f, kPi * 0.3f, col, t);
        arc(draw, c, r * 0.5f, kPi * 0.7f, kPi * 1.3f, col, t);
        arc(draw, c, r * 0.9f, -kPi * 0.3f, kPi * 0.3f, col, t);
        arc(draw, c, r * 0.9f, kPi * 0.7f, kPi * 1.3f, col, t);
        break;
    case Icon::Gauge:
        arc(draw, P(0.0f, 0.35f), r * 0.9f, kPi, 2.0f * kPi, col, t);
        draw->AddLine(P(0.0f, 0.35f), P(0.5f, -0.3f), col, t);
        draw->AddCircleFilled(P(0.0f, 0.35f), r * 0.14f, col, 0);
        break;
    }
}

float button_width(const char *label, Icon with)
{
    const float s = scale();
    const float text = ImGui::CalcTextSize(upper(visible(label).c_str()).c_str(), nullptr, false, -1.0f).x;
    const float height = ImGui::GetFrameHeight() + 4.0f * s;
    const float icon = with != Icon::None ? height * 0.5f + 9.0f * s : 0.0f;
    return text + icon + 28.0f * s + height * 0.24f;
}

bool button(const char *label, Look look, Icon with, float width)
{
    const float s = scale();
    const std::string text = upper(visible(label).c_str());
    const ImVec2 text_size = ImGui::CalcTextSize(text.c_str(), nullptr, false, -1.0f);
    const float height = ImGui::GetFrameHeight() + 4.0f * s;
    const float skew = height * 0.24f;
    const float pad = 14.0f * s;
    const float icon_size = with != Icon::None ? height * 0.5f : 0.0f;
    const float gap = with != Icon::None ? 9.0f * s : 0.0f;
    const float content = icon_size + gap + text_size.x;
    const float w = std::max(width, content + pad * 2.0f + skew);

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, height), 0);
    const bool hovered = ImGui::IsItemHovered(0);
    const bool held = ImGui::IsItemActive();
    const ImVec2 max(min.x + w, min.y + height);

    ImVec4 fill(0, 0, 0, 0), frame = color::border, fore = color::text;
    switch (look)
    {
    case Look::Normal:
        fill = hovered ? color::hover : color::surface_high;
        break;
    case Look::Primary:
        fill = hovered ? color::accent_hover : color::accent;
        frame = fill;
        fore = color::on_accent;
        break;
    case Look::Outline:
        fill = hovered ? color::hover : ImVec4(0.039f, 0.043f, 0.051f, 0.6f);
        frame = color::accent;
        break;
    case Look::Go:
        fill = hovered ? ImVec4(0.30f, 0.82f, 0.54f, 1.0f) : color::go;
        frame = fill;
        fore = ImVec4(0.024f, 0.078f, 0.047f, 1.0f);
        break;
    case Look::Danger:
        fill = hovered ? color::hover : ImVec4(0.039f, 0.043f, 0.051f, 0.6f);
        frame = color::bad;
        fore = color::bad;
        break;
    }
    if (held)
        fill.w *= 0.85f;

    ImDrawList *draw = ImGui::GetWindowDrawList();
    slant_fill(draw, min, max, skew, u32(fill));
    slant_frame(draw, min, max, skew, u32(frame), 1.0f);

    float x = min.x + (w - content) * 0.5f;
    if (with != Icon::None)
    {
        icon(draw, with, ImVec2(x + icon_size * 0.5f, min.y + height * 0.5f), icon_size, u32(fore));
        x += icon_size + gap;
    }
    draw->AddText(ImVec2(x, min.y + (height - text_size.y) * 0.5f), u32(fore), text.c_str(), nullptr);
    return clicked;
}

bool toggle(const char *id, bool *value)
{
    const float frame = ImGui::GetFrameHeight();
    const float height = frame * 0.66f;
    const float width = height * 2.1f;
    const float skew = height * 0.3f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, frame), 0);
    if (clicked)
        *value = !*value;
    const bool hovered = ImGui::IsItemHovered(0);

    const float top = origin.y + (frame - height) * 0.5f;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec4 track = *value ? (hovered ? color::accent_hover : color::accent)
                                : (hovered ? color::hover : color::surface_high);
    const ImVec2 min(origin.x, top), max(origin.x + width, top + height);
    slant_fill(draw, min, max, skew, u32(track));
    slant_frame(draw, min, max, skew, u32(*value ? track : color::border), 1.0f);

    const float knob = height - 6.0f;
    const float kx = *value ? max.x - knob - skew * 0.5f - 3.0f : min.x + skew * 0.5f + 3.0f;
    slant_fill(draw, ImVec2(kx, top + 3.0f), ImVec2(kx + knob, top + 3.0f + knob), skew * 0.6f,
               u32(*value ? color::on_accent : color::text_dim));
    return clicked;
}

void chip(const char *text, const ImVec4 &background, const ImVec4 &foreground)
{
    const float s = scale();
    const std::string label = upper(text);
    const ImVec2 padding(10.0f * s, 2.0f * s);
    const ImVec2 text_size = ImGui::CalcTextSize(label.c_str(), nullptr, false, -1.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float frame = ImGui::GetFrameHeight();
    const float h = text_size.y + padding.y * 2.0f;
    const float top = origin.y + (frame - h) * 0.5f;
    const float skew = h * 0.25f;
    const float w = text_size.x + padding.x * 2.0f + skew;

    ImDrawList *draw = ImGui::GetWindowDrawList();
    slant_fill(draw, ImVec2(origin.x, top), ImVec2(origin.x + w, top + h), skew, u32(background));
    draw->AddText(ImVec2(origin.x + (w - text_size.x) * 0.5f, top + padding.y), u32(foreground), label.c_str(), nullptr);
    ImGui::Dummy(ImVec2(w, frame));
}

void budget_bar(float used_ms, float budget_ms, bool measured)
{
    const float s = scale();
    const float budget = std::max(budget_ms, 0.1f);
    const bool over = used_ms > budget;

    char label[96];
    std::snprintf(label, sizeof(label), "GPU %.2f / %.1f ms%s", used_ms, budget, measured ? "" : " (stima)");
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, over ? color::bad : color::text_dim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, 14.0f * s);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float frame = ImGui::GetFrameHeight();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    const float height = 7.0f * s;
    const float skew = height * 1.2f;
    const ImVec2 min(origin.x, origin.y + (frame - height) * 0.5f), max(origin.x + width, min.y + height);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    slant_fill(draw, min, max, skew, u32(ImVec4(0.141f, 0.153f, 0.176f, 1.0f)));
    const float fraction = std::clamp(used_ms / budget, 0.0f, 1.0f);
    if (fraction > 0.0f)
        slant_fill(draw, min, ImVec2(min.x + std::max(width * fraction, skew * 2.0f), max.y), skew, u32(over ? color::red : color::accent));
    ImGui::Dummy(ImVec2(width, frame));
}

void section(const char *title)
{
    const float s = scale();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    ImGui::PushFont(nullptr, font_size() * 1.12f);
    ImGui::PushStyleColor(ImGuiCol_Text, color::accent);
    ImGui::TextUnformatted(upper(title).c_str());
    ImGui::PopStyleColor(1);
    ImGui::PopFont();
}

void group_label(const char *title)
{
    ImGui::PushFont(nullptr, font_size() * 0.82f);
    ImGui::PushStyleColor(ImGuiCol_Text, color::label);
    ImGui::TextUnformatted(upper(title).c_str());
    ImGui::PopStyleColor(1);
    ImGui::PopFont();
}
}
