## Description
This pass aims to enforce spatial and a weaker type safety for the C language via disallowing out-of-bound pointer accesses and having pointers with invalid addresses.
### Out-of-bound pointer access 
``` c
int arr[50];
arr[51] = 1;
&arr+52 = 1;
foo(&arr+53); 
```
### Write barriers (pointers storing invalid addresses)
``` c
int *ptr = (int *) malloc(sizeof(int)*50);
int *a = ptr;
free(ptr);
// a contains invalid address, after ptr being freed
````

## Building the pass from scratch
- Clone the repository https://github.com/Systems-IIITD/CSE601.git
- Download the `Memsafe.cpp` pass from this repository and replace it with the file present at `CSE601/llvm/lib/CodeGen/SafeC/MemSafe.cpp`
- Download the `support.c` pass from this repository and replace it with the file present at `CSE601/support/SafeGC/support.c`
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
> For testing the pass run the following command `cd tests/PA4/ && make clean && make -B && make run` inside `CSE601` directory

## How does this work
Because C lacks type safety, we cannot directly enforce type checks at runtime. To address this, we utilize the "mymalloc" routine, which tracks 
both the size and type information of objects. This enables dynamic enforcement of memory safety by storing object metadata just before 
the object itself. Our initial step involves replacing each alloca instruction and malloc call instruction with a mymalloc call instruction. 
For the alloca instruction, we must determine the size of the requested object, a task handled by the 
[getSizeOfAlloca](https://github.com/hyouteki/cop/blob/85915ab3f302626b6d80e7687dd354431654bb06/memsafe/MemSafe.cpp#L67-L80C2) routine. Once the size 
of the alloca instruction is calculated, we insert a mymalloc API call and include the myfree (equivalent to the free API) after the last use 
of the alloca instruction found in the original LLVM IR.
