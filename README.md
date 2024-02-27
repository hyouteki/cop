> `nullchecks`: this pass does not allow null pointer dereferences and would return an error if do so

## Building the pass from scratch
- Clone the repository https://github.com/Systems-IIITD/CSE601.git
- Download the `NullChecks.cpp` pass from this repository and replace it with the file present at `CSE601/llvm/lib/CodeGen/SafeC/Nullchecks.cpp`
- Install cmake and ninja build systems using `sudo apt install cmake ninja-build`
- Run the following commands inside CSE601 directory
  ``` console
  mkdir build
  cd build && cp ../scripts/build.sh . && sh build.sh && ninja -j 4
  cd tests && make
  ```
> [!Note]
> If you encounter error such as
> ``` console
> /home/puneetku/CSE601/llvm/utils/benchmark/src/benchmark_register.h:17:30: error: ‘numeric_limits’ is not a member of ‘std’
> 17 |   static const T kmax = std::numeric_limits<T>::max();
> ```
> Try adding `#include<limits>` in the header file and re-building the project
- For subsequent builds just run `cd build && ninja -j 4` inside `CSE601` directory
> [!Note]
> For testing the pass run the following command `cd tests/PA1/ && make clean && make -B` inside `CSE601` directory
