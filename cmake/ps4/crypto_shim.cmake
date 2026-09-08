# WoWee PS4 build: OpenSSL-compatible crypto shim.
#
# include(cmake/ps4/crypto_shim.cmake) from the top-level CMakeLists.txt when
# WOWEE_PS4 is on, *instead of* find_package(OpenSSL).  It defines:
#
#   wowee_ps4_crypto      STATIC library implementing the OpenSSL subset WoWee
#                         uses (BN, SHA1/SHA256/MD5, HMAC, EVP digests,
#                         RAND_bytes, and the EVP_PKEY_fromdata /
#                         EVP_PKEY_verify_recover RSA path of warden_module.cpp).
#                         PUBLIC include dir: ps4/compat, so the unchanged
#                         #include <openssl/*.h> lines resolve to the shim.
#   OpenSSL::Crypto       ALIAS of wowee_ps4_crypto (only if not already defined)
#   OpenSSL::SSL          INTERFACE target linking wowee_ps4_crypto (only if not
#                         already defined) so existing target_link_libraries()
#                         lines keep working; no TLS is provided.
#   WOWEE_PS4_CRYPTO_SHIM_DIR   the ps4/compat directory
#   OPENSSL_VERSION       "shim" (for the configuration summary)
#
# Tests: ps4/compat/tests/run_crypto_tests.sh (host reference comparison against
# the system libcrypto + PS4 cross-compile/link probe).

get_filename_component(WOWEE_PS4_CRYPTO_SHIM_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ps4/compat" ABSOLUTE)
set(WOWEE_PS4_CRYPTO_SHIM_DIR "${WOWEE_PS4_CRYPTO_SHIM_DIR}" CACHE INTERNAL "WoWee PS4 crypto shim root")

set(WOWEE_PS4_CRYPTO_SOURCES
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/bn.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/sha1.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/sha256.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/md5.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/evp.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/hmac.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/rand.cpp
    ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src/rsa.cpp
)

add_library(wowee_ps4_crypto STATIC ${WOWEE_PS4_CRYPTO_SOURCES})
target_include_directories(wowee_ps4_crypto
    PUBLIC  ${WOWEE_PS4_CRYPTO_SHIM_DIR}
    PRIVATE ${WOWEE_PS4_CRYPTO_SHIM_DIR}/src)
target_compile_features(wowee_ps4_crypto PUBLIC cxx_std_17)
target_compile_definitions(wowee_ps4_crypto PUBLIC WOWEE_CRYPTO_SHIM=1)

if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto ALIAS wowee_ps4_crypto)
endif()
if(NOT TARGET OpenSSL::SSL)
    add_library(wowee_ps4_ssl_stub INTERFACE)
    target_link_libraries(wowee_ps4_ssl_stub INTERFACE wowee_ps4_crypto)
    add_library(OpenSSL::SSL ALIAS wowee_ps4_ssl_stub)
endif()
if(NOT DEFINED OPENSSL_VERSION)
    set(OPENSSL_VERSION "shim (ps4/compat)")
endif()
set(OPENSSL_FOUND TRUE)
