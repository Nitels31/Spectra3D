// Spectra3D Script - public C API: the native functions
#ifndef SPS_PUBLIC_SPS_FUNCTION_H
#define SPS_PUBLIC_SPS_FUNCTION_H

#include "sps_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// A function of the host.
//
// The arguments of a method call start with the receiver: args[0] is the
// object the method was called on, args[1] are the arguments of the call.
typedef SpsValue (*SpsNativeFn)(SpsContext* context, int argument_count,
                                const SpsValue* arguments, void* user);

// Registers a global function. The scripts can call it by name.
void sps_register_function(SpsContext* context, const char* name,
                            SpsNativeFn function, void* user);

// Removes a registered function. Returns true when it existed.
bool sps_unregister_function(SpsContext* context, const char* name);

// Calls a function of the language.
// The result is stored in *result when it is not NULL.
bool sps_call(SpsContext* context, SpsValue function, int argument_count,
              const SpsValue* arguments, SpsValue* result);

#ifdef __cplusplus
}
#endif

#endif /* SPS_PUBLIC_SPS_FUNCTION_H */
