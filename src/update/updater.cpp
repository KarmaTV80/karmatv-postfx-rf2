#include "update/updater.hpp"
#include "common.hpp"
#include "core/log.hpp"

#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#ifndef KTV_VERSION
#define KTV_VERSION "0.0.0"
#endif

namespace ktv::update
{
namespace
{
constexpr const char kRepository[] = "KarmaTV80/karmatv-postfx-rf2";

std::mutex g_mutex;
Status g_status;
std::string g_exe_url;
std::string g_hash_url;
bool g_busy = false;
HANDLE g_waiter = nullptr; // processo che aspetta la chiusura di rF2 e avvia l'installer

void set_state(State state, const std::string &message = {})
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_status.state = state;
    g_status.message = message;
}

std::wstring widen(const std::string &text)
{
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), static_cast<int>(out.size()));
    out.resize(n > 0 ? n : 0);
    return out;
}

// KTV_UPDATE_URL: solo per le prove ("off" spegne il controllo, un URL sostituisce GitHub)
std::string override_url()
{
    char value[512] = {};
    const DWORD n = GetEnvironmentVariableA("KTV_UPDATE_URL", value, sizeof(value));
    return n > 0 && n < sizeof(value) ? std::string(value, n) : std::string();
}

// GET via WinHTTP (segue i reindirizzamenti di GitHub verso i file). Nessun dato dell'utente:
// GitHub chiede solo il nome del programma.
bool http_get(const std::string &url, std::string &body, const std::function<void(uint64_t, uint64_t)> &progress = {})
{
    const std::wstring wurl = widen(url);
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts))
        return false;

    const std::wstring agent = widen(std::string("KarmaTV-PostFX/") + KTV_VERSION);
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return false;
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
    bool ok = false;
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection != nullptr
                            ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr;
    if (request != nullptr &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json, application/octet-stream\r\n", static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr))
    {
        DWORD code = 0, size = sizeof(code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        uint64_t total = 0;
        wchar_t length[32] = {};
        size = sizeof(length);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &size, WINHTTP_NO_HEADER_INDEX))
            total = _wcstoui64(length, nullptr, 10);
        if (code == 200)
        {
            body.clear();
            ok = true;
            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available))
                {
                    ok = false;
                    break;
                }
                if (available == 0)
                    break;
                const size_t old = body.size();
                body.resize(old + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, body.data() + old, available, &read))
                {
                    ok = false;
                    break;
                }
                body.resize(old + read);
                if (progress)
                    progress(body.size(), total);
            }
        }
        else
        {
            log::info("Aggiornamenti: risposta " + std::to_string(code) + " da " + url);
        }
    }
    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// Valore stringa JSON dopo la chiave indicata (a partire da from); vuoto se manca.
std::string json_string(const std::string &json, const char *key, size_t from = 0, size_t *end = nullptr)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = json.find(needle, from);
    if (p == std::string::npos)
        return {};
    p = json.find(':', p + needle.size());
    if (p == std::string::npos)
        return {};
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || json[p] != '"')
        return {};
    std::string out;
    for (++p; p < json.size() && json[p] != '"'; ++p)
    {
        char c = json[p];
        if (c != '\\' || p + 1 >= json.size())
        {
            out += c;
            continue;
        }
        c = json[++p];
        switch (c)
        {
        case 'n': out += '\n'; break;
        case 'r': break;
        case 't': out += '\t'; break;
        case 'u':
            if (p + 4 < json.size())
            {
                const unsigned cp = static_cast<unsigned>(std::stoul(json.substr(p + 1, 4), nullptr, 16));
                p += 4;
                if (cp < 0x80)
                    out += static_cast<char>(cp);
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }
            break;
        default: out += c; break;
        }
    }
    if (end != nullptr)
        *end = p;
    return out;
}

bool json_true(const std::string &json, const char *key)
{
    const std::string needle = std::string("\"") + key + "\"";
    const size_t p = json.find(needle);
    if (p == std::string::npos)
        return false;
    const size_t v = json.find_first_not_of(" :\t", p + needle.size());
    return v != std::string::npos && json.compare(v, 4, "true") == 0;
}

// "v1.2.3" -> {1,2,3}; false se non e' un numero di versione
bool parse_version(const std::string &text, int out[3])
{
    const char *s = text.c_str();
    if (*s == 'v' || *s == 'V')
        ++s;
    out[0] = out[1] = out[2] = 0;
    return sscanf_s(s, "%d.%d.%d", &out[0], &out[1], &out[2]) >= 2;
}

bool newer(const std::string &candidate, const std::string &current)
{
    int a[3], b[3];
    if (!parse_version(candidate, a) || !parse_version(current, b))
        return false;
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i])
            return a[i] > b[i];
    return false;
}

bool ends_with(const std::string &text, const std::string &suffix)
{
    if (text.size() < suffix.size())
        return false;
    for (size_t i = 0; i < suffix.size(); ++i)
        if (tolower(static_cast<unsigned char>(text[text.size() - suffix.size() + i])) != tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    return true;
}

std::string sha256_hex(const std::string &data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
    {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())), static_cast<ULONG>(data.size()), 0) == 0 &&
            BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
        {
            char hex[65] = {};
            for (int i = 0; i < 32; ++i)
                std::snprintf(hex + i * 2, 3, "%02X", digest[i]);
            out = hex;
        }
        if (hash != nullptr)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return out;
}

// Prima impronta SHA-256 nel testo ("HASH  KarmaTV-PostFX-Setup-x.exe")
std::string find_hash(const std::string &text)
{
    for (size_t i = 0; i + 64 <= text.size(); ++i)
    {
        size_t n = 0;
        while (n < 64 && isxdigit(static_cast<unsigned char>(text[i + n])))
            ++n;
        if (n == 64 && (i + 64 == text.size() || !isxdigit(static_cast<unsigned char>(text[i + 64]))))
        {
            std::string hash = text.substr(i, 64);
            for (char &c : hash)
                c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            return hash;
        }
        i += n;
    }
    return {};
}

void run_check()
{
    const std::string override = override_url();
    if (override == "off")
    {
        set_state(State::UpToDate);
        return;
    }
    const std::string url = override.empty() ? std::string("https://api.github.com/repos/") + kRepository + "/releases/latest" : override;
    std::string json;
    if (!http_get(url, json))
    {
        // Nessuna versione pubblicata (404) o niente rete: nessun messaggio invadente
        log::info("Aggiornamenti: controllo non riuscito o nessuna versione pubblicata.");
        set_state(State::UpToDate);
        return;
    }
    const std::string tag = json_string(json, "tag_name");
    if (json_true(json, "draft") || json_true(json, "prerelease") || !newer(tag, KTV_VERSION))
    {
        log::info("Aggiornamenti: nessuna versione nuova (ultima " + tag + ", in uso " + KTV_VERSION + ").");
        set_state(State::UpToDate);
        return;
    }
    // File della versione: l'installer .exe e la sua impronta .sha256.txt
    std::string exe, hash;
    for (size_t from = 0;;)
    {
        size_t end = 0;
        const std::string asset = json_string(json, "browser_download_url", from, &end);
        if (asset.empty())
            break;
        from = end;
        if (ends_with(asset, ".exe.sha256.txt"))
            hash = asset;
        else if (ends_with(asset, ".exe"))
            exe = asset;
    }
    if (exe.empty() || hash.empty())
    {
        log::info("Aggiornamenti: la versione " + tag + " non ha l'installer o la sua impronta.");
        set_state(State::UpToDate);
        return;
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_exe_url = exe;
    g_hash_url = hash;
    g_status.version = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
    g_status.notes = json_string(json, "body");
    g_status.page = json_string(json, "html_url");
    g_status.state = State::Available;
    log::info("Aggiornamenti: disponibile la versione " + g_status.version + ".");
}

bool start_waiter(const std::filesystem::path &setup)
{
    // PowerShell nascosto: aspetta la chiusura di rF2 (questo processo), poi avvia l'installer.
    // Windows chiede il permesso di amministratore solo allora, a gioco chiuso.
    std::wstring file = setup.wstring();
    std::wstring quoted;
    for (wchar_t c : file)
        quoted += (c == L'\'') ? std::wstring(L"''") : std::wstring(1, c);
    std::wstring command = L"powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"Wait-Process -Id " +
                           std::to_wstring(GetCurrentProcessId()) +
                           L" -ErrorAction SilentlyContinue; Start-Process -FilePath '" + quoted +
                           L"' -ArgumentList '/SILENT','/SUPPRESSMSGBOXES','/NORESTART'\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
    if (!ok)
        ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok)
        return false;
    CloseHandle(pi.hThread);
    g_waiter = pi.hProcess;
    return true;
}

