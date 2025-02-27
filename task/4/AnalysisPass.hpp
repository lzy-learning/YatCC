#pragma once

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/Analysis/ValueTracking.h>
#include <llvm/CodeGen/ReachingDefAnalysis.h>
#include <llvm/IR/Dominators.h>

using namespace llvm;

/**
 * 定值-引用链构造
 */
class Store2LoadAnalysis : public llvm::AnalysisInfoMixin<Store2LoadAnalysis>
{
public:
  using Result = std::unordered_map<StoreInst*, std::vector<LoadInst*>>;
  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager&)
  {
    // 该数据结构存储定值到引用的映射
    std::unordered_map<StoreInst*, std::vector<LoadInst*>> store2Loads;
    // 该数据结构存储某地址到StoreInst的映射，也就是有哪些StoreInst向某地址写入了值
    std::unordered_map<BasicBlock*,
                       std::unordered_map<Value*, std::vector<StoreInst*>>>
      allBlockValue2Store;
    // 进行两次对所有基本块的遍历操作，这是考虑了回边的影响，后续基本块可能作为它祖先基本块的前驱基本块
    int times = 2;
    while (times--) {
      for (auto& func : mod) {
        if (func.isDeclaration())
          continue;
        for (auto& block : func) {
          std::unordered_map<Value*, std::vector<StoreInst*>>
            curBlockValue2Store;
          // 将前驱基本块的定值保存下来
          for (auto prevBlockIter = pred_begin(&block);
               prevBlockIter != pred_end(&block);
               prevBlockIter++) {
            std::unordered_map<Value*, std::vector<StoreInst*>>&
              prevBlockValue2Store = allBlockValue2Store[*prevBlockIter];
            for (auto it = prevBlockValue2Store.begin();
                 it != prevBlockValue2Store.end();
                 it++)
              curBlockValue2Store[it->first].insert(
                curBlockValue2Store[it->first].end(),
                it->second.begin(),
                it->second.end());
          }

          // 遍历当前基本块的指令
          for (auto& inst : block) {
            // 如果是Store指令，则将其添加到curBlockValue2Store中，并杀死原本curBlockValue2Store中的定值（如果有的话）
            if (auto storeInst = dyn_cast<StoreInst>(&inst))
              curBlockValue2Store[storeInst->getPointerOperand()] = {
                storeInst
              };
            // 如果是Load指令，则判断能否在curBlockValue2Store中找到Load的地址，如果有，将对应的Store指令和Load指令作映射
            else if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
              if (curBlockValue2Store.find(loadInst->getPointerOperand()) !=
                  curBlockValue2Store.end()) {
                // 对于Load地址的所有定值，定值可能有一个或多个（来自前驱基本块的叠加），如果在当前基本块之前指令定值那么自然只有一个
                for (auto correspondingStore :
                     curBlockValue2Store[loadInst->getPointerOperand()]) {
                  // 当前Store指令还没有记录
                  if (store2Loads.find(correspondingStore) == store2Loads.end())
                    store2Loads[correspondingStore] = { loadInst };
                  // 已经有记录了还需要避免重复添加
                  else if (std::find(store2Loads[correspondingStore].begin(),
                                     store2Loads[correspondingStore].end(),
                                     loadInst) ==
                           store2Loads[correspondingStore].end())
                    store2Loads[correspondingStore].push_back(loadInst);
                }
              }
            }
          }

          // 保存当前基本块的定值情况
          allBlockValue2Store[&block] = curBlockValue2Store;
        }
      }
    }
    return store2Loads;
  }

private:
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<
    Store2LoadAnalysis>; // AnalysisKey是私有变量，所以需要声明为友元类
};
AnalysisKey Store2LoadAnalysis::Key;

/**
 * 引用-定值链构造
 */
