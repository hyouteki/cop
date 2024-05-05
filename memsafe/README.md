> This pass aims to enforce spatial and a weaker type safety for the C language via disallowing out-of-bound pointer accesses and having pointers with invalid addresses.

#### Write barriers (pointers storing invalid addresses)


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
C allows pointer typecasting to non-pointer types and pointer arithmetic. Thus, directly enforcing type checks at runtime is not feasible. To address this, we utilize the `mymalloc` routine, which tracks objects' size and type information, enabling dynamic enforcement of memory safety by storing object metadata just before the object itself.

More than just keeping track of the base pointer is required; as in C, a pointer can be typecasted to unsigned long and passed to a function. Thus, we must keep track of all the variations (child pointers) of the base pointer (parent pointer). For example:
``` c
int *ptr = (int *)mymalloc(sizeof(int)*1);
unsigned long long a = (unsigned long long)ptr;
```
``` llvm
%ptr_addr = alloca i32*, align 8
%a = alloca i64, align 8	
%mymalloc_call = call noalias i8* @mymalloc(i64 4) #2
%mymalloc_output = bitcast i8* %mymalloc_call to i32*
store i32* %0, i32** %ptr_addr, align 8
%1 = load i32*, i32** %ptr_addr, align 8
%2 = ptrtoint i32* %1 to i64
store i64 %2, i64* %a, align 8
```
> As `a` is not a pointer but dynamically contains a pointer value. Thus we need to keep track of it as well. For this we need to track the pointer going through different instructions such as bitcast, getElementPtrInst, etc. In this example `a` is a child pointer of the parent pointer `ptr`.

The first step is to replace every alloca instruction (stack allocation) and malloc call instruction (heap allocation) with a mymalloc call instruction (heap allocation of object and object metadata) so that we have access to the object metadata. For the alloca instruction, we need to determine the size of the requested object, which is done using the [getSizeOfAlloca](https://github.com/hyouteki/cop/blob/85915ab3f302626b6d80e7687dd354431654bb06/memsafe/MemSafe.cpp#L67-L80C2) routine. Once the size of the alloca instruction is calculated, we insert a mymalloc API call and include the myfree (equivalent to the free API) after the last use of the alloca instruction found in the original LLVM IR.

> This is handled by the [replaceAllocaToMymalloc](https://github.com/hyouteki/cop/blob/8c91b14a81bb1a3a23e77d700422e2ac2c6161ab/memsafe/MemSafe.cpp#L82-L191) API.

Next step is to disallow out-of-bounds pointer accesses which is handled by the [disallowOutOfBoundsPtr](https://github.com/hyouteki/cop/blob/8c91b14a81bb1a3a23e77d700422e2ac2c6161ab/memsafe/MemSafe.cpp#L193-L259C2) API. It works by finding all the pointer accesses and adds a call instruction to the [isSafeToEscapeFunc](https://github.com/hyouteki/cop/blob/25c99cc5e4b7b7f1dde801def996db181f25a3f1/memsafe/support.c#L96-L115C2)(which is declared in the support.c file) before the current pointer access. The `isSafeToEscapeFunc` routine works by finding the closest base pointer of the given pointer and checks whether the given pointer lies in the bounds, i.e. `[basePointer, basePointer+baseSize)` of the base pointer.
``` c
int *arr = (int *)mymalloc(sizeof(int)*50);
arr[0] = 1;
arr[51] = 1;   // OOB access
*(arr+52) = 1; // OOB access
foo(&arr+53);  // OOB access
```
> Demonstrations of Out-of-bounds pointer accesses.
``` c
int *arr = (int *)mymalloc(sizeof(int)*50);
isSafeToEscapeFunc(arr);    // Pass
arr[0] = 1;
isSafeToEscapeFunc(arr+51); // `Error: invalid pointer\nIssue: pointer out of bounds of base pointer\n`
arr[51] = 1;
isSafeToEscapeFunc(arr+52); // `Error: invalid pointer\nIssue: pointer out of bounds of base pointer\n`
*(arr+52) = 1;
isSafeToEscapeFunc(arr+53); // `Error: invalid pointer\nIssue: pointer out of bounds of base pointer\n`
foo(arr+53);
```
> C equivalent of the updated LLVM IR after passing through the `disallowOutOfBoundsPtr` API.

Lastly, we add write barriers via [addWriteBarriers](https://github.com/hyouteki/cop/blob/8c91b14a81bb1a3a23e77d700422e2ac2c6161ab/memsafe/MemSafe.cpp#L261-L286C2) API to identify instances where variables are getting stored invalid heap addresses. This API works by getting the pointer operand from all the store instructions and passing it to a [checkWriteBarrier](https://github.com/hyouteki/cop/blob/25c99cc5e4b7b7f1dde801def996db181f25a3f1/memsafe/support.c#L117-L141C2) routine which validates the heap address. This API also works if we store an object which contains a pointer operand.
``` c
int *ptr = (int *)mymalloc(sizeof(int)*50);                  // valid address
int *a = (int *)0;                                           // invalid address
LinkedListNode node = {.Value=1, .Next=(LinkedListNode *)0}; // invalid address
```
> Demonstration of invalid address. The address 0 is getting stored in the variable `a`, which is invalid. The same is true for the variable `node`, where address 0 is getting stored in the `Next` field, which is invalid.
``` c
int *ptr = (int *)mymalloc(sizeof(int)*50);
checkWriteBarrier(ptr); // Pass
checkWriteBarrier((int *)0);
int *a = (int *)0;      // `Error: invalid pointer (int *)0 found inside (int *)0\n`
checkWriteBarrier((LinkedListNode){.Value=1, .Next=(LinkedListNode *)0});
// `Error: invalid pointer (LinkedListNode *)0 found inside (LinkedListNode){.Value=1, .Next=(LinkedListNode *)0}\n`
LinkedListNode node = {.Value=1, .Next=(LinkedListNode *)0};
```
> C equivalent of the updated LLVM IR after passing through the `addWriteBarriers` API.

## Miscellaneous
> [!Note]
> We can also use a custom null pointer dereference detection compiler pass in conjunction with this pass for added type safety for C lang. Which can be found [here](../nullchecks)