void run_download()
{
    std::string exe_url, hash_url, version;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        exe_url = g_exe_url;
        hash_url = g_hash_url;
        version = g_status.version;
        g_status.state = State::Downloading;
        g_status.progress = 0.0f;
    }
    std::string hash_text, data;
    if (!http_get(hash_url, hash_text))
    {
        set_state(State::Failed, "Impronta dell'aggiornamento non scaricata.");
        return;
    }
    const std::string expected = find_hash(hash_text);
    const bool got = http_get(exe_url, data, [](uint64_t done, uint64_t total) {
        if (total == 0)
            return;
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_status.progress = static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
    });
    if (!got || data.empty())
    {
        set_state(State::Failed, "Download dell'aggiornamento non riuscito.");
        return;
    }
    if (expected.empty() || sha256_hex(data) != expected)
    {
        set_state(State::Failed, "L'installer scaricato non corrisponde all'impronta: aggiornamento annullato.");
        log::warning("Aggiornamenti: impronta SHA-256 diversa, file scartato.");
        return;
    }

    wchar_t local[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    const std::filesystem::path dir = std::filesystem::path(local) / L"KarmaTV-PostFX" / L"updates";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path setup = dir / widen("KarmaTV-PostFX-Setup-" + version + ".exe");
    {
        std::ofstream out(setup, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out)
        {
            set_state(State::Failed, "Impossibile salvare l'installer in " + setup.string());
            return;
        }
    }
    if (!start_waiter(setup))
    {
        set_state(State::Failed, "Impossibile preparare l'installazione automatica.");
        return;
    }
    log::info("Aggiornamenti: versione " + version + " scaricata, si installa alla chiusura di rF2.");
    set_state(State::Scheduled);
}

void run_async(void (*work)())
{
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        if (g_busy)
            return;
        g_busy = true;
    }
    std::thread([work] {
        work();
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_busy = false;
    }).detach();
}
}

const char *current_version()
{
    return KTV_VERSION;
}

void check(bool force)
{
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        if (g_status.state == State::Downloading || g_status.state == State::Scheduled)
            return;
        if (!force && g_status.state != State::Idle)
            return;
        g_status.state = State::Checking;
    }
    run_async(&run_check);
}

Status status()
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_status;
}

void download_and_schedule()
{
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        if (g_status.state != State::Available && g_status.state != State::Failed)
            return;
        if (g_exe_url.empty())
            return;
    }
    run_async(&run_download);
}

void cancel_schedule()
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_waiter != nullptr)
    {
        TerminateProcess(g_waiter, 0);
        CloseHandle(g_waiter);
        g_waiter = nullptr;
    }
    if (g_status.state == State::Scheduled)
        g_status.state = State::Available;
    log::info("Aggiornamenti: installazione alla chiusura annullata.");
}

void open_page()
{
    std::string page;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        page = g_status.page;
    }
    if (page.empty())
        page = std::string("https://github.com/") + kRepository + "/releases/latest";
    ShellExecuteW(nullptr, L"open", widen(page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
}
