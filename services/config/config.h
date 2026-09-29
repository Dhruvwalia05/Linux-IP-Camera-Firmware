#ifndef CONFIG_H
#define CONFIG_H

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

// ===========================================================================
// Config
// ===========================================================================
//
// INI-style configuration loader for the firmware.
//
// Design summary:
//
//   * File format is INI: [section] headers, key = value lines,
//     '#' and ';' full-line comments, blank lines skipped. Values
//     are literal bytes (trimmed); no quotes, no escapes, no inline
//     comments. See the parser section below for exact rules.
//
//   * Hand-rolled parser, zero dependencies. ~200 lines. No external
//     library, no cJSON, no nlohmann. Consistent with every other
//     component in the project.
//
//   * Two API families for reads:
//        GetX(key, default)  — optional keys; caller supplies fallback
//        RequireX(key, out)  — required keys; false on missing/invalid
//     Missing optional keys are logged at DEBUG (invisible in
//     production). Invalid values (present but unparseable) and
//     missing required keys log at WARN/ERROR with line numbers so
//     the operator can fix the file.
//
//   * Sections are flattened into dotted keys: [network] + port
//     becomes "network.port". Nested sections work: [network.mqtt]
//     + broker becomes "network.mqtt.broker".
//
//   * Load() is single-shot and single-threaded. After Load() returns,
//     concurrent reads from multiple threads are safe (internal map is
//     read-only from that point). Load() itself is not thread-safe.
//
// Not supported (deliberately):
//   * Hot reload (deferred to Phase 6)
//   * Writing / saving (config is read-only)
//   * Schema files (validation is per-key in code)
//   * Remote config, environment variable overrides (Phase 4/6)
//   * Secrets masking in logs (Phase 4 will add this when secrets
//     management arrives)
//

enum class ConfigError
{
    SUCCESS,
    INVALID_ARGUMENT,   // empty path
    FILE_NOT_FOUND      // cannot open the file
};

class Config
{
public:
    Config();
    ~Config() = default;

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&)                 = delete;
    Config& operator=(Config&&)      = delete;

    // -- Lifecycle ------------------------------------------------------

    // Parse the file at `path`. On success (SUCCESS), the internal
    // map contains every key=value pair found in the file. Individual
    // malformed lines are logged at WARN and skipped; they do not
    // cause Load() to fail.
    //
    // Returns:
    //   SUCCESS            file opened and parsed (possibly with
    //                      per-line warnings)
    //   INVALID_ARGUMENT   path is empty
    //   FILE_NOT_FOUND     ::open() failed (permission, missing, ...)
    ConfigError Load(const std::string& path);

    // Discard all loaded entries. After Clear(), Size() == 0 and every
    // GetX(key, default) returns its default.
    void Clear();

    // -- Introspection --------------------------------------------------

    std::size_t Size() const;
    bool        Has(const std::string& key) const;

    // All keys, sorted lexicographically. Used for diagnostics and
    // tests. Never contains duplicates.
    std::vector<std::string> Keys() const;

    // -- Read with default (optional keys) ------------------------------
    //
    // If the key is missing, `default_value` is returned and a DEBUG
    // log is emitted. If the key is present but cannot be parsed as
    // the requested type, a WARN log with the line number is emitted
    // and `default_value` is returned. GetString has no parsing step,
    // so it never warns.

    std::string GetString(const std::string& key,
                          const std::string& default_value) const;

    int GetInt(const std::string& key,
               int default_value) const;

    bool GetBool(const std::string& key,
                 bool default_value) const;

    std::chrono::milliseconds
    GetDurationMs(const std::string& key,
                  std::chrono::milliseconds default_value) const;

    // -- Require (mandatory keys) ---------------------------------------
    //
    // If the key is missing or its value cannot be parsed as the
    // requested type, an ERROR log is emitted and the function returns
    // false. On success, `out` is assigned and the function returns
    // true. `out` is never modified on failure.

    bool RequireString(const std::string& key,
                       std::string& out) const;

    bool RequireInt(const std::string& key,
                    int& out) const;

    bool RequireBool(const std::string& key,
                     bool& out) const;

    bool RequireDurationMs(const std::string& key,
                           std::chrono::milliseconds& out) const;

private:
    struct Entry
    {
        std::string value;
        std::size_t line = 0;
    };

    const Entry* Find(const std::string& key) const;

    static bool ParseInt(const std::string& raw, int& out);
    static bool ParseBool(const std::string& raw, bool& out);
    static bool ParseDurationMs(const std::string& raw,
                                std::chrono::milliseconds& out);

    std::map<std::string, Entry> entries_;
};

#endif // CONFIG_H