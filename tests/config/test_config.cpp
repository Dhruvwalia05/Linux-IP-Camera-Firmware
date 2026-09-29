#include "services/config/config.h"
#include "framework/logger/logger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int tests_run    = 0;
int tests_passed = 0;

void Check(bool condition, const char* name)
{
    ++tests_run;
    if (condition)
    {
        ++tests_passed;
        std::fprintf(stderr, "[PASS] %s\n", name);
    }
    else
    {
        std::fprintf(stderr, "[FAIL] %s\n", name);
    }
}

std::string MakeTempFile(const std::string& content)
{
    static std::atomic<int> counter{0};
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/config_test_%d_%d.ini",
                  static_cast<int>(::getpid()), counter.fetch_add(1));

    std::ofstream f(path);
    f << content;
    f.close();

    return std::string(path);
}

// ================= Load basics =================

void TestLoadMissingFile()
{
    Config cfg;
    const auto r = cfg.Load("/tmp/config_definitely_missing_xyz.ini");
    Check(r == ConfigError::FILE_NOT_FOUND,
          "load missing file returns FILE_NOT_FOUND");
}

void TestLoadEmptyPath()
{
    Config cfg;
    const auto r = cfg.Load("");
    Check(r == ConfigError::INVALID_ARGUMENT,
          "load empty path returns INVALID_ARGUMENT");
}

void TestLoadEmptyFile()
{
    const auto path = MakeTempFile("");
    Config cfg;

    Check(cfg.Load(path) == ConfigError::SUCCESS,
          "load empty file succeeds");
    Check(cfg.Size() == 0,
          "empty file results in zero entries");

    ::unlink(path.c_str());
}

void TestLoadSimpleKeyValue()
{
    const auto path = MakeTempFile("port = 8080\n");
    Config cfg;
    cfg.Load(path);

    Check(cfg.Size() == 1,                "one entry loaded");
    Check(cfg.Has("port"),                "key present without section");
    Check(cfg.GetInt("port", 0) == 8080,  "int value parsed");

    ::unlink(path.c_str());
}

void TestLoadWithSection()
{
    const auto path = MakeTempFile(
        "[network]\n"
        "port = 8080\n"
        "bind = 0.0.0.0\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Has("network.port"), "section-prefixed key present");
    Check(cfg.Has("network.bind"), "second section key present");
    Check(cfg.GetString("network.bind", "") == "0.0.0.0",
          "section key value correct");

    ::unlink(path.c_str());
}

void TestLoadNestedSection()
{
    const auto path = MakeTempFile(
        "[network.mqtt]\n"
        "broker = tls://mqtt.example.com:8883\n"
        "client_id = camera-001\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Has("network.mqtt.broker"),    "nested section key present");
    Check(cfg.GetString("network.mqtt.broker", "") ==
          "tls://mqtt.example.com:8883",     "nested value correct");
    Check(cfg.GetString("network.mqtt.client_id", "") == "camera-001",
          "second nested value correct");

    ::unlink(path.c_str());
}

// ================= Comments and blank lines =================

void TestComments()
{
    const auto path = MakeTempFile(
        "# comment with hash\n"
        "; comment with semicolon\n"
        "   # indented comment\n"
        "\n"
        "port = 8080\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Size() == 1,               "comments and blank lines skipped");
    Check(cfg.GetInt("port", 0) == 8080, "value after comments parsed");

    ::unlink(path.c_str());
}

void TestValueWithHash()
{
    const auto path = MakeTempFile("password = abc#123\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.GetString("password", "") == "abc#123",
          "hash inside value is literal (no inline comments)");

    ::unlink(path.c_str());
}

// ================= Whitespace =================

void TestWhitespaceVariants()
{
    const auto path = MakeTempFile(
        "   key1    =    value1   \n"
        "key2=value2\n"
        "\tkey3\t=\tvalue3\t\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.GetString("key1", "") == "value1", "spaces trimmed");
    Check(cfg.GetString("key2", "") == "value2", "no-space form parsed");
    Check(cfg.GetString("key3", "") == "value3", "tab form parsed");

    ::unlink(path.c_str());
}

// ================= GetX: missing keys =================

void TestGetMissingReturnsDefault()
{
    Config cfg;

    Check(cfg.GetString("nope", "fallback") == "fallback",
          "missing string returns default");
    Check(cfg.GetInt("nope", 42) == 42,
          "missing int returns default");
    Check(cfg.GetBool("nope", true) == true,
          "missing bool returns default");
    Check(cfg.GetDurationMs("nope",
                            std::chrono::milliseconds(1234)) ==
          std::chrono::milliseconds(1234),
          "missing duration returns default");
}

// ================= GetX: invalid values =================

void TestGetInvalidReturnsDefault()
{
    const auto path = MakeTempFile(
        "bad_int = abc\n"
        "bad_bool = maybe\n"
        "negative_ms = -5\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.GetInt("bad_int", 99) == 99,
          "invalid int returns default");
    Check(cfg.GetBool("bad_bool", false) == false,
          "invalid bool returns default");
    Check(cfg.GetDurationMs("negative_ms",
                            std::chrono::milliseconds(1)) ==
          std::chrono::milliseconds(1),
          "negative duration rejected, default returned");

    ::unlink(path.c_str());
}

// ================= Bool parsing =================

void TestBoolVariants()
{
    const auto path = MakeTempFile(
        "a = true\n"
        "b = TRUE\n"
        "c = yes\n"
        "d = 1\n"
        "e = on\n"
        "f = false\n"
        "g = No\n"
        "h = 0\n"
        "i = OFF\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.GetBool("a", false) == true,  "true parsed");
    Check(cfg.GetBool("b", false) == true,  "TRUE parsed case-insensitively");
    Check(cfg.GetBool("c", false) == true,  "yes parsed");
    Check(cfg.GetBool("d", false) == true,  "1 parsed as true");
    Check(cfg.GetBool("e", false) == true,  "on parsed as true");
    Check(cfg.GetBool("f", true)  == false, "false parsed");
    Check(cfg.GetBool("g", true)  == false, "No parsed case-insensitively");
    Check(cfg.GetBool("h", true)  == false, "0 parsed as false");
    Check(cfg.GetBool("i", true)  == false, "OFF parsed");

    ::unlink(path.c_str());
}

// ================= Duration =================

void TestDuration()
{
    const auto path = MakeTempFile(
        "short = 100\n"
        "long  = 60000\n"
        "zero  = 0\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.GetDurationMs("short", std::chrono::milliseconds(0)) ==
          std::chrono::milliseconds(100),   "short duration");
    Check(cfg.GetDurationMs("long", std::chrono::milliseconds(0)) ==
          std::chrono::milliseconds(60000), "long duration");
    Check(cfg.GetDurationMs("zero", std::chrono::milliseconds(999)) ==
          std::chrono::milliseconds(0),     "zero duration valid");

    ::unlink(path.c_str());
}

// ================= RequireX =================

void TestRequireStringSuccess()
{
    const auto path = MakeTempFile("name = camera-001\n");
    Config cfg;
    cfg.Load(path);

    std::string out;
    Check(cfg.RequireString("name", out),
          "RequireString succeeds for present key");
    Check(out == "camera-001", "out was assigned");

    ::unlink(path.c_str());
}

void TestRequireStringMissing()
{
    Config cfg;

    std::string out = "untouched";
    Check(!cfg.RequireString("nope", out),
          "RequireString fails for missing key");
    Check(out == "untouched",
          "out not modified on failure");
}

void TestRequireIntSuccess()
{
    const auto path = MakeTempFile("port = 8080\n");
    Config cfg;
    cfg.Load(path);

    int out = 0;
    Check(cfg.RequireInt("port", out),
          "RequireInt succeeds");
    Check(out == 8080, "int assigned correctly");

    ::unlink(path.c_str());
}

void TestRequireIntInvalid()
{
    const auto path = MakeTempFile("port = abc\n");
    Config cfg;
    cfg.Load(path);

    int out = 77;
    Check(!cfg.RequireInt("port", out),
          "RequireInt fails on invalid value");
    Check(out == 77, "out not modified on invalid");

    ::unlink(path.c_str());
}

void TestRequireIntMissing()
{
    Config cfg;

    int out = 55;
    Check(!cfg.RequireInt("nope", out),
          "RequireInt fails on missing key");
    Check(out == 55, "out not modified on missing");
}

void TestRequireBool()
{
    const auto path = MakeTempFile("verbose = yes\n");
    Config cfg;
    cfg.Load(path);

    bool out = false;
    Check(cfg.RequireBool("verbose", out),
          "RequireBool succeeds");
    Check(out == true, "bool assigned correctly");

    ::unlink(path.c_str());
}

void TestRequireDuration()
{
    const auto path = MakeTempFile("timeout = 5000\n");
    Config cfg;
    cfg.Load(path);

    std::chrono::milliseconds out{0};
    Check(cfg.RequireDurationMs("timeout", out),
          "RequireDurationMs succeeds");
    Check(out == std::chrono::milliseconds(5000),
          "duration assigned correctly");

    ::unlink(path.c_str());
}

// ================= Malformed lines =================

void TestMalformedLinesSkipped()
{
    const auto path = MakeTempFile(
        "good1 = ok\n"
        "this line has no equals sign\n"
        "good2 = also ok\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Size() == 2,            "malformed line skipped");
    Check(cfg.Has("good1"),           "first good key present");
    Check(cfg.Has("good2"),           "second good key present");

    ::unlink(path.c_str());
}

void TestMalformedSectionSkipped()
{
    const auto path = MakeTempFile(
        "[unterminated\n"
        "port = 8080\n");

    Config cfg;
    cfg.Load(path);

    // The malformed section header is skipped, and the following
    // key is stored at top level (no section prefix).
    Check(cfg.Has("port"), "key after malformed section uses no prefix");

    ::unlink(path.c_str());
}

void TestEmptySectionName()
{
    const auto path = MakeTempFile(
        "[]\n"
        "port = 8080\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Has("port"), "empty section name rejected, key top-level");

    ::unlink(path.c_str());
}

// ================= Duplicate keys =================

void TestDuplicateKeyLastWins()
{
    const auto path = MakeTempFile(
        "port = 8080\n"
        "port = 9090\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Size() == 1,               "duplicate key counts as one");
    Check(cfg.GetInt("port", 0) == 9090, "last value wins");

    ::unlink(path.c_str());
}

// ================= Introspection =================

void TestSizeAndKeys()
{
    const auto path = MakeTempFile(
        "[a]\n"
        "x = 1\n"
        "y = 2\n"
        "[b]\n"
        "x = 3\n");

    Config cfg;
    cfg.Load(path);

    Check(cfg.Size() == 3, "three unique keys");

    const auto keys = cfg.Keys();
    Check(keys.size() == 3, "Keys returns three entries");
    // std::map iterates in sorted order
    Check(keys[0] == "a.x" && keys[1] == "a.y" && keys[2] == "b.x",
          "Keys sorted lexicographically");

    ::unlink(path.c_str());
}

void TestClear()
{
    const auto path = MakeTempFile("port = 8080\n");
    Config cfg;
    cfg.Load(path);

    Check(cfg.Size() == 1, "loaded");
    cfg.Clear();
    Check(cfg.Size() == 0, "cleared");
    Check(!cfg.Has("port"), "keys removed after Clear");
    Check(cfg.GetInt("port", 999) == 999,
          "Get returns default after Clear");

    ::unlink(path.c_str());
}

void TestReloadReplacesContents()
{
    const auto path1 = MakeTempFile("a = 1\nb = 2\n");
    const auto path2 = MakeTempFile("c = 3\n");

    Config cfg;
    cfg.Load(path1);
    Check(cfg.Size() == 2, "first file loaded");

    cfg.Load(path2);
    Check(cfg.Size() == 1, "second Load replaces contents");
    Check(cfg.Has("c"), "new key present");
    Check(!cfg.Has("a"), "old key gone");

    ::unlink(path1.c_str());
    ::unlink(path2.c_str());
}

// ================= Full example =================

void TestRealisticConfig()
{
    const auto path = MakeTempFile(
        "# Camera firmware configuration\n"
        "\n"
        "[network]\n"
        "bind_address = 0.0.0.0\n"
        "port = 8080\n"
        "timeout_ms = 5000\n"
        "\n"
        "[network.mqtt]\n"
        "broker = tls://mqtt.example.com:8883\n"
        "client_id = camera-001\n"
        "keepalive = 60\n"
        "\n"
        "[camera]\n"
        "device = /dev/video0\n"
        "width = 1920\n"
        "height = 1080\n"
        "fps = 30\n"
        "h264 = yes\n");

    Config cfg;
    Check(cfg.Load(path) == ConfigError::SUCCESS,
          "realistic config loads");

    std::string bind;
    int port = 0;
    int timeout = 0;
    std::string broker;

    Check(cfg.RequireString("network.bind_address", bind) &&
          bind == "0.0.0.0", "bind_address required, correct");
    Check(cfg.RequireInt("network.port", port) && port == 8080,
          "port required, correct");
    Check(cfg.RequireInt("network.timeout_ms", timeout) && timeout == 5000,
          "timeout required, correct");
    Check(cfg.RequireString("network.mqtt.broker", broker) &&
          broker == "tls://mqtt.example.com:8883",
          "broker required, correct");

    Check(cfg.GetInt("camera.width", 0) == 1920,      "camera width");
    Check(cfg.GetInt("camera.height", 0) == 1080,     "camera height");
    Check(cfg.GetBool("camera.h264", false) == true,  "camera h264 flag");

        Check(cfg.Size() == 11, "eleven unique keys loaded");

    ::unlink(path.c_str());
}

} // namespace

int main()
{
    std::fprintf(stderr, "=== Config Test Suite ===\n\n");

    Logger::GetInstance().Initialize();

    TestLoadMissingFile();
    TestLoadEmptyPath();
    TestLoadEmptyFile();
    TestLoadSimpleKeyValue();
    TestLoadWithSection();
    TestLoadNestedSection();

    TestComments();
    TestValueWithHash();

    TestWhitespaceVariants();

    TestGetMissingReturnsDefault();
    TestGetInvalidReturnsDefault();
    TestBoolVariants();
    TestDuration();

    TestRequireStringSuccess();
    TestRequireStringMissing();
    TestRequireIntSuccess();
    TestRequireIntInvalid();
    TestRequireIntMissing();
    TestRequireBool();
    TestRequireDuration();

    TestMalformedLinesSkipped();
    TestMalformedSectionSkipped();
    TestEmptySectionName();

    TestDuplicateKeyLastWins();

    TestSizeAndKeys();
    TestClear();
    TestReloadReplacesContents();

    TestRealisticConfig();

    Logger::GetInstance().Shutdown();

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL CONFIG TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "CONFIG TESTS FAILED\n");
    return 1;
}