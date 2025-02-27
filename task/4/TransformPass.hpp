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
// #define PASS_DEBUG

using namespace llvm;

class StrengthReduction : public llvm::PassInfoMixin<StrengthReduction>
{
private:
  int64_t hight_bit(int64_t x)
  {
    int64_t bit = -1;
    while (x) {
      x >>= 1;
      bit++;
    }
    return bit;
  }

public:
  explicit StrengthReduction(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    int reductionTimes = 0;
    std::vector<Instruction*> instToErase;

    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
            Value* lhs = binOp->getOperand(0);
            Value* rhs = binOp->getOperand(1);
            auto constLhs = dyn_cast<ConstantInt>(lhs);
            auto constRhs = dyn_cast<ConstantInt>(rhs);
            switch (binOp->getOpcode()) {
              case Instruction::Mul: {
                // 右边是常量操作数
                if (!constLhs && constRhs) {
                  int64_t rhsOperandValue = constRhs->getSExtValue();
                  // 正数，且为2的幂次
                  if (rhsOperandValue > 0 &&
                      !(rhsOperandValue & rhsOperandValue - 1)) {
                    Instruction* leftShiftInst = BinaryOperator::CreateShl(
                      lhs,
                      ConstantInt::getSigned(binOp->getType(),
                                             hight_bit(rhsOperandValue)));
                    leftShiftInst->insertAfter(&inst);
                    binOp->replaceAllUsesWith(leftShiftInst);
                    instToErase.push_back(&inst);
                    reductionTimes++;
                  }
                }
                // 左边是常量操作数
                else if (constLhs && !constRhs) {
                  int64_t lhsOperandValue = constLhs->getSExtValue();
                  // 正数，且为2的幂次
                  if (lhsOperandValue > 0 &&
                      !(lhsOperandValue & lhsOperandValue - 1)) {
                    Instruction* leftShiftInst = BinaryOperator::CreateShl(
                      rhs,
                      ConstantInt::getSigned(binOp->getType(),
                                             hight_bit(lhsOperandValue)));
                    leftShiftInst->insertAfter(&inst);
                    binOp->replaceAllUsesWith(leftShiftInst);
                    instToErase.push_back(&inst);
                    reductionTimes++;
                  }
                }
                break;
              }
              // case Instruction::SDiv: {
              //   // 当操作数是2的幂次的时候进行左移操作
              //   // 右边是常量操作数
              //   if (!constLhs && constRhs) {
              //     int64_t rhsOperandValue = constRhs->getSExtValue();
              //     // 正数，且为2的幂次
              //     if (rhsOperandValue > 0 &&
              //         !(rhsOperandValue & rhsOperandValue - 1)) {
              //       Instruction* leftShiftInst = BinaryOperator::CreateAShr(
              //         lhs,
              //         ConstantInt::getSigned(binOp->getType(),
              //                                hight_bit(rhsOperandValue)));
              //       leftShiftInst->insertAfter(&inst);
              //       binOp->replaceAllUsesWith(leftShiftInst);
              //       instToErase.push_back(&inst);
              //       reductionTimes++;
              //     }
              //   }
              //   // 左边是常量操作数
              //   else if (constLhs && !constRhs) {
              //     int64_t lhsOperandValue = constLhs->getSExtValue();
              //     // 正数，且为2的幂次
              //     if (lhsOperandValue > 0 &&
              //         !(lhsOperandValue & lhsOperandValue - 1)) {
              //       Instruction* leftShiftInst = BinaryOperator::CreateAShr(
              //         rhs,
              //         ConstantInt::getSigned(binOp->getType(),
              //                                hight_bit(lhsOperandValue)));
              //       leftShiftInst->insertAfter(&inst);
              //       binOp->replaceAllUsesWith(leftShiftInst);
              //       instToErase.push_back(&inst);
              //       reductionTimes++;
              //     }
              //   }
              //   break;
              // }
              case Instruction::SRem: {
                auto insertPoint = &inst;
                // 先进行除法，然后进行移位，再进行减法：a%32 => a - (a/32) << 5
                // 右边是常量操作数
                if (!constLhs && constRhs) {
                  int64_t rhsOperandValue = constRhs->getSExtValue();
                  // 正数，且为2的幂次
                  if (rhsOperandValue > 0 &&
                      !(rhsOperandValue & rhsOperandValue - 1)) {
                    auto sDivInst = BinaryOperator::CreateSDiv(
                      lhs,
                      ConstantInt::getSigned(binOp->getType(),
                                             rhsOperandValue));
                    sDivInst->insertAfter(insertPoint);
                    insertPoint = sDivInst;
                    auto shiftLeftInst = BinaryOperator::CreateShl(
                      sDivInst,
                      ConstantInt::getSigned(binOp->getType(),
                                             hight_bit(rhsOperandValue)));
                    shiftLeftInst->insertAfter(insertPoint);
                    insertPoint = shiftLeftInst;
                    auto subInst =
                      BinaryOperator::CreateSub(lhs, shiftLeftInst);
                    subInst->insertAfter(insertPoint);

                    binOp->replaceAllUsesWith(subInst);
                    instToErase.push_back(&inst);
                    reductionTimes++;
                  }
                }
                break;
              }
              default:
                break;
            }
          }
        }
      }
    }
    for (auto& inst : instToErase)
      inst->eraseFromParent();
    mOut << "StrengthReduction running...\nReduction time: " << reductionTimes
         << "\n";
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

/**
 * PS. 没写rewriteSingleStore和同一个基本块内的Store到Load的传递
 * 既然助教发出来了，我就不写了吧~~~~~~~~~~~
 */
