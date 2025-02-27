#pragma once

#include "AnalysisPass.hpp"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <random>
#include <set>
#include <unordered_set>

using namespace llvm;

class FunctionInliner : public llvm::PassInfoMixin<FunctionInliner>
{
public:
  explicit FunctionInliner(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  // 函数调用图
  std::unordered_map<Function*, std::unordered_set<Function*>> callGraph;
  // 记录函数调用是否有环
  std::unordered_map<Function*, bool> isCalledFuncHasLoop;

  // /**
  //  * DFS判断从某调用点开始是否有环
  //  */
  // bool hasCallLoop(Function* callOne, std::unordered_set<Function*>& visited)
  // {
  //   if (callGraph.find(callOne) == callGraph.end())
  //     return false;
  //   visited.insert(callOne);
  //   for (auto func : callGraph[callOne]) {
  //     // 已经被访问过
  //     if (visited.find(func) != visited.end())
  //       return true;
  //     // 继续深度优先搜索，如果有一条调用链有环则返回
  //     if (hasCallLoop(func, visited))
  //       return true;
  //   }
  //   return false;
  // }

  /**
   * 广搜判断调用点是否包含在某个环中
   * 注意不是简单地判断是否有环，因为从调用点出发就算有环，只要调用点不在环内依然可以内联
   */
  bool hasCallLoop(Function* callOne)
  {
    if (isCalledFuncHasLoop.find(callOne) != isCalledFuncHasLoop.end())
      return isCalledFuncHasLoop[callOne];
    // 如果是外部函数则默认没有环
    if (callOne->isDeclaration()) {
      isCalledFuncHasLoop[callOne] = false;
      return false;
    }

    // 得到从当前函数出发广度优先搜索，如果回到callOne则说明从调用点出发有环回到调用点
    std::unordered_set<Function*> visited;
    std::queue<Function*> q;
    q.push(callOne);
    while (!q.empty()) {
      Function* cur = q.front();
      q.pop();
      for (auto callOtherIter = callGraph[cur].begin();
           callOtherIter != callGraph[cur].end();
           callOtherIter++) {
        if (*callOtherIter == callOne) {
          isCalledFuncHasLoop[callOne] = true;
          return true;
        }
        if (visited.find(*callOtherIter) != visited.end())
          continue;
        visited.insert(*callOtherIter);
        q.push(*callOtherIter);
      }
    }
    isCalledFuncHasLoop[callOne] = false;
    return false;
  }

  std::unordered_map<Function*, bool> suitableToInlineMap;
  /**
   * 判断某个函数是否适合被展开
   *    1.有循环则不适合展开
   *    2.这里有条件跳转也展示不展开
   */
  bool isSuitableToInline(Function* func)
  {
    if (suitableToInlineMap.find(func) != suitableToInlineMap.end())
      return suitableToInlineMap[func];
    if (func->isDeclaration()) {
      suitableToInlineMap[func] = false;
      return false;
    }
    for (auto& block : *func) {
      for (auto& inst : block) {
        if (auto brInst = dyn_cast<BranchInst>(&inst)) {
          suitableToInlineMap[func] = false;
          return false;
        }
      }
    }
    suitableToInlineMap[func] = true;
    return true;
  }

  std::unordered_set<int> existedLabels;
  /**
   * 生成一段1-10000之间的随机数，且保证不重复
   * 这是为了替换Call插入一个新的函数基本块的时候使得基本块不重名
   */
  int generateRandomNumber(int min = 10, int max = 10000)
  {
    std::random_device seed;
    std::ranlux48 engine(seed());
    std::uniform_int_distribution<> distribution(min, max);
    int randomNum = distribution(engine);
    while (existedLabels.find(randomNum) != existedLabels.end())
      randomNum = distribution(engine);
    existedLabels.insert(randomNum);
    return randomNum;
  }

  /**
   * 获取一个Func中的所有Alloca指令变量名和基本块名称，判断在内联的时候是否需要重命名
   */
  void getAllLocalVarName(Function* func,
                          std::unordered_set<std::string>& localVarNames)
  {
    for (auto& inst : func->getEntryBlock()) {
      if (auto allocaInst = dyn_cast<AllocaInst>(&inst)) {
        localVarNames.insert(allocaInst->getName().str());
      }
    }
  }
  /**
   * 如果传入的名称在localVarNames中出现，则添加一段随机数后返回
   * 使用的地方包括函数内局部变量重命名、函数内标签重命名
   */
  std::string renameLocalLabel(std::string oldName,
                               std::unordered_set<std::string>& localLabelNames)
  {
    if (localLabelNames.find(oldName) != localLabelNames.end()) {
      do {
        oldName = oldName + std::to_string(generateRandomNumber());
      } while (localLabelNames.find(oldName) != localLabelNames.end());
    }
    localLabelNames.insert(oldName);
    return oldName;
  }
  std::unique_ptr<llvm::IRBuilder<>> mBuilder;

  /**
   * 如果能内联，将函数内指令复制到CallInst指令之后，返回true，否则返回false
   */
  bool processCallInst(CallInst* callInst, Function* func)
  {
    Function* callee = callInst->getCalledFunction();
    // 有环则不进行内联
    if (!isSuitableToInline(callee) || hasCallLoop(callee))
      return false;

    // 建立从形参到实参的映射
    std::unordered_map<Value*, Value*> parameter2argument;
    unsigned int callArgumentIdx = 0;
    for (auto parameterIter = callee->arg_begin();
         parameterIter != callee->arg_end();
         parameterIter++) {
      parameter2argument[parameterIter] =
        callInst->getArgOperand(callArgumentIdx);
      callArgumentIdx++;
    }

    // 先获得当前函数的所有Alloca指令，以判断callee中的Alloca得到的局部变量是否需要重命名
    std::unordered_set<std::string> localLabelNames;
    getAllLocalVarName(func, localLabelNames);

    // 在移动过程中应该建立寄存器映射，就是原寄存器(callee中)到新寄存器(内联后在caller中)的映射，其中包含了形参到实参的映射
    std::unordered_map<Value*, Value*> valueMap;
    for (auto it = parameter2argument.begin(); it != parameter2argument.end();
         it++)
      valueMap[it->first] = it->second;

    // 用于创建指令的Builder，并设置插入点在callInst之后的指令之前
    auto insertPoint = callInst->getNextNode();
    mBuilder->SetInsertPoint(insertPoint);

    // 将指令复制到当前基本块中CallInst之后
    for (auto& calleeBlock : *callee) {
      for (auto& calleeInst : calleeBlock) {
        if (auto calleeAllocaInst = dyn_cast<AllocaInst>(&calleeInst)) {
          auto cpyAllocaInst =
            mBuilder->CreateAlloca(calleeAllocaInst->getAllocatedType());
          cpyAllocaInst->insertBefore(&*func->getEntryBlock().begin());
          valueMap[calleeAllocaInst] = cpyAllocaInst;
        }
        // ReturnInst需要将caller中所有用到返回值的地方进行替换，这里不需要进行任何处理
        else if (auto calleeRetInst = dyn_cast<ReturnInst>(&calleeInst)) {
          if (calleeRetInst->getReturnValue() != nullptr)
            callInst->replaceAllUsesWith(
              valueMap[calleeRetInst->getReturnValue()]);
        }
        // GEP指令需要判断操作数是否来自形参，以及替换操作数
        else if (auto calleeGEPInst =
                   dyn_cast<GetElementPtrInst>(&calleeInst)) {
          // 判断取值地址是否需要替换
          Value* accessPtr = calleeGEPInst->getPointerOperand();
          if (valueMap.find(accessPtr) != valueMap.end())
            accessPtr = valueMap[accessPtr];
          // 获取索引列表
          std::vector<Value*> idxList;
          for (unsigned int idx = 1; idx < calleeGEPInst->getNumOperands();
               idx++) {
            auto operand = calleeGEPInst->getOperand(idx);
            if (valueMap.find(operand) != valueMap.end())
              idxList.push_back(valueMap[operand]);
            else
              idxList.push_back(operand);
          }
          // 复制指令
          auto cpyGEPInst =
            mBuilder->CreateGEP(calleeGEPInst->getType(), accessPtr, idxList);
          // 保存映射
          valueMap[calleeGEPInst] = cpyGEPInst;
        }
        // StoreInst需要判断Store地址是否来自参数
        else if (auto calleeStoreInst = dyn_cast<StoreInst>(&calleeInst)) {
          auto storeTo = calleeStoreInst->getPointerOperand();
          auto storeValue = calleeStoreInst->getValueOperand();
          if (valueMap.find(storeTo) != valueMap.end())
            storeTo = valueMap[storeTo];
          if (valueMap.find(storeValue) != valueMap.end())
            storeValue = valueMap[storeValue];
          mBuilder->CreateStore(storeValue, storeTo);
        }
        //
        else if (auto calleeLoadInst = dyn_cast<LoadInst>(&calleeInst)) {
          auto loadFrom = calleeLoadInst->getPointerOperand();
          if (valueMap.find(loadFrom) != valueMap.end())
            loadFrom = valueMap[loadFrom];
          auto cpyLoadInst =
            mBuilder->CreateLoad(calleeLoadInst->getAccessType(), loadFrom);
          valueMap[calleeLoadInst] = cpyLoadInst;
        }
        //
        else if (auto calleeBinOp = dyn_cast<BinaryOperator>(&calleeInst)) {
          auto lhsOperand = calleeBinOp->getOperand(0);
          auto rhsOperand = calleeBinOp->getOperand(1);
          if (valueMap.find(lhsOperand) != valueMap.end())
            lhsOperand = valueMap[lhsOperand];
          if (valueMap.find(rhsOperand) != valueMap.end())
            rhsOperand = valueMap[rhsOperand];
          auto cpyBinOp = BinaryOperator::Create(
            calleeBinOp->getOpcode(), lhsOperand, rhsOperand);
          cpyBinOp->insertBefore(insertPoint);
          valueMap[calleeBinOp] = cpyBinOp;
        }
        //
        else if (auto calleeCallInst = dyn_cast<CallInst>(&calleeInst)) {
          // 获取CallInst指令参数
          std::vector<Value*> args;
          for (auto argIter = calleeCallInst->arg_begin();
               argIter != calleeCallInst->arg_end();
               argIter++) {
            auto arg = &*argIter;
            if (valueMap.find(arg->get()) != valueMap.end())
              args.push_back(valueMap[arg->get()]);
            else
              args.push_back(arg->get());
          }
          auto cpyCallInst =
            mBuilder->CreateCall(calleeCallInst->getCalledFunction(), args);
          valueMap[calleeCallInst] = cpyCallInst;
        }
        //
        else if (auto calleeSExtInst = dyn_cast<SExtInst>(&calleeInst)) {
          auto SExtValue = calleeSExtInst->getOperand(0);
          if (valueMap.find(SExtValue) != valueMap.end())
            SExtValue = valueMap[SExtValue];
          auto cpySExtInst =
            mBuilder->CreateSExt(SExtValue, calleeSExtInst->getDestTy());
          valueMap[calleeSExtInst] = cpySExtInst;
        }
        //
        else {
          mOut << "Unhandle Instruction Type!!!\n";
          abort();
        }
      }
    }
    return true;
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    unsigned int inlineCount = 0;
    // 获取IRBuilder
    mBuilder = std::make_unique<IRBuilder<>>(mod.getContext());

    // 获取函数调用图
    callGraph = mam.getResult<CallGraphAnalysis>(mod);
    std::vector<Instruction*> instToErase;
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto instIter = block.begin(); instIter != block.end();
             instIter++) {
          if (auto callInst = dyn_cast<CallInst>(&*instIter)) {
            if (processCallInst(callInst, &func))
              instToErase.push_back(callInst);
          }
        }
      }
    }
    // 移除无用的CallInst
    inlineCount += instToErase.size();
    for (auto inst : instToErase)
      inst->eraseFromParent();
    // 删除没有引用的Function
    std::vector<Function*> funcToErase;
    for (auto& func : mod) {
      // 注意别把main函数给删了
      if (func.user_empty() && func.getName() != "main")
        funcToErase.push_back(&func);
    }
    for (auto func : funcToErase)
      func->eraseFromParent();

    mOut << "Function Inlinier running...\nTo inline " << inlineCount
         << " call instruction\n";
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};