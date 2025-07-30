cmake -DCMAKE_BUILD_TYPE=Debug -DWITH_DEBUG=1 -DWITH_UNIT_TESTS=0 \
	-DCMAKE_C_COMPILER=clang \
	-DCMAKE_CXX_COMPILER=clang++ \
	-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -L/usr/local/lib/x86_64-linux-gnu" \
	-DCMAKE_MODULE_LINKER_FLAGS="-fuse-ld=lld -L/usr/local/lib/x86_64-linux-gnu" \
	-DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld -L/usr/local/lib/x86_64-linux-gnu" \
	-B build
