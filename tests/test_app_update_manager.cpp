#include "runtime/app_update_manager.h"
#include <cassert>
#include <iostream>

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

    std::cout << "[PASS] test_app_update_manager\n";
    return 0;
}
