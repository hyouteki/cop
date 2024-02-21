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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <deque>
#include <vector>

using namespace llvm;
namespace {
	struct NullCheck : public FunctionPass {
		static char ID;
		NullCheck() : FunctionPass(ID) {}

		bool runOnFunction(Function &F) override {
			IRBuilder<> Builder(F.getContext());
			std::vector<Instruction *> instsToProcess;
			
			BasicBlock *nullBB = BasicBlock::Create(F.getContext(), "NullBB", &F);
			Builder.SetInsertPoint(nullBB);
			Type* returnType = F.getReturnType();
			if (returnType->isVoidTy()) Builder.CreateRetVoid();
			else if (returnType->isIntegerTy()) Builder.CreateRet(ConstantInt::get(returnType, 0));
			else if (PointerType* ptrType = dyn_cast<PointerType>(returnType)) {
				if (ptrType->getElementType()->isIntegerTy()) {
					Builder.CreateRet(ConstantPointerNull::get(ptrType));
				}
			}
			
			for (BasicBlock &BB: F) {
				for (Instruction &Inst: BB) {
					if (includeInst(Inst)) instsToProcess.push_back(&Inst);
				}
			}

			for (Instruction *inst_ptr: instsToProcess) {
				processInst(*inst_ptr, Builder, nullBB);
			}

			errs() << F << "\n";
			dbgs() << "running nullcheck pass on: " << F.getName() << "\n";
			return false;
		}

		bool includeInst(Instruction &Inst) {
			return dyn_cast<StoreInst>(&Inst)
				|| dyn_cast<LoadInst>(&Inst)
				|| dyn_cast<GetElementPtrInst>(&Inst);
		}
		
		void processInst(Instruction &Inst, IRBuilder<> &Builder, BasicBlock* nullBB) {
			Value* basePointer;
			if (auto *inst = dyn_cast<StoreInst>(&Inst)) {
				basePointer = inst->getPointerOperand();
			} else if (auto *inst = dyn_cast<LoadInst>(&Inst)) {
				basePointer = inst->getPointerOperand();
			} else if (auto *inst = dyn_cast<GetElementPtrInst>(&Inst)) {
				basePointer = inst->getPointerOperand();
			} else return;
			Type* basePointerType = basePointer->getType();
			PointerType* pointerType = PointerType::get(basePointerType->getPointerElementType(),
														basePointerType->getPointerAddressSpace());
			BasicBlock *currentBB = Inst.getParent();
			BasicBlock *notNullBB = currentBB->splitBasicBlock(&Inst, "NotNullBB");
			currentBB->getTerminator()->eraseFromParent();
			Builder.SetInsertPoint(currentBB);
			Value* icmpEqInst = Builder.CreateICmpEQ(basePointer, ConstantPointerNull::get(pointerType),
													 "IsNull");
			Builder.CreateCondBr(icmpEqInst, nullBB, notNullBB);
		}
	};
}

char NullCheck::ID = 0;
static RegisterPass<NullCheck> X("nullcheck", "Null Check Pass",
                                 false /* Only looks at CFG */,
                                 false /* Analysis Pass */);

static RegisterStandardPasses Y(PassManagerBuilder::EP_EarlyAsPossible,
								[](const PassManagerBuilder &Builder,
								   legacy::PassManagerBase &PM) { PM.add(new NullCheck()); });
