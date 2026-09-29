#include "config.h"
#include "framework/logger/logger.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <string>

namespace
{

std::string Trim(const std::string& s)
{
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";

    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ToLower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

Config::Config() = default;

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

ConfigError Config::Load(const std::string& path)
{
    if (path.empty())
        return ConfigError::INVALID_ARGUMENT;

    std::ifstream file(path);
    if (!file.is_open())
        return ConfigError::FILE_NOT_FOUND;

    Logger& logger = Logger::GetInstance();

    entries_.clear();

    std::string current_section;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        const std::string trimmed = Trim(line);

        if (trimmed.empty())
            continue;

        if (trimmed.front() == '#' || trimmed.front() == ';')
            continue;

        if (trimmed.front() == '[')
        {
            if (trimmed.back() != ']')
            {
                logger.Warn("[Config] malformed section header at line " +
                            std::to_string(line_number) + ": '" + trimmed + "'");
                continue;
            }

            const std::string name = Trim(trimmed.substr(1, trimmed.size() - 2));

            if (name.empty())
            {
                logger.Warn("[Config] empty section name at line " +
                            std::to_string(line_number));
                continue;
            }

            if (name.find('[') != std::string::npos ||
                name.find(']') != std::string::npos)
            {
                logger.Warn("[Config] invalid character in section name at line " +
                            std::to_string(line_number) + ": '" + name + "'");
                continue;
            }

            current_section = name;
            continue;
        }

        const auto eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            logger.Warn("[Config] malformed line at line " +
                        std::to_string(line_number) + ": '" + trimmed + "'");
            continue;
        }

        const std::string key   = Trim(trimmed.substr(0, eq));
        const std::string value = Trim(trimmed.substr(eq + 1));

        if (key.empty())
        {
            logger.Warn("[Config] empty key at line " +
                        std::to_string(line_number));
            continue;
        }

        const std::string full_key = current_section.empty()
            ? key
            : current_section + "." + key;

        auto existing = entries_.find(full_key);
        if (existing != entries_.end())
        {
            logger.Warn("[Config] duplicate key '" + full_key +
                        "' at line " + std::to_string(line_number) +
                        " (overrides value from line " +
                        std::to_string(existing->second.line) + ")");
        }

        entries_[full_key] = Entry{value, line_number};
    }

    return ConfigError::SUCCESS;
}

void Config::Clear()
{
    entries_.clear();
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

std::size_t Config::Size() const
{
    return entries_.size();
}

bool Config::Has(const std::string& key) const
{
    return entries_.find(key) != entries_.end();
}

std::vector<std::string> Config::Keys() const
{
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& [k, e] : entries_)
        out.push_back(k);
    return out;
}

// ---------------------------------------------------------------------------
// Internal lookup
// ---------------------------------------------------------------------------

const Config::Entry* Config::Find(const std::string& key) const
{
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

bool Config::ParseInt(const std::string& raw, int& out)
{
    if (raw.empty())
        return false;

    int value = 0;
    const auto* begin = raw.data();
    const auto* end   = raw.data() + raw.size();

    const auto [ptr, ec] = std::from_chars(begin, end, value);

    if (ec != std::errc{} || ptr != end)
        return false;

    out = value;
    return true;
}

bool Config::ParseBool(const std::string& raw, bool& out)
{
    const std::string lower = ToLower(raw);

    if (lower == "true"  || lower == "yes" ||
        lower == "1"     || lower == "on")
    {
        out = true;
        return true;
    }

    if (lower == "false" || lower == "no" ||
        lower == "0"     || lower == "off")
    {
        out = false;
        return true;
    }

    return false;
}

bool Config::ParseDurationMs(const std::string& raw,
                             std::chrono::milliseconds& out)
{
    int value = 0;
    if (!ParseInt(raw, value))
        return false;

    if (value < 0)
        return false;

    out = std::chrono::milliseconds(value);
    return true;
}

// ---------------------------------------------------------------------------
// Get — optional keys (default on missing/invalid)
// ---------------------------------------------------------------------------

std::string Config::GetString(const std::string& key,
                              const std::string& default_value) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Debug("[Config] missing key '" + key +
                                    "', using default");
        return default_value;
    }

    return entry->value;
}

int Config::GetInt(const std::string& key, int default_value) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Debug("[Config] missing key '" + key +
                                    "', using default");
        return default_value;
    }

    int parsed = 0;
    if (!ParseInt(entry->value, parsed))
    {
        Logger::GetInstance().Warn("[Config] invalid int for '" + key +
                                   "' at line " + std::to_string(entry->line) +
                                   ": '" + entry->value + "'");
        return default_value;
    }

    return parsed;
}

bool Config::GetBool(const std::string& key, bool default_value) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Debug("[Config] missing key '" + key +
                                    "', using default");
        return default_value;
    }

    bool parsed = false;
    if (!ParseBool(entry->value, parsed))
    {
        Logger::GetInstance().Warn("[Config] invalid bool for '" + key +
                                   "' at line " + std::to_string(entry->line) +
                                   ": '" + entry->value + "'");
        return default_value;
    }

    return parsed;
}

std::chrono::milliseconds
Config::GetDurationMs(const std::string& key,
                      std::chrono::milliseconds default_value) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Debug("[Config] missing key '" + key +
                                    "', using default");
        return default_value;
    }

    std::chrono::milliseconds parsed{0};
    if (!ParseDurationMs(entry->value, parsed))
    {
        Logger::GetInstance().Warn("[Config] invalid duration for '" + key +
                                   "' at line " + std::to_string(entry->line) +
                                   ": '" + entry->value + "'");
        return default_value;
    }

    return parsed;
}

// ---------------------------------------------------------------------------
// Require — mandatory keys (false on missing/invalid)
// ---------------------------------------------------------------------------

bool Config::RequireString(const std::string& key, std::string& out) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' is missing");
        return false;
    }

    out = entry->value;
    return true;
}

bool Config::RequireInt(const std::string& key, int& out) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' is missing");
        return false;
    }

    int parsed = 0;
    if (!ParseInt(entry->value, parsed))
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' has invalid int at line " +
                                    std::to_string(entry->line) +
                                    ": '" + entry->value + "'");
        return false;
    }

    out = parsed;
    return true;
}

bool Config::RequireBool(const std::string& key, bool& out) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' is missing");
        return false;
    }

    bool parsed = false;
    if (!ParseBool(entry->value, parsed))
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' has invalid bool at line " +
                                    std::to_string(entry->line) +
                                    ": '" + entry->value + "'");
        return false;
    }

    out = parsed;
    return true;
}

bool Config::RequireDurationMs(const std::string& key,
                               std::chrono::milliseconds& out) const
{
    const Entry* entry = Find(key);

    if (entry == nullptr)
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' is missing");
        return false;
    }

    std::chrono::milliseconds parsed{0};
    if (!ParseDurationMs(entry->value, parsed))
    {
        Logger::GetInstance().Error("[Config] required key '" + key +
                                    "' has invalid duration at line " +
                                    std::to_string(entry->line) +
                                    ": '" + entry->value + "'");
        return false;
    }

    out = parsed;
    return true;
}