class Load2StoreAnalysis : public llvm::AnalysisInfoMixin<Load2StoreAnalysis>
{
public:
  using Result = std::unordered_map<LoadInst*, std::vector<StoreInst*>>;
  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager&)
  {
    std::unordered_map<LoadInst*, std::vector<StoreInst*>> load2Stores;
    // 该数据结构存储某地址到StoreInst的映射，也就是有哪些StoreInst向某地址写入了值
    std::unordered_map<BasicBlock*,
                       std::unordered_map<Value*, std::vector<StoreInst*>>>
      allBlockValue2Store;

    // 进行两次对所有基本块的遍历操作，这是考虑了回边的影响，后续基本块可能作为它祖先基本块的前驱基本块
    int times = 2;
    while (times--) {
      for (auto& func : mod) {
        if (func.isDeclaration())
          continue;

        for (auto& block : func) {
          std::unordered_map<Value*, std::vector<StoreInst*>>
            curBlockValue2Store;
          // 将前驱基本块的定值保存下来
          for (auto prevBlockIter = pred_begin(&block);
               prevBlockIter != pred_end(&block);
               prevBlockIter++) {
            std::unordered_map<Value*, std::vector<StoreInst*>>&
              prevBlockValue2Store = allBlockValue2Store[*prevBlockIter];
            for (auto it = prevBlockValue2Store.begin();
                 it != prevBlockValue2Store.end();
                 it++)
              curBlockValue2Store[it->first].insert(
                curBlockValue2Store[it->first].end(),
                it->second.begin(),
                it->second.end());
          }

          // 遍历当前基本块的指令
          std::unordered_set<StoreInst*> curBlockStoreInsts;
          for (auto& inst : block) {
            // 如果是Store指令，则将其添加到curBlockValue2Store中，并杀死原本curBlockValue2Store中的定值（如果有的话）
            if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
              curBlockValue2Store[storeInst->getPointerOperand()] = {
                storeInst
              };
              // 记录每个基本块中的StoreInst指令，这是为了判断LoadInst地址是否在当前基本块内有Store
              // 如果是则不需要考虑前驱基本块的Store，而且当前基本块中的Store保证只有一个
              curBlockStoreInsts.insert(storeInst);
            }

            // 如果是Load指令，则判断能否在curBlockValue2Store中找到Load的地址，如果有，将对应的Store指令和Load指令作映射
            else if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
              if (curBlockValue2Store.find(loadInst->getPointerOperand()) !=
                  curBlockValue2Store.end()) {
                // 如果是来自当前基本块内的定值则立即更新LoadInst的记录
                if (curBlockValue2Store[loadInst->getPointerOperand()].size() ==
                      1 &&
                    curBlockStoreInsts.find(
                      curBlockValue2Store[loadInst->getPointerOperand()]
                        .back()) != curBlockStoreInsts.end()) {
                  load2Stores[loadInst] = {
                    curBlockValue2Store[loadInst->getPointerOperand()].back()
                  };
                  continue;
                }

                // 对于Load地址的所有定值，定值可能有一个或多个（来自前驱基本块的叠加），如果在当前基本块之前指令定值那么自然只有一个
                for (auto correspondingStore :
                     curBlockValue2Store[loadInst->getPointerOperand()]) {
                  // 当前Load指令还没有记录
                  if (load2Stores.find(loadInst) == load2Stores.end())
                    load2Stores[loadInst] = { correspondingStore };
                  // 如果当前Load指令已有记录
                  else {
                    // 如果记录是来自基本块外的，则需要继续添加，注意不要重复添加即可
                    if (std::find(load2Stores[loadInst].begin(),
                                  load2Stores[loadInst].end(),
                                  correspondingStore) ==
                        load2Stores[loadInst].end())
                      load2Stores[loadInst].push_back(correspondingStore);
                  }
                }
              }
            }
          }

          // 保存当前基本块的定值情况
          allBlockValue2Store[&block] = curBlockValue2Store;
        }
      }
    }
    return load2Stores;
  }

private:
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<
    Load2StoreAnalysis>; // AnalysisKey是私有变量，所以需要声明为友元类
};
AnalysisKey Load2StoreAnalysis::Key;

/**
 * 构造函数调用链
 */
class CallGraphAnalysis : public llvm::AnalysisInfoMixin<CallGraphAnalysis>
{
public:
  using Result = std::unordered_map<Function*, std::unordered_set<Function*>>;

  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager& MAM)
  {
    Result callGraph;
    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      for (auto& block : func) {
        for (auto& inst : block) {
          if (auto callInst = dyn_cast<CallInst>(&inst)) {
            if (callGraph[&func].find(callInst->getCalledFunction()) ==
                callGraph[&func].end())
              callGraph[&func].insert(callInst->getCalledFunction());
          }
        }
      }
    }
    return callGraph;
  }

