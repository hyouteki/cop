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
#include <unordered_map>
#include <unordered_set>
#include <queue>

using namespace llvm;
namespace {
	struct NullCheck : public FunctionPass {
		static char ID;
		NullCheck() : FunctionPass(ID) {}

		bool runOnFunction(Function &F) override {
			dbgs() << "running nullcheck pass on: " << F.getName() << "\n";
			LLVMContext &context = F.getContext();
			IRBuilder<> Builder(context);
			std::vector<Instruction *> instsToProcess;
	   
			BasicBlock *nullBB = BasicBlock::Create(F.getContext(), "NullBB", &F);
			Builder.SetInsertPoint(nullBB);

			// if `printf` api not already present then declare it
			Module *M = F.getParent(); 
			if (!M->getFunction("printf")) {
				FunctionType *printfType = FunctionType::get(IntegerType::getInt32Ty(context),
															 PointerType::get(Type::getInt8Ty(context), 0), true);
				Function::Create(printfType, Function::ExternalLinkage, "printf", M);
			}
			// add a error message in `nullBB`
            Function *printfFunc = M->getFunction("printf");
			std::string message = "error: null pointer dereference found in function `"
				+ F.getName().str() + "`\n";
            Constant *formatString = Builder.CreateGlobalStringPtr(message);
            Builder.CreateCall(printfFunc, {formatString});
			// add a default return type from `nullBB`
			Type *returnType = F.getReturnType();
			if (returnType->isVoidTy()) Builder.CreateRetVoid();
			else if (returnType->isIntegerTy()) Builder.CreateRet(ConstantInt::get(returnType, 0));
			else if (PointerType *ptrType = dyn_cast<PointerType>(returnType)) {
				if (ptrType->getElementType()->isIntegerTy()) {
					Builder.CreateRet(ConstantPointerNull::get(ptrType));
				}
			}
			
			std::unordered_map<Value *, std::unordered_set<Value *>> childPointers;
			for (BasicBlock &BB: F) {
				if (&BB == nullBB) continue;
				std::unordered_map<Value *, bool> unsafePointers;
				for (Instruction &Inst: BB) {
					if (includeInst(Inst, unsafePointers, childPointers)) instsToProcess.push_back(&Inst);
				}
			}

			for (Instruction *inst_ptr: instsToProcess) {
				processInst(*inst_ptr, Builder, nullBB);
			}

			// errs() << F << "\n";
			return false;
		}

		bool includeInst(Instruction &Inst, std::unordered_map<Value *, bool> &unsafePointers,
						 std::unordered_map<Value *, std::unordered_set<Value *>> &childPointers) {
			Value *basePointer = getPointerOperand(&Inst);
			bool isPresent = unsafePointers.find(basePointer) != unsafePointers.end();
			bool addNullCheck = isPresent? unsafePointers.at(basePointer): true;
			unsafePointers[basePointer] = false;
			if (dyn_cast<StoreInst>(&Inst)) {
				makeUnsafe(unsafePointers, childPointers, basePointer);
				return addNullCheck;
			}
			if (auto *inst = dyn_cast<LoadInst>(&Inst)) {
				Value *value = inst->getOperand(0);
				childPointers[basePointer].insert(value);
				unsafePointers[value] = false;
				return addNullCheck;
			}
			if (auto *inst = dyn_cast<GetElementPtrInst>(&Inst)) {
				// Value *value = inst->getOperand(0);
				// childPointers[basePointer].insert(value);
				return addNullCheck;
			}
			if (auto *inst = dyn_cast<CallInst>(&Inst)) {
				if (inst->isIndirectCall()) {
					Value *value = inst->getCalledOperand();
					addNullCheck = (unsafePointers.find(value) != unsafePointers.end())
						? unsafePointers.at(value): true;
					unsafePointers[value] = false;
					return addNullCheck;
				}
			}
			return false;
		}

		void makeUnsafe(std::unordered_map<Value *, bool> &unsafePointers,
						std::unordered_map<Value *, std::unordered_set<Value *>> &childPointers,
						Value *basePointer) {
			std::queue<Value *> q;
			q.push(basePointer);
			while (!q.empty()) {
				Value *ptr = q.front();
				q.pop();
				unsafePointers[ptr] = true;
				for (Value *child: childPointers[ptr]) {
					if (!unsafePointers[child])
						q.push(child);
				}
			}
		}
		
		void processInst(Instruction &Inst, IRBuilder<> &Builder, BasicBlock* nullBB) {
			BasicBlock *currentBB = Inst.getParent();
		    if (auto *inst = dyn_cast<CallInst>(&Inst)) {
				if (inst->isIndirectCall()) {
					BasicBlock *notNullBB = currentBB->splitBasicBlock(&Inst, "NotNullBB");
					currentBB->getTerminator()->eraseFromParent();
					Builder.SetInsertPoint(currentBB);
					Value *fnPtr = inst->getCalledOperand();
					Constant *nullValue = Constant::getNullValue(fnPtr->getType());
					Value *icmpEqInst = Builder.CreateICmpEQ(fnPtr, nullValue, "IsNull");
					Builder.CreateCondBr(icmpEqInst, nullBB, notNullBB);
				}
				return;
			}
			Value *basePointer = getPointerOperand(&Inst);
			BasicBlock *notNullBB = currentBB->splitBasicBlock(&Inst, "NotNullBB");
			currentBB->getTerminator()->eraseFromParent();
			Builder.SetInsertPoint(currentBB);
			Type *basePointerType = basePointer->getType();
			PointerType *pointerType = PointerType::get(basePointerType->getPointerElementType(),
														basePointerType->getPointerAddressSpace());
			Constant *nullValue = ConstantPointerNull::get(pointerType);
			Value *icmpEqInst = Builder.CreateICmpEQ(basePointer, nullValue, "IsNull");
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
