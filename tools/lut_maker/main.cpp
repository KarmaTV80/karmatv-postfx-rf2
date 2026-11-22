// ktv_lut - LUT per KarmaTV PostFX.
//
// Formato di una LUT: striscia PNG 1024x32 = 32 fette da 32x32. Nella fetta b, il pixel
// (r, g) contiene il colore di uscita per l'ingresso (r, g, b) / 31.
// L'atlante KarmaTV_LUTs.png impila 10 LUT in verticale (1024x320):
//   0 Neutra, 1 Broadcast TV, 2 Pomeriggio caldo, 3 Nuvoloso freddo, 4 Cinematica,
//   5 Notte, 6-9 Personale 1-4 (da luts\personale_1.png ... personale_4.png).
//
// Uso:
//   ktv_lut atlas  <uscita.png> [cartella_luts]
//   ktv_lut match  <originale.png> <riferimento.png> <uscita_lut.png>
//       Stessa scena (stessa risoluzione): ricostruisce la trasformazione dei colori
//       confrontando i pixel. Risoluzioni diverse: trasferimento statistico del look.
//   ktv_lut apply  <lut.png> <immagine.png> <uscita.png>     (anteprima)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr int N = 32; // punti per canale
constexpr int kAtlasCount = 10;

struct Rgb
{
    float r = 0, g = 0, b = 0;
};

struct Image
{
    int width = 0, height = 0;
    std::vector<Rgb> pixels; // 0..1, sRGB
};

using Lut = std::vector<Rgb>; // N*N*N, indice r + g*N + b*N*N

// --- WIC: lettura e scrittura PNG -------------------------------------------------------

ComPtr<IWICImagingFactory> factory()
{
    static ComPtr<IWICImagingFactory> instance;
    if (instance == nullptr)
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&instance));
    return instance;
}

bool load(const std::wstring &path, Image &out)
{
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory()->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<BYTE> bytes(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(bytes.size()), bytes.data())))
        return false;
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.pixels.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.pixels.size(); ++i)
        out.pixels[i] = {bytes[i * 4] / 255.0f, bytes[i * 4 + 1] / 255.0f, bytes[i * 4 + 2] / 255.0f};
    return true;
}