class Memory2Register : public llvm::PassInfoMixin<Memory2Register>
{
public:
  explicit Memory2Register(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    // 支配树，通过传入任意的函数来重新计算函数内所有基本块的支配情况
    DominatorTree domTree;

    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      domTree.recalculate(func);

      /**
       * 获取所有基本块的支配边界
       */
      // 基本块的支配边界，也就是当前基本块支配的块的后继基本块，且当前基本块不支配那个后继基本块
      std::unordered_map<BasicBlock*, std::vector<BasicBlock*>>
        dominatorFrontiers;
      for (BasicBlock& block : func)
        dominatorFrontiers[&block] = {};
      for (BasicBlock& block : func) {
        // 前驱基本块数量大于1，说明这是一个汇点
        if (pred_size(&block) > 1) {
          // 找到当前基本块的支配结点，因为支配树已经给出，所以取出直接支配结点即可（在支配树上是当前结点的直接前驱）
          // getIDom取出的是直接支配者
          BasicBlock* iDominatorBlock = domTree[&block]->getIDom()->getBlock();
          for (auto pred : predecessors(&block)) {
            BasicBlock* prevBlock = dyn_cast<BasicBlock>(pred);
            // 对于所有的前驱结点，只要它不是当前block的直接支配者，我们就沿着它的支配者路径找出所有结点，这些结点构成block的支配边界
            while (prevBlock != iDominatorBlock) {
              dominatorFrontiers[prevBlock].push_back(&block);
              // 沿着支配者路径，直到到达block的支配者
              prevBlock = domTree[prevBlock]->getIDom()->getBlock();
            }
          }
        }
      }

      /**
       * 能够被消除的前提是只对Alloca的结果进行了Load或者Store操作
       * 另外Alloca如果是开辟一个结构体或者数组，也不能进行优化
       */
      std::function<bool(AllocaInst*)> canBePromoted =
        [](AllocaInst* allocaInst) -> bool {
        if (!allocaInst->getAllocatedType()->isIntegerTy())
          return false;
        // 判断所有用到Alloca结果的地方，如果不是Load或Store则返回false，表示不能被优化
        for (auto user : allocaInst->users()) {
          if (dyn_cast<LoadInst>(user) || dyn_cast<StoreInst>(user))
            continue;
          else
            return false;
        }
        return true;
      };

      /**
       * 找出所有能被优化的Alloca指令
       */
      std::vector<AllocaInst*> promotedAllocas;
      for (auto& block : func) {
        for (auto& inst : block) {
          if (AllocaInst* allocaInst = dyn_cast<AllocaInst>(&inst))
            if (canBePromoted(allocaInst))
              promotedAllocas.push_back(allocaInst);
        }
      }

      /**
       * 找出定义某Alloca指令以及使用它的块
       */
      std::unordered_map<Instruction*, std::unordered_set<BasicBlock*>>
        allocaDefsBlock; // alloca语句结果被定值的基本块（Store）
      std::unordered_map<Instruction*, std::unordered_set<BasicBlock*>>
        allocaUsesBlock; // alloca指令结果被使用的基本块（Load）
      for (size_t idx = 0; idx != promotedAllocas.size(); idx++) {
        AllocaInst* allocaInst = promotedAllocas[idx];
        // 如果allocaInst指令没有被使用则删除
        if (allocaInst->use_empty()) {
          allocaInst->eraseFromParent();
          promotedAllocas[idx] = promotedAllocas.back();
          promotedAllocas.pop_back();
          --idx;
          continue;
        }

        for (auto user : allocaInst->users()) {
          // 能优化的Alloca指令的使用者只能是Store或者Load，Store代表Def，Load代表Use
          if (StoreInst* storeInst = dyn_cast<StoreInst>(user))
            allocaDefsBlock[allocaInst].insert(storeInst->getParent());
          else if (LoadInst* loadInst = dyn_cast<LoadInst>(user))
            allocaUsesBlock[allocaInst].insert(loadInst->getParent());
        }
      }

      /**
       * 向支配边界中插入PHI节点
       */
      // 存储支配边界上的PHI结点以及对应的Alloca指令
      std::unordered_map<BasicBlock*, std::unordered_map<PHINode*, AllocaInst*>>
        phiMap;
      for (AllocaInst* allocaInst : promotedAllocas) {
        // 记录已经插入了PHI节点的基本块，避免重复插入
        std::unordered_set<BasicBlock*> hadPhiBlocks;
        // 找出所有对Alloca结果的定值，也就是Store指令所在基本块
        std::vector<BasicBlock*> defBlocks(allocaDefsBlock[allocaInst].begin(),
                                           allocaDefsBlock[allocaInst].end());
        while (!defBlocks.empty()) {
          BasicBlock* defBlock = defBlocks.back();
          defBlocks.pop_back();
          // 找到定值所在基本块的所有支配边界
          // 支配边界上就是无法确定该定值变量定值来源的地方，因为支配边界没有被当前Store所在基本块支配
          for (BasicBlock* domFsBlock : dominatorFrontiers[defBlock]) {
            // 支配边界还没有插入PHI结点
            if (hadPhiBlocks.find(domFsBlock) == hadPhiBlocks.end()) {
              PHINode* phi = PHINode::Create(allocaInst->getAllocatedType(), 0);
              phi->insertBefore(dyn_cast<Instruction>(domFsBlock->begin()));
              hadPhiBlocks.insert(domFsBlock);
              phiMap[domFsBlock].insert({ phi, allocaInst });
              // PHI结点把当前基本块的定值传到了支配边界上，所以支配边界上也有Alloca指令对应的定值了
              if (std::find(defBlocks.begin(), defBlocks.end(), domFsBlock) ==
                  defBlocks.end())
                defBlocks.push_back(domFsBlock);
            }
          }
        }
      }

      /**
       * 支配边界上的PHI结点的赋值以及结点重命名
       */
      std::vector<Instruction*> instToErase;
      // 基本块，以及对应的可优化的Alloca指令及其IncomingValue
      std::vector<
        std::pair<BasicBlock*, std::unordered_map<AllocaInst*, Value*>>>
        workList;
      std::unordered_set<BasicBlock*> visited;
      workList.push_back({ &func.getEntryBlock(), {} });
      // 一开始IncomingValue设置为Undefined
      for (auto alloca : promotedAllocas)
        workList[0].second[alloca] =
          UndefValue::get(alloca->getAllocatedType());

      // 遍历所有基本块，用phi节点替换掉那些没有使用到的基本块以及填充phi的incoming
      // value
      while (!workList.empty()) {
        auto block = workList.back().first;
        std::unordered_map<AllocaInst*, Value*> incomingValues =
          workList.back().second;
        workList.pop_back();

        if (visited.find(block) != visited.end())
          continue;
        visited.insert(block);
        // 遍历基本块中所有指令，这里基本块是BasicBlock*类型所以不能直接for(auto
        // & inst : block)
        for (auto instIter = block->begin(), instIterEnd = block->end();
             instIter != instIterEnd;
             instIter++) {
          // 如果是PHI结点，则保存定值，因为PHI结点保存了前面基本块传过来的定值
          if (PHINode* phiNode = dyn_cast<PHINode>(instIter)) {
            if (phiMap[block].find(phiNode) != phiMap[block].end())
              incomingValues[phiMap[block][phiNode]] = phiNode;
          }
          // 如果是可优化的Alloca结点，直接删除
          else if (AllocaInst* allocaInst = dyn_cast<AllocaInst>(instIter)) {
            if (std::find(promotedAllocas.begin(),
                          promotedAllocas.end(),
                          allocaInst) == promotedAllocas.end())
              continue;
            instToErase.push_back(allocaInst);
          }
          // 如果是Load结点，需要在使用的地方进行值的替换，然后删除
          else if (LoadInst* loadInst = dyn_cast<LoadInst>(instIter)) {
            // 获取Load指令对应之前的Alloca指令
            AllocaInst* allocaInst =
              dyn_cast<AllocaInst>(loadInst->getPointerOperand());
            if (!allocaInst)
              continue;
            if (std::find(promotedAllocas.begin(),
                          promotedAllocas.end(),
                          allocaInst) != promotedAllocas.end()) {
              // 取出前面传过来的Store的值，也许来自当前基本块也许来自前驱基本块
              // 如果程序合法，在Load之前一定会有Store
              if (incomingValues.find(allocaInst) == incomingValues.end())
                abort();
              loadInst->replaceAllUsesWith(incomingValues[allocaInst]);
              instToErase.push_back(loadInst);
            }
          }
          // 如果是Store结点且对应的Alloca是可以优化的，则保存定值到incomingValues中，然后删除
          else if (StoreInst* storeInst = dyn_cast<StoreInst>(instIter)) {
            AllocaInst* allocaInst =
              dyn_cast<AllocaInst>(storeInst->getPointerOperand());
            if (!allocaInst)
              continue;
            if (std::find(promotedAllocas.begin(),
                          promotedAllocas.end(),
                          allocaInst) != promotedAllocas.end()) {
              // 保存定值，用于当前基本块后面的Load指令的值的传递，或者后继基本块中的Load
              incomingValues[allocaInst] = storeInst->getValueOperand();
              instToErase.push_back(storeInst);
            }
          }
        }

        // 遍历所有后继基本块，将之前的定值传下去
        for (auto nxtBlockIter = succ_begin(block),
                  nxtBlockIterEnd = succ_end(block);
             nxtBlockIter != nxtBlockIterEnd;
             ++nxtBlockIter) {
          auto nxtBlock = dyn_cast<BasicBlock>(*nxtBlockIter);
          // incomingValues中是当前基本块传给下一基本块的Store值，也就是定值
          workList.push_back({ nxtBlock, incomingValues });
          // 对于后继基本块的PHI结点，如果当前基本块有值传下去，就addIncoming
          for (auto instIter = nxtBlock->begin(), instIterEnd = nxtBlock->end();
               instIter != instIterEnd;
               ++instIter) {
            if (PHINode* phiNode = dyn_cast<PHINode>(instIter))
              if (phiMap[nxtBlock].find(phiNode) != phiMap[nxtBlock].end() &&
                  incomingValues.find(phiMap[nxtBlock][phiNode]) !=
                    incomingValues.end())
                phiNode->addIncoming(incomingValues[phiMap[nxtBlock][phiNode]],
                                     block);
          }
        }
      }
      for (auto inst : instToErase)
        inst->eraseFromParent();
      for (auto& blockIter : phiMap) {
        for (auto& phiIter : blockIter.second) {
          if (phiIter.first->use_empty())
            phiIter.first->eraseFromParent();
        }
      }
    }

    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

class ConstantPropagate : public llvm::PassInfoMixin<ConstantPropagate>
{
public:
  explicit ConstantPropagate(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    unsigned int constantPropagateTimes = 0;
    std::unordered_set<GlobalVariable*> modifiedGVs; // 存储所有被更改的全局变量
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            auto globalVariable =
              dyn_cast<GlobalVariable>(storeInst->getPointerOperand());
            if (globalVariable)
              modifiedGVs.insert(globalVariable);
          }
        }
      }
    }
    std::vector<Instruction*> instToErase;
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
            auto globalVariable =
              dyn_cast<GlobalVariable>(loadInst->getPointerOperand());
            if (globalVariable &&
                modifiedGVs.find(globalVariable) == modifiedGVs.end() &&
                globalVariable->hasInitializer()) {
              auto initValue =
                dyn_cast<ConstantInt>(globalVariable->getInitializer());
              if (initValue) {
                loadInst->replaceAllUsesWith(initValue);
                instToErase.push_back(loadInst);
              }
            }
          }
        }
      }
    }
    constantPropagateTimes += instToErase.size();
    for (auto inst : instToErase)
      inst->eraseFromParent();

    mOut << "ConstantPropagate running...\nTo propagate "
         << constantPropagateTimes << " registers\n";
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

