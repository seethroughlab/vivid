#include "runtime/platform/app_update_manager.h"
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

    // --- Invalid XML ---
    {
        vivid::AppUpdateInfo inv;
        std::string inv_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test("<<<not xml>>>", "0.1.0", inv, inv_err);
        assert(!ok);
        assert(inv_err == "failed to parse appcast XML");
    }

    // --- Missing enclosure URL ---
    {
        const std::string no_url_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 0.3.0</title>
      <enclosure sparkle:version="0.3.0" length="100" type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo nu;
        std::string nu_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(no_url_xml, "0.1.0", nu, nu_err);
        assert(!ok);
        assert(nu_err == "no valid enclosure/version in appcast");
    }

    // --- Missing version ---
    {
        const std::string no_ver_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid Unknown</title>
      <enclosure url="https://example.com/Vivid.zip" length="100" type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo nv;
        std::string nv_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(no_ver_xml, "0.1.0", nv, nv_err);
        assert(!ok);
        assert(nv_err == "no valid enclosure/version in appcast");
    }

    // --- Attribute order change (url not first) ---
    {
        const std::string reorder_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 0.5.0</title>
      <enclosure sparkle:shortVersionString="0.5.0" sparkle:version="0.5.0"
                 length="789" type="application/octet-stream"
                 url="https://example.com/Vivid-0.5.0.zip"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo ro;
        std::string ro_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(reorder_xml, "0.1.0", ro, ro_err);
        assert(ok);
        assert(ro.latest_version == "0.5.0");
        assert(ro.download_url == "https://example.com/Vivid-0.5.0.zip");
        assert(ro.update_available);
    }

    // --- sparkle:shortVersionString preferred over sparkle:version ---
    {
        const std::string prefer_short_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 1.0.0</title>
      <enclosure url="https://example.com/Vivid-1.0.0.zip"
                 sparkle:version="100"
                 sparkle:shortVersionString="1.0.0"
                 length="100" type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo ps;
        std::string ps_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(prefer_short_xml, "0.1.0", ps, ps_err);
        assert(ok);
        assert(ps.latest_version == "1.0.0");
    }

    // --- sparkle:version fallback when shortVersionString absent ---
    {
        const std::string fallback_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 0.9.0</title>
      <enclosure url="https://example.com/Vivid-0.9.0.zip"
                 sparkle:version="0.9.0"
                 length="100" type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo fb;
        std::string fb_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(fallback_xml, "0.1.0", fb, fb_err);
        assert(ok);
        assert(fb.latest_version == "0.9.0");
    }

    // --- sparkle:minimumSystemVersion extraction ---
    {
        const std::string min_os_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 0.4.0</title>
      <enclosure url="https://example.com/Vivid-0.4.0.zip"
                 sparkle:shortVersionString="0.4.0"
                 sparkle:minimumSystemVersion="14.0"
                 length="100" type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo mo;
        std::string mo_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(min_os_xml, "0.1.0", mo, mo_err);
        assert(ok);
        assert(mo.minimum_system_version == "14.0");
    }

    // --- sparkle:releaseNotesLink extraction ---
    {
        vivid::AppUpdateInfo rn;
        std::string rn_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(xml, "0.1.0", rn, rn_err);
        assert(ok);
        assert(rn.release_notes_url == "https://example.com/notes/0.2.0");
        assert(rn.publication_date == "Thu, 06 Mar 2026 18:00:00 +0000");
        assert(rn.title == "Vivid 0.2.0");
    }

    // --- XML entity decoding in title ---
    {
        const std::string entity_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid &amp; Friends 0.6.0</title>
      <sparkle:releaseNotesLink>https://example.com/notes?a=1&amp;b=2</sparkle:releaseNotesLink>
      <enclosure url="https://example.com/Vivid-0.6.0.zip"
                 sparkle:shortVersionString="0.6.0"
                 length="100" type="application/octet-stream"/>
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo ent;
        std::string ent_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(entity_xml, "0.1.0", ent, ent_err);
        assert(ok);
        assert(ent.title == "Vivid & Friends 0.6.0");
        assert(ent.release_notes_url == "https://example.com/notes?a=1&b=2");
    }

    // --- Multiline enclosure attributes ---
    {
        const std::string multiline_xml = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <title>Vivid 0.7.0</title>
      <enclosure
          url="https://example.com/Vivid-0.7.0.zip"
          sparkle:shortVersionString="0.7.0"
          sparkle:version="0.7.0"
          length="100"
          type="application/octet-stream"
      />
    </item>
  </channel>
</rss>
)xml";
        vivid::AppUpdateInfo ml;
        std::string ml_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(multiline_xml, "0.1.0", ml, ml_err);
        assert(ok);
        assert(ml.latest_version == "0.7.0");
        assert(ml.download_url == "https://example.com/Vivid-0.7.0.zip");
    }

    // --- update_available false when current >= latest ---
    {
        vivid::AppUpdateInfo eq;
        std::string eq_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(xml, "0.2.0", eq, eq_err);
        assert(ok);
        assert(!eq.update_available);

        vivid::AppUpdateInfo newer;
        std::string newer_err;
        ok = vivid::AppUpdateManager::parse_appcast_for_test(xml, "0.3.0", newer, newer_err);
        assert(ok);
        assert(!newer.update_available);
    }

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