bool save(const std::wstring &path, const Image &image)
{
    std::vector<BYTE> bytes(image.pixels.size() * 4);
    for (size_t i = 0; i < image.pixels.size(); ++i)
    {
        const Rgb &c = image.pixels[i];
        // Ordine BGRA: il formato nativo del codificatore PNG di Windows
        bytes[i * 4] = static_cast<BYTE>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
        bytes[i * 4 + 1] = static_cast<BYTE>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
        bytes[i * 4 + 2] = static_cast<BYTE>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
        bytes[i * 4 + 3] = 255;
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    return SUCCEEDED(factory()->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(factory()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
           SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize(image.width, image.height)) &&
           SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
           SUCCEEDED(frame->WritePixels(image.height, image.width * 4, static_cast<UINT>(bytes.size()), bytes.data())) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

// --- LUT -----------------------------------------------------------------------------------

int idx(int r, int g, int b) { return r + g * N + b * N * N; }

Lut identity()
{
    Lut lut(N * N * N);
    for (int b = 0; b < N; ++b)
        for (int g = 0; g < N; ++g)
            for (int r = 0; r < N; ++r)
                lut[idx(r, g, b)] = {r / float(N - 1), g / float(N - 1), b / float(N - 1)};
    return lut;
}

Image lut_to_strip(const Lut &lut)
{
    Image strip;
    strip.width = N * N;
    strip.height = N;
    strip.pixels.resize(static_cast<size_t>(strip.width) * strip.height);
    for (int b = 0; b < N; ++b)
        for (int g = 0; g < N; ++g)
            for (int r = 0; r < N; ++r)
                strip.pixels[static_cast<size_t>(g) * strip.width + b * N + r] = lut[idx(r, g, b)];
    return strip;
}

bool strip_to_lut(const Image &strip, Lut &out)
{
    if (strip.width != N * N || strip.height < N)
        return false;
    out.resize(N * N * N);
    for (int b = 0; b < N; ++b)
        for (int g = 0; g < N; ++g)
            for (int r = 0; r < N; ++r)
                out[idx(r, g, b)] = strip.pixels[static_cast<size_t>(g) * strip.width + b * N + r];
    return true;
}

Rgb sample(const Lut &lut, Rgb c)
{
    const float x = std::clamp(c.r, 0.0f, 1.0f) * (N - 1), y = std::clamp(c.g, 0.0f, 1.0f) * (N - 1), z = std::clamp(c.b, 0.0f, 1.0f) * (N - 1);
    const int x0 = std::min(int(x), N - 2), y0 = std::min(int(y), N - 2), z0 = std::min(int(z), N - 2);
    const float fx = x - x0, fy = y - y0, fz = z - z0;
    Rgb result;
    for (int k = 0; k < 8; ++k)
    {
        const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        const float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
        const Rgb &v = lut[idx(x0 + dx, y0 + dy, z0 + dz)];
        result.r += v.r * w;
        result.g += v.g * w;
        result.b += v.b * w;
    }
    return result;
}

// --- Preset integrati (formule semplici su colore sRGB 0..1) --------------------------------

float luma(Rgb c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb saturation(Rgb c, float s) { const float l = luma(c); return mix({l, l, l}, c, s); }
float scurve(float x, float amount) { const float s = x * x * (3 - 2 * x); return x + (s - x) * amount; }
Rgb contrast(Rgb c, float amount) { return {scurve(std::clamp(c.r, 0.f, 1.f), amount), scurve(std::clamp(c.g, 0.f, 1.f), amount), scurve(std::clamp(c.b, 0.f, 1.f), amount)}; }
Rgb multiply(Rgb c, Rgb m) { return {c.r * m.r, c.g * m.g, c.b * m.b}; }
Rgb lift(Rgb c, float black) { return {black + c.r * (1 - black), black + c.g * (1 - black), black + c.b * (1 - black)}; }
// Ombre verso un colore, alte luci verso un altro
Rgb split_tone(Rgb c, Rgb shadows, Rgb highlights, float amount)
{
    const float l = luma(c);
    const Rgb tint = mix(shadows, highlights, std::clamp(l, 0.f, 1.f));
    return mix(c, multiply(c, tint), amount);
}

Lut build_preset(int preset)
{
    Lut lut = identity();
    for (Rgb &c : lut)
    {
        switch (preset)
        {
        case 1: // Broadcast TV: piu' contrasto e colore, bianchi puliti
            c = contrast(c, 0.30f);
            c = saturation(c, 1.18f);
            break;
        case 2: // Pomeriggio caldo: luce dorata, ombre morbide
            c = multiply(c, {1.06f, 1.0f, 0.90f});
            c = split_tone(c, {1.0f, 0.98f, 1.02f}, {1.06f, 1.0f, 0.88f}, 0.6f);
            c = contrast(c, 0.15f);
            c = saturation(c, 1.08f);
            break;
        case 3: // Nuvoloso freddo: tonalita' fredde, neri sollevati, colori trattenuti
            c = multiply(c, {0.95f, 0.99f, 1.07f});
            c = lift(c, 0.035f);
            c = saturation(c, 0.88f);
            break;
        case 4: // Cinematica: ombre verso il ciano, luci verso l'arancio
            c = split_tone(c, {0.88f, 1.02f, 1.10f}, {1.10f, 1.0f, 0.86f}, 0.8f);
            c = contrast(c, 0.28f);
            c = saturation(c, 1.05f);
            break;
        case 5: // Notte: blu nelle ombre, neri profondi, colori calmi
            c = split_tone(c, {0.85f, 0.95f, 1.18f}, {1.0f, 1.0f, 1.0f}, 0.7f);
            c = contrast(c, 0.22f);
            c = saturation(c, 0.85f);
            break;
        default:
            break;
        }
        c = {std::clamp(c.r, 0.f, 1.f), std::clamp(c.g, 0.f, 1.f), std::clamp(c.b, 0.f, 1.f)};
    }
    return lut;
}

// --- LUT da screenshot ------------------------------------------------------------------------

// Stessa scena: per ogni cella del cubo si fa la media dei colori del riferimento
// corrispondenti ai pixel dell'originale che cadono in quella cella. Le celle vuote
// prendono lo spostamento medio delle vicine, poi tutto viene ammorbidito.
Lut match_paired(const Image &source, const Image &target)
{
    std::vector<std::array<double, 4>> sums(N * N * N, {0, 0, 0, 0});
    for (size_t i = 0; i < source.pixels.size(); ++i)
    {
        const Rgb &s = source.pixels[i];
        const Rgb &t = target.pixels[i];
        const int r = int(std::lround(s.r * (N - 1))), g = int(std::lround(s.g * (N - 1))), b = int(std::lround(s.b * (N - 1)));
        auto &cell = sums[idx(r, g, b)];
        cell[0] += t.r - r / double(N - 1);
        cell[1] += t.g - g / double(N - 1);
        cell[2] += t.b - b / double(N - 1);
        cell[3] += 1;
    }

    // Spostamento per cella (solo dove ci sono abbastanza campioni)
    std::vector<Rgb> offset(N * N * N);
    std::vector<float> weight(N * N * N, 0.0f);
    for (int i = 0; i < N * N * N; ++i)
    {
        if (sums[i][3] >= 4)
        {
            offset[i] = {float(sums[i][0] / sums[i][3]), float(sums[i][1] / sums[i][3]), float(sums[i][2] / sums[i][3])};
            weight[i] = 1.0f;
        }
    }

    // Riempimento delle celle vuote per diffusione (le celle lontane da ogni dato tendono a 0)
    for (int pass = 0; pass < 24; ++pass)
    {
        std::vector<Rgb> next = offset;
        std::vector<float> next_weight = weight;
        for (int b = 0; b < N; ++b)
            for (int g = 0; g < N; ++g)
                for (int r = 0; r < N; ++r)
                {
                    const int i = idx(r, g, b);
                    if (weight[i] >= 1.0f)
                        continue;
                    Rgb sum;
                    float total = 0;
                    const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (const auto &o : d)
                    {
                        const int rr = r + o[0], gg = g + o[1], bb = b + o[2];
                        if (rr < 0 || gg < 0 || bb < 0 || rr >= N || gg >= N || bb >= N)
                            continue;
                        const int j = idx(rr, gg, bb);
                        sum.r += offset[j].r * weight[j];
                        sum.g += offset[j].g * weight[j];
                        sum.b += offset[j].b * weight[j];
                        total += weight[j];
                    }
                    if (total > 0)
                    {
                        next[i] = {sum.r / total, sum.g / total, sum.b / total};
                        next_weight[i] = std::min(0.99f, total / 6.0f * 0.9f);
                    }
                }
        offset.swap(next);
        weight.swap(next_weight);
    }

    // Ammorbidimento (2 passaggi di media 3x3x3)
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<Rgb> next(N * N * N);
        for (int b = 0; b < N; ++b)
            for (int g = 0; g < N; ++g)
                for (int r = 0; r < N; ++r)
                {
                    Rgb sum;
                    int count = 0;
                    for (int db = -1; db <= 1; ++db)
                        for (int dg = -1; dg <= 1; ++dg)
                            for (int dr = -1; dr <= 1; ++dr)
                            {
                                const int rr = r + dr, gg = g + dg, bb = b + db;
                                if (rr < 0 || gg < 0 || bb < 0 || rr >= N || gg >= N || bb >= N)
                                    continue;
                                const Rgb &o = offset[idx(rr, gg, bb)];
                                sum.r += o.r;
                                sum.g += o.g;
                                sum.b += o.b;
                                ++count;
                            }
                    next[idx(r, g, b)] = {sum.r / count, sum.g / count, sum.b / count};
                }
        offset.swap(next);
    }

    Lut lut = identity();
    for (int i = 0; i < N * N * N; ++i)
        lut[i] = {std::clamp(lut[i].r + offset[i].r, 0.f, 1.f), std::clamp(lut[i].g + offset[i].g, 0.f, 1.f), std::clamp(lut[i].b + offset[i].b, 0.f, 1.f)};
    return lut;
}

// Scene diverse: trasferimento statistico (media e deviazione per canale in uno spazio
// tipo Lab), cioe' tono, contrasto e dominante del riferimento applicati all'originale.
struct Stats
{
    double mean[3] = {0, 0, 0};
    double deviation[3] = {0, 0, 0};
};

std::array<double, 3> to_lab_like(Rgb c)
{
    // Spazio "l-alpha-beta" semplificato su valori logaritmici (Reinhard et al.)
    const double L = 0.3811 * c.r + 0.5783 * c.g + 0.0402 * c.b + 1e-4;
    const double M = 0.1967 * c.r + 0.7244 * c.g + 0.0782 * c.b + 1e-4;
    const double S = 0.0241 * c.r + 0.1288 * c.g + 0.8444 * c.b + 1e-4;
    const double l = std::log10(L), m = std::log10(M), s = std::log10(S);
    return {(l + m + s) / std::sqrt(3.0), (l + m - 2 * s) / std::sqrt(6.0), (l - m) / std::sqrt(2.0)};
}

Rgb from_lab_like(const std::array<double, 3> &v)
{
    const double l = v[0] / std::sqrt(3.0) + v[1] / std::sqrt(6.0) + v[2] / std::sqrt(2.0);
    const double m = v[0] / std::sqrt(3.0) + v[1] / std::sqrt(6.0) - v[2] / std::sqrt(2.0);
    const double s = v[0] / std::sqrt(3.0) - 2 * v[1] / std::sqrt(6.0);
    const double L = std::pow(10.0, l) - 1e-4, M = std::pow(10.0, m) - 1e-4, S = std::pow(10.0, s) - 1e-4;
    return {float(4.4679 * L - 3.5873 * M + 0.1193 * S), float(-1.2186 * L + 2.3809 * M - 0.1624 * S), float(0.0497 * L - 0.2439 * M + 1.2045 * S)};
}

Stats statistics(const Image &image)
{
    Stats st;
    const size_t step = std::max<size_t>(1, image.pixels.size() / 400000);
    size_t count = 0;
    for (size_t i = 0; i < image.pixels.size(); i += step, ++count)
    {
        const auto v = to_lab_like(image.pixels[i]);
        for (int k = 0; k < 3; ++k)
            st.mean[k] += v[k];
    }
    for (int k = 0; k < 3; ++k)
        st.mean[k] /= count;
    for (size_t i = 0; i < image.pixels.size(); i += step)
    {
        const auto v = to_lab_like(image.pixels[i]);
        for (int k = 0; k < 3; ++k)
            st.deviation[k] += (v[k] - st.mean[k]) * (v[k] - st.mean[k]);
    }
    for (int k = 0; k < 3; ++k)
        st.deviation[k] = std::sqrt(st.deviation[k] / count) + 1e-6;
    return st;
}

Lut match_statistical(const Image &source, const Image &target)
{
    const Stats a = statistics(source), b = statistics(target);
    Lut lut = identity();
    for (Rgb &c : lut)
    {
        auto v = to_lab_like(c);
        for (int k = 0; k < 3; ++k)
        {
            // Rapporto di contrasto limitato: evita trasformazioni estreme
            const double ratio = std::clamp(b.deviation[k] / a.deviation[k], 0.6, 1.6);
            v[k] = (v[k] - a.mean[k]) * ratio + b.mean[k];
        }
        const Rgb out = from_lab_like(v);
        c = {std::clamp(out.r, 0.f, 1.f), std::clamp(out.g, 0.f, 1.f), std::clamp(out.b, 0.f, 1.f)};
    }
    return lut;
}

Image downscale(const Image &image, int factor)
{
    Image out;
    out.width = image.width / factor;
    out.height = image.height / factor;
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
        {
            Rgb sum;
            for (int dy = 0; dy < factor; ++dy)
                for (int dx = 0; dx < factor; ++dx)
                {
                    const Rgb &p = image.pixels[static_cast<size_t>(y * factor + dy) * image.width + x * factor + dx];
                    sum.r += p.r;
                    sum.g += p.g;
                    sum.b += p.b;
                }
            const float n = float(factor * factor);
            out.pixels[static_cast<size_t>(y) * out.width + x] = {sum.r / n, sum.g / n, sum.b / n};
        }
    return out;
}

int fail(const char *message)
{
    std::fprintf(stderr, "Errore: %s\n", message);
    return 1;
}
}

int wmain(int argc, wchar_t **argv)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc < 3)
    {
        std::printf("Uso:\n  ktv_lut atlas <uscita.png> [cartella_luts]\n  ktv_lut match <originale.png> <riferimento.png> <uscita_lut.png>\n  ktv_lut apply <lut.png> <immagine.png> <uscita.png>\n");
        return 1;
    }
    const std::wstring mode = argv[1];

    if (mode == L"atlas")
    {
        const std::filesystem::path folder = argc >= 4 ? std::filesystem::path(argv[3]) : std::filesystem::path();
        Image atlas;
        atlas.width = N * N;
        atlas.height = N * kAtlasCount;
        atlas.pixels.resize(static_cast<size_t>(atlas.width) * atlas.height);
        for (int i = 0; i < kAtlasCount; ++i)
        {
            Lut lut = i < 6 ? build_preset(i) : identity();
            if (i >= 6 && !folder.empty())
            {
                const auto custom = folder / (L"personale_" + std::to_wstring(i - 5) + L".png");
                Image strip;
                if (std::filesystem::exists(custom) && load(custom.wstring(), strip) && strip_to_lut(strip, lut))
                    std::wprintf(L"Personale %d: %ls\n", i - 5, custom.c_str());
            }
            const Image strip = lut_to_strip(lut);
            std::copy(strip.pixels.begin(), strip.pixels.end(), atlas.pixels.begin() + static_cast<size_t>(i) * N * atlas.width);
        }
        if (!save(argv[2], atlas))
            return fail("impossibile scrivere l'atlante");
        std::wprintf(L"Atlante scritto: %ls\n", argv[2]);
        return 0;
    }

    if (mode == L"match" && argc >= 5)
    {
        // ktv_lut match <originale> <riferimento> [altri riferimenti...] <uscita>
        Image source, target;
        if (!load(argv[2], source))
            return fail("impossibile leggere lo screenshot originale");
        const int reference_count = argc - 4;
        for (int r = 0; r < reference_count; ++r)
        {
            Image reference;
            if (!load(argv[3 + r], reference))
            {
                std::fwprintf(stderr, L"Errore: impossibile leggere %ls (per i .webp serve l'estensione WebP di Windows)\n", argv[3 + r]);
                return 1;
            }
            if (r == 0)
                target = reference;
            else
            {
                // Piu' riferimenti: le statistiche si calcolano su tutti i pixel insieme
                target.pixels.insert(target.pixels.end(), reference.pixels.begin(), reference.pixels.end());
                target.width = 0;
            }
        }
        std::printf("Riferimenti: %d\n", reference_count);
        Lut lut;
        if (reference_count == 1 && source.width == target.width && source.height == target.height)
        {
            // Riduzione 4x: attenua piccoli disallineamenti e rumore tra i due screenshot
            const int factor = source.width >= 2048 ? 4 : 2;
            lut = match_paired(downscale(source, factor), downscale(target, factor));
            std::printf("Stessa scena: trasformazione ricostruita pixel per pixel.\n");
        }
        else
        {
            lut = match_statistical(source, target);
            std::printf("Scene diverse: trasferimento statistico del look.\n");
        }
        if (!save(argv[argc - 1], lut_to_strip(lut)))
            return fail("impossibile scrivere la LUT");
        std::wprintf(L"LUT scritta: %ls\n", argv[argc - 1]);
        return 0;
    }

    if (mode == L"levels" && argc >= 4)
    {
        // ktv_lut levels <lut> <uscita> [gamma] [contrasto]
        // Riporta nero e bianco della LUT a 0 e 1 (i riferimenti presi da video compressi
        // hanno i neri sollevati = patina grigia), poi gamma (>1 scurisce i medi) e curva a S.
        Image strip;
        Lut lut;
        if (!load(argv[2], strip) || !strip_to_lut(strip, lut))
            return fail("impossibile leggere la LUT");
        const float gamma = argc >= 5 ? static_cast<float>(_wtof(argv[4])) : 1.1f;
        const float amount = argc >= 6 ? static_cast<float>(_wtof(argv[5])) : 0.25f;
        const Rgb black = lut[idx(0, 0, 0)];
        const Rgb white = lut[idx(N - 1, N - 1, N - 1)];
        const auto level = [](float v, float lo, float hi) { return std::clamp((v - lo) / std::max(hi - lo, 0.05f), 0.0f, 1.0f); };
        for (Rgb &c : lut)
        {
            Rgb l = {level(c.r, black.r, white.r), level(c.g, black.g, white.g), level(c.b, black.b, white.b)};
            l = {std::pow(l.r, gamma), std::pow(l.g, gamma), std::pow(l.b, gamma)};
            c = contrast(l, amount);
        }
        if (!save(argv[3], lut_to_strip(lut)))
            return fail("impossibile scrivere la LUT");
        std::printf("Nero %.3f %.3f %.3f -> 0, bianco %.3f %.3f %.3f -> 1, gamma %.2f, contrasto %.2f\n",
                    black.r, black.g, black.b, white.r, white.g, white.b, gamma, amount);
        std::wprintf(L"LUT scritta: %ls\n", argv[3]);
        return 0;
    }

    if (mode == L"apply" && argc >= 5)
    {
        Image strip, image;
        Lut lut;
        if (!load(argv[2], strip) || !strip_to_lut(strip, lut) || !load(argv[3], image))
            return fail("impossibile leggere la LUT o l'immagine");
        for (Rgb &p : image.pixels)
            p = sample(lut, p);
        if (!save(argv[4], image))
            return fail("impossibile scrivere l'anteprima");
        std::wprintf(L"Anteprima scritta: %ls\n", argv[4]);
        return 0;
    }

    return fail("comando non riconosciuto");
}
