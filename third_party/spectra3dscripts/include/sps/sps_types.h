// Spectra3D Script - public C API: the basic types
#ifndef SPS_PUBLIC_SPS_TYPES_H
#define SPS_PUBLIC_SPS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// An independent instance of the virtual machine. Every context has its own
// global variables, so several scripts can run side by side.
typedef struct SpsContext SpsContext;

// The type of a value.
typedef enum {
    SPS_NIL = 0,
    SPS_BOOL,
    SPS_INT,
    SPS_FLOAT,
    SPS_STRING,
    SPS_LIST,
    SPS_DICT,
    SPS_OBJECT,   // an instance of a class of the language
    SPS_CLASS,    // a class of the language
    SPS_USERDATA, // an object of the host (the engine)
    SPS_FUNCTION
} SpsType;

// A value of the language.
//
// The string and the pointer payloads are borrowed: they stay valid until
// the next call of the API on the same context. Use sps_value_copy() when a
// value has to outlive the call.
typedef struct {
    SpsType type;
    union {
        bool    boolean;
        int64_t integer;
        double  number;
        const char* string;
        void*   pointer;
    } as;
} SpsValue;

// A class of the host: the engine can expose its own objects to the scripts.
typedef struct SpsHostClass SpsHostClass;

// A compiled script.
typedef struct SpsChunk SpsChunk;

#ifdef __cplusplus
}
#endif

#endif /* SPS_PUBLIC_SPS_TYPES_H */
