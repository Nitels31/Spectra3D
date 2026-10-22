// Spectra3D Script - public C API: the values
#ifndef SPS_PUBLIC_SPS_VALUE_H
#define SPS_PUBLIC_SPS_VALUE_H

#include "sps_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The constructors.
SpsValue sps_nil(void);
SpsValue sps_bool(bool value);
SpsValue sps_int(int64_t value);
SpsValue sps_float(double value);
SpsValue sps_string(const char* text);

// The accessors. They return a default value when the type does not match.
bool        sps_value_is_nil(SpsValue value);
bool        sps_value_to_bool(SpsValue value);
int64_t     sps_value_to_int(SpsValue value);
double      sps_value_to_float(SpsValue value);
const char* sps_value_to_string(SpsValue value);
int         sps_value_length(SpsValue value);
SpsType     sps_value_type(SpsValue value);
const char* sps_type_name(SpsType type);

// The text of a value, the same as str() in the language. The result is
// borrowed and valid until the next call on the same context.
const char* sps_value_to_text(SpsContext* context, SpsValue value);

// Makes an independent copy of a value (the strings are copied too).
SpsValue sps_value_copy(SpsContext* context, SpsValue value);
void     sps_value_free(SpsContext* context, SpsValue value);

// Creates a list of the given values.
SpsValue sps_value_list(SpsContext* context, int count, const SpsValue* values);

// Appends a value to a list.
void sps_value_list_append(SpsContext* context, SpsValue list, SpsValue value);

// The SPL format of Spectra3D: the same text that the C++ header
// (include/spl/spl.hpp) reads and writes.
SpsValue sps_spl_parse(SpsContext* context, const char* text);
SpsValue sps_spl_stringify(SpsContext* context, SpsValue value);
SpsValue sps_spl_pretty(SpsContext* context, SpsValue value);

// The equality of two values.
bool sps_value_equal(SpsValue left, SpsValue right);

#ifdef __cplusplus
}
#endif

#endif /* SPS_PUBLIC_SPS_VALUE_H */
