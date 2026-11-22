#include "core/i18n.hpp"
#include "app.hpp"
#include "common.hpp"

#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>

namespace ktv::i18n
{
namespace
{
// Il portoghese e' quello del Brasile, come nel Server Manager.
constexpr Language kLanguages[] = {
    {"en", "English"},
    {"it", "Italiano"},
    {"es", "Español"},
    {"fr", "Français"},
    {"de", "Deutsch"},
    {"pt", "Português (Brasil)"},
};

// Risorse RCDATA con i file lang/<codice>.tsv (vedi resources.rc)
int resource_id(const std::string &code)
{
    if (code == "en") return 201;
    if (code == "es") return 202;
    if (code == "fr") return 203;
    if (code == "de") return 204;
    if (code == "pt") return 205;
    return 0;
}

using Table = std::unordered_map<std::string, std::string>;
std::map<std::string, std::unique_ptr<Table>> g_tables; // mai liberate: i puntatori restano validi
Table *g_table = nullptr;                                  // nullptr = italiano
std::string g_current = "en";
// Testi composti (etichette con "##", liste): cache per lingua
std::map<std::string, std::unique_ptr<Table>> g_composed;

std::string unescape(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\\' || i + 1 >= text.size())
        {
            out += text[i];
            continue;
        }
        const char c = text[++i];
        if (c == 'n') out += '\n';
        else if (c == 't') out += '\t';
        else if (c == '0') out += '\0';
        else out += c;
    }
    return out;
}

Table *load(const std::string &code)
{
    const auto found = g_tables.find(code);
    if (found != g_tables.end())
        return found->second.get();
    auto table = std::make_unique<Table>();
    HMODULE module = app().module;
    const int id = resource_id(code);
    HRSRC info = id != 0 ? FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA) : nullptr;
    HGLOBAL handle = info != nullptr ? LoadResource(module, info) : nullptr;
    const char *data = handle != nullptr ? static_cast<const char *>(LockResource(handle)) : nullptr;
    if (data != nullptr)
    {
        // Righe "italiano<TAB>traduzione", con \n \t \0 \\ come sequenze; # = commento
        const std::string text(data, SizeofResource(module, info));
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            start = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF)
                line.erase(0, 3); // BOM
            if (line.empty() || line[0] == '#')
                continue;
            const size_t tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            const std::string value = unescape(line.substr(tab + 1));
            if (!value.empty())
                (*table)[unescape(line.substr(0, tab))] = value;
        }
    }
    Table *raw = table.get();
    g_tables[code] = std::move(table);
    return raw;
}

Table &composed()
{
    auto &slot = g_composed[g_current];
    if (!slot)
        slot = std::make_unique<Table>();
    return *slot;
}

std::string lookup(const std::string &key)
{
    if (g_table == nullptr)
        return key;
    const auto it = g_table->find(key);
    return it != g_table->end() ? it->second : key;
}
}

const Language *languages(int *count)
{
    *count = static_cast<int>(sizeof(kLanguages) / sizeof(kLanguages[0]));
    return kLanguages;
}

const char *current()
{
    return g_current.c_str();
}

void set_language(const std::string &code)
{
    std::string chosen = code;
    if (chosen.empty())
    {
        switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
        {
        case LANG_ITALIAN: chosen = "it"; break;
        case LANG_SPANISH: chosen = "es"; break;
        case LANG_FRENCH: chosen = "fr"; break;
        case LANG_GERMAN: chosen = "de"; break;
        case LANG_PORTUGUESE: chosen = "pt"; break;
        default: chosen = "en"; break;
        }
    }
    bool known = false;
    for (const Language &l : kLanguages)
        known = known || chosen == l.code;
    if (!known)
        chosen = "en";
    g_current = chosen;
    g_table = chosen == "it" ? nullptr : load(chosen);
}

const char *tr(const char *italian)
{
    if (g_table == nullptr || italian == nullptr || *italian == '\0')
        return italian;
    const char *hash = std::strstr(italian, "##");
    if (hash == nullptr)
    {
        const auto it = g_table->find(italian);
        return it != g_table->end() ? it->second.c_str() : italian;
    }
    // "Etichetta##id": si traduce la parte visibile, l'id resta uguale (ImGui lo usa come chiave)
    Table &cache = composed();
    const auto cached = cache.find(italian);
    if (cached != cache.end())
        return cached->second.c_str();
    const std::string result = lookup(std::string(italian, hash)) + hash;
    return cache.emplace(italian, result).first->second.c_str();
}

const char *tr_items(const char *italian)
{
    if (g_table == nullptr || italian == nullptr)
        return italian;
    // Lunghezza fino al doppio '\0' finale
    size_t length = 0;
    while (italian[length] != '\0' || italian[length + 1] != '\0')
        ++length;
    const std::string key(italian, length + 1); // con l'ultimo '\0' della voce finale
    Table &cache = composed();
    const auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second.c_str();
    // Ogni voce tradotta da sola
    std::string result;
    for (size_t start = 0; start < length + 1;)
    {
        const char *item = italian + start;
        const size_t n = std::strlen(item);
        result += lookup(std::string(item, n));
        result += '\0';
        start += n + 1;
    }
    result += '\0';
    return cache.emplace(key, result).first->second.c_str();
}
}
