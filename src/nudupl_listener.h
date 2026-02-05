#ifndef CHIAVDF_NUDUPL_LISTENER_H
#define CHIAVDF_NUDUPL_LISTENER_H

#include "include.h"

// Notification types for `INUDUPLListener::OnIteration`.
//
// NL_SQUARESTATE: payload is `square_state_type*` (x86/x64 phased pipeline only).
// NL_FORM: payload is `vdf_original::form*` (used by both the original slow loop and the ARM NUDUPL loop via a view).
#define NL_SQUARESTATE 1
#define NL_FORM 2

class INUDUPLListener {
public:
    virtual ~INUDUPLListener() = default;
    virtual void OnIteration(int type, void* data, uint64 iteration) = 0;
};

#endif // CHIAVDF_NUDUPL_LISTENER_H
