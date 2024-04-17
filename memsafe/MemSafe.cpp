#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/LowLevelTypeImpl.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <deque>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <assert.h>

using namespace llvm;

namespace {
struct MemSafe: public FunctionPass {
	static char ID;
	const TargetLibraryInfo *TLI = nullptr;

	MemSafe() : FunctionPass(ID) {}

	void getAnalysisUsage(AnalysisUsage &AU) const override {
		AU.addRequired<TargetLibraryInfoWrapperPass>();
	}

	bool runOnFunction(Function &F) override;
	
}; // end of struct MemSafe
}  // end of anonymous namespace

static bool isLibraryCall(const CallInst *CI, const TargetLibraryInfo *TLI) {
	LibFunc Func;
	if (TLI->getLibFunc(ImmutableCallSite(CI), Func)) return true;
	auto Callee = CI->getCalledFunction();
	if (Callee && Callee->getName() == "readArgv") return true;
	if (isa<IntrinsicInst>(CI)) return true;
	return false;
}

Instruction *getPreviousInstruction(Instruction *Inst) {
    BasicBlock *BB = Inst->getParent();
    BasicBlock::iterator itr(Inst);
    if (itr != BB->begin()) return &*(--itr);
	if (BB == &BB->getParent()->getEntryBlock()) return nullptr;
	BasicBlock *prevBB = &*(--BB->getIterator());
	return &prevBB->back();    
}

Value *getSizeOfAlloca(AllocaInst *allocaInst, IRBuilder<> &Builder, const DataLayout &dataLayout) {
	Type *Int32Ty = Type::getInt32Ty(allocaInst->getContext());
	if (Value *sizeOperand = allocaInst->getArraySize()) {
		Type *allocatedType = allocaInst->getAllocatedType();
		uint64_t typeSize = dataLayout.getTypeAllocSize(allocatedType);
		Builder.SetInsertPoint(allocaInst);
		if (sizeOperand->getType() != Int32Ty)
			sizeOperand = Builder.CreateZExtOrTrunc(sizeOperand, Int32Ty);
		return Builder.CreateMul(sizeOperand, ConstantInt::get(Int32Ty, typeSize));
	}
	perror("Error: exhaustive handling of allocaInst size calculation\n");
	exit(1);
	return nullptr;
}