class EDC : public llvm::PassInfoMixin<EDC>
{
public:
  explicit EDC(llvm::raw_ostream& out)
    : mOut(out)
  {
  }
  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    // #define EDC_DEBUG

    unsigned int EDCCount = 0;
    /**
     * 删除对全局变量的死代码
     */
    // 找出所有被load的全局变量，即有被使用的全局变量。局部变量不需要存，因为它通过mem2reg删除
    std::unordered_set<GlobalVariable*> usedGVs;
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
            auto globalVariable =
              dyn_cast<GlobalVariable>(loadInst->getPointerOperand());
            if (globalVariable)
              usedGVs.insert(globalVariable);
          }
        }
      }
    }
    std::queue<Instruction*> cascadeDelInst;
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            auto storeTo = storeInst->getPointerOperand();
            auto gv = dyn_cast<GlobalVariable>(storeTo);
            if (gv && usedGVs.find(gv) == usedGVs.end()) {
              cascadeDelInst.push(storeInst);
            }
          }
        }
      }
    }
    // 进行级联删除
    while (!cascadeDelInst.empty()) {
      auto instToDel = cascadeDelInst.front();
      cascadeDelInst.pop();
#ifdef EDC_DEBUG
      instToDel->print(mOut);
      mOut << "\n";
#endif
      // 要删除的指令的操作数对应的指令，我们需要先保存
      std::vector<Instruction*> instToDelOperandInst;
      if (auto storeInst = dyn_cast<StoreInst>(instToDel)) {
        auto storeVal = storeInst->getValueOperand();
#ifdef EDC_DEBUG
        storeVal->print(mOut);
        mOut << "\n";
#endif
        // store的值可能是常数或指令结果，如果是指令结果可能要级联删除
        if (auto storeValInst = dyn_cast<Instruction>(storeVal)) {
          instToDelOperandInst.push_back(storeValInst);
        }
      } else if (auto binOp = dyn_cast<BinaryOperator>(instToDel)) {
        auto lhsOperand = binOp->getOperand(0);
        auto rhsOperand = binOp->getOperand(1);
        auto lhsOperandInst = dyn_cast<Instruction>(lhsOperand);
        auto rhsOperandInst = dyn_cast<Instruction>(rhsOperand);
        if (lhsOperandInst)
          instToDelOperandInst.push_back(lhsOperandInst);
        if (rhsOperandInst)
          instToDelOperandInst.push_back(rhsOperandInst);
      }
      instToDel->eraseFromParent();
      for (auto operandInst : instToDelOperandInst) {
        if (operandInst->user_empty()) {
#ifdef EDC_DEBUG
          operandInst->print(mOut);
          mOut << "\n";
#endif
          cascadeDelInst.push(operandInst);
        }
      }
    }

    /**
     * 删除多余的存储操作
     */
    std::vector<Instruction*> instToErase;
    std::unordered_map<StoreInst*, std::vector<LoadInst*>> store2Loads =
      mam.getResult<Store2LoadAnalysis>(mod);
    instToErase.clear();
    std::unordered_set<Value*> signToArr;
    // 删除多余的Store指令
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          // 如果是对数组的操作就不删除了
          if (auto getElementPtrInst = dyn_cast<GetElementPtrInst>(&inst))
            signToArr.insert(getElementPtrInst);

          if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            // 如果是对全局变量的赋值，则不删除
            if (GlobalVariable* gv =
                  dyn_cast<GlobalVariable>(storeInst->getPointerOperand()))
              continue;
            // 如果是对数组的赋值，也不删除
            if (signToArr.find(storeInst->getPointerOperand()) !=
                signToArr.end())
              continue;
            if (store2Loads.find(storeInst) == store2Loads.end())
              instToErase.push_back(&inst);
          }
          // else if (inst.user_empty() && !dyn_cast<CallInst>(&inst) &&
          //            &inst != block.getTerminator()) {
          //   instToErase.push_back(&inst);
          // }
        }
      }
    }
    EDCCount += instToErase.size();
    for (auto inst : instToErase)
      inst->eraseFromParent();

    instToErase.clear();
    // 删除多余的Alloca指令
    for (auto& func : mod) {
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto allocaInst = dyn_cast<AllocaInst>(&inst)) {
            if (allocaInst->user_empty())
              instToErase.push_back(allocaInst);
          }
        }
      }
    }
    EDCCount += instToErase.size();
    for (auto inst : instToErase)
      inst->eraseFromParent();
    instToErase.clear();

    /**
     * 消除与常数相加的形式：i=i+1, i=i+2, ......
     */
    for (auto& func : mod) {
      for (auto& block : func) {
        block.print(mOut);
        mOut<<"\n";
        auto instIter = block.begin();
        auto instIterEnd = block.end();
        std::vector<Instruction*> instToErase;
        while (instIter != instIterEnd) {
          std::vector<Instruction*> prevInsts;
          while (instIter != instIterEnd) {
            auto binOp = dyn_cast<BinaryOperator>(&*instIter);
            if (binOp && binOp->getOpcode() == BinaryOperator::Add &&
                dyn_cast<ConstantInt>(binOp->getOperand(1))) {
              if (prevInsts.empty()) {
                prevInsts.push_back(binOp);
              } else {
                auto prevBinOp = prevInsts.back();
                if (binOp->getOperand(0) == prevBinOp) {
                  // !为了安全起见应该还要判断指令的user是否只为1，不过这里案例中不会出现这种情况!
                  prevInsts.push_back(binOp);
                }
                // 不符合判断形式则直接中断
                else
                  break;
              }
            }
            // 遇到不是连续的加法且前面已经有符合条件的就直接终止内层判断
            else if (!prevInsts.empty())
              break;
            instIter++;
          }
          if (prevInsts.size() > 2) {
            auto curInst = &*instIter;
            auto initValue = (*prevInsts.begin())->getOperand(0);
            int64_t totalSum = 0;
            for (auto inst : prevInsts){
              totalSum +=
                dyn_cast<ConstantInt>(inst->getOperand(0))->getSExtValue();
              inst->print(mOut);
              mOut<<"\n";
            }
            auto finalAddInst = prevInsts.back();

            Value* finalAddValue = nullptr;
            finalAddValue =
              ConstantInt::getSigned(initValue->getType(), totalSum);

            auto newAdd = BinaryOperator::CreateAdd(initValue, finalAddValue);
            newAdd->insertBefore(finalAddInst);
            finalAddInst->replaceAllUsesWith(newAdd);
            // 删除无用指令
            for (auto inst : prevInsts)
              instToErase.push_back(inst);
          }
        }
        EDCCount += instToErase.size();
        for (auto inst : instToErase)
          inst->eraseFromParent();
      }
    }

    mOut << "EDC running...\nTo eliminate " << EDCCount << " dead code\n";
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

