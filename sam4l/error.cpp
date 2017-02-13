#include "error.h"
#include "core.h"

namespace Error {

    Error errors[MAX_ERROR_NUMBER];
    int nErrors = 0;

    void (*handlers[N_SEVERITY])();

    void init() {
        for (int i = 0; i < N_SEVERITY; i++) {
            handlers[i] = nullptr;
        }
    }

    void setHandler(Severity severity, void (*handler)()) {
        handlers[static_cast<int>(severity)] = handler;
    }

    void happened(Module module, int userModule, Code code, Severity severity) {
        if (nErrors < MAX_ERROR_NUMBER) {
            Error& error = errors[nErrors];
            error.time = Core::time();
            error.module = module;
            error.userModule = userModule;
            error.code = code;
            error.severity = severity;
            nErrors++;
        }
        if (handlers[static_cast<int>(severity)] != nullptr) {
            handlers[static_cast<int>(severity)]();
        }
    }

    void happened(Module module, Code code, Severity severity) {
        happened(module, -1, code, severity);
    }

    void happened(int userModule, Code code, Severity severity) {
        happened(static_cast<Module>(-1), userModule, code, severity);
    }

    unsigned int getNumber() {
        return nErrors;
    }

    const Error& getLast() {
        return errors[nErrors - 1];
    }
}