void replaceAllocaToMymalloc(Function &F) {
	LLVMContext &context = F.getContext();
	IRBuilder<> Builder(context);
			
	dbgs() << "Debug: replacing alloca with mymalloc in `" << F.getName() << "`\n";
	// errs() << F << "\n";

	std::unordered_set<AllocaInst *> unsafeAllocaInsts;
	std::unordered_set<Value *> checkedPointers;
	FunctionCallee mymallocFunc = F.getParent()
		->getOrInsertFunction("mymalloc", Type::getInt8PtrTy(context), Type::getInt32Ty(context));
	FunctionCallee myfreeFunc = F.getParent()
		->getOrInsertFunction("myfree", Type::getVoidTy(context), Type::getInt8PtrTy(context));

	errs() << "Debug: uncheckedPointers\n";
	for (BasicBlock &BB: F) {
		for (Instruction &Inst: BB) {
			if (!isa<CallInst>(&Inst) && !isa<StoreInst>(&Inst)) continue;
			std::queue<Value *> uncheckedPointers;

			if (CallInst *callInst = dyn_cast<CallInst>(&Inst)) {
				for (unsigned i = 0; i < callInst->getNumArgOperands(); ++i) {
					Value *argOperand = callInst->getArgOperand(i);
					if (!argOperand->getType()->isPointerTy()) continue;
					ConstantExpr *CE = dyn_cast<ConstantExpr>(argOperand);
					if (CE && CE->getOpcode() == Instruction::GetElementPtr
						&& dyn_cast<GlobalVariable>(CE->getOperand(0))) continue;
					uncheckedPointers.push(argOperand);
				}
			} else {
				Value *valueOperand = dyn_cast<StoreInst>(&Inst)->getValueOperand();
				if(valueOperand->getType()->isPointerTy()) uncheckedPointers.push(valueOperand);
			}

			while (!uncheckedPointers.empty()) {
				Value *uncheckedPointer = uncheckedPointers.front();
				uncheckedPointers.pop();
				if (checkedPointers.count(uncheckedPointer)) continue;
				errs() << "|\t" << *uncheckedPointer << "\n";
				
				Instruction *curInst = &Inst;
				while (true) {
					curInst = getPreviousInstruction(curInst);
					if (curInst == nullptr) {
						if (none_of(F.args(), [&](const Argument &arg) {
							return arg.getType()->isPointerTy() && uncheckedPointer == &arg;
						})) perror("Error: cannot find the pointer operand used in function call");
						break;
					}

					// no need to handle unsafe pointer returning from function calls
					if (CallInst *callInst = dyn_cast<CallInst>(curInst)) {
						if (uncheckedPointer == callInst) break;
						continue;
					}

					// no need to handle unsafe pointer stored in memory
					if (LoadInst *loadInst = dyn_cast<LoadInst>(curInst)) {
						if (uncheckedPointer == loadInst) break;
						continue;
					}
					
					if (AllocaInst *allocaInst = dyn_cast<AllocaInst>(curInst)) {
						if (uncheckedPointer != allocaInst
							|| checkedPointers.count(uncheckedPointer)) continue;
						unsafeAllocaInsts.insert(allocaInst);
						break;
					}

					if (BitCastInst *bitCastInst = dyn_cast<BitCastInst>(curInst)) {
						if (uncheckedPointer != bitCastInst) continue;
						if (!checkedPointers.count(uncheckedPointer))
							uncheckedPointers.push(bitCastInst->getOperand(0));
						break;
					}
					
					if (GetElementPtrInst *getElementPtrInst = dyn_cast<GetElementPtrInst>(curInst)) {
						if (uncheckedPointer != getElementPtrInst) continue;
						if (!checkedPointers.count(uncheckedPointer))
							uncheckedPointers.push(getElementPtrInst->getPointerOperand());
						break;
					}
				}
				
				checkedPointers.insert(uncheckedPointer);
			}
		}
	}
	
	errs() << "Debug: unsafeAllocaInsts\n";
	// replace AllocInst with mymalloc CallInst
	for (AllocaInst * unsafeAllocaInst: unsafeAllocaInsts) {
		errs() << "|\t" << *unsafeAllocaInst << "\n"; 
		Builder.SetInsertPoint(unsafeAllocaInst);
		const DataLayout &dataLayout = F.getParent()->getDataLayout();
		Value *allocaSize = getSizeOfAlloca(unsafeAllocaInst, Builder, dataLayout);
		CallInst *mallocCallInst = Builder.CreateCall(mymallocFunc, {allocaSize});
		Value *bitCastInst = Builder.CreateBitCast(mallocCallInst, unsafeAllocaInst->getType());
		unsafeAllocaInst->replaceAllUsesWith(bitCastInst);
		// insert myfree CallInst after the last use of BitCastInst 
		Use *lastBitCastUse = nullptr;
		for (Use &use : bitCastInst->uses()) lastBitCastUse = &use;
		Instruction *lastBitCastInst = dyn_cast<Instruction>(lastBitCastUse->getUser());
		Builder.SetInsertPoint(lastBitCastInst->getNextNode());
		Type *bitCastTy = Type::getInt8PtrTy(context);
		Value *freeBitCastInst = Builder.CreateBitCast(bitCastInst, bitCastTy);
		Builder.CreateCall(myfreeFunc, {freeBitCastInst});
		unsafeAllocaInst->eraseFromParent();
	}
}