class CSE : public llvm::PassInfoMixin<CSE>
{
public:
  explicit CSE(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  bool isGEPInstEquivalent(GetElementPtrInst* GEPInst1,
                           GetElementPtrInst* GEPInst2)
  {
    if (GEPInst1->getNumOperands() != GEPInst2->getNumOperands())
      return false;
    for (unsigned int idx = 0; idx < GEPInst1->getNumOperands(); idx++) {
      auto operand1 = GEPInst1->getOperand(idx);
      auto operand2 = GEPInst2->getOperand(idx);
      auto constantOp1 = dyn_cast<ConstantInt>(operand1);
      auto constantOp2 = dyn_cast<ConstantInt>(operand2);
      if (constantOp1 && constantOp2) {
        if (constantOp1->getSExtValue() == constantOp2->getSExtValue())
          continue;
        else
          return false;
      }
      if (GEPInst1->getOperand(idx) != GEPInst2->getOperand(idx))
        return false;
    }
    return true;
  }
  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    unsigned int removeCount = 0;
    /**
     * 先来消除第一种形式：i1+i2+...+in的加法操作多次重复(hoist-*和algebraic-*中出现)
     */
    for (auto& func : mod) {
      for (auto& block : func) {
        std::vector<Instruction*> instToErase;

        auto instIter = block.begin(), instIterEnd = block.end();
        while (instIter != instIterEnd) {
          auto binOp = dyn_cast<BinaryOperator>(&*instIter);
          if (!binOp || binOp->getOpcode() != BinaryOperator::Add) {
            instIter++;
            continue;
          }
          // 开始统计连续加的操作数，如果操作数开始出现重复，就统计重复出现次数
          std::unordered_set<Value*> operands;
          Value* prevInst = nullptr;
          auto startIter = instIter;
          bool flag = true;
          do {
            binOp = dyn_cast<BinaryOperator>(&*instIter);
            if (!binOp || binOp->getOpcode() != BinaryOperator::Add) {
              flag = false;
              break;
            }
            auto lhsOperand = binOp->getOperand(0);
            auto rhsOperand = binOp->getOperand(1);
            // 开始的时候没有前置指令
            if (prevInst == nullptr) {
              prevInst = binOp;
              operands.insert(lhsOperand);
              operands.insert(rhsOperand);
            }
            // 不属于连加的情况
            else if (lhsOperand != prevInst) {
              flag = false;
              break;
            }
            // 属于连加且已经开始重复
            else if (operands.find(rhsOperand) != operands.end())
              break;
            // 还在累加过程中
            else {
              operands.insert(rhsOperand);
              prevInst = binOp;
            }
            instIter++;
          } while (instIter != instIterEnd);
          if (!flag)
            continue;

          // 统计累加次数，也就是加了多少次i1+i2+...+in
          int dupTimes = 1;
          BasicBlock::iterator endIter; // 累加结束的指令的下一条指令
          flag = true;
          while (instIter != instIterEnd) {
            std::unordered_set<Value*> tmpOperands(operands);
            while (instIter != instIterEnd && !tmpOperands.empty()) {
              binOp = dyn_cast<BinaryOperator>(&*instIter);
              if (!binOp || binOp->getOpcode() != BinaryOperator::Add ||
                  binOp->getOperand(0) != prevInst) {
                flag = false;
                break;
              }
              auto rhsOperand = binOp->getOperand(1);
              if (tmpOperands.find(rhsOperand) == tmpOperands.end()) {
                flag = false;
                break;
              }
              tmpOperands.erase(rhsOperand);
              prevInst = binOp;
              instIter++;
            }
            if (!flag)
              break;
            if (tmpOperands.empty()) {
              endIter = instIter;
              dupTimes += 1;
            }
          }
          // 如果累加次数大于2，则创建一条乘法指令，并将startIter+operands.size()到endIter之间的指令全部删除
          if (dupTimes > 1) {
            mOut << "dup times: " << dupTimes << "\n";
            for (int i = 2; i < operands.size(); i++)
              startIter++;
            Value* dupRes = &*startIter;
            startIter++;
            while (startIter != endIter) {
              instToErase.push_back(&*startIter);
              startIter++;
            }
            startIter--;
            Instruction* lastAddInst = &*startIter;
            Instruction* mulInst = BinaryOperator::CreateMul(
              dupRes, ConstantInt::getSigned(lastAddInst->getType(), dupTimes));
            mulInst->insertAfter(lastAddInst);
            lastAddInst->replaceAllUsesWith(mulInst);
          }
        }
        removeCount += instToErase.size();
        for (auto inst : instToErase) {
          inst->eraseFromParent();
        }
      }
    }

    /**
     * 第二种形式：消除重复的store和load操作，主要应对dead-storage-elimination中的下面形式：
     * store i32 %i.0, ptr @global, align 4
     * %1 = load i32, ptr @global, align 4
     * %2 = add i32 0, %1
     * store i32 %i.0, ptr @global, align 4
     * %3 = load i32, ptr @global, align 4
     * %4 = add i32 %2, %3
     * ......
     * 消除思路其实很简单，只需要保存一个基本块内**最后一次**对全局变量的store，因为是静态单赋值所以不用担心store的值改变
     */
    for (auto& func : mod) {
      for (auto& block : func) {
        std::vector<Instruction*> instToErase;
        // 记录基本块中最后一次对某var的store操作
        std::unordered_map<Value*, StoreInst*> var2store;
        for (auto& inst : block) {
          if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            auto storeTo = storeInst->getPointerOperand();
            // 如果对某变量的定值已经存在，则判断该定值是否更改，如果没有更改则删除这个多余的store指令
            if (var2store.find(storeTo) != var2store.end() &&
                storeInst->getValueOperand() ==
                  var2store[storeTo]->getValueOperand()) {
              instToErase.push_back(storeInst);
            }
            // 如果定值的变量还没有记录，或者定值的值不同了
            else
              var2store[storeTo] = storeInst;
          } else if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
            auto loadFrom = loadInst->getPointerOperand();
            // 如果load的变量在当前基本块中有对应store（最后一个store记录）
            if (var2store.find(loadFrom) != var2store.end()) {
              loadInst->replaceAllUsesWith(
                var2store[loadFrom]->getValueOperand());
              instToErase.push_back(loadInst);
            }
          }
        }
        removeCount += instToErase.size();
        for (auto inst : instToErase)
          inst->eraseFromParent();
      }
    }

    /**
     * 消除第三种形式：i=i+j; i=i+j; ......
     */
    for (auto& func : mod) {
      for (auto& block : func) {
        auto instIter = block.begin();
        auto instIterEnd = block.end();
        std::vector<Instruction*> instToErase;
        while (instIter != instIterEnd) {
          std::vector<Instruction*> prevInsts;
          while (instIter != instIterEnd) {
            auto binOp = dyn_cast<BinaryOperator>(&*instIter);
            if (binOp && binOp->getOpcode() == BinaryOperator::Add) {
              if (prevInsts.empty()) {
                prevInsts.push_back(binOp);
              } else {
                auto prevBinOp = prevInsts.back();
                // 前一个指令的结果作为当前指令的操作数，且两条指令的第二个操作数一样
                if (prevBinOp->getOperand(1) == binOp->getOperand(1) &&
                    binOp->getOperand(0) == prevBinOp) {
                  // !为了安全起见应该还要判断指令的user是否只为1，不过这里案例中不会出现这种情况!
                  prevInsts.push_back(binOp);
                }
                // 不符合判断形式则直接中断
                else
                  break;
              }
            }
            // 遇到不是连续的加法且前面已经有符合条件的就直接终止内层判断
            else if (!prevInsts.empty())
              break;
            instIter++;
          }
          // 超过5个连加再转化为乘法
          if (prevInsts.size() > 5) {
            auto curInst = &*instIter;
            auto initValue = (*prevInsts.begin())->getOperand(0);
            auto incrementValue = (*prevInsts.begin())->getOperand(1);
            auto finalAddInst = prevInsts.back();

            Value* finalAddValue = nullptr;
            // 如果递增量是一个常数就不要创建指令了
            if (auto constantInt = dyn_cast<ConstantInt>(incrementValue)) {
              finalAddValue = ConstantInt::getSigned(
                constantInt->getType(),
                prevInsts.size() * constantInt->getSExtValue());
            } else {
              auto mulInst = BinaryOperator::CreateMul(
                incrementValue,
                ConstantInt::getSigned(incrementValue->getType(),
                                       prevInsts.size()));
              mulInst->insertBefore(curInst);
              finalAddValue = mulInst;
            }
            auto newAdd = BinaryOperator::CreateAdd(initValue, finalAddValue);
            newAdd->insertBefore(curInst);
            finalAddInst->replaceAllUsesWith(newAdd);
            // 删除无用指令
            for (auto inst : prevInsts)
              instToErase.push_back(inst);
          }
        }
        removeCount += instToErase.size();
        for (auto inst : instToErase)
          inst->eraseFromParent();
      }
    }

    /**
     * 第四种形式：删除多余的GEP指令
     */
    DominatorTree domTree;
    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      domTree.recalculate(func);
      std::vector<GetElementPtrInst*> GEPInsts;
      std::vector<Instruction*> instToErase;
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto curGEPInst = dyn_cast<GetElementPtrInst>(&inst)) {
            bool appendFlag = true;
            for (auto existedGEPInst : GEPInsts) {
              // 两个操作等价，而且出现过的GEP所在基本块支配当前基本块
              if (isGEPInstEquivalent(existedGEPInst, curGEPInst) &&
                  domTree.dominates(existedGEPInst->getParent(),
                                    curGEPInst->getParent())) {
                curGEPInst->replaceAllUsesWith(existedGEPInst);
                instToErase.push_back(curGEPInst);
                appendFlag = false;
                break;
              }
            }
            if (appendFlag)
              GEPInsts.push_back(curGEPInst);
          }
        }
      }
      removeCount += instToErase.size();
      for (auto inst : instToErase)
        inst->eraseFromParent();
    }

    /**
     * 第五种形式：两个操作数值相等的情况
     */
    std::vector<Instruction*> instToErase;
    // 在删除公共表达式之后，可能需要连带删除它们的Load指令，但是不确定这个Load指令是否在其他地方用到，所以后面要根据user_empty函数判断
    std::vector<LoadInst*> whetherToErase;
    for (auto& func : mod) {
      for (auto& block : func) {
        // 下面数据结构对应的意思是 操作->两个操作数->可用表达式
        // 如果是操作数来自load指令，那么操作数的key就是load的地址；否则就是指令结果
        std::unordered_map<Instruction::BinaryOps,
                           std::map<std::pair<Value*, Value*>, Value*>>
          availableExpr;
        for (auto& inst : block) {
          if (dyn_cast<StoreInst>(&inst))
            availableExpr.clear();

          auto binOp = dyn_cast<BinaryOperator>(&inst);
          if (!binOp)
            continue;
          auto opCode = binOp->getOpcode();
          Value *key1 = binOp->getOperand(0), *key2 = binOp->getOperand(1);
          if (auto loadInst = dyn_cast<LoadInst>(key1))
            key1 = loadInst->getPointerOperand();
          if (auto loadInst = dyn_cast<LoadInst>(key2))
            key2 = loadInst->getPointerOperand();
          auto keyPair = std::make_pair(key1, key2);
          // 当前表达式没有可用的
          if (availableExpr.find(opCode) == availableExpr.end() ||
              availableExpr[opCode].find(keyPair) ==
                availableExpr[opCode].end()) {
            availableExpr[opCode][keyPair] = binOp;
          }
          // 有可用表达式则替换所有用到当前指令结果的地方
          else {
            auto replaceExpr = availableExpr[opCode][keyPair];
            binOp->replaceAllUsesWith(replaceExpr);
            instToErase.push_back(binOp);
            // 注意还要递归删除两个操作数的load操作
            if (auto loadInst = dyn_cast<LoadInst>(binOp->getOperand(0)))
              whetherToErase.push_back(loadInst);
            if (auto loadInst = dyn_cast<LoadInst>(binOp->getOperand(1)))
              whetherToErase.push_back(loadInst);
          }
        }
      }
    }

    removeCount += instToErase.size();
    for (auto inst : instToErase)
      inst->eraseFromParent();
    for (auto inst : whetherToErase) {
      if (inst->user_empty()) {
        inst->eraseFromParent();
        removeCount += 1;
      }
    }

    mOut << "CSE running...\nTo eiminate " << removeCount
         << " comment sub expression\n";
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