private:
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<
    CallGraphAnalysis>; // AnalysisKey是私有变量，所以需要声明为友元类
};
AnalysisKey CallGraphAnalysis::Key;

/**
 * 构造控制流图
 * 对于分支语句如果跳转语句能直接确定是否跳转，那么流图只会构造跳转边
 */
class MyCFGAnalysis : public llvm::AnalysisInfoMixin<MyCFGAnalysis>
{
public:
  using Result =
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>>;

  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager& MAM)
  {
    Result cfg;
    for (auto& func : mod) {
      if (func.isDeclaration())
        continue;
      std::queue<BasicBlock*> workList;
      std::unordered_set<BasicBlock*> visited;
      workList.push(&func.getEntryBlock());
      // 遍历从entry块出发到达的块，如果条件cond能提前确定，则那个基本块只会有一个出边
      while (!workList.empty()) {
        BasicBlock* curBlock = workList.front();
        workList.pop();
        if (visited.find(curBlock) != visited.end())
          continue;
        visited.insert(curBlock);
        auto branchInst = dyn_cast<BranchInst>(curBlock->getTerminator());
        if (!branchInst)
          continue;
        // 如果是非条件跳转则直接添加该边
        if (branchInst->isUnconditional()) {
          auto succBlock = branchInst->getSuccessor(0);
          cfg[curBlock].insert(succBlock);
          if (visited.find(succBlock) == visited.end())
            workList.push(succBlock);
        } 
        // 如果是非条件跳转则判断能否提前得知跳转方向
        else {
          auto icmpInst = dyn_cast<ICmpInst>(branchInst->getCondition());
          auto lhsConstant = dyn_cast<ConstantInt>(icmpInst->getOperand(0));
          auto rhsConstant = dyn_cast<ConstantInt>(icmpInst->getOperand(1));
          // 如果两边都是常数就可以比较（记得先实现常量传播）
          if (lhsConstant && rhsConstant) {
            BasicBlock* succBlock;
            switch (icmpInst->getPredicate()) {
              case ICmpInst::ICMP_SLT: {
                if (lhsConstant->getSExtValue() < rhsConstant->getSExtValue())
                  succBlock = branchInst->getSuccessor(0);
                else
                  succBlock = branchInst->getSuccessor(1);
              } break;
              case ICmpInst::ICMP_SGT: {
                if (lhsConstant->getSExtValue() > rhsConstant->getSExtValue())
                  succBlock = branchInst->getSuccessor(0);
                else
                  succBlock = branchInst->getSuccessor(1);
              } break;
              case ICmpInst::ICMP_EQ: {
                if (lhsConstant->getSExtValue() == rhsConstant->getSExtValue())
                  succBlock = branchInst->getSuccessor(0);
                else
                  succBlock = branchInst->getSuccessor(1);
              } break;
              case ICmpInst::ICMP_NE: {
                if (lhsConstant->getSExtValue() != rhsConstant->getSExtValue())
                  succBlock = branchInst->getSuccessor(0);
                else
                  succBlock = branchInst->getSuccessor(1);
              } break;
              case ICmpInst::ICMP_SLE: {
                if (lhsConstant->getSExtValue() <= rhsConstant->getSExtValue())
                  succBlock = branchInst->getSuccessor(0);
                else
                  succBlock = branchInst->getSuccessor(1);
              } break;
              case ICmpInst::ICMP_SGE: {
                if (lhsConstant->getSExtValue() >= rhsConstant->getSExtValue())
                  succBlock = branchInst->getSuccessor(0);
                else
                  succBlock = branchInst->getSuccessor(1);
              } break;
              default:
                abort(); // 没有考虑到的情况
                break;
            }
            cfg[curBlock].insert(succBlock);
            if (visited.find(succBlock) == visited.end())
              workList.push(succBlock);
          }
          // 不能提前预知跳转情况，就添加两条边
          else {
            for (int idx = 0; idx < branchInst->getNumSuccessors(); idx++) {
              auto succBlock = branchInst->getSuccessor(idx);
              cfg[curBlock].insert(succBlock);
              if (visited.find(succBlock) == visited.end())
                workList.push(succBlock);
            }
          }
        }
      }
    }
    return cfg;
  }

private:
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<
    MyCFGAnalysis>; // AnalysisKey是私有变量，所以需要声明为友元类
};
AnalysisKey MyCFGAnalysis::Key;

