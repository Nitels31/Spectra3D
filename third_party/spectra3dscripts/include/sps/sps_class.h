// Spectra3D Script - public C API: the host classes
#ifndef SPS_PUBLIC_SPS_CLASS_H
#define SPS_PUBLIC_SPS_CLASS_H

#include "sps_types.h"
#include "sps_function.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called when a host object is released.
typedef void (*SpsUserdataFree)(void* data);

// The getter of a property: it receives the object and returns the value.
typedef SpsValue (*SpsPropertyGetter)(SpsContext* context, SpsValue object, void* user);

// The setter of a property: it receives the object and the new value.
typedef void (*SpsPropertySetter)(SpsContext* context, SpsValue object, SpsValue value, void* user);

// Creates a class of the host. The name is the one the scripts use.
SpsHostClass* sps_host_class_new(SpsContext* context, const char* name);

// Adds a method to the class of the host.
void sps_host_class_method(SpsHostClass* klass, const char* name,
                           SpsNativeFn function, void* user);

// Sets the function that is called when the class is used as a constructor:
// Entity() creates a new object of the class.
void sps_host_class_constructor(SpsHostClass* klass, SpsNativeFn function, void* user);

// Sets the function that releases the data of the objects.
void sps_host_class_set_free(SpsHostClass* klass, SpsUserdataFree function);

// Adds a property to the class of the host. The scripts read and write it
// with the dot: entity.position = vec3(0, 1, 0);
// The setter may be NULL, then the property is read only.
void sps_host_class_property(SpsHostClass* klass, const char* name,
                              SpsPropertyGetter getter, SpsPropertySetter setter, void* user);

// Creates an object of the class of the host. The data is a pointer to the
// object of the engine.
SpsValue sps_host_class_new_object(SpsHostClass* klass, void* data);

// Wraps an existing object of the engine into a value of the language.
// The object is not owned by the script.
SpsValue sps_host_object(SpsHostClass* klass, void* data);

// The data of a host object. Returns NULL when the value is not one.
void* sps_value_to_userdata(SpsValue value);

// The class of a host object. Returns NULL when the value is not one.
SpsHostClass* sps_value_to_host_class(SpsValue value);

#ifdef __cplusplus
}
#endif

#endif /* SPS_PUBLIC_SPS_CLASS_H */
