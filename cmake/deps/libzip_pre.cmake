option(ENABLE_COMMONCRYPTO "" OFF)
option(ENABLE_GNUTLS "Enable use of GnuTLS" OFF)
option(ENABLE_MBEDTLS "Enable use of mbed TLS" OFF)
option(ENABLE_OPENSSL "Enable use of OpenSSL" OFF)
option(ENABLE_WINDOWS_CRYPTO "Enable use of Windows cryptography libraries" OFF)

option(ENABLE_BZIP2 "Enable use of BZip2" OFF)
option(ENABLE_LZMA "Enable use of LZMA" OFF)
option(ENABLE_ZSTD "Enable use of Zstandard" OFF)

option(ENABLE_FDOPEN "Enable zip_fdopen, which is not allowed in Microsoft CRT secure libraries" OFF)

option(BUILD_TOOLS "Build tools in the src directory (zipcmp, zipmerge, ziptool)" OFF)
option(BUILD_REGRESS "Build regression tests" OFF)
option(BUILD_OSSFUZZ "Build fuzzers for ossfuzz" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_DOC "Build documentation" OFF)
option(LIBZIP_DO_INSTALL "Install libzip and the related files" OFF)
option(SHARED_LIB_VERSIONNING "Add SO version in .so build" OFF)
