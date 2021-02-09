Concise Binary Object Representation (CBOR) Library
---------------------------------------------------

To build TinyCBOR:

```bash
make
```

If you want to change the compiler or pass extra compiler flags:

```bash
make CC=clang CFLAGS="-m32 -Oz" LDFLAGS="-m32"
```

You may also use CMake to build and integrate with your project:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Documentation: https://intel.github.io/tinycbor/current/
