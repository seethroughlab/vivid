#pragma once
// Shim: trunk registers custom-ref types via compile-time traits (type_id.h);
// classic's runtime ref-type descriptor registration is a no-op here.
#define VIVID_DESCRIBE_REF_TYPE(...)
#define VIVID_DESCRIBE_REF_TYPES(...)
