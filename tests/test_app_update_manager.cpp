#include "runtime/app_update_manager.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main() {
    const std::string xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 0.1.1</title>
      <pubDate>Wed, 05 Mar 2026 18:00:00 +0000</pubDate>
      <sparkle:releaseNotesLink>https://example.com/notes/0.1.1</sparkle:releaseNotesLink>
      <enclosure url="https://example.com/Vivid-0.1.1.zip"
                 sparkle:version="0.1.1"
                 sparkle:shortVersionString="0.1.1"
                 length="123"
                 type="application/octet-stream"/>
    </item>
    <item>
      <title>Vivid 0.2.0</title>
      <pubDate>Thu, 06 Mar 2026 18:00:00 +0000</pubDate>
      <sparkle:releaseNotesLink>https://example.com/notes/0.2.0</sparkle:releaseNotesLink>
      <enclosure url="https://example.com/Vivid-0.2.0.zip"
                 sparkle:version="0.2.0"
                 sparkle:shortVersionString="0.2.0"
                 length="456"
                 type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";

    vivid::AppUpdateInfo info;
    std::string err;
    bool ok = vivid::AppUpdateManager::parse_appcast_for_test(xml, "0.1.0", info, err);
    assert(ok);
    assert(err.empty());
    assert(info.latest_version == "0.2.0");
    assert(info.update_available);
    assert(info.download_url == "https://example.com/Vivid-0.2.0.zip");

    vivid::AppUpdateInfo info2;
    std::string err2;
    ok = vivid::AppUpdateManager::parse_appcast_for_test(xml, "0.2.0", info2, err2);
    assert(ok);
    assert(!info2.update_available);

    vivid::AppUpdateInfo bad;
    std::string bad_err;
    ok = vivid::AppUpdateManager::parse_appcast_for_test("<rss/>", "0.1.0", bad, bad_err);
    assert(!ok);
    assert(!bad_err.empty());

    const auto tmp_dir = std::filesystem::temp_directory_path() / "vivid_test_app_update_manager";
    std::filesystem::create_directories(tmp_dir);
    const auto appcast_path = tmp_dir / "appcast.xml";
    {
        std::ofstream out(appcast_path);
        out << xml;
    }

    std::string old_url = vivid::AppUpdateManager::appcast_url();
    setenv("VIVID_APPCAST_URL", ("file://" + appcast_path.string()).c_str(), 1);
    setenv("VIVID_APP_UPDATE_TEST_DELAY_MS", "100", 1);
    vivid::AppUpdateManager::reset_worker_metrics_for_test();

    {
        vivid::AppUpdateManager mgr("0.1.0");
        mgr.refresh();
        mgr.refresh();
        mgr.refresh();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(vivid::AppUpdateManager::max_concurrent_workers_for_test() == 1);
    }
    assert(vivid::AppUpdateManager::active_workers_for_test() == 0);

    vivid::AppUpdateManager::reset_worker_metrics_for_test();
    {
        vivid::AppUpdateManager mgr("0.1.0");
        mgr.refresh();
    }
    assert(vivid::AppUpdateManager::active_workers_for_test() == 0);

    unsetenv("VIVID_APP_UPDATE_TEST_DELAY_MS");
    setenv("VIVID_APPCAST_URL", old_url.c_str(), 1);
    std::filesystem::remove_all(tmp_dir);

    std::cout << "[PASS] test_app_update_manager\n";
    return 0;
}
