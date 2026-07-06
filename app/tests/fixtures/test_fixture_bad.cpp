// Headless test fixture: a malformed "operator" exporting only vivid_abi_version
// (no descriptor/create/destroy/process_*). The loader must reject it with
// "missing_required_symbols".
#include "operator_api/types.h"

extern "C" uint32_t vivid_abi_version() { return VIVID_OPERATOR_ABI_VERSION; }
