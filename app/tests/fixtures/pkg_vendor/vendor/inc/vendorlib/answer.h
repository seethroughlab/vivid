// A trivial "vendored" header for the ADR-0054 Stage-1 test. It lives under
// vendor/inc/vendorlib/ so it is reachable ONLY via the vendor include dir
// (-I <pkg>/vendor/inc), NOT via the package-dir -I — which is exactly what proves
// dependencies.vendor put the flag on the compile line.
#pragma once
namespace vendorlib { constexpr int kVendorAnswer = 42; }
