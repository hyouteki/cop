#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "memory.h"
#include "support.h"

typedef struct ObjHeader {
	unsigned Size;
	unsigned short Status;
	unsigned short Alignment;
	unsigned long long Type;
} ObjHeader;

#define OBJ_HEADER_SIZE (sizeof(ObjHeader))
#define GetObjHeader(Ptr) ((ObjHeader*)((char*)(Ptr) - OBJ_HEADER_SIZE))

typedef struct BasePointerList {
	void *Ptr;
	struct BasePointerList *Next;
} BasePointerList;

static BasePointerList *BasePointers = NULL;

static void insertFrontBasePointers(void *);
static void *getBasePointer(void *);
static int IsValidPointer(void *);

static void insertFrontBasePointers(void *Ptr) {
	BasePointerList *L = malloc(sizeof(BasePointerList));
	if (L == NULL) {
		printf("ERROR: unable to allocate memory for BasePointerList object\n");
		exit(0);
	}
	L->Ptr = Ptr;
	L->Next = BasePointers;
	BasePointers = L;
}

static void *getBasePointer(void *Ptr) {
	assert(BasePointers);
	void *basePtr = 0;
	BasePointerList *Itr = BasePointers;
	while (Itr != NULL) {
		if (Ptr >= Itr->Ptr && Ptr-basePtr >= Ptr-Itr->Ptr) basePtr = Itr->Ptr;
		Itr = Itr->Next;
	}
	if (basePtr == 0) {
		printf("Error: invalid pointer(%p)\n", Ptr);
		exit(0);
	}
	return basePtr;
}

static int IsValidPointer(void *Ptr) {
	assert(BasePointers);
	BasePointerList *Itr = BasePointers;
	while (Itr != NULL) {
		ObjHeader *BaseObjHeader = GetObjHeader(Itr->Ptr);
		unsigned int BaseSize = BaseObjHeader->Size - OBJ_HEADER_SIZE;
		if (Itr->Ptr <= Ptr && Ptr < Itr->Ptr + BaseSize) return 1; 
		Itr = Itr->Next;
	}
	return 0;
}

void checkTypeInv(void *Src, unsigned long long SrcType) {
	unsigned long long SrcOrigType = GetType(Src);
	if (SrcOrigType != SrcType) {
		printf("Invalid obj type; expected: %lld, found: %lld\n", SrcType, SrcOrigType);
		exit(0);
	}
}

void checkSizeInv(void *Dst, unsigned DstSize) {
	unsigned DstOrigSize = GetSize(Dst);
	if (DstOrigSize < DstSize) {
		printf("Invalid obj size: min_required: %x, current: %x\n", (unsigned)DstSize, DstOrigSize);
		exit(0);
	}
}

void checkSizeAndTypeInv(void *Src, unsigned long long DstType, unsigned DstSize) {
	checkTypeInv(Src, DstType);
	checkSizeInv(Src, DstSize);
}

void* mycast(void *Ptr, unsigned long long Bitmap, unsigned Size) {
	printf("Debug: mycast called on BasePtr(%p) with bitmat(%lld)\n", Ptr, Bitmap);
	SetType(Ptr-OBJ_HEADER_SIZE, Bitmap);
	insertFrontBasePointers(Ptr);
	return Ptr;
}

void IsSafeToEscape(void *Base, void *Ptr) {
	printf("Debug: base=%p->", Base);
	Base = getBasePointer(Base);
	printf("%p\n", Base);
	if (Base == Ptr) return;
	if (Base > Ptr) {
		printf("Error: Invalid Ptr\nIssue: Base > Ptr\n");
		exit(0);
	}

	ObjHeader *BaseObjHeader = GetObjHeader(Base);
	unsigned int BaseSize = BaseObjHeader->Size - OBJ_HEADER_SIZE;
	unsigned long long BaseType = BaseObjHeader->Type;

	if (Base + BaseSize <= Ptr) {
		printf("Error: invalid pointer\nIssue: pointer out of bounds of base pointer\n");
		printf("Debug: base=%p, ptr=%p, bounds=[%p, %p)\n", Base, Ptr, Base, Base+BaseSize);
		exit(0);
	}
}

void CheckWriteBarrier(void *Base) {
	printf("Debug: base=%p->", Base);
	Base = getBasePointer(Base);
	printf("%p\n", Base);

	ObjHeader *BaseObjHeader = GetObjHeader(Base);
	unsigned int BaseSize = BaseObjHeader->Size - OBJ_HEADER_SIZE;
	unsigned long long BaseType = BaseObjHeader->Type;

	unsigned short numFields = 0;
	for (unsigned long long i = BaseType; i; i>>1, ++numFields);
	--numFields;
	
	unsigned long long *Itr = (unsigned long long *)Base;
	int field = 0;
	while (BaseType && field < numFields) {
		if ((BaseType&1) && !(Itr && IsValidPointer(Itr))) {
			printf("Error: invalid pointer(%p) found inside(%p)\n", Itr, Base);
			exit(0);
		}
		++field;
		BaseType >>= 1;
		++Itr;
	}
}
