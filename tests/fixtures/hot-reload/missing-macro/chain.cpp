// Test fixture: compiles but has no VIVID_CHAIN macro
// Symbols won't be exported, so dlopen will find no entry points
#include <vivid/vivid.h>
using namespace vivid;

void setup(Context& ctx) {}
void update(Context& ctx) {}
// No VIVID_CHAIN macro
