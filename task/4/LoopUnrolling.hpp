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
#include <queue>
#include <random>
#include <set>
#include <unordered_set>
// #define LOOPUNROLL_DEBUG

using namespace llvm;

class LoopUnrolling : public llvm::PassInfoMixin<LoopUnrolling>
{
public:
  explicit LoopUnrolling(llvm::raw_ostream& out)
    : mOut(out)
  {
  }
  // 循环中的自增语句、每次自增的量和初始值、当前循环变量
  BinaryOperator* incrementInst;
  ConstantInt *increment, *initValue;
  int curTripCount;

  /**
   * 获取循环跳转次数，循环while(i<n)可以完全展开需要两个条件
   *    1.循环终止条件是不变的，也就是n在循环体中不能被赋值
   *    2.循环变量i每次迭代增加的量是知道的，且i的更改所在基本块必须支配
   * 跳转语句中循环变量一般从PHI结点获得，来源是preheader和latch block
   * 而结束标志一般是到达某个常数
   */
  int getLoopTripCount(Loop* loop)
  {
    BasicBlock* loopHeader = loop->getHeader();
#ifdef LOOPUNROLL_DEBUG
    loopHeader->print(mOut);
#endif

    auto branchInst = dyn_cast<BranchInst>(loopHeader->getTerminator());
    if (!branchInst)
      return -1;
    auto icmpInst = dyn_cast<ICmpInst>(branchInst->getOperand(0));
    if (!icmpInst)
      return -1;

    // 这里仅考虑小于号，且前面操作数是循环变量后面操作数是不变量的情况
    auto phiNode = dyn_cast<PHINode>(icmpInst->getOperand(0));
    ConstantInt* endValue = dyn_cast<ConstantInt>(icmpInst->getOperand(1));
    if (icmpInst->getPredicate() != ICmpInst::ICMP_SLT || !phiNode || !endValue)
      return -1;

#ifdef LOOPUNROLL_DEBUG
    branchInst->print(mOut);
    icmpInst->print(mOut);
    phiNode->print(mOut);
    mOut << "\n";
#endif

    // 找PHI结点的IncomingValue
    // 如果一个来自外部且直接给出初值，一个来自latch block则符合条件
    auto latchBlock = loop->getLoopLatch();
    auto preheaderBlock = loop->getLoopPreheader();
    for (unsigned int idx = 0; idx < phiNode->getNumIncomingValues(); idx++) {
      Value* incomingValue = phiNode->getIncomingValue(idx);
      BasicBlock* incomingBlock = phiNode->getIncomingBlock(idx);
      if (incomingBlock == latchBlock) {
        auto binOp = dyn_cast<BinaryOperator>(incomingValue);
        // %i.0 = phi i32 [ 0, %entry ], [ %add7, %while.end ]
        // ......
        // %add7 = add nsw i32 %i.0, 1
        if (binOp && binOp->getOpcode() == BinaryOperator::Add &&
            binOp->getOperand(0) == phiNode &&
            dyn_cast<ConstantInt>(binOp->getOperand(1))) {
          increment = dyn_cast<ConstantInt>(binOp->getOperand(1));
          incrementInst = binOp;
        }
      } else if (incomingBlock == preheaderBlock) {
        initValue = dyn_cast<ConstantInt>(incomingValue);
      }
    }
    if (!initValue || !increment)
      return -1;
    int tripCount = (endValue->getSExtValue() - initValue->getSExtValue()) /
                    increment->getSExtValue();
    return tripCount;
  }

  /**
   * 判断循环是否可以展开，条件如下：
   *    1.循环退出块只有一个，后继块也只有一个，latch块也只有一个
   *    2.循环没有内循环，且循环迭代次数有限，这里设置不超过80次
   *    3.循环内有其他跳转也不展开
   */
  bool isLoopLegal(Loop* loop)
  {
    if (loop->getExitingBlock() == nullptr || loop->getExitBlock() == nullptr ||
        loop->getLoopLatch() == nullptr || loop->getSubLoops().size() != 0 ||
        loop->getLoopPreheader() == nullptr)
      return false;

    for (auto block : loop->getBlocksVector()) {
      for (auto& inst : *block) {
        if (inst.getOpcode() == Instruction::ICmp && block != loop->getHeader())
          return false;
      }
    }

    auto latchBlock = loop->getLoopLatch();
    for (auto& latchInst : *latchBlock) {
      if (!dyn_cast<LoadInst>(&latchInst) &&
          !dyn_cast<BinaryOperator>(&latchInst) &&
          !dyn_cast<StoreInst>(&latchInst) &&
          !dyn_cast<GetElementPtrInst>(&latchInst) &&
          !dyn_cast<BranchInst>(&latchInst) &&
          !dyn_cast<CallInst>(&latchInst) && !dyn_cast<SExtInst>(&latchInst))
        return false;
    }
    return true;
  }

