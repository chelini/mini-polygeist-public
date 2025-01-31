```sh

# Clone in /home/mlir

git clone https://github.com/chelini/mini-polygeist-public.git

cd mini-polygeist-public/

mkdir build

cd build

cmake ../ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_LINKER=lld -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DMLIR_DIR=/home/mlir/llvm-project/build/lib/cmake/mlir/ -DLLVM_DIR=/home/mlir/llvm-project/prefix/lib/cmake/llvm/ -DClang_DIR=/home/mlir/llvm-project/build/lib/cmake/clang/ -DLLVM_EXTERNAL_LIT=/home/mlir/llvm-project/build/bin/llvm-lit

ninja

```
