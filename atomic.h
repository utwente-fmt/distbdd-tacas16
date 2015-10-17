#ifndef ATOMIC_H
#define ATOMIC_H

#include <bupc_extensions.h>

#ifndef LOCAL_CAS
#define LOCAL_CAS(ptr, old, new) (__sync_bool_compare_and_swap((ptr),(old),(new)))
#endif

#ifndef CAS32
#define CAS32(ptr, old, new) (bupc_atomicU32_cswap_relaxed((ptr), (old), (new)))
#endif

#ifndef CAS64
#define CAS64(ptr, old, new) (bupc_atomicU64_cswap_relaxed((ptr), (old), (new)))
#endif

#endif