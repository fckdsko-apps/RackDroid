#pragma once
#include "base.h"
#include "vmio.h"

#ifdef __cplusplus
extern "C" {
#endif

void orca_run(Glyph *restrict gbuffer, Mark *restrict mbuffer, Usz height,
              Usz width, Usz tick_number, Oevent_list *oevent_list,
              Usz random_seed);

// Function to get random value from C++ code
Usz custom_vcvin(void* ptr, Usz port_num, Usz a, Usz b);
// Function to write a CV value to C++ code
void custom_vcvout(void* ptr, Usz port_num, Usz a, Usz b, Usz value);

#ifdef __cplusplus
}
#endif