class AlgebraicIdentities : public llvm::PassInfoMixin<AlgebraicIdentities>
{
public:
  explicit AlgebraicIdentities(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    unsigned int algebraicIdentitiedTimes = 0;
    for (auto& func : mod) {
      for (auto& block : func) {
        std::vector<Instruction*> instToErase;
        for (auto& inst : block) {
          if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
            Value* lhs = binOp->getOperand(0);
            Value* rhs = binOp->getOperand(1);
            auto constLhs = dyn_cast<ConstantInt>(lhs);
            auto constRhs = dyn_cast<ConstantInt>(rhs);
            switch (binOp->getOpcode()) {
              case Instruction::Add: {
                if (constRhs && constRhs->getSExtValue() == 0) {
                  binOp->replaceAllUsesWith(lhs);
                  instToErase.push_back(binOp);
                  ++algebraicIdentitiedTimes;
                } else if (constLhs && constLhs->getSExtValue() == 0) {
                  binOp->replaceAllUsesWith(rhs);
                  instToErase.push_back(binOp);
                  ++algebraicIdentitiedTimes;
                }
                break;
              }
              case Instruction::Sub: {
                if (constRhs && constRhs->getSExtValue() == 0) {
                  binOp->replaceAllUsesWith(lhs);
                  instToErase.push_back(binOp);
                  ++algebraicIdentitiedTimes;
                }
                break;
              }
              case Instruction::Mul: {
                if (constRhs) {
                  if (constRhs->getSExtValue() == 1) {
                    binOp->replaceAllUsesWith(lhs);
                    instToErase.push_back(binOp);
                    ++algebraicIdentitiedTimes;
                  } else if (constRhs->getSExtValue() == 0) {
                    binOp->replaceAllUsesWith(
                      ConstantInt::getSigned(binOp->getType(), 0));
                    instToErase.push_back(binOp);
                    ++algebraicIdentitiedTimes;
                  }
                } else if (constLhs) {
                  if (constLhs->getSExtValue() == 1) {
                    binOp->replaceAllUsesWith(rhs);
                    instToErase.push_back(binOp);
                    ++algebraicIdentitiedTimes;
                  } else if (constLhs->getSExtValue() == 0) {
                    binOp->replaceAllUsesWith(
                      ConstantInt::getSigned(binOp->getType(), 0));
                    instToErase.push_back(binOp);
                    ++algebraicIdentitiedTimes;
                  }
                }
                break;
              }
              case Instruction::UDiv:
              case Instruction::SDiv: {
                if (constLhs && constLhs->getSExtValue() == 0) {
                  binOp->replaceAllUsesWith(
                    ConstantInt::getSigned(binOp->getType(), 0));
                  instToErase.push_back(binOp);
                  ++algebraicIdentitiedTimes;
                } else if (constRhs && constRhs->getSExtValue() == 1) {
                  binOp->replaceAllUsesWith(lhs);
                  instToErase.push_back(binOp);
                  ++algebraicIdentitiedTimes;
                }
                break;
              }
              case Instruction::SRem:
              case Instruction::URem: {
                if (constRhs && constRhs->getSExtValue() == 1) {
                  binOp->replaceAllUsesWith(
                    ConstantInt::getSigned(binOp->getType(), 0));
                  instToErase.push_back(binOp);
                  ++algebraicIdentitiedTimes;
                }
                break;
              }
              default:
                break;
            }
          }
        }
        for (auto& i : instToErase)
          i->eraseFromParent();
      }
    }
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

class ControlFlowSimplification
  : public llvm::PassInfoMixin<ControlFlowSimplification>
{
public:
  explicit ControlFlowSimplification(llvm::raw_ostream& out)
    : mOut(out)
  {
  }
  std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> cfg;
  /**
   * 打印某函数的CFG观察是否正确
   */
  void printCFG(BasicBlock* entryBlock)
  {
    std::queue<BasicBlock*> q;
    std::unordered_set<BasicBlock*> visited;
    q.push(entryBlock);
    while (!q.empty()) {
      auto block = q.front();
      q.pop();
      if (visited.find(block) != visited.end())
        continue;
      visited.insert(block);
      for (auto succBlock : cfg[block]) {
        if (visited.find(succBlock) == visited.end())
          q.push(succBlock);
      }
    }
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    cfg = mam.getResult<MyCFGAnalysis>(mod);
    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      // printCFG(&func.getEntryBlock());

      /**
       * 1.遍历CFG，找出所有从函数entry块能到达的基本块
       * 同时修改已经确认跳转情况的跳转语句为直接跳转，还要删除对应的icmp指令
       * 如果后继基本块有PHI结点的来源是已经确认被删除基本块，则修改PHI结点来源为唯一？？？这里要怎么修改，因为PHI可能用到还没有定义的值，还是说不用修改
       */
      std::unordered_set<BasicBlock*> reachableBlock;
      std::queue<BasicBlock*> worklist;
      worklist.push(&func.getEntryBlock());
      while (!worklist.empty()) {
        auto curBlock = worklist.front();
        worklist.pop();
        if (reachableBlock.find(curBlock) != reachableBlock.end())
          continue;
        reachableBlock.insert(curBlock);
        auto branchInst = dyn_cast<BranchInst>(curBlock->getTerminator());
        if (!branchInst)
          continue;
        // 如果是条件跳转但是在控制流图上只有一条出边，则改为直接跳转
        if (!branchInst->isUnconditional() && cfg[curBlock].size() == 1) {
          auto succBlock = *cfg[curBlock].begin();
          auto icmpInst = dyn_cast<ICmpInst>(branchInst->getCondition());
          branchInst->eraseFromParent();
          // 其实这里还应该判断有没有其他user的？？？
          if (icmpInst)
            icmpInst->eraseFromParent();
          auto directBranchInst = BranchInst::Create(succBlock, curBlock);
          if (reachableBlock.find(succBlock) == reachableBlock.end())
            worklist.push(succBlock);
        }
        // 否则添加所有跳转边到reachableBlock中
        else {
          for (auto succBlock : cfg[curBlock]) {
            if (reachableBlock.find(succBlock) == reachableBlock.end())
              worklist.push(succBlock);
          }
        }
      }
      /**
       * 2.删除所有不能到达的基本块
       */
      std::vector<BasicBlock*> blockToErase;
      for (auto& block : func) {
        if (reachableBlock.find(&block) == reachableBlock.end())
          blockToErase.push_back(&block);
      }
      mOut << "unreachable block count: " << blockToErase.size() << "\n";
      for (auto block : blockToErase)
        block->eraseFromParent();
      /**
       * 3.合并所有的只有一个后继块直接跳转基本块，且那个后继块只有一个前驱
       */
      // 遍历所有基本块合并直接跳转，并保存需要删除的基本块
      bool flag = true;
      while (flag) {
        flag = false;
        std::unordered_set<BasicBlock*> blockToDel;
        for (auto& block : func) {
          auto curBlock = &block;
          if (blockToDel.find(curBlock) != blockToDel.end())
            continue;
          auto branchInst = dyn_cast<BranchInst>(curBlock->getTerminator());
          if (!branchInst || !branchInst->isUnconditional())
            continue;
          auto succBlock = dyn_cast<BasicBlock>(branchInst->getOperand(0));
          // 判断是否基本块的前驱基本块是否唯一，或者基本块是否已经标记为删除
          if (!succBlock && blockToDel.find(succBlock) != blockToDel.end() ||
              !succBlock->getSinglePredecessor())
            continue;

          // 下面进行基本块合并
          // 首先需要将后继基本块的所有后继基本块的PHI结点来源改为当前基本块
          for (auto succSuccBlock : successors(succBlock)) {
            for (auto& succSuccInst : *succSuccBlock) {
              if (auto phiNode = dyn_cast<PHINode>(&succSuccInst)) {
                for (unsigned int idx = 0;
                     idx < phiNode->getNumIncomingValues();
                     idx++) {
                  auto incomingBlock = phiNode->getIncomingBlock(idx);
                  if (incomingBlock == succBlock)
                    phiNode->setIncomingBlock(idx, curBlock);
                }
              }
            }
          }
#ifdef PASS_DEBUG
          mOut << "====curBlock before inst move:====\n";
          curBlock->print(mOut);
          mOut << "\n\n";
          mOut << "====succBlock before inst move:====\n";
          succBlock->print(mOut);
          mOut << "\n\n";
#endif
          // 然后进行指令移动，注意insertBefore的问题，insertBefore不会移动指令，而只会在其他基本块构造引用
          std::vector<Instruction*> succInsts;
          for (auto& succInst : *succBlock)
            succInsts.push_back(&succInst);
          for (auto succInst : succInsts) {
            succInst->removeFromParent();
            succInst->insertBefore(branchInst);
          }
          branchInst->eraseFromParent();
          ReturnInst::Create(func.getContext(), succBlock);
#ifdef PASS_DEBUG
          mOut << "====curBlock before inst move:====\n";
          curBlock->print(mOut);
          mOut << "\n\n";
          mOut << "====succBlock before inst move:====\n";
          succBlock->print(mOut);
          mOut << "\n\n";
#endif
          blockToDel.insert(succBlock);
        }
        if (blockToDel.size() != 0)
          flag = true;
        for (auto delBlock : blockToDel) {
#ifdef PASS_DEBUG
          mOut << "====block to delete:====\n";
          delBlock->print(mOut);
          mOut << "\n\n";
#endif
          delBlock->eraseFromParent();
        }
      }
    }
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};

/**
 * 该优化是针对 局部数组 元素是常数的情况，局部变量的优化已在Mem2Reg中实现，如：
 * %s = alloca [16 x i32], align 16
 * ......
 * %arrayidx5 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 1
 * store i32 1, ptr %arrayidx5, align 4
 * ......
 * %arrayidx68 = getelementptr inbounds [16 x i32], ptr %s, i64 0, i64 1
 * %0 = load i32, ptr %arrayidx68, align 4
 * %add69 = add nsw i32 %sum.1, %0
 *
 * -->
 *
 * %add69 = add nsw i32 %sum.1, 1
 * 这个优化针对if-combine*.c，优化力度巨大，不过需要控制流优化和循环展开的配合
 * 简单来说就是识别局部常量数组，数组的所有操作都是显式已知的，比如GEP操作所有运算符都必须是常数
 */
class OptimizeConstantArray : public llvm::PassInfoMixin<OptimizeConstantArray>
{
public:
  explicit OptimizeConstantArray(llvm::raw_ostream& out)
    : mOut(out)
  {
  }
  /**
   * 判断GEP指令是否是对局部数组的显式操作
   */
  bool isWeConcernedGEP(GetElementPtrInst* GEPInst,
                        std::unordered_set<Value*>& localAllocaArr)
  {
    // 判断地址是否是局部数组
    auto getFrom = GEPInst->getPointerOperand();
    auto subGEPInst = dyn_cast<GetElementPtrInst>(getFrom);
    if (subGEPInst && !isWeConcernedGEP(subGEPInst, localAllocaArr))
      return false;
    else if (localAllocaArr.find(getFrom) == localAllocaArr.end())
      return false;

    // 判断索引操作数是否是常数
    for (unsigned int idx = 1; idx < GEPInst->getNumOperands(); idx++) {
      auto constantOperand = dyn_cast<Constant>(GEPInst->getOperand(idx));
      if (!constantOperand)
        return false;
    }
    return true;
  }

