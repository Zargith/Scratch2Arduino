// Tests of the JavaScript <-> C++ bridge without a webview (audit #197): a fake window records the
// bound functions, calls them as the editor would and waits for their answer.
#include "bridge.hpp"
#include "bridge_helpers.hpp"

#include "s2a/version.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using s2a::webview::HostWindow;

/// The answer to a call: fulfilled (ok) or rejected, with its JSON text.
struct Answer {
    bool ok = false;
    std::string result;
};

class FakeWindow final : public HostWindow {
public:
    void bind(const std::string& name, Callback callback) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        bound_[name] = std::move(callback);
    }
    void resolve(const std::string& id, bool ok, const std::string& result) override {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            answers_[id] = Answer{ok, result};
        }
        answered_.notify_all();
    }
    void setTitle(const std::string& title) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        title_ = title;
    }
    void dispatch(std::function<void()> work) override { work(); }
    void terminate() override {}

    [[nodiscard]] std::set<std::string> names() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> result;
        for (const auto& entry : bound_) {
            result.insert(entry.first);
        }
        return result;
    }
    [[nodiscard]] std::string title() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return title_;
    }

    /// Calls `name` with the JSON array `arguments`, as the editor does, and waits for the answer.
    Answer call(const std::string& name, const Json& arguments) {
        Callback callback;
        std::string id;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto found = bound_.find(name);
            if (found == bound_.end()) {
                ADD_FAILURE() << name << " is not bound";
                return {};
            }
            callback = found->second;
            id = std::to_string(++nextId_);
        }
        callback(id, arguments.dump());
        std::unique_lock<std::mutex> lock(mutex_);
        if (!answered_.wait_for(lock, std::chrono::seconds(10), [&] { return answers_.count(id) != 0; })) {
            ADD_FAILURE() << name << " never answered";
            return {};
        }
        return answers_[id];
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable answered_;
    std::map<std::string, Callback> bound_;
    std::map<std::string, Answer> answers_;
    std::string title_;
    int nextId_ = 0;
};

/// Settings, recent projects and autosave in a folder of the test, never the user's.
class BridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Unique per process and test: ctest runs the tests side by side.
        userDir_ = fs::temp_directory_path() / ("s2a-bridge-test-" + std::to_string(std::random_device{}()) + "-" +
                                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(userDir_);
#ifdef _WIN32
        _putenv_s("S2A_USER_DIR", userDir_.u8string().c_str());
#else
        setenv("S2A_USER_DIR", userDir_.u8string().c_str(), 1); // NOLINT(concurrency-mt-unsafe): before the bridge's threads
#endif
        bridge_.emplace(window_, s2a::Language::English);
        bridge_->bind();
    }
    void TearDown() override {
        bridge_.reset(); // joins the worker threads before the folder goes
        std::error_code ignored;
        fs::remove_all(userDir_, ignored);
    }

    static Json blinkProject() {
        std::ifstream in(fs::path(S2A_EXAMPLES_DIR) / "blink" / "project.json");
        return Json::parse(in);
    }

    FakeWindow window_;
    std::optional<s2a::webview::Bridge> bridge_;
    fs::path userDir_;
};

/// Every s2a_... function that frontend/src/bridge.ts calls.
std::set<std::string> frontendFunctions() {
    std::ifstream in(S2A_FRONTEND_BRIDGE_TS);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::set<std::string> names;
    const std::regex pattern(R"(\b(s2a_[a-z0-9_]+)\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        names.insert((*it)[1].str());
    }
    return names;
}

// The contract between the editor and the program: a function the editor calls but the program
// does not offer would only fail on a user's computer.
TEST_F(BridgeTest, OffersEveryFunctionTheEditorCalls) {
    const std::set<std::string> called = frontendFunctions();
    ASSERT_GT(called.size(), 40U) << "frontend/src/bridge.ts not found or changed shape";
    const std::set<std::string> bound = window_.names();
    for (const std::string& name : called) {
        EXPECT_EQ(bound.count(name), 1U) << name << " is called by the editor but not bound";
    }
    for (const std::string& name : bound) {
        EXPECT_EQ(called.count(name), 1U) << name << " is bound but the editor never calls it";
    }
}

TEST_F(BridgeTest, SaysHelloWithItsVersion) {
    const Answer answer = window_.call("s2a_hello", Json::array());
    ASSERT_TRUE(answer.ok) << answer.result;
    const Json hello = Json::parse(answer.result);
    EXPECT_EQ(hello["version"], s2a::version::kString);
    EXPECT_EQ(hello["language"], "en");
    EXPECT_FALSE(hello["system"].get<std::string>().empty());
}

TEST_F(BridgeTest, SetsTheWindowTitle) {
    EXPECT_TRUE(window_.call("s2a_set_title", Json::array({"Robot - Scratch2Arduino"})).ok);
    EXPECT_EQ(window_.title(), "Robot - Scratch2Arduino");
}