  /**
   * 复制循环中的所有基本块和对应指令，并返回最后一个复制块
   */
  void loopBodyCopy(BasicBlock* loopHeaderBlock,
                    BasicBlock* loopBodyStartBlock,
                    std::unordered_map<Value*, Value*>& valueMap)
  {
    std::queue<BasicBlock*> worklist;
    worklist.push(loopBodyStartBlock);
    // 用队列保证当循环体有多个基本块的时候也能复制（这里先不考虑多个基本块的情况）
    while (!worklist.empty()) {
      auto loopBlock = worklist.front();
      worklist.pop();
      for (auto& inst : *loopBlock) {
#ifdef LOOPUNROLL_DEBUG
        inst.print(mOut);
        mOut << "\n";
#endif
        // 前面验证是否可以展开时已经保证，循环内不会再有其他条件跳转
        if (auto branchInst = dyn_cast<BranchInst>(&inst)) {
          // if (!branchInst->isUnconditional())
          //   abort();
          // //
          // 需要跳转到其他块，但必定没有产生分支（因为有分支我们不会展开循环）
          // if (branchInst->getSuccessor(0) != loopHeaderBlock) {
          //   auto newBlock = BasicBlock::Create(mBuilder->getContext(),
          //                                      "",
          //                                      loopHeaderBlock->getParent(),
          //                                      cpyBlock);
          //   mBuilder->CreateBr(newBlock);
          //   mBuilder->SetInsertPoint(newBlock);
          //   worklist.push(newBlock);
          // }
          // // 如果是跳转到while.cond，也就是loopHeader块，代表复制结束
          // else
          break;
        }
        // 判断load的地址是否来自PHI结点中的IncomingBlock
        else if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
          auto loadFrom = loadInst->getPointerOperand();
          if (valueMap.find(loadFrom) != valueMap.end())
            loadFrom = valueMap[loadFrom];
          auto cpyLoadInst =
            mBuilder->CreateLoad(loadInst->getAccessType(), loadFrom);
          valueMap[loadInst] = cpyLoadInst;
        }
        //
        else if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
          auto storeTo = storeInst->getPointerOperand();
          auto storeValue = storeInst->getValueOperand();
          if (valueMap.find(storeTo) != valueMap.end())
            storeTo = valueMap[storeTo];
          if (valueMap.find(storeValue) != valueMap.end())
            storeValue = valueMap[storeValue];
          auto cpyStoreInst = mBuilder->CreateStore(storeValue, storeTo);
        }
        //
        else if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
          // 不需要复制循环迭代变量的自增语句，但需要更新循环变量以及valueMap对应值
          if (binOp == incrementInst) {
            valueMap[incrementInst] = ConstantInt::getSigned(
              initValue->getType(),
              initValue->getSExtValue() +
                (curTripCount + 1) * increment->getSExtValue());
            continue;
          }
          auto lhsOperand = binOp->getOperand(0);
          auto rhsOperand = binOp->getOperand(1);
          if (valueMap.find(lhsOperand) != valueMap.end())
            lhsOperand = valueMap[lhsOperand];
          if (valueMap.find(rhsOperand) != valueMap.end())
            rhsOperand = valueMap[rhsOperand];
          Value* cpyBinOp;
          // 如果两个操作数均为常数，那么不需要创建指令
          auto constantLhsOperand = dyn_cast<ConstantInt>(lhsOperand);
          auto constantRhsOperand = dyn_cast<ConstantInt>(rhsOperand);
          if (constantLhsOperand && constantRhsOperand) {
            switch (binOp->getOpcode()) {
              case BinaryOperator::Add:
                cpyBinOp =
                  ConstantInt::getSigned(binOp->getType(),
                                         constantLhsOperand->getSExtValue() +
                                           constantRhsOperand->getSExtValue());
                break;
              case BinaryOperator::Sub:
                cpyBinOp =
                  ConstantInt::getSigned(binOp->getType(),
                                         constantLhsOperand->getSExtValue() -
                                           constantRhsOperand->getSExtValue());
                break;
              case BinaryOperator::Mul:
                cpyBinOp =
                  ConstantInt::getSigned(binOp->getType(),
                                         constantLhsOperand->getSExtValue() *
                                           constantRhsOperand->getSExtValue());
                break;
              case BinaryOperator::SDiv:
                cpyBinOp =
                  ConstantInt::getSigned(binOp->getType(),
                                         constantLhsOperand->getSExtValue() /
                                           constantRhsOperand->getSExtValue());
                break;
              case BinaryOperator::SRem:
                cpyBinOp =
                  ConstantInt::getSigned(binOp->getType(),
                                         constantLhsOperand->getSExtValue() %
                                           constantRhsOperand->getSExtValue());
                break;
              case BinaryOperator::Shl:
                cpyBinOp = ConstantInt::getSigned(
                  binOp->getType(),
                  constantLhsOperand->getSExtValue()
                    << constantRhsOperand->getSExtValue());
                break;
              case BinaryOperator::AShr:
                cpyBinOp =
                  ConstantInt::getSigned(binOp->getType(),
                                         constantLhsOperand->getSExtValue() >>
                                           constantRhsOperand->getSExtValue());
                break;
              default:
                mOut << "Unhandled binary operate\n";
                abort();
                break;
            }

          } else {
            cpyBinOp =
              mBuilder->CreateBinOp(binOp->getOpcode(), lhsOperand, rhsOperand);
          }
          valueMap[binOp] = cpyBinOp;
#ifdef LOOPUNROLL_DEBUG
          // mOut << "copy binary operator: ";
          // cpyBinOp->print(mOut);
          // mOut << " lhs: ";
          // lhsOperand->print(mOut);
          // mOut << " rhs: ";
          // rhsOperand->print(mOut);
          // mOut << "\n";
#endif
        }
        //
        else if (auto GEPInst = dyn_cast<GetElementPtrInst>(&inst)) {
          // 判断取值地址是否需要替换
          Value* accessPtr = GEPInst->getPointerOperand();
          if (valueMap.find(accessPtr) != valueMap.end())
            accessPtr = valueMap[accessPtr];
          // 获取索引列表
          std::vector<Value*> idxList;
          for (unsigned int idx = 1; idx < GEPInst->getNumOperands(); idx++) {
            auto operand = GEPInst->getOperand(idx);
            if (valueMap.find(operand) != valueMap.end())
              idxList.push_back(valueMap[operand]);
            else
              idxList.push_back(operand);
          }
          // 复制指令
          auto cpyGEPInst = mBuilder->CreateGEP(GEPInst->getSourceElementType(),
                                                accessPtr,
                                                idxList,
                                                "",
                                                GEPInst->isInBounds());
#ifdef LOOPUNROLL_DEBUG
          mOut << "copy GEP Inst: ";
          cpyGEPInst->print(mOut);
          mOut << "\n";
#endif
          // 保存映射
          valueMap[GEPInst] = cpyGEPInst;
        }
        //
        else if (auto sextInst = dyn_cast<SExtInst>(&inst)) {
          auto SExtValue = sextInst->getOperand(0);
#ifdef LOOPUNROLL_DEBUG
          mOut << "to copy SExt Inst: ";
          SExtValue->print(mOut);
          mOut << "\n";
#endif
          if (valueMap.find(SExtValue) != valueMap.end())
            SExtValue = valueMap[SExtValue];
          // 循环展开的一大要点就是判断操作数均为常数的情况，因为这种情况下不用创建指令，只需要调用replaceAllUsesWith
          Value* cpySExtInst;
          if (auto constantSExtValue = dyn_cast<ConstantInt>(SExtValue)) {
            cpySExtInst = ConstantInt::get(sextInst->getDestTy(),
                                           constantSExtValue->getSExtValue());
          } else {
            cpySExtInst =
              mBuilder->CreateSExt(SExtValue, sextInst->getDestTy());
          }
          valueMap[sextInst] = cpySExtInst;
        }
        //
        else if (auto callInst = dyn_cast<CallInst>(&inst)) {
          std::vector<Value*> args;
          for (auto argIter = callInst->arg_begin();
               argIter != callInst->arg_end();
               argIter++) {
            auto arg = &*argIter;
            if (valueMap.find(arg->get()) != valueMap.end())
              args.push_back(valueMap[arg->get()]);
            else
              args.push_back(arg->get());
          }
          auto cpyCallInst =
            mBuilder->CreateCall(callInst->getCalledFunction(), args);
          valueMap[callInst] = cpyCallInst;
        }
      }
    }
  }

  /**
   * 此函数中进行循环展开，如果这个循环被展开了，返回true
   */
  bool processLoop(Loop* loop)
  {
#ifdef LOOPUNROLL_DEBUG
    loop->print(mOut);
    mOut << "\n";
#endif
    if (!isLoopLegal(loop))
      return false;

    int tripCount = getLoopTripCount(loop);
    if (tripCount <= 0 || tripCount > 80)
      return false;

    auto loopPreheaderBlock = loop->getLoopPreheader(); // 进入循环的前驱块
    auto loopLatchBlock = loop->getLoopLatch(); // 循环返回到判断条件的块
    auto loopHeaderBlock = loop->getHeader(); // while.cond
    auto condBranchInst =
      dyn_cast<BranchInst>(loopHeaderBlock->getTerminator());
    auto loopExitedBlock = loop->getExitBlock(); // 循环退出之后的第一个块
    auto loopBodyStartBlock = condBranchInst->getSuccessor(0); // while.body

    /**
     * 在循环条件基本块(while.cond)中常常有PHI结点，标明值从preheader来还是latch
     * block来，我们要展开的也是LatchBlock和while.body等在循环体中的块，所以要保
     * 存PHI中有哪些值来自latch循环展开后正确更改PHI结点
     */
    std::unordered_set<Value*> phiSet;
    bool phiFlag = false; // 判断PHI结点之间有没有循环依赖？？？
    for (auto& inst : *loopHeaderBlock) {
      // 下面将连续的PHI结点的来自latch block的值保存
      if (auto phiInst = dyn_cast<PHINode>(&inst)) {
        for (unsigned int phiInstIdx = 0;
             phiInstIdx < phiInst->getNumIncomingValues();
             ++phiInstIdx) {
          if (phiInst->getIncomingBlock(phiInstIdx) == loopLatchBlock)
            phiSet.insert(phiInst->getIncomingValue(phiInstIdx));
        }
      } else
        break; // 注意这个break
    }
    for (auto& inst : *loopHeaderBlock) {
      if (auto phiInst = dyn_cast<PHINode>(&inst)) {
        if (phiSet.find(phiInst) != phiSet.end()) {
          phiFlag = true;
          break;
        }
      } else
        break; // 注意这个break
    }
    if (phiFlag)
      return false;

    /**
     * 下面开始展开
     *   |-----                a <- a0
     *  a <- phi(a0, a1)        header
     *  header |                  |
     *    |    |       -->     a1 <- ..
     *  a0 <- ...               latch
     *  latch  |                  |
     *    |    |                a2 <- a1
     *  exit ---                header'
     *                             |
     *                           exit
     */

    // 在preheader后面创建一个块，用于承接展开循环后的指令
    auto cpyBlock = BasicBlock::Create(mBuilder->getContext(),
                                       "",
                                       loopHeaderBlock->getParent(),
                                       loopHeaderBlock);
    mBuilder->SetInsertPoint(cpyBlock);
    // 设置preheader跳转到复制的块
    auto enterLoopBranchInst =
      dyn_cast<BranchInst>(loopPreheaderBlock->getTerminator());
    enterLoopBranchInst->setSuccessor(0, cpyBlock);

    // 保存从原指令到复制指令的映射，以及PHI结点值的映射关系
    std::unordered_map<Value*, Value*> valueMap;
    // 遍历LoopHeader的所有PHI结点并保存映射，这个映射作为第一个复制块的映射，值来自preheader
    for (auto& inst : *loopHeaderBlock) {
      if (auto phiNode = dyn_cast<PHINode>(&inst)) {
        for (unsigned int idx = 0; idx < phiNode->getNumIncomingValues();
             idx++) {
          auto incomingBlock = phiNode->getIncomingBlock(idx);
          // 注意这里选择来源是preheader块
          if (incomingBlock == loopPreheaderBlock) {
            valueMap[phiNode] = phiNode->getIncomingValue(idx);
            break;
          }
        }
      } else
        break;
    }
    // 复制tripCount次
    for (curTripCount = 0; curTripCount < tripCount; curTripCount++) {
#ifdef LOOPUNROLL_DEBUG
      // 打印valueMap进行观察
      mOut << "===========cur Loop: " << curTripCount << "; valueMap:\n";
      for (auto it = valueMap.begin(); it != valueMap.end(); it++) {
        it->first->print(mOut);
        it->second->print(mOut);
        mOut << "\n";
      }
      mOut << "=================================================\n";
#endif
      // 注意别忘了在loopBodyCopy()中的更新循环迭代量的映射
      loopBodyCopy(loopHeaderBlock, loopBodyStartBlock, valueMap);
      // 替换ValueMap中的PHI结点返回值的来源，因为从第二次迭代开始，PHI结点的来源换为Latch
      // block而不再是preheader
      for (auto& inst : *loopHeaderBlock) {
        if (auto phiNode = dyn_cast<PHINode>(&inst)) {
          for (unsigned int idx = 0; idx < phiNode->getNumIncomingValues();
               idx++) {
            auto incomingBlock = phiNode->getIncomingBlock(idx);
            // 注意这里选择来源是latch块，还需要进行二次映射，
            // 因为我们已经复制了循环体中latch block中的incomingValue
            if (incomingBlock == loopLatchBlock) {
              // !!
              valueMap[phiNode] = valueMap[phiNode->getIncomingValue(idx)];
              break;
            }
          }
        } else
          break;
      }
#ifdef LOOPUNROLL_DEBUG
      cpyBlock->print(mOut);
#endif
    }
    // 将最后一个复制块指向while.end，即loopExitBlock
    mBuilder->CreateBr(loopExitedBlock);
    // 将循环块中的指令结果在循环外使用的地方替换为ValueMap中的值
    for (auto block : loop->getBlocksVector()) {
      for (auto& inst : *block) {
        if (valueMap.find(&inst) != valueMap.end()) {
          inst.replaceAllUsesWith(valueMap[&inst]);
        }
      }
    }
#ifdef LOOPUNROLL_DEBUG
    cpyBlock->print(mOut);
#endif
    return true;
  }

  std::unique_ptr<llvm::IRBuilder<>> mBuilder;
  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    unsigned int unrollTimes = 0;

    FunctionAnalysisManager fam;
    PassBuilder pb;
    fam.registerPass([&] { return LoopAnalysis(); });
    pb.registerFunctionAnalyses(fam);

    mBuilder = std::make_unique<IRBuilder<>>(mod.getContext());

    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      // 先展开内循环，再处理外循环
      std::vector<Loop*> loopToErase;
      LoopInfo& loopInfos = fam.getResult<LoopAnalysis>(func);
      for (auto loop : loopInfos) {
#ifdef LOOPUNROLL_DEBUG
        mOut << "subloop count: " << loop->getSubLoops().size() << "\n";
#endif
        for (auto subLoop : loop->getSubLoops()) {
          if (processLoop(subLoop))
            loopToErase.push_back(subLoop);
        }
        if (processLoop(loop))
          loopToErase.push_back(loop);
      }
      // 删除已经展开了的循环块
      std::vector<BasicBlock*> blockToErase;
      for (auto loop : loopToErase) {
#ifdef LOOPUNROLL_DEBUG
        mOut << "=========Loop To Be Deleted=======\n";
        loop->print(mOut);
        mOut << "\n";
#endif
        for (auto block : loop->getBlocksVector())
          blockToErase.push_back(block);
      }
      for (auto block : blockToErase)
        block->eraseFromParent();
    }
    mOut << "Loop Unrolling running...\nTo unroll " << unrollTimes
         << " loops\n";

    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};