void disallowOutOfBoundsPtr(Function &F) {
	LLVMContext &context = F.getContext();
	IRBuilder<> Builder(context);
	
	dbgs() << "Debug: disallowing out of bounds pointer in `" << F.getName() << "`\n";
	// errs() << F << "\n";
	
	std::unordered_map<Value *, Value *> pointerToBasePointerMap;
	std::vector<Instruction *> unsafeBoundAccesses;
	FunctionCallee isSafeToEscapeFunc = F.getParent()->
		getOrInsertFunction("IsSafeToEscape", Type::getVoidTy(context),
							Type::getInt8PtrTy(context), Type::getInt8PtrTy(context));
	
	for (BasicBlock &BB: F) {
		for (Instruction &Inst: BB) {
			if (isa<CallInst>(&Inst) || isa<StoreInst>(&Inst)) unsafeBoundAccesses.push_back(&Inst);

			if (CallInst *callInst = dyn_cast<CallInst>(&Inst)) {
				if (!callInst->getType()->isPointerTy() || !callInst->getCalledValue()) continue;
				auto *callee = callInst->getCalledValue()->stripPointerCasts();
				if (!callee->getName().compare("mymalloc")) pointerToBasePointerMap[callInst] = nullptr;
			}

			if (GetElementPtrInst *getElementPtrInst = dyn_cast<GetElementPtrInst>(&Inst)) {
				Value *basePointer = getElementPtrInst->getPointerOperand();
				while (true) {
					if (pointerToBasePointerMap.find(basePointer) == pointerToBasePointerMap.end()
						|| pointerToBasePointerMap[basePointer] == nullptr) break;
					basePointer = pointerToBasePointerMap[basePointer];
				}
				pointerToBasePointerMap[getElementPtrInst] = basePointer;
			}
		}
	}

	errs() << "Debug: unsafeBoundAccesses\n";
	for (Instruction *unsafeBoundAccess: unsafeBoundAccesses) {
		errs() << "|\t" << *unsafeBoundAccess << "\n";
		std::unordered_set<Value *> pointers;

		if (StoreInst *storeInst = dyn_cast<StoreInst>(unsafeBoundAccess)) {
			pointers.insert(storeInst->getValueOperand());
			pointers.insert(storeInst->getPointerOperand());
		}

		if (CallInst *callInst = dyn_cast<CallInst>(unsafeBoundAccess)) {
			for (unsigned i = 0; i < callInst->getNumArgOperands(); ++i) {
				Value *argOperand = callInst->getArgOperand(i);
				if (!argOperand->getType()->isPointerTy()) continue;
				ConstantExpr *CE = dyn_cast<ConstantExpr>(argOperand);
				if (CE && CE->getOpcode() == Instruction::GetElementPtr
					&& dyn_cast<GlobalVariable>(CE->getOperand(0))) continue;
				pointers.insert(argOperand);
			}
		}

		for (Value *pointer: pointers) {
			Value *basePointer = pointerToBasePointerMap[pointer];
			if (!basePointer) continue;
			Builder.SetInsertPoint(unsafeBoundAccess);
			Type *bitCastTy = Type::getInt8PtrTy(context);
			Value *bitCastPointer = Builder.CreateBitCast(pointer, bitCastTy);
			Value *bitCastBasePointer = Builder.CreateBitCast(basePointer, bitCastTy);
			Builder.CreateCall(isSafeToEscapeFunc, {bitCastBasePointer, bitCastPointer});
		}
	}
}

void addWriteBarriers(Function &F) {
	LLVMContext &context = F.getContext();
	IRBuilder<> Builder(context);
	
	dbgs() << "Debug: adding write barriers in `" << F.getName() << "`\n";

	std::vector<StoreInst *> unsafeStoreInsts;
	FunctionCallee checkWriteBarrierFunc = F.getParent()
		->getOrInsertFunction("CheckWriteBarrier", Type::getVoidTy(context), Type::getInt8PtrTy(context));

	
	for (BasicBlock &BB: F) {
		for (Instruction &Inst: BB) {
			if (StoreInst *storeInst = dyn_cast<StoreInst>(&Inst)) unsafeStoreInsts.push_back(storeInst);
		}
	}

	errs() << "Debug: unsafeStoreInsts\n";
	for (StoreInst *storeInst: unsafeStoreInsts) {
		errs() << "|\t" << *storeInst << "\n";
		Builder.SetInsertPoint(storeInst->getNextNode());
		Type *bitCastTy = Type::getInt8PtrTy(context);
		Value *bitCastedPtr = Builder.CreateBitCast(storeInst->getPointerOperand(), bitCastTy);
		Builder.CreateCall(checkWriteBarrierFunc, {bitCastedPtr});
	}
}


bool MemSafe::runOnFunction(Function &F) {
	TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

	replaceAllocaToMymalloc(F);
	disallowOutOfBoundsPtr(F);
	addWriteBarriers(F);
	
	return true;
}

char MemSafe::ID = 0;
static RegisterPass<MemSafe> X("memsafe", "Memory Safety Pass",
                               false /* Only looks at CFG */,
                               false /* Analysis Pass */);

static RegisterStandardPasses Y(PassManagerBuilder::EP_EarlyAsPossible,
								[](const PassManagerBuilder &Builder,
								   legacy::PassManagerBase &PM) { PM.add(new MemSafe()); });