  /**
   * 判断两个GEP指令是否等价
   */
  bool isGEPInstEquivalent(GetElementPtrInst* GEPInst1,
                           GetElementPtrInst* GEPInst2)
  {
    if (GEPInst1 == GEPInst2)
      return true;
    if (GEPInst1->getNumOperands() != GEPInst2->getNumOperands())
      return false;
    // 判断地址是否等价
    auto getFrom1 = GEPInst1->getPointerOperand();
    auto getFrom2 = GEPInst2->getPointerOperand();
    auto subGEPInst1 = dyn_cast<GetElementPtrInst>(getFrom1);
    auto subGEPInst2 = dyn_cast<GetElementPtrInst>(getFrom2);
    if (subGEPInst1 && subGEPInst2 &&
        !isGEPInstEquivalent(subGEPInst1, subGEPInst2))
      return false;
    if (getFrom1 != getFrom2)
      return false;
    // 判断索引是否等价（索引只能在是常数的时候判断）
    for (unsigned int idx = 1; idx < GEPInst1->getNumOperands(); idx++) {
      auto constantOp1 = dyn_cast<ConstantInt>(GEPInst1->getOperand(idx));
      auto constantOp2 = dyn_cast<ConstantInt>(GEPInst2->getOperand(idx));
      if (constantOp1 && constantOp2 &&
          constantOp1->getSExtValue() == constantOp2->getSExtValue())
        continue;
      else
        return false;
    }
    return true;
  }

