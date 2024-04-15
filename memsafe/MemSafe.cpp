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

bool MemSafe::runOnFunction(Function &F) {
	TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
	LLVMContext &context = F.getContext();
	IRBuilder<> Builder(context);
			
	dbgs() << "Debug: running memsafe pass on - " << F.getName() << "\n";
	// errs() << F << "\n";

	std::unordered_set<AllocaInst *> unsafeAllocaInsts;
	std::unordered_set<GetElementPtrInst *> unsafeGEPInsts;
	// TODO: complete this unsafeGEP insts
	std::set<std::pair<CallInst *, Value *>> myfreeCache;
	FunctionCallee mymallocFunc = F.getParent()->getOrInsertFunction("mymalloc", Type::getInt8PtrTy(context),
																	 Type::getInt32Ty(context));
	FunctionCallee myfreeFunc = F.getParent()->getOrInsertFunction("myfree", Type::getVoidTy(context),
																   Type::getInt8PtrTy(context));
	
	for (BasicBlock &BB: F) {
		for (Instruction &Inst: BB) {
			CallInst *callInst = dyn_cast<CallInst>(&Inst);
			if (!callInst) continue;

			std::unordered_set<Value *> uncheckedPointers;
			for (unsigned i = 0; i < callInst->getNumArgOperands(); ++i) {
				Value *argOperand = callInst->getArgOperand(i);
				if (!argOperand->getType()->isPointerTy()) continue;
				ConstantExpr *CE = dyn_cast<ConstantExpr>(argOperand);
				if (CE && CE->getOpcode() == Instruction::GetElementPtr
					&& dyn_cast<GlobalVariable>(CE->getOperand(0))) continue;
				uncheckedPointers.insert(argOperand);
			}

			for (Value *uncheckedPointer: uncheckedPointers) {
				errs() << "Debug: uncheckedPointer - " << *uncheckedPointer << "\n";
				Instruction *curInst = &Inst;
				while (true) {
					curInst = getPreviousInstruction(curInst);
					if (curInst == nullptr) {
						if (none_of(F.args(), [&](const Argument &arg) {
							return arg.getType()->isPointerTy() && uncheckedPointer == &arg;
						})) perror("Error: cannot find the pointer operand used in function call\n");
						break;
					}
					if (AllocaInst *allocaInst = dyn_cast<AllocaInst>(curInst)) {
						if (none_of(curInst->operands(), [&](const Use &use) {
							return use == uncheckedPointer;
						})) continue;
						unsafeAllocaInsts.insert(allocaInst);
						myfreeCache.insert(std::make_pair(callInst, uncheckedPointer));
						break;
					}
					if (GetElementPtrInst *getElementPtrInst = dyn_cast<GetElementPtrInst>(curInst)) {
						break;
					}
				}
			}
		}
	}

	std::vector<Value *> bitCastInsts;
	// replace AllocInst with mymalloc CallInst
	for (AllocaInst * unsafeAllocaInst: unsafeAllocaInsts) {
		Builder.SetInsertPoint(unsafeAllocaInst);
		Type *allocatedType = unsafeAllocaInst->getAllocatedType();
		unsigned int bitSize = 0;
		if (CompositeType *compositeType = dyn_cast<CompositeType>(allocatedType)) {
			const DataLayout &dataLayout = F.getParent()->getDataLayout();
			bitSize = dataLayout.getTypeAllocSize(compositeType)<<3;
		} else bitSize = allocatedType->getScalarSizeInBits();
		Value *size = Builder.getInt32(bitSize/8 + bitSize%8 != 0);
		CallInst *mallocCallInst = Builder.CreateCall(mymallocFunc, {size});
		Value *bitCastInst = Builder.CreateBitCast(mallocCallInst, unsafeAllocaInst->getType(), "");
		bitCastInsts.push_back(bitCastInst);
		unsafeAllocaInst->replaceAllUsesWith(bitCastInst);
		unsafeAllocaInst->eraseFromParent();
	}

	// insert myfree CallInst after CallInst
	for (std::pair<CallInst *, Value *> myfreeCacheEntry: myfreeCache) {
		if (myfreeCacheEntry.first->getNextNode())
			Builder.SetInsertPoint(myfreeCacheEntry.first->getNextNode());
		else Builder.SetInsertPoint(myfreeCacheEntry.first->getParent());
		Type *bitCastTy = Type::getInt8PtrTy(context);
		Value *bitCastInst = Builder.CreateBitCast(myfreeCacheEntry.second, bitCastTy, "");
		Builder.CreateCall(myfreeFunc, {bitCastInst});
	}
	
	return true;
}

char MemSafe::ID = 0;
static RegisterPass<MemSafe> X("memsafe", "Memory Safety Pass",
                               false /* Only looks at CFG */,
                               false /* Analysis Pass */);

static RegisterStandardPasses Y(PassManagerBuilder::EP_EarlyAsPossible,
								[](const PassManagerBuilder &Builder,
								   legacy::PassManagerBase &PM) { PM.add(new MemSafe()); });
