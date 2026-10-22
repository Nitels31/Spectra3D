// Spectra3D Script - public C API: the context
#ifndef SPS_PUBLIC_SPS_CONTEXT_H
#define SPS_PUBLIC_SPS_CONTEXT_H

#include "sps_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called when a script raises an error that is not caught by try/catch.
// The message and the stack trace are borrowed.
typedef void (*SpsErrorHandler)(SpsContext* context, const char* message,
                                const char* stack_trace, void* user);

// Creates a new context. Returns NULL when the memory is exhausted.
SpsContext* sps_context_new(void);

// Destroys a context together with everything that belongs to it.
void sps_context_free(SpsContext* context);

// Installs the handler that receives the errors of the scripts.
void sps_context_set_error_handler(SpsContext* context, SpsErrorHandler handler, void* user);

// The last error of the context (an empty string when there was none).
const char* sps_context_last_error(SpsContext* context);

// Compiles and runs a source. Returns true on success.
bool sps_context_exec(SpsContext* context, const char* source, const char* chunk_name);

// Compiles a file and runs it. Returns true on success.
bool sps_context_exec_file(SpsContext* context, const char* path);

// Runs a chunk that was compiled before. Returns true on success.
bool sps_context_run(SpsContext* context, SpsChunk* chunk);

// The global variables of the context.
SpsValue sps_context_get_global(SpsContext* context, const char* name);
void     sps_context_set_global(SpsContext* context, const char* name, SpsValue value);
bool     sps_context_has_global(SpsContext* context, const char* name);

// Removes a global variable. Returns true when it existed.
bool sps_context_clear_global(SpsContext* context, const char* name);

// The number of the global variables.
int sps_context_global_count(SpsContext* context);

// The name of the global variable by its index.
const char* sps_context_global_name(SpsContext* context, int index);

#ifdef __cplusplus
}
#endif

#endif /* SPS_PUBLIC_SPS_CONTEXT_H */
