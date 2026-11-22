#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace ktv
{
struct TelemetrySnapshot;

enum class RuleId : int
{
    Overcast,
    Night,
    Pits,
    Flags,
    Count
};

struct RuleInfo
{
    const char *label;  // nome nel menu
    const char *source; // dato letto
    bool implemented;   // false = regola predisposta, effetto collegato in fase 2
};

// Come una regola modifica un effetto.
struct EffectModifier
{
    float multiplier = 1.0f;   // moltiplica l'intensita' del profilo
    float force_weight = 0.0f; // > 0: accende l'effetto anche se spento nel profilo, con questa intensita' relativa
};

class AutoRules
{
public:
    AutoRules();

    // Aggiorna i pesi con transizioni morbide (costante di tempo di circa 2,5 s).
    void update(const TelemetrySnapshot &telemetry, float dt_seconds, bool active);

    EffectModifier modifier(std::string_view effect_id) const;

    float weight(RuleId rule) const { return weights_[index(rule)]; }
    bool enabled(RuleId rule) const { return enabled_[index(rule)]; }
    void set_enabled(RuleId rule, bool on) { enabled_[index(rule)] = on; }

    static const RuleInfo &info(RuleId rule);
    static constexpr size_t count = static_cast<size_t>(RuleId::Count);

private:
    static constexpr size_t index(RuleId rule) { return static_cast<size_t>(rule); }

    std::array<float, count> weights_{};
    std::array<bool, count> enabled_{};
};
}