/**
 * 理论上基于基本块数据流图的分析函数，这里没有使用
 */
class DefineUseChain : public llvm::AnalysisInfoMixin<DefineUseChain>
{
public:
  using Result = std::unordered_map<
    Instruction*,
    std::unordered_map<Instruction*, std::vector<Instruction*>>>;

  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager& MAM)
  {
    /**
     * 对于Alloca指令，Store是它的使用
     * 对于Store指令，Load是它的使用
     * 这里只计算Store指令和Load指令之间的关系
     */

    /**
     * 计算每个基本块的定值和引用
     */
    std::unordered_map<BasicBlock*, std::unordered_set<Instruction*>>
      blockDefines; // 基本块中首次出现是定值的形式的那些变量
    std::unordered_map<BasicBlock*, std::unordered_set<Instruction*>>
      blockUses; // 基本块中首次出现是引用的形式
    // 计算defB和useB
    for (auto& func : mod) {
      for (auto& block : func) {
        auto& curBlockUse = blockUses[&block];
        auto& curBlockDefine = blockDefines[&block];
        for (auto& inst : block) {
          if (auto loadInst = dyn_cast<LoadInst>(&inst)) {
            auto storeInst = dyn_cast<StoreInst>(loadInst->getPointerOperand());
            // 首次出现就是引用，也就是在定值集中没有出现
            if (curBlockDefine.find(storeInst) == curBlockDefine.end())
              curBlockUse.insert(loadInst);
          } else if (auto storeInst = dyn_cast<StoreInst>(&inst)) {
            auto loadInst = dyn_cast<LoadInst>(storeInst->getPointerOperand());
            // 首次出现是以定值的形式出现
            if (curBlockUse.find(loadInst) == curBlockUse.end())
              curBlockDefine.insert(storeInst);
          }
        }
      }
    }

    /**
     * 根据数据流方程计算每个基本块入口处和出口处的活跃变量集合
     */
    // 基本块入口处活跃变量集合：也就是后续指令会引用当前基本块后面的值
    std::unordered_map<BasicBlock*, std::unordered_set<Instruction*>> blockIns;
    // 基本块出口处活跃变量集合：后续指令会引用在B出口处的值
    std::unordered_map<BasicBlock*, std::unordered_set<Instruction*>> blockOuts;
    bool flag = true;
    while (flag) {
      flag = false;
      for (auto& func : mod) {
        for (auto& block : func) {
          auto& curBlockIn = blockIns[&block];
          auto& curBlockOut = blockOuts[&block];
          auto& curBlockDef = blockDefines[&block];
          auto& curBlockUse = blockUses[&block];

          // 将后继基本块的入口处的活跃变量加入到当前基本块出口活跃变量集
          for (auto nxtBlockIter = succ_begin(&block),
                    nxtBlockIterEnd = succ_end(&block);
               nxtBlockIter != nxtBlockIterEnd;
               ++nxtBlockIter) {
            auto nxtBlock = *nxtBlockIter;
            for (auto it = blockIns[nxtBlock].begin();
                 it != blockIns[nxtBlock].end();
                 it++) {
              curBlockOut.insert(*it);
            }
          }

          // 更新当前基本块入口处的活跃变量
          for (auto it = curBlockUse.begin(); it != curBlockUse.end(); it++) {
            if (curBlockIn.find(*it) == curBlockIn.end()) {
              curBlockIn.insert(*it);
              flag = true;
            }
          }
          for (auto it = curBlockOut.begin(); it != curBlockOut.end(); it++) {
            if (curBlockDef.find(*it) == curBlockDef.end() &&
                curBlockIn.find(*it) == curBlockIn.end()) {
              curBlockIn.insert(*it);
              flag = true;
            }
          }
        }
      }
    }

    /**
     * 计算每个定值的引用链
     * 对于Store的所有Use，如果某个Use对应Load指令，且Load指令出现在某基本块的活跃变量中，则Load指令能到达该活跃变量
     * 活跃变量就是在基本块或者后续基本块中被Load的指令结果
     */
    Result defineUseChain;

    return defineUseChain;
  }

private:
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<
    DefineUseChain>; // AnalysisKey是私有变量，所以需要声明为友元类
};

AnalysisKey DefineUseChain::Key;