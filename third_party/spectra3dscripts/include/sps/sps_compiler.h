// Spectra3D Script - public C API: the compiler
#ifndef SPS_PUBLIC_SPS_COMPILER_H
#define SPS_PUBLIC_SPS_COMPILER_H

#include "sps_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The diagnostics of the compiler.
typedef struct {
    int         line;
    int         column;
    char        message[512];
    char        file[512];
} SpsDiagnostic;

// Compiles a source into a chunk. Returns NULL when the source has an error,
// then the diagnostics of the context can be read with
// sps_context_last_error() and sps_compiler_diagnostics().
SpsChunk* sps_compile(SpsContext* context, const char* source, const char* chunk_name);

// Compiles a file into a chunk.
SpsChunk* sps_compile_file(SpsContext* context, const char* path);

// Releases a chunk.
void sps_chunk_free(SpsContext* context, SpsChunk* chunk);

// The diagnostics of the last compilation. The array is owned by the context.
const SpsDiagnostic* sps_compiler_diagnostics(SpsContext* context, int* count);

// Writes the bytecode of a chunk into a file.
bool sps_chunk_write(SpsContext* context, SpsChunk* chunk, const char* path);

#ifdef __cplusplus
}
#endif

#endif /* SPS_PUBLIC_SPS_COMPILER_H */
