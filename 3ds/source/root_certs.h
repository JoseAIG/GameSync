#ifndef ROOT_CERTS_H
#define ROOT_CERTS_H

#include "common.h"

// A DER-encoded root CA certificate built into the app
typedef struct {
    const char *name;
    const u8 *der;
    u32 size;
} RootCert;

extern const RootCert root_certs[];
extern const int root_cert_count;

#endif // ROOT_CERTS_H
