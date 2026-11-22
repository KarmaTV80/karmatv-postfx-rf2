#pragma once

// Include comune: Windows, Dear ImGui e API add-on di ReShade, nell'ordine richiesto.
// reshade_overlay.hpp verifica che ImTextureID sia a 64 bit: va definito prima di imgui.h.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifndef ImTextureID
#define ImTextureID ImU64
#endif
#include <imgui.h>
#include <reshade.hpp>
