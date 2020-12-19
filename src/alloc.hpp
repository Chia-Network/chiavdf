#ifndef ALLOC_H
#define ALLOC_H

#include <stdlib.h> // for posix_memalign

inline void* mp_alloc_func(size_t new_bytes)
{
    new_bytes = ((new_bytes + 8) + 15) & ~15;
#if defined _MSC_VER
    uint8_t* ret = static_cast<uint8_t*>(_aligned_malloc(new_bytes, 16));
#else
    void* ptr = nullptr;
    if (::posix_memalign(&ptr, 16, new_bytes) != 0) return nullptr;
    uint8_t* ret = static_cast<uint8_t*>(ptr);
#endif
    return ret + 8;
}

inline void mp_free_func(void* old_ptr, size_t) {
    // if the old_ptr alignment is not to 16 bytes + 8 bytes offset, we did not
    // allocate it. It's an in-place buffer and should not be freed
    if ((std::uintptr_t(old_ptr) & 15) == 8) {
#if defined _MSC_VER
        _aligned_free(static_cast<uint8_t*>(old_ptr) - 8);
#else
        std::free(static_cast<uint8_t*>(old_ptr) - 8);
#endif
    }
    else if ((std::uintptr_t(old_ptr) & 63) != 0) {
        // this is a bit mysterious. Our allocator only allocates buffers
        // aligned to 16 bytes + 8 (i.e. the address always ends with 8)
        // The only other kind of buffer there should be, are the in-place
        // buffers that are members of the mpz class. Those are specifically
        // made to be 64 byte aligned (the assumed cache line size). If we're
        // asked to free such buffer, we just ignore it, since it wasn't
        // heap allocated. however, this isn't aligned to 64 bytes, so it may
        // be a default allocated buffer. It's not supposed to happen, but if it
        // does, we better free it
        std::free(old_ptr);
    }
}

inline void* mp_realloc_func(void* old_ptr, size_t old_size, size_t new_bytes) {

    void* ret = mp_alloc_func(new_bytes);
    ::memcpy(ret, old_ptr, std::min(old_size, new_bytes));
    mp_free_func(old_ptr, old_size);
    return ret;
}

//must call this before calling any gmp functions
//(the mpz class constructor does not call any gmp functions)
inline void init_gmp() {
    mp_set_memory_functions(mp_alloc_func, mp_realloc_func, mp_free_func);
    allow_integer_constructor=true; //make sure the old gmp allocator isn't used
}

#endif