TEST_F(BridgeTest, TreatsAMissingTextAsEmpty) {
    // A number where a text is expected: an empty title, not a crash.
    EXPECT_TRUE(window_.call("s2a_set_title", Json::array({42})).ok);
    EXPECT_EQ(window_.title(), "");
}

TEST_F(BridgeTest, GeneratesTheSketchOfAProject) {
    const Answer answer = window_.call("s2a_generate", Json::array({blinkProject()}));
    ASSERT_TRUE(answer.ok) << answer.result;
    const std::string sketch = Json::parse(answer.result).get<std::string>();
    EXPECT_NE(sketch.find("void setup()"), std::string::npos);
    EXPECT_NE(sketch.find("void loop()"), std::string::npos);
}

// A broken project from the editor is refused with a message, never by ending the program.
TEST_F(BridgeTest, RejectsABrokenProjectWithAMessage) {
    const Answer missing = window_.call("s2a_generate", Json::array());
    EXPECT_FALSE(missing.ok);
    EXPECT_TRUE(Json::parse(missing.result).is_string());

    const Answer broken = window_.call("s2a_generate", Json::array({Json{{"format", "something else"}}}));
    EXPECT_FALSE(broken.ok);
    EXPECT_FALSE(Json::parse(broken.result).get<std::string>().empty());
}

TEST_F(BridgeTest, KeepsTheSettingsItSaves) {
    const Answer saved = window_.call(
        "s2a_save_settings", Json::array({Json{{"language", "fr"}, {"checkUpdates", false}, {"theme", "dark"}}}));
    ASSERT_TRUE(saved.ok) << saved.result;
    const Json settings = Json::parse(window_.call("s2a_settings", Json::array()).result);
    EXPECT_EQ(settings["language"], "fr");
    EXPECT_EQ(settings["checkUpdates"], false);
    EXPECT_EQ(settings["theme"], "dark");
}

TEST_F(BridgeTest, LiveIsStoppedAtFirst) {
    const Answer answer = window_.call("s2a_live_state", Json::array());
    ASSERT_TRUE(answer.ok);
    EXPECT_EQ(Json::parse(answer.result)["state"], "stopped");
}

TEST_F(BridgeTest, OpensNoLinkOutsideTheProject) {
    const Answer answer = window_.call("s2a_open_url", Json::array({"https://example.com/"}));
    ASSERT_TRUE(answer.ok);
    EXPECT_EQ(Json::parse(answer.result), false);
}

TEST_F(BridgeTest, ConvertsCodeBetweenLanguages) {
    const Answer answer = window_.call("s2a_convert", Json::array({"digitalWrite(13, HIGH);", "cpp", "python"}));
    ASSERT_TRUE(answer.ok) << answer.result;
    const Json converted = Json::parse(answer.result);
    EXPECT_NE(converted["code"].get<std::string>(), "digitalWrite(13, HIGH);") << converted["code"];
}

TEST(BridgeHelpers, Base64RoundTrip) {
    for (const std::string& bytes : {std::string{}, std::string("a"), std::string("ab"), std::string("abc"),
                                     std::string("\x00\xff\x10 binary", 10)}) {
        EXPECT_EQ(s2a::webview::detail::base64Decode(s2a::webview::detail::base64Encode(bytes)), bytes);
    }
    EXPECT_EQ(s2a::webview::detail::base64Encode("Man"), "TWFu");
    EXPECT_EQ(s2a::webview::detail::base64Decode("data:image/png;base64,TWFu"), "Man");
}

TEST(BridgeHelpers, DownloadsOnlyTheProjectsInstallers) {
    using s2a::webview::detail::isReleaseInstaller;
    EXPECT_TRUE(isReleaseInstaller(
        "https://github.com/Zargith/Scratch2Arduino/releases/download/v1.3.3/Scratch2Arduino_installer.exe"));
    EXPECT_FALSE(isReleaseInstaller("https://github.com/Zargith/Scratch2Arduino/releases/download/v1.3.3/setup.dmg"));
    EXPECT_FALSE(isReleaseInstaller("https://evil.example/Scratch2Arduino/releases/download/v1/x.exe"));
    EXPECT_FALSE(isReleaseInstaller("https://github.com/Zargith/Scratch2Arduino/releases/download/../../x.exe"));
    EXPECT_FALSE(isReleaseInstaller("https://github.com/Zargith/Scratch2Arduino/releases/download/v1/a b.exe"));
}

TEST(BridgeHelpers, OpensOnlyThePagesOfTheProject) {
    using s2a::webview::detail::isProjectPage;
    EXPECT_TRUE(isProjectPage("https://github.com/Zargith/Scratch2Arduino/releases/tag/v1.3.3"));
    EXPECT_FALSE(isProjectPage("https://github.com/someone/else"));
    EXPECT_FALSE(isProjectPage("https://github.com/Zargith/Scratch2Arduino/\"; rm -rf ~"));
    EXPECT_FALSE(isProjectPage("https://github.com/Zargith/Scratch2Arduino/" + std::string(600, 'a')));
}

} // namespace
