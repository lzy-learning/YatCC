#pragma once

#include "AnalysisPass.hpp"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/CodeGen/ReachingDefAnalysis.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <unordered_set>
// #define LICM_DEBUG

using namespace llvm;

class LoopInvariantCodeMotion
  : public llvm::PassInfoMixin<LoopInvariantCodeMotion>
{
public:
  explicit LoopInvariantCodeMotion(llvm::raw_ostream& out)
    : mOut(out)
  {
  }
  // 保存每个循环中的循环不变量
  // !!!注意别用unordered_set，因为在外提的时候需要保持顺序不变
  std::unordered_map<Loop*, std::vector<Instruction*>> allLIC;
  // 记录某个循环中某条指令是否是循环不变量，如果一条指令以不是循环不变量为参数，则它一定不是循环不变量
  std::unordered_map<Loop*, std::unordered_set<Instruction*>> notLIC;

  // 引用-定值链
  std::unordered_map<LoadInst*, std::vector<StoreInst*>> load2Stores;
  // 是否是幂等函数
  std::unordered_map<Function*, bool> isIdemFuncs;
  // 请一定保证在函数模块开始判断的时候生成函数模块的支配树：domTree.recalculate(func);
  DominatorTree domTree;

  /**
   * 判断函数调用是否是幂等的，也就是多次调用不影响其他变量（全局变量），并且每次调用都是一样的结果
   * 用一个map保存函数调用的结果，这样不用进行多次判断
   */
  bool isCallIdempotent(CallInst* callInst)
  {
    bool isIdempotent = true;
    /**
     * 首先判断参数是否是全局变量、数组类型，因为这两个类型在函数中很可能改变
     */
    for (auto arg = callInst->arg_begin(); arg != callInst->arg_end(); ++arg) {
      // 全局变量
      if (auto globalVar = dyn_cast<GlobalVariable>(arg)) {
        isIdempotent = false;
        break;
      }
      // 数组
      else if (auto getElePtr = dyn_cast<GetElementPtrInst>(arg)) {
        isIdempotent = false;
        break;
      }
    }
    if (!isIdempotent)
      return false;

    Function* callFunc = callInst->getCalledFunction();

    if (isIdemFuncs.find(callFunc) != isIdemFuncs.end())
      return isIdemFuncs[callFunc];
    // 如果函数是在外部实现的，就直接返回true，只需要判断参数通过即可
    if (callFunc->isDeclaration())
      return true;

    isIdemFuncs[callFunc] = true; // 暂且认为递归函数是幂等的
    /**
     * 然后遍历Function中的所有指令，如果其中：
     *    （1）有对全局变量的操作
     *    （2）有对其他函数的调用，且这些函数不是幂等的
     * 则认为当前函数不是幂等的
     */
    for (auto& block : *callFunc) {
      for (auto& inst : block) {

        // 这里看到数组操作先直接判断为不是幂等先
        if (auto getElePtrInst = dyn_cast<GetElementPtrInst>(&inst)) {
          isIdempotent = false;
          break;
        }
        // store操作的是数组和全局变量
        else if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
          if (dyn_cast<GlobalVariable>(storeInst->getPointerOperand())) {
            isIdempotent = false;
            break;
          } else if (dyn_cast<GetElementPtrInst>(
                       storeInst->getPointerOperand())) {
            isIdempotent = false;
            break;
          }
        }
        // load操作的是数组（因为数组可能被外部改变）
        else if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
          if (dyn_cast<GetElementPtrInst>(loadInst->getPointerOperand())) {
            isIdempotent = false;
            break;
          }
        }
        // 内部函数调用则递归判断该函数是否是幂等的
        else if (auto callOtherFuncInst = dyn_cast<CallInst>(&inst)) {
          if (isIdemFuncs.find(callOtherFuncInst->getCalledFunction()) !=
                isIdemFuncs.end() &&
              !isIdemFuncs[callOtherFuncInst->getCalledFunction()]) {
            isIdempotent = false;
            break;
          } else {
            isIdempotent = isCallIdempotent(callOtherFuncInst);
            if (!isIdempotent)
              break;
          }
        }
      }
      if (!isIdempotent)
        break;
    }
    isIdemFuncs[callFunc] = isIdempotent;
    return isIdempotent;
  }
  /**
   * 判断变量（有可能是匿名寄存器、变量以及数组类型）是否在参数列表中
   */
  bool isArgInCall(Value* value, CallInst* callInst)
  {
    for (auto arg = callInst->arg_begin(); arg != callInst->arg_end(); arg++) {
      if (arg->get() == value)
        return true;
      auto eleGetPtrInst = dyn_cast<GetElementPtrInst>(arg);
      if (eleGetPtrInst && eleGetPtrInst->getPointerOperand() == value)
        return true;
    }
    return false;
  }

  /**
   * 判断一个变量在函数中是否被store，主要用于isChangedInBlock函数中CallInst的判断
   */
  bool isChangedInCallFunc(Value* loadFrom, CallInst* callInst)
  {
    // 如果变量v直接作为参数被传递到函数中，则认为它会被修改
    if (isArgInCall(loadFrom, callInst))
      return true;
    // 如果load的是一个全局变量，还需要判断该全局变量在call的函数中是否被更改（这里不是和判断幂等性冲突了？？？）
    auto globalVar = dyn_cast<GlobalVariable>(loadFrom);
    if (globalVar) {
      Function* callFunc = callInst->getCalledFunction();
      for (auto& callFuncBlock : *callFunc) {
        for (auto& callFuncInst : callFuncBlock) {
          // 遇到store指令则判断指令地址是否是globalVar
          if (auto storeInst = dyn_cast<StoreInst>(&callFuncInst)) {
            if (globalVar == storeInst->getPointerOperand())
              return true;
            // 也可能是全局数组，所以得麻烦地判断数组基地址
            auto getElePtrInst =
              dyn_cast<GetElementPtrInst>(storeInst->getPointerOperand());
            if (getElePtrInst &&
                getElePtrInst->getPointerOperand() == globalVar)
              return true;
          }
          // 遇到Call指令则递归判断
          else if (auto callCallInst = dyn_cast<CallInst>(&callFuncInst)) {
            // 如果是递归调用自己则继续判断指令
            if (callCallInst->getCalledFunction() == callFunc)
              continue;
            if (isArgInCall(loadFrom, callInst))
              return true;
            if (isChangedInCallFunc(loadFrom, callCallInst))
              return true;
          }
        }
      }
    }
    return false;
  }

  /**
   * 判断Load指令的地址在Loop中是否会被Store
   */
  bool isChangedInBlock(LoadInst* loadInst, Loop* curLoop)
  {
    std::vector<BasicBlock*> loopBlocks = curLoop->getBlocksVector();
    auto loadFrom = loadInst->getPointerOperand();
    // 如果是从数组从取，就一直向上遍历直到找到数组的地址（Alloca返回值）
    while (dyn_cast<GetElementPtrInst>(loadFrom))
      loadFrom = dyn_cast<GetElementPtrInst>(loadFrom)->getPointerOperand();

    for (auto loopBlock : loopBlocks) {
      for (auto& loopInst : *loopBlock) {
        // 如果是store指令则判断store的地址是否是Load的地址
        if (auto storeInst = dyn_cast<StoreInst>(&loopInst)) {
          // 往一个变量中store
          if (storeInst->getPointerOperand() == loadFrom)
            return true;
          // 往数组中store，需要递归向上判断
          auto getElePtrInst =
            dyn_cast<GetElementPtrInst>(storeInst->getPointerOperand());
          while (getElePtrInst) {
            if (getElePtrInst->getPointerOperand() == loadFrom)
              return true;
            getElePtrInst =
              dyn_cast<GetElementPtrInst>(getElePtrInst->getPointerOperand());
          }
        }
        // 如果是Call指令
        else if (auto callInst = dyn_cast<CallInst>(&loopInst)) {
          if (isChangedInCallFunc(loadFrom, callInst))
            return true;
        }
      }
    }

    return false;
  }

  /**
   * 对于GetElementPtrInst，我们需要递归判断，因为可能有如下情况（二维数组）：
   * %6 = load ptr, ptr %C.addr, align 8
   * %9 = getelementptr inbounds [1024 x i32], ptr %6, i64 %8
   * %10 = getelementptr inbounds [1024 x i32], ptr %9, i64 0, i64 0
   * 可以外提则返回true
   */
  bool handleGetElementPtrInst(GetElementPtrInst* getElePtrInst, Loop* curLoop)
  {
    Value* op0 = getElePtrInst->getPointerOperand();
    // 二维数组则递归
    if (auto subGetElePtrInst = dyn_cast<GetElementPtrInst>(op0)) {
      if (!handleGetElementPtrInst(subGetElePtrInst, curLoop))
        return false;
    }
    // 从1遍历起，上面判断过地址操作数了
    for (int i = 1; i < getElePtrInst->getNumOperands(); i++) {
      if (!isOperandComputedOutsideLoop(getElePtrInst->getOperand(i), curLoop))
        return false;
    }
    return true;
  }
  /**
   * 判断某变量或者匿名寄存器的操作是否在集合中
   * 比如%mul = mul nsw i32 %i.0,
   * %1，需要找到$i.0所有相关指令，找到%1所有相关指令
   *   如果相关指令都在循环外，或者在循环内但是已经被标记为循环不变量
   */
  bool isOperandComputedOutsideLoop(Value* operand, Loop* curLoop)
  {
    Instruction* inst = dyn_cast<Instruction>(operand);
    // Value就是常数ConstantInt或者变量的时候直接返回true
    // 因为在isLoopInvariantCode函数中会限制可以外提的类型，所以对PHINode返回true也不会出错
    if (!inst || dyn_cast<AllocaInst>(operand))
      return true;
    if (dyn_cast<BranchInst>(operand) ||
        (dyn_cast<PHINode>(operand) && curLoop->contains(inst->getParent())) ||
        dyn_cast<SExtInst>(operand))
      return false;

    // 如果对应指令不在循环包含的所有基本块中
    if (!curLoop->contains(inst->getParent()))
      return true;

    // 如果对应指令已经被标记为循环不变量
    if (std::find(allLIC[curLoop].begin(), allLIC[curLoop].end(), inst) !=
        allLIC[curLoop].end())
      return true;

    // 如果对应指令在当前循环已经被标记为非循环不变量
    if (notLIC[curLoop].find(inst) != notLIC[curLoop].end())
      return false;

    // 如果是icmp指令且使用者是br指令
    if (auto icmpInst = dyn_cast<ICmpInst>(inst)) {
      for (auto user : icmpInst->users()) {
        if (dyn_cast<BranchInst>(user))
          return false;
      }
    }

    // 遍历当前指令所有操作数
    int numOperand = inst->getNumOperands();
    bool computedOutsideLoop = true;
    for (int i = 0; i < numOperand; i++) {
      Instruction* operandInst = dyn_cast<Instruction>(inst->getOperand(i));
      // 操作数来源不是指令，有可能是常数或者是icmp指令比大小之类的
      if (!operandInst)
        continue;
      // 如果是操作数来源是二元操作则递归判断
      if (auto binOp = dyn_cast<BinaryOperator>(operandInst)) {
        if (!isOperandComputedOutsideLoop(inst->getOperand(i), curLoop)) {
          computedOutsideLoop = false;
          break;
        }
      }
      // 如果是Load指令
      else if (auto loadInst = dyn_cast<LoadInst>(operandInst)) {
        // 如果操作对应的是数组取值，则递归判断GetElementIPtrInst是否可以外提
        if (dyn_cast<GetElementPtrInst>(loadInst->getPointerOperand())) {
          if (!isOperandComputedOutsideLoop(loadInst->getPointerOperand(),
                                            curLoop)) {
            computedOutsideLoop = false;
            break;
          }
        }
        // 根据引用-定值链判断循环内是否有对应定值
        // for (auto correspondingStoreInst : load2Stores[loadInst]) {
        //   if (curLoop->contains(correspondingStoreInst->getParent())) {
        //     computedOutsideLoop = false;
        //     break;
        //   }
        // }
        // 遍历循环中所有指令，判断是否有load对应地址的store
        if (isChangedInBlock(loadInst, curLoop)) {
          computedOutsideLoop = false;
          break;
        }
      }
      // 如果是Store指令
      else if (auto storeInst = dyn_cast<StoreInst>(operandInst)) {
        // 判断Store操作的ValueOperand是否是循环不变的
        if (!isOperandComputedOutsideLoop(storeInst->getValueOperand(),
                                          curLoop)) {
          computedOutsideLoop = false;
          break;
        }
      }
      // 如果是GetElementPtrInst也就是数组操作指令
      else if (auto getElePtrInst = dyn_cast<GetElementPtrInst>(operandInst)) {
#ifdef LICM_DEBUG
        getElePtrInst->print(mOut);
        mOut << "\n";
#endif
        // 判断该指令所有操作数来源是否在循环中，如果在循环中是否已经被标记为循环不变量
        if (!handleGetElementPtrInst(getElePtrInst, curLoop)) {
          computedOutsideLoop = false;
          break;
        }
      }
      // 如果是CallInst
      else if (auto callInst = dyn_cast<CallInst>(inst)) {
        // 遍历其所有参数，幂等性这里不用判断，因为isLoopInvariant中会进行判断
        for (int i = 0; i < callInst->getNumOperands(); i++) {
          if (!isOperandComputedOutsideLoop(callInst->getArgOperand(i),
                                            curLoop)) {
            computedOutsideLoop = false;
            break;
          }
        }
      }
      // 如果是PHI则不能外提
      else if (auto phiNode = dyn_cast<PHINode>(inst)) {
        computedOutsideLoop = false;
        break;
      }
      if (!computedOutsideLoop)
        break;
    }
    return computedOutsideLoop;
  }

  /**
   * 判断某指令是否是循环不变量，理论上有下面条件
   *   1.先前没有标记为循环不变量，且所有运算分量为常数
   *   2.从操作数出发找出所有相应指令操作，如果所有指令操作都在环外或者被标记为了循环不变量
   */
  bool isLoopInvariant(Instruction* inst, Loop* curLoop)
  {
#ifdef LICM_DEBUG
    if (dyn_cast<CallInst>(inst)) {
      mOut << "CallInst\n";
    } else {
      inst->print(mOut);
      mOut << "\n";
    }
#endif
    /**
     * 1.根据指令类型判断其是否可能被外提
     */
    auto opCode = inst->getOpcode();
    switch (opCode) {
      case Instruction::PHI:
      case Instruction::Br:
      case Instruction::Ret:
      case Instruction::ICmp:
      case Instruction::SExt:
        return false;
      case Instruction::Store: {
        /**
         * 2.判断指令所在基本块是否是循环所有退出块的支配结点
         *    保证循环退出时指令一定执行
         */
        // 接着判断指令所在基本块是否是所有出口基本块的支配结点
        SmallVector<BasicBlock*> exitingBlocks;
        curLoop->getExitingBlocks(exitingBlocks);
        for (auto exitBlock : exitingBlocks) {
          if (!domTree.dominates(inst->getParent(), exitBlock))
            return false;
        }
      } break;
      case Instruction::Call: {
        /**
         * 判断函数调用是否幂等以及是否是在外部实现
         */
        if (!isCallIdempotent(dyn_cast<CallInst>(inst)))
          return false;
        auto callInst = dyn_cast<CallInst>(inst);
        if (callInst->getCalledFunction()->isDeclaration())
          return false;
      }
      default:
        break;
    }

    /**
     * 3.根据指令类型及其参数逐个判断其是否可以被外提
     */
    if (auto binOp = dyn_cast<BinaryOperator>(inst)) {
      int numOperand = inst->getNumOperands();
      for (int i = 0; i < numOperand; i++) {
        if (!isOperandComputedOutsideLoop(inst->getOperand(i), curLoop))
          return false;
      }
    }
    // 如果是Load指令
    else if (auto loadInst = dyn_cast<LoadInst>(inst)) {
      // 如果操作对应的是数组取值，则判断GetElementIPtrInst是否可以外提
      if (dyn_cast<GetElementPtrInst>(loadInst->getPointerOperand())) {
        if (!isOperandComputedOutsideLoop(loadInst->getPointerOperand(),
                                          curLoop))
          return false;
      }
      // 遍历循环中所有指令，判断是否有load对应地址的store
      if (isChangedInBlock(loadInst, curLoop))
        return false;
    }
    // 如果是Store指令
    else if (auto storeInst = dyn_cast<StoreInst>(inst)) {
#ifdef LICM_DEBUG
      storeInst->print(mOut);
      mOut << "\n";
#endif
      // 判断Store操作的ValueOperand是否是循环不变的
      if (!isOperandComputedOutsideLoop(storeInst->getValueOperand(),
                                        curLoop) ||
          !isOperandComputedOutsideLoop(storeInst->getPointerOperand(),
                                        curLoop))
        return false;
    }
    // 如果是GetElementPtrInst也就是数组操作指令
    else if (auto getElePtrInst = dyn_cast<GetElementPtrInst>(inst)) {
      // 判断该指令所有操作数来源是否在循环中，如果在循环中是否已经被标记为循环不变量
      if (!handleGetElementPtrInst(getElePtrInst, curLoop))
        return false;
    }
    // 如果是CallInst
    else if (auto callInst = dyn_cast<CallInst>(inst)) {
      // 遍历其所有参数，幂等性这里不用判断，前面代码中已进行判断
      for (auto arg = callInst->arg_begin(); arg != callInst->arg_end(); arg++)
        if (!isOperandComputedOutsideLoop(arg->get(), curLoop))
          return false;
    }
    return true;
  }

  /**
   * 递归处理循环，因为可能有内循环
   * 外循环不需要处理内循环的块，所以有visited数组记录访问过的基本块
   */
  void processLoop(Loop* loop, std::unordered_set<BasicBlock*>& visited)
  {
    const std::vector<Loop*>& subLoops = loop->getSubLoops();
    mOut << subLoops.size() << "\n";
    for (auto subLoop : subLoops) {
#ifdef LICM_DEBUG
      subLoop->print(mOut);
#endif
      processLoop(subLoop, visited);
    }

    for (auto block : loop->getBlocksVector()) {
      // 不处理内循环的基本块
      if (visited.find(block) != visited.end())
        continue;

#ifdef LICM_DEBUG
      block->print(mOut);
      mOut << "\n";
#endif

      for (auto& inst : *block) {
        if (isLoopInvariant(&inst, loop))
          allLIC[loop].push_back(&inst);
        else
          notLIC[loop].insert(&inst);
      }
      visited.insert(block);
    }
    // 下面进行不变量外提，将不变量移到preheader中
    BasicBlock* preheader = loop->getLoopPreheader();
    for (auto inst : allLIC[loop]) {

#ifdef LICM_DEBUG
      if (dyn_cast<CallInst>(inst)) {
        mOut << "CallInst\n";
      } else {
        inst->print(mOut);
        mOut << "\n";
      }
#endif

      inst->moveBefore(preheader->getTerminator());
    }
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    FunctionAnalysisManager fam;
    PassBuilder pb;
    fam.registerPass([&] { return LoopAnalysis(); });
    pb.registerFunctionAnalyses(fam);

    load2Stores = mam.getResult<Load2StoreAnalysis>(mod);

    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      LoopInfo& loopInfos = fam.getResult<LoopAnalysis>(func);
      domTree.recalculate(func);
      std::unordered_set<BasicBlock*> visited;
      // 避免重复处理内循环
      for (Loop* loop : loopInfos) {
        if (allLIC.find(loop) != allLIC.end())
          continue;
        processLoop(loop, visited);
      }
    }
    unsigned int hoistCount = 0;
    for (auto it = allLIC.begin(); it != allLIC.end(); it++)
      hoistCount += it->second.size();
    mOut << "LoopInvariantCodeMotion running...\nHosited time: " << hoistCount
         << "\n";
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};