  /**
   * 获取一个GEP指令真正的地址操作数
   */
  Value* getGEPInstAddr(GetElementPtrInst* GEPInst)
  {
    auto subGEPInst = dyn_cast<GetElementPtrInst>(GEPInst->getPointerOperand());
    if (subGEPInst)
      return getGEPInstAddr(subGEPInst);
    return GEPInst->getPointerOperand();
  }

  /**
   * 判断一个GEP指令的所有操作数是否都是常数，也就是显式的GEP指令
   */
  bool isGEPInstExpcilit(GetElementPtrInst* GEPInst)
  {
    auto subGEPInst = dyn_cast<GetElementPtrInst>(GEPInst->getPointerOperand());
    if (subGEPInst && !isGEPInstExpcilit(subGEPInst))
      return false;
    for (unsigned int idx = 1; idx < GEPInst->getNumOperands(); idx++) {
      auto operand = GEPInst->getOperand(idx);
      if (!dyn_cast<ConstantInt>(operand))
        return false;
    }
    return true;
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam)
  {
    DominatorTree domTree;
    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;

      /**
       * 判断当前函数中是否有局部数组，只对局部常量数组进行优化
       */
      BasicBlock& entryBlock = func.getEntryBlock();
      std::unordered_set<Value*> localAllocaArr;
      for (auto& inst : entryBlock) {
        if (auto allocaInst = dyn_cast<AllocaInst>(&inst)) {
          if (allocaInst->getAllocatedType()->isArrayTy()) {
            localAllocaArr.insert(allocaInst);
          }
        }
      }
      if (localAllocaArr.empty())
        continue;
      /**
       * 如果对局部数组有隐式操作，我们也不进行优化
       * 比如存入一个非常数值、索引不是常数、在Call指令的参数中
       */
      std::unordered_set<Value*> localArrWithUnclearOp;
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            auto storeTo = storeInst->getPointerOperand();
            auto GEPInst = dyn_cast<GetElementPtrInst>(storeTo);
            // 如果操作的不是一个数组，或者这个数组不是局部数组，则跳过
            if (!GEPInst || localAllocaArr.find(getGEPInstAddr(GEPInst)) ==
                              localAllocaArr.end())
              continue;
            // 这个GEP指令是否显式会在识别到这条GEP指令的时候进行判断，所以这里不需要重复判断，只需要判断存入的是否是常数
            auto storeVal = storeInst->getValueOperand();
            if (!dyn_cast<ConstantInt>(storeVal))
              localArrWithUnclearOp.insert(getGEPInstAddr(GEPInst));
          } else if (auto GEPInst = dyn_cast<GetElementPtrInst>(&inst)) {
            auto GEPAddr = getGEPInstAddr(GEPInst);
            if (localAllocaArr.find(GEPAddr) == localAllocaArr.end())
              continue;
            if (!isGEPInstExpcilit(GEPInst))
              localArrWithUnclearOp.insert(GEPAddr);
          } else if (auto callInst = dyn_cast<CallInst>(&inst)) {
            for (unsigned int idx = 0; idx < callInst->getNumOperands();
                 idx++) {
              auto operand = callInst->getOperand(idx);
              if (auto GEPInst = dyn_cast<GetElementPtrInst>(operand)) {
                auto GEPAddr = getGEPInstAddr(GEPInst);
                if (localAllocaArr.find(GEPAddr) != localAllocaArr.end())
                  localArrWithUnclearOp.insert(GEPAddr);
              } else if (auto allocaInst = dyn_cast<AllocaInst>(operand)) {
                if (localAllocaArr.find(allocaInst) != localAllocaArr.end())
                  localArrWithUnclearOp.insert(allocaInst);
              }
            }
          }
        }
      }
      for (auto arr : localArrWithUnclearOp)
        localAllocaArr.erase(arr);
      if (localAllocaArr.empty())
        continue;
      /**
       * 下面进行常量数组的传播
       */
      // 获取支配树
      domTree.recalculate(func);
      // 保存局部数组下标对应的store指令，如果store在循环内则判断是否为常数，store在循环外则可以直接替换
      std::unordered_map<GetElementPtrInst*, StoreInst*> valueMap;
      // 保存对局部数组的操作指令，后面进行删除
      std::unordered_map<Value*, std::vector<Instruction*>> delInsts;
      // 考虑load指令不能被传播的情况：在循环中常常有store在load之后发生，这个时候可以进行外提而不能进行传播
      std::unordered_set<Value*> cannotBeFullyOptimize;

      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            auto storeTo = storeInst->getPointerOperand();
            auto GEPInst = dyn_cast<GetElementPtrInst>(storeTo);
            if (!GEPInst)
              continue;
            auto GEPAddr = getGEPInstAddr(GEPInst);
            if (localAllocaArr.find(GEPAddr) == localAllocaArr.end())
              continue;
            valueMap[GEPInst] = storeInst;
            delInsts[GEPAddr].push_back(storeInst);
          }
          // 判断是否能替换
          else if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
            auto GEPInst =
              dyn_cast<GetElementPtrInst>(loadInst->getPointerOperand());
            if (!GEPInst)
              continue;
            auto GEPAddr = getGEPInstAddr(GEPInst);
            if (localAllocaArr.find(GEPAddr) == localAllocaArr.end())
              continue;
            bool canPropagate = false;
            for (auto it = valueMap.begin(); it != valueMap.end(); it++) {
              // 如果找到等价的GEP指令，则判断store指令所在基本块是否支配load指令所在基本块
              if (isGEPInstEquivalent(it->first, GEPInst) &&
                  domTree.dominates(it->second->getParent(), &block)) {
                auto val = it->second->getValueOperand();
                loadInst->replaceAllUsesWith(val);
                delInsts[GEPAddr].push_back(loadInst);
                canPropagate = true;
                break;
              }
            }
            if (!canPropagate)
              cannotBeFullyOptimize.insert(GEPAddr);
          } else if (auto GEPInst = dyn_cast<GetElementPtrInst>(&inst)) {
            auto GEPAddr = getGEPInstAddr(GEPInst);
            if (localAllocaArr.find(GEPAddr) != localAllocaArr.end())
              delInsts[GEPAddr].push_back(GEPInst);
          }
        }
      }

      /**
       * 删除所有无关指令
       */
      for (auto it = delInsts.begin(); it != delInsts.end(); it++) {
        if (cannotBeFullyOptimize.find(it->first) !=
            cannotBeFullyOptimize.end())
          continue;
        for (auto inst : it->second)
          inst->eraseFromParent();
      }
    }
    return PreservedAnalyses::all();
  }

private:
  llvm::raw_ostream& mOut;
};
