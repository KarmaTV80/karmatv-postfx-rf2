#include "rules/auto_rules.hpp"
#include "telemetry/rf2_telemetry.hpp"

#include <algorithm>
#include <cmath>

namespace ktv
{
namespace
{
constexpr std::array<RuleInfo, AutoRules::count> kRules = {{
    {"Cielo coperto", "mDarkCloud", false},
    {"Notte o crepuscolo", "ora del giorno (mStartET + mCurrentET)", true},
    {"Pit lane", "mInPits", false},
    {"Bandiera gialla o blu", "mYellowFlagState, mFlag", false},
}};

struct Link
{
    RuleId rule;
    const char *effect;
    float gain;  // multiplier = 1 + gain * peso
    bool force;  // accende l'effetto in proporzione al peso
};

// Collegamenti regola -> effetto (sezione "Automatismi da telemetria" della specifica).
constexpr Link kLinks[] = {
    {RuleId::Night, "bloom", 0.3f, false},
    {RuleId::Night, "star_glare", 0.0f, true},
};

constexpr float kTimeConstantSeconds = 2.5f;

// 1 di notte, 0 di giorno, con alba (5:30-7:00) e tramonto (18:30-20:00) graduali.
float night_weight(double seconds_since_midnight)
{
    const float hour = static_cast<float>(seconds_since_midnight / 3600.0);
    if (hour < 5.5f || hour >= 20.0f)
        return 1.0f;
    if (hour < 7.0f)
        return 1.0f - (hour - 5.5f) / 1.5f;
    if (hour >= 18.5f)
        return (hour - 18.5f) / 1.5f;
    return 0.0f;
}
}

AutoRules::AutoRules()
{
    enabled_.fill(true);
}

const RuleInfo &AutoRules::info(RuleId rule)
{
    return kRules[index(rule)];
}

void AutoRules::update(const TelemetrySnapshot &t, float dt_seconds, bool active)
{
    std::array<float, count> target{};
    if (active && t.connected && t.in_realtime)
    {
        target[index(RuleId::Overcast)] = static_cast<float>(t.dark_cloud);
        target[index(RuleId::Night)] = night_weight(t.time_of_day);
        target[index(RuleId::Pits)] = t.in_pits ? 1.0f : 0.0f;
        target[index(RuleId::Flags)] = (t.yellow_flag_state > 0 || t.player_flag == 6) ? 1.0f : 0.0f;
    }

    const float alpha = dt_seconds > 0.0f ? 1.0f - std::exp(-dt_seconds / kTimeConstantSeconds) : 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        const float goal = enabled_[i] ? std::clamp(target[i], 0.0f, 1.0f) : 0.0f;
        weights_[i] += (goal - weights_[i]) * alpha;
    }
}

EffectModifier AutoRules::modifier(std::string_view effect_id) const
{
    EffectModifier m;
    for (const Link &link : kLinks)
    {
        if (effect_id != link.effect)
            continue;
        const float w = weights_[index(link.rule)];
        if (link.force)
            m.force_weight = std::max(m.force_weight, w);
        else
            m.multiplier *= 1.0f + link.gain * w;
    }
    return m;
}
}
