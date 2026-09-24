#pragma once
// Shim: the trunk registers custom-ref types via compile-time traits (type_id.h);
// classic's runtime ref-type descriptor registration (single- and multi-type variants)
// is a no-op here. Variadic so any arg count is swallowed.
#define VIVID_DESCRIBE_REF_TYPE(...)
#define VIVID_DESCRIBE_REF_TYPES(...)
#define VIVID_DESCRIBE_REF_TYPES2(...)
#define VIVID_DESCRIBE_REF_TYPES3(...)
#define VIVID_DESCRIBE_REF_TYPES4(...)
