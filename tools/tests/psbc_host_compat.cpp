// Host adapter only: the archive was compiled against libc++ on Orbis.
// Missing BLAKE3 SIMD entrypoints are linked to the archive's portable versions.
// Optional unavailable paths abort; shader compiler/output are never substituted.
#include <cstdlib>
#include <cstdio>
#include <cstddef>
namespace std { namespace __1 {
size_t __next_prime(size_t n) {
 if(n<2)return 2;for(;;++n){bool prime=true;for(size_t d=2;d<=n/d;++d)if(n%d==0){prime=false;break;}if(prime)return n;}
}
template<bool> class __basic_string_common {public:[[noreturn]] void __throw_length_error()const;};
template<bool B> void __basic_string_common<B>::__throw_length_error()const{std::abort();}
template class __basic_string_common<true>;
template<bool> class __vector_base_common {public:[[noreturn]] void __throw_length_error()const;};
template<bool B> void __vector_base_common<B>::__throw_length_error()const{std::abort();}
template class __vector_base_common<true>;
}}
extern "C" {
void _mesa_log_multiline(int, const char*, const char* str) { if(str)std::fprintf(stderr,"%s\n",str); }
#define UNAVAILABLE(name) void name(){std::fprintf(stderr,"Unavailable host path: " #name "\n");std::abort();}
UNAVAILABLE(blake3_xof_many_avx512)
}

extern "C" {
unsigned long __stack_chk_guard=0x73adc951c4b179e0ul;
UNAVAILABLE(sceKernelAllocateDirectMemory)
UNAVAILABLE(sceKernelCreateEqueue)
UNAVAILABLE(sceKernelDeleteEqueue)
UNAVAILABLE(sceKernelGetDirectMemorySize)
UNAVAILABLE(sceKernelIsNeoMode)
UNAVAILABLE(sceKernelMapDirectMemory)
UNAVAILABLE(sceKernelMunmap)
UNAVAILABLE(sceKernelReleaseDirectMemory)
UNAVAILABLE(sceKernelWaitEqueue)
UNAVAILABLE(sceVideoOutAddFlipEvent)
UNAVAILABLE(sceVideoOutClose)
UNAVAILABLE(sceVideoOutGetBufferLabelAddress)
UNAVAILABLE(sceVideoOutOpen)
UNAVAILABLE(sceVideoOutRegisterBuffers)
UNAVAILABLE(sceVideoOutSetBufferAttribute)
UNAVAILABLE(sceVideoOutSetFlipRate)
UNAVAILABLE(sceVideoOutSubmitFlip)
UNAVAILABLE(sceVideoOutUnregisterBuffers)
}
