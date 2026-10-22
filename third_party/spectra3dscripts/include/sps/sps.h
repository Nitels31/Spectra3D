// Spectra3D Script - the public C API
//
// A pure C API for embedding Spectra3D Script into a host application (a
// game engine for example). Everything the host needs is declared here.
//
//   #include "sps/sps.h"
//
//   SpsContext* context = sps_context_new();
//   sps_context_set_error_handler(context, on_error, NULL);
//   sps_register_function(context, "print", native_print, NULL);
//   sps_context_exec_file(context, "game/main.sps");
//
// The API is stable: the layout of the structures is private, the host works
// with the SpsValue handles.
#ifndef SPS_H
#define SPS_H

#include "sps_types.h"
#include "sps_value.h"
#include "sps_context.h"
#include "sps_function.h"
#include "sps_class.h"
#include "sps_compiler.h"

#define SPS_VERSION_MAJOR 0
#define SPS_VERSION_MINOR 1
#define SPS_VERSION_PATCH 0

// The version of the API the host was compiled against.
const char* sps_version(void);

#endif /* SPS_H */
