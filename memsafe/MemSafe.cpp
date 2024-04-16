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

void replaceAllocaToMymalloc(Function &F) {
	LLVMContext &context = F.getContext();
	IRBuilder<> Builder(context);
			
	dbgs() << "Debug: running memsafe pass on - " << F.getName() << "\n";
	// errs() << F << "\n";

	std::unordered_set<AllocaInst *> unsafeAllocaInsts;
	std::unordered_set<Value *> checkedPointers;
	FunctionCallee mymallocFunc = F.getParent()->getOrInsertFunction("mymalloc", Type::getInt8PtrTy(context),
																	 Type::getInt32Ty(context));
	FunctionCallee myfreeFunc = F.getParent()->getOrInsertFunction("myfree", Type::getVoidTy(context),
																   Type::getInt8PtrTy(context));

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
			} else uncheckedPointers.push(dyn_cast<StoreInst>(&Inst)->getPointerOperand());

			while (!uncheckedPointers.empty()) {
				Value *uncheckedPointer = uncheckedPointers.front();
				uncheckedPointers.pop();
				if (checkedPointers.count(uncheckedPointer)) continue;
				errs() << "Debug: uncheckedPointer - " << *uncheckedPointer << "\n";
				
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

	// replace AllocInst with mymalloc CallInst
	for (AllocaInst * unsafeAllocaInst: unsafeAllocaInsts) {
		errs() << "Debug: unsafeAllocaInst - " << *unsafeAllocaInst << "\n"; 
		Builder.SetInsertPoint(unsafeAllocaInst);
		Type *allocatedType = unsafeAllocaInst->getAllocatedType();
		unsigned int bitSize = 0;
		if (CompositeType *compositeType = dyn_cast<CompositeType>(allocatedType)) {
			const DataLayout &dataLayout = F.getParent()->getDataLayout();
			bitSize = dataLayout.getTypeAllocSize(compositeType)<<3;
		} else bitSize = allocatedType->getScalarSizeInBits();
		Value *size = Builder.getInt32(bitSize/8 + bitSize%8 != 0);
		CallInst *mallocCallInst = Builder.CreateCall(mymallocFunc, {size});
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
	std::unordered_map<Value *, Value *> pointerToBasePointerMap;
	
	for (BasicBlock &BB: F) {
		for (Instruction &Inst: BB) {

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

	
}

bool MemSafe::runOnFunction(Function &F) {
	TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

	replaceAllocaToMymalloc(F);
	disallowOutOfBoundsPtr(F);
	
	return true;
}

char MemSafe::ID = 0;
static RegisterPass<MemSafe> X("memsafe", "Memory Safety Pass",
                               false /* Only looks at CFG */,
                               false /* Analysis Pass */);

static RegisterStandardPasses Y(PassManagerBuilder::EP_EarlyAsPossible,
								[](const PassManagerBuilder &Builder,
								   legacy::PassManagerBase &PM) { PM.add(new MemSafe()); });
