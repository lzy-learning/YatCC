#include "EmitIR.hpp"
#include <functional>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#define self (*this)

using namespace asg;

EmitIR::EmitIR(Obj::Mgr& mgr, llvm::LLVMContext& ctx, llvm::StringRef mid)
  : mMgr(mgr)
  , mMod(mid, ctx)
  , mCtx(ctx)
  , mIntTy(llvm::Type::getInt32Ty(ctx))
  , mCurIrb(std::make_unique<llvm::IRBuilder<>>(ctx))
  , mCtorTy(llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false))
  , mCurFunc(nullptr)
{
}

llvm::Module&
EmitIR::operator()(asg::TranslationUnit* tu)
{
  for (auto&& i : tu->decls)
    self(i);
  return mMod;
}

//==============================================================================
// 类型
//==============================================================================

llvm::Type*
EmitIR::operator()(const Type* type)
{
  if (type->texp == nullptr) {
    switch (type->spec) {
      case Type::Spec::kInt:
        return llvm::Type::getInt32Ty(mCtx);
      // TODO: 在此添加对更多基础类型的处理
      case Type::Spec::kVoid:
        return llvm::Type::getVoidTy(mCtx);
      case Type::Spec::kChar:
        return llvm::IntegerType::get(mCtx, 8);
      case Type::Spec::kLong:
        return llvm::IntegerType::get(mCtx, 32);
      case Type::Spec::kLongLong:
        return llvm::IntegerType::get(mCtx, 64);

      default:
        ABORT();
    }
  }

  // 子类型的信息，
  Type subt;
  subt.spec = type->spec;
  subt.qual = type->qual;
  subt.texp = type->texp->sub; // texp就是TypeExpr的简写，表示类型表达式

  // TODO: 在此添加对指针类型、数组类型和函数类型的处理

  if (auto p = type->texp->dcst<FunctionType>()) {
    std::vector<llvm::Type*> pty;
    // TODO: 在此添加对函数参数类型的处理
    for (auto&& i : p->params)
      pty.push_back(self(i));
    return llvm::FunctionType::get(self(&subt), std::move(pty), false);
  } else if (auto p = type->texp->dcst<PointerType>()) {
    return llvm::PointerType::get(self(&subt), 0);
  } else if (auto p = type->texp->dcst<ArrayType>()) {
    return llvm::ArrayType::get(self(&subt), p->len);
  }

  ABORT();
}

//==============================================================================
// 表达式
//==============================================================================

llvm::Value*
EmitIR::operator()(Expr* obj)
{
  // TODO: 在此添加对更多表达式处理的跳转
  if (auto p = obj->dcst<IntegerLiteral>())
    return self(p);
  else if (auto p = obj->dcst<StringLiteral>())
    return self(p);
  else if (auto p = obj->dcst<DeclRefExpr>())
    return self(p);
  else if (auto p = obj->dcst<ParenExpr>())
    return self(p);
  else if (auto p = obj->dcst<UnaryExpr>())
    return self(p);
  else if (auto p = obj->dcst<BinaryExpr>())
    return self(p);
  else if (auto p = obj->dcst<CallExpr>())
    return self(p);
  else if (auto p = obj->dcst<InitListExpr>())
    return self(p);
  else if (auto p = obj->dcst<ImplicitInitExpr>())
    return self(p);
  else if (auto p = obj->dcst<ImplicitCastExpr>())
    return self(p);

  ABORT();
}

llvm::Constant*
EmitIR::operator()(IntegerLiteral* obj)
{
  return llvm::ConstantInt::get(self(obj->type), obj->val);
}

// TODO: 在此添加对更多表达式类型的处理
llvm::Value*
EmitIR::operator()(StringLiteral* obj)
{
  return llvm::ConstantDataArray::getString(mCtx, llvm::StringRef(obj->val));
}

llvm::Value*
EmitIR::operator()(DeclRefExpr* obj)
{
  auto& irb = *mCurIrb;
  if (mCurFunc != nullptr) {
    if (mCurFunc->getValueSymbolTable()->lookup(obj->decl->name + ".addr") !=
        nullptr) {
      llvm::Value* a =
        mCurFunc->getValueSymbolTable()->lookup(obj->decl->name + ".addr");
      // 这里不要load，后面有ImplicitCast帮你load，这里主要还是找到变量的地址
      return a;
    } else if (mCurFunc->getValueSymbolTable()->lookup(obj->decl->name) !=
               nullptr) {
      llvm::Value* a = mCurFunc->getValueSymbolTable()->lookup(obj->decl->name);
      return a;
    } else
      return mMod.getGlobalVariable(obj->decl->name);
  }
  return mMod.getGlobalVariable(obj->decl->name);

  // 也可以在处理变量声明的时候将CreateAlloca返回值存在obj的any字段中
  // return reinterpret_cast<llvm::Value*>(obj->decl->any);
}

llvm::Value*
EmitIR::operator()(ParenExpr* obj)
{
  return self(obj->sub);
}

llvm::Value*
EmitIR::operator()(UnaryExpr* obj)
{
  switch (obj->op) {
    case UnaryExpr::Op::kNot: {
      llvm::Value* boolValue = self(obj->sub);
      if (!boolValue->getType()->isIntegerTy(1))
        boolValue = mCurIrb->CreateICmpNE(
          boolValue, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));
      return mCurIrb->CreateNot(boolValue);
    }
    case UnaryExpr::Op::kNeg: {
      return mCurIrb->CreateNeg(self(obj->sub));
    }
    case UnaryExpr::Op::kPos: {
      return self(obj->sub);
    }
    case UnaryExpr::Op::kINVALID: {
      ABORT();
    }
    default:
      ABORT();
  }
}
llvm::Value*
EmitIR::operator()(BinaryExpr* obj)
{
  // if (obj->op == BinaryExpr::kIndex) {
  //   std::vector<llvm::Value*> idxList = { mCurIrb->CreateSExt(self(obj->rht), llvm::Type::getInt64Ty(mCtx)) };
  //   llvm::Value* arr = nullptr;
  //   llvm::ArrayType* arrType = nullptr;
  //   std::function<void(Expr*)> handleFetchArray = [&](Expr* expr) {
  //     if (expr->dcst<ImplicitCastExpr>() != nullptr) {
  //       auto cast = expr->dcst<ImplicitCastExpr>();
  //       if (cast->sub->dcst<DeclRefExpr>() != nullptr) {
  //         arr = mCurFunc->getValueSymbolTable()->lookup(
  //           cast->sub->dcst<DeclRefExpr>()->decl->name);
  //         if (arr == nullptr)
  //           arr = mMod.getGlobalVariable(
  //             cast->sub->dcst<DeclRefExpr>()->decl->name);
  //         arrType = llvm::dyn_cast<llvm::ArrayType>(
  //           self(cast->sub->dcst<DeclRefExpr>()->decl->type));
  //       } else if (cast->sub->dcst<BinaryExpr>() != nullptr) {
  //         auto binaryExpr = cast->sub->dcst<BinaryExpr>();
  //         idxList.push_back(mCurIrb->getInt64(
  //           llvm::dyn_cast<llvm::ConstantInt>(self(binaryExpr->rht))
  //             ->getZExtValue()));
  //         handleFetchArray(binaryExpr->lft);
  //       } else
  //         ABORT();
  //     }
  //   };
  //   handleFetchArray(obj->lft);
  //   arr = mCurIrb->CreateLoad(arrType, arr);
  //   if (arr->getType()->isArrayTy()) {
  //     idxList.push_back(mCurIrb->getInt64(0));
  //     std::reverse(idxList.begin(), idxList.end());
  //     llvm::Value* element = mCurIrb->CreateInBoundsGEP(arrType, arr, idxList);
  //     return element;
  //   }else{
  //     std::reverse(idxList.begin(), idxList.end());
  //     llvm::Value* element = mCurIrb->CreateInBoundsGEP(mCurIrb->getInt32Ty(), arr, idxList);
  //     return element;
  //   }
  // }
  // 在条件语句中，&&和||的短路求值实现
  if (obj->op == BinaryExpr::kAnd) {
    llvm::Value* lft = self(obj->lft);
    if (!lft->getType()->isIntegerTy(1))
      lft = mCurIrb->CreateICmpNE(
        lft, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));

    auto continueJudgeBlock =
      llvm::BasicBlock::Create(mCtx, "land.lhs.true", mCurFunc);

    mShortCircuitAndValue.push(lft);
    mShortCircuitAndBlock.push(mCurIrb->GetInsertBlock());
    mShortCircuitAndBlock.push(continueJudgeBlock);

    mCurIrb->SetInsertPoint(continueJudgeBlock);
    llvm::Value* rht = self(obj->rht);

    mLandLhsValues.push(rht);
    mLandLhsBlocks.push(continueJudgeBlock);
    return rht;
  }
  if (obj->op == BinaryExpr::kOr) {
    llvm::Value* lft = self(obj->lft);
    if (!lft->getType()->isIntegerTy(1))
      lft = mCurIrb->CreateICmpNE(
        lft, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));
    lft = mCurIrb->CreateICmpNE(
      lft, llvm::ConstantInt::get(mCurIrb->getInt1Ty(), 1));

    llvm::IRBuilderBase::InsertPoint savedInsertPoint = mCurIrb->saveIP();

    auto continueJudgeBlock =
      llvm::BasicBlock::Create(mCtx, "land.lhs.false", mCurFunc);

    if (!mShortCircuitAndBlock.empty()) {
      llvm::BasicBlock* nxt = mShortCircuitAndBlock.top();
      mShortCircuitAndBlock.pop();
      if (mShortCircuitAndBlock.empty())
        ABORT();
      llvm::BasicBlock* cur = mShortCircuitAndBlock.top();
      mShortCircuitAndBlock.pop();
      llvm::Value* judgeCond = mShortCircuitAndValue.top();
      mShortCircuitAndValue.pop();
      mCurIrb->SetInsertPoint(cur);
      mCurIrb->CreateCondBr(judgeCond, nxt, continueJudgeBlock);
    }
    mCurIrb->restoreIP(savedInsertPoint);

    mShortCircuitOrValue.push(lft);
    mShortCircuitOrBlock.push(mCurIrb->GetInsertBlock());
    mShortCircuitOrBlock.push(continueJudgeBlock);

    mCurIrb->SetInsertPoint(continueJudgeBlock);
    llvm::Value* rht = self(obj->rht);
    return rht;
  }
  llvm::Value* lft = self(obj->lft);
  llvm::Value* rht = self(obj->rht);
  switch (obj->op) {
    case BinaryExpr::kMul: {
      return mCurIrb->CreateMul(lft, rht);
    }
    case BinaryExpr::kDiv: {
      return mCurIrb->CreateSDiv(lft, rht);
    }
    case BinaryExpr::kMod: {
      return mCurIrb->CreateSRem(lft, rht);
    }
    case BinaryExpr::kAdd: {
      return mCurIrb->CreateAdd(lft, rht);
    }
    case BinaryExpr::kSub: {
      return mCurIrb->CreateSub(lft, rht);
    }
    case BinaryExpr::kGt: {
      return mCurIrb->CreateICmpSGT(lft, rht);
    }
    case BinaryExpr::kLt: {
      return mCurIrb->CreateICmpSLT(lft, rht);
    }
    case BinaryExpr::kGe: {
      return mCurIrb->CreateICmpSGE(lft, rht);
    }
    case BinaryExpr::kLe: {
      return mCurIrb->CreateICmpSLE(lft, rht);
    }
    case BinaryExpr::kEq: {
      return mCurIrb->CreateICmpEQ(lft, rht);
    }
    case BinaryExpr::kNe: {
      return mCurIrb->CreateICmpNE(lft, rht);
    }
    // case BinaryExpr::kAnd: {
    //   if (!lft->getType()->isIntegerTy(1))
    //     lft = mCurIrb->CreateICmpNE(
    //       lft, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));
    //   if (!rht->getType()->isIntegerTy(1))
    //     rht = mCurIrb->CreateICmpNE(
    //       rht, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));
    //   return mCurIrb->CreateAnd(lft, rht);
    // }
    // case BinaryExpr::kOr: {
    //   if (!lft->getType()->isIntegerTy(1))
    //     lft = mCurIrb->CreateICmpNE(
    //       lft, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));
    //   if (!rht->getType()->isIntegerTy(1))
    //     rht = mCurIrb->CreateICmpNE(
    //       rht, llvm::ConstantInt::get(mCurIrb->getInt32Ty(), 0));
    //   return mCurIrb->CreateOr(lft, rht);
    // }
    case BinaryExpr::kAssign: {
      return mCurIrb->CreateStore(rht, lft);
    }
    case BinaryExpr::kComma: {
      return rht;
    }
    case BinaryExpr::kIndex: {
      // rht始终是一个IntegerLiteral，对应着数组下标
      // 在llvm中对通过数组下标取值，会先发生一个ArrayToPointerDecay的隐式类型转换，所以我们在这里只需要操作指针
      return mCurIrb->CreateInBoundsGEP(
        self(obj->type),
        lft,
        { mCurIrb->CreateSExt(rht, llvm::Type::getInt64Ty(mCtx)) });
    }
    default:
      ABORT();
  }
}
llvm::Value*
EmitIR::operator()(CallExpr* obj)
{
  std::string funcName =
    obj->head->dcst<ImplicitCastExpr>()->sub->dcst<DeclRefExpr>()->decl->name;
  llvm::Function* func = mMod.getFunction(funcName);
  unsigned int argsNum = (obj->args).size();
  std::vector<llvm::Value*> args(argsNum, nullptr);
  for (int i = 0; i < argsNum; i++) {
    args[i] = self(obj->args[i]);
  }

  return mCurIrb->CreateCall(func, llvm::ArrayRef<llvm::Value*>(args));
}
llvm::Value*
EmitIR::operator()(InitListExpr* obj)
{
  // std::vector<llvm::Value*> args(obj->list.size(), nullptr);
  // int k = 0;
  // for (auto&& expr : obj->list) {
  //   args[k++] = self(expr);
  // }
  // return args;
  return nullptr;
}
// 隐式初始化表达式，一般用于int a[5] = {};这种情况
llvm::Value*
EmitIR::operator()(ImplicitInitExpr* obj)
{
  return nullptr;
}

llvm::Value*
EmitIR::operator()(ImplicitCastExpr* obj)
{
  auto sub = self(obj->sub);
  auto& irb = *mCurIrb;

  switch (obj->kind) {
    case ImplicitCastExpr::kLValueToRValue: {
      auto ty = self(obj->sub->type);
      auto loadVal = irb.CreateLoad(ty, sub);
      return loadVal;
    }
    case ImplicitCastExpr::kIntegralCast: {
      auto srcType = self(obj->sub->type);
      auto destType = self(obj->type);

      auto castVal = irb.CreateIntCast(sub, destType, true);
      return castVal;
    }
    case ImplicitCastExpr::kFunctionToPointerDecay: {
      return sub;
    }
    case ImplicitCastExpr::kNoOp: {
      return sub;
    }
    case ImplicitCastExpr::kArrayToPointerDecay: {
      auto arrayType = llvm::dyn_cast<llvm::ArrayType>(self(obj->sub->type));
      llvm::Type* elementType = arrayType->getElementType();
      std::vector<llvm::Value*> idxList = { irb.getInt64(0), irb.getInt64(0) };
      // while (elementType->isArrayTy()) {
      //   idxList.push_back(irb.getInt64(0));
      //   elementType = elementType->getArrayElementType();
      // }
      return irb.CreateInBoundsGEP(arrayType, sub, idxList);
    }
    case ImplicitCastExpr::kINVALID: {
      return llvm::ConstantPointerNull::get(irb.getInt8PtrTy());
    }
    default:
      ABORT();
  }
}

//==============================================================================
// 语句
//==============================================================================

void
EmitIR::operator()(Stmt* obj)
{
  // TODO: 在此添加对更多Stmt类型的处理的跳转

  if (auto p = obj->dcst<CompoundStmt>())
    return self(p);
  if (auto p = obj->dcst<ReturnStmt>())
    return self(p);
  if (auto p = obj->dcst<NullStmt>())
    return self(p);
  if (auto p = obj->dcst<DeclStmt>())
    return self(p);
  if (auto p = obj->dcst<ExprStmt>())
    return self(p);
  if (auto p = obj->dcst<IfStmt>())
    return self(p);
  if (auto p = obj->dcst<WhileStmt>())
    return self(p);
  if (auto p = obj->dcst<BreakStmt>())
    return self(p);
  if (auto p = obj->dcst<ContinueStmt>())
    return self(p);
  ABORT();
}

// TODO: 在此添加对更多Stmt类型的处理

void
EmitIR::operator()(CompoundStmt* obj)
{
  // TODO: 可以在此添加对符号重名的处理
  // 因为不同作用域的变量可能重名，所以需要重命名
  for (auto&& stmt : obj->subs)
    self(stmt);
}

void
EmitIR::operator()(ReturnStmt* obj)
{
  auto& irb = *mCurIrb;

  llvm::Value* retVal;
  if (!obj->expr)
    retVal = nullptr;
  else
    retVal = self(obj->expr);

  mCurIrb->CreateRet(retVal);
  // auto exitBb = llvm::BasicBlock::Create(mCtx, "return_exit", mCurFunc);
  // mCurIrb = std::make_unique<llvm::IRBuilder<>>(exitBb);
}

void
EmitIR::operator()(NullStmt* obj)
{
  // auto& irb = *mCurIrb;
  // irb.CreateRetVoid();
}

void
EmitIR::operator()(DeclStmt* obj)
{
  for (auto&& decl : obj->decls)
    self(decl);
}
void
EmitIR::operator()(ExprStmt* obj)
{
  self(obj->expr);
}
void
EmitIR::operator()(IfStmt* obj)
{
  // 后续补充短路求值思路，也就是多个&&语句的时候，当表达式为false的时候就不用判断后续的表达式了
  llvm::Value* condVal = self(obj->cond);
  if (!condVal->getType()->isIntegerTy(1))
    condVal = mCurIrb->CreateICmpNE(
      condVal, llvm::ConstantInt::get(condVal->getType(), 0));
  auto thenBb = llvm::BasicBlock::Create(mCtx, "if.then", mCurFunc);
  auto elseBb = llvm::BasicBlock::Create(mCtx, "if.else", mCurFunc);
  mCurIrb->CreateCondBr(condVal, thenBb, elseBb);

  while (!mShortCircuitAndBlock.empty()) {
    llvm::BasicBlock* nxt = mShortCircuitAndBlock.top();
    mShortCircuitAndBlock.pop();
    if (mShortCircuitAndBlock.empty())
      ABORT();
    llvm::BasicBlock* cur = mShortCircuitAndBlock.top();
    mShortCircuitAndBlock.pop();
    llvm::Value* jumpVal = mShortCircuitAndValue.top();
    mShortCircuitAndValue.pop();
    mCurIrb->SetInsertPoint(cur);
    mCurIrb->CreateCondBr(jumpVal, nxt, elseBb);
  }
  while (!mShortCircuitOrBlock.empty()) {
    llvm::BasicBlock* nxt = mShortCircuitOrBlock.top();
    mShortCircuitOrBlock.pop();
    if (mShortCircuitOrBlock.empty())
      ABORT();
    llvm::BasicBlock* cur = mShortCircuitOrBlock.top();
    mShortCircuitOrBlock.pop();
    llvm::Value* jumpVal = mShortCircuitOrValue.top();
    mShortCircuitOrValue.pop();
    mCurIrb->SetInsertPoint(cur);
    mCurIrb->CreateCondBr(jumpVal, nxt, thenBb);
  }

  mCurIrb->SetInsertPoint(thenBb);
  if (obj->then != nullptr) {
    self(obj->then);
  }
  mCurIrb->SetInsertPoint(elseBb);
  if (obj->else_ != nullptr) {
    self(obj->else_);
  }
  auto endBb = llvm::BasicBlock::Create(mCtx, "if.end", mCurFunc);

  mCurIrb->SetInsertPoint(thenBb);
  if (thenBb->getTerminator() == nullptr &&
      mPrepareToEndBlock.find(thenBb) == mPrepareToEndBlock.end())
    mCurIrb->CreateBr(endBb);
  mCurIrb->SetInsertPoint(elseBb);
  if (elseBb->getTerminator() == nullptr &&
      mPrepareToEndBlock.find(elseBb) == mPrepareToEndBlock.end())
    mCurIrb->CreateBr(endBb);

  if (!mBlockStack.empty()) {
    llvm::BasicBlock* block = mBlockStack.top();
    mBlockStack.pop();
    if (block->getTerminator() == nullptr) {
      mCurIrb->SetInsertPoint(block);
      mCurIrb->CreateBr(endBb);
    }
  }
  mCurIrb->SetInsertPoint(endBb);
  mBlockStack.push(endBb);
}
void
EmitIR::operator()(WhileStmt* obj)
{
  auto condBb = llvm::BasicBlock::Create(mCtx, "while.cond", mCurFunc);

  mCurIrb->CreateBr(condBb);

  mCurIrb->SetInsertPoint(condBb);
  llvm::Value* condVal = self(obj->cond);
  if (!condVal->getType()->isIntegerTy(1))
    condVal = mCurIrb->CreateICmpNE(
      condVal, llvm::ConstantInt::get(condVal->getType(), 0));
  
  auto bodyBb = llvm::BasicBlock::Create(mCtx, "while.body", mCurFunc);
  // 处理while循环中的||短路
  while (!mShortCircuitOrBlock.empty()) {
    llvm::BasicBlock* nxt = mShortCircuitOrBlock.top();
    mShortCircuitOrBlock.pop();
    if (mShortCircuitOrBlock.empty())
      ABORT();
    llvm::BasicBlock* cur = mShortCircuitOrBlock.top();
    mShortCircuitOrBlock.pop();
    llvm::Value* jumpVal = mShortCircuitOrValue.top();
    mShortCircuitOrValue.pop();
    mCurIrb->SetInsertPoint(cur);
    mCurIrb->CreateCondBr(jumpVal, nxt, bodyBb);
  }

  // 需要保存&&短路，因为&&短路需要跳到while.end，而while.end必须等到obj->body翻译完后才创建
  std::stack<llvm::BasicBlock*> storeAndBlock = mShortCircuitAndBlock;
  std::stack<llvm::Value*> storeAndValue = mShortCircuitAndValue;
  while(!mShortCircuitAndBlock.empty())mShortCircuitAndBlock.pop();
  while(!mShortCircuitAndValue.empty())mShortCircuitAndValue.pop();

  mCurIrb->SetInsertPoint(bodyBb);
  if (obj->body != nullptr) {
    self(obj->body);
  }
  // 如果在循环体中有br语句，比如while套一个if，那么不需要给bodyBb加回到condBb的语句，而需要给if.end加
  if (bodyBb->getTerminator() == nullptr) {
    mCurIrb->SetInsertPoint(bodyBb);
    mCurIrb->CreateBr(condBb);
  }

  auto endBb = llvm::BasicBlock::Create(mCtx, "while.end", mCurFunc);
  while (!storeAndBlock.empty()) {
    llvm::BasicBlock* nxt = storeAndBlock.top();
    storeAndBlock.pop();
    if (storeAndBlock.empty())
      ABORT();
    llvm::BasicBlock* cur = storeAndBlock.top();
    storeAndBlock.pop();
    llvm::Value* jumpVal = storeAndValue.top();
    storeAndValue.pop();
    mCurIrb->SetInsertPoint(cur);
    mCurIrb->CreateCondBr(jumpVal, nxt, endBb);
  }
  
  while (!mBreakStack.empty()) {
    llvm::BasicBlock* breakBb = mBreakStack.top();
    mBreakStack.pop();
    mPrepareToEndBlock.erase(breakBb);
    mCurIrb->SetInsertPoint(breakBb);
    mCurIrb->CreateBr(endBb);
  }
  while (!mContinueStack.empty()) {
    llvm::BasicBlock* continueBb = mContinueStack.top();
    mContinueStack.pop();
    mPrepareToEndBlock.erase(continueBb);
    mCurIrb->SetInsertPoint(continueBb);
    mCurIrb->CreateBr(condBb);
  }

  if(condBb->getTerminator() == nullptr){
    mCurIrb->SetInsertPoint(condBb);
    mCurIrb->CreateCondBr(condVal, bodyBb, endBb);
  }

  while (!mBlockStack.empty()) {
    llvm::BasicBlock* block = mBlockStack.top();
    mBlockStack.pop();
    if (block->getTerminator() == nullptr) {
      mCurIrb->SetInsertPoint(block);
      mCurIrb->CreateBr(condBb);
    }
  }

  // 处理while短路求值过程中，land.lhs.*没有终结指令的问题
  while(!mLandLhsBlocks.empty()){
    if(mLandLhsBlocks.top()->getTerminator() == nullptr){
      mCurIrb->SetInsertPoint(mLandLhsBlocks.top());
      mCurIrb->CreateCondBr(mLandLhsValues.top(), bodyBb, endBb);
    }
    mLandLhsBlocks.pop();
    mLandLhsValues.pop();
  }

  mCurIrb->SetInsertPoint(endBb);
  mBlockStack.push(endBb);
}
void
EmitIR::operator()(BreakStmt* obj)
{
  mBreakStack.push(mCurIrb->GetInsertBlock());
  mPrepareToEndBlock.insert(mCurIrb->GetInsertBlock());
}
void
EmitIR::operator()(ContinueStmt* obj)
{
  mContinueStack.push(mCurIrb->GetInsertBlock());
  mPrepareToEndBlock.insert(mCurIrb->GetInsertBlock());
}

//==============================================================================
// 声明
//==============================================================================

void
EmitIR::operator()(Decl* obj)
{
  // TODO: 添加变量声明处理的跳转

  if (auto p = obj->dcst<FunctionDecl>())
    return self(p);

  else if (auto p = obj->dcst<VarDecl>())
    return self(p);
  ABORT();
}

// TODO: 添加变量声明的处理
void
EmitIR::operator()(VarDecl* obj)
{
  // 类型
  llvm::Type* ty = self(obj->type);

  // 局部变量声明
  if (mCurFunc != nullptr) {
    // 如果局部变量已经存在，则重新进行初始化
    llvm::Value* var = mCurFunc->getValueSymbolTable()->lookup(obj->name);
    if(var != nullptr){
      llvm::Value* initVal = self(obj->init);
      mCurIrb->CreateStore(initVal, var);
      return;
    }

    llvm::AllocaInst* alloc = mCurIrb->CreateAlloca(ty, nullptr, obj->name);
    obj->any = alloc;
    // 声明并初始化
    if (obj->init != nullptr) {
      if (dynamic_cast<InitListExpr*>(obj->init) != nullptr) {
        // 递归处理初始化列表的函数
        std::vector<llvm::Value*> initVals;
        bool zeroFill = false; // 是否用0填充，如果出现ImplicitInitExpr则用0填充
        std::function<void(InitListExpr * initListExpr)> handleInitListExpr =
          [&](InitListExpr* initListExpr) -> void {
          for (auto&& expr : initListExpr->list) {
            if (expr->dcst<ImplicitInitExpr>() != nullptr) {
              zeroFill = true;
              continue;
            }
            // 注意二维数组的InitListExpr的形式
            else if (expr->dcst<InitListExpr>() != nullptr)
              handleInitListExpr(expr->dcst<InitListExpr>());
            else
              initVals.push_back(self(expr));
          }
        };
        // 初始化列表初始化，这里一般对应数组类型
        handleInitListExpr(dynamic_cast<InitListExpr*>(obj->init));
        int initValNum = initVals.size();

        auto arrType = llvm::dyn_cast<llvm::ArrayType>(ty);
        uint64_t rowNum = arrType->getNumElements();
        // 一维数组和二维数组分开处理
        llvm::Type* elementType = arrType->getElementType();
        if (elementType->isArrayTy()) {
          // 二维数组
          uint64_t colNum = elementType->getArrayNumElements();
          if (initValNum > rowNum * colNum)
            ABORT();
          if (zeroFill) {
            while (initValNum < rowNum * colNum) {
              initVals.push_back(mCurIrb->getInt32(0));
              initValNum++;
            }
          }
          for (int i = 0; i < initValNum; i++) {
            llvm::Value* initVal = initVals[i];
            int r = i / colNum, c = i % colNum;
            llvm::Value* index[] = { mCurIrb->getInt64(0),
                                     mCurIrb->getInt64(r),
                                     mCurIrb->getInt64(c) };
            llvm::Value* ptr =
              mCurIrb->CreateInBoundsGEP(arrType, alloc, index);
            mCurIrb->CreateStore(initVal, ptr);
          }
        } else {
          // 一维数组
          if (initValNum > rowNum)
            ABORT();
          for (int i = 0; i < initValNum; i++) {
            llvm::Value* initVal = initVals[i];
            llvm::Value* index[] = { mCurIrb->getInt64(0),
                                     mCurIrb->getInt64(i) };
            llvm::Value* ptr =
              mCurIrb->CreateInBoundsGEP(arrType, alloc, index);
            mCurIrb->CreateStore(initVal, ptr);
          }
        }
      } else {
        // 普通的变量声明并初始化
        llvm::Value* initVal = self(obj->init);
        mCurIrb->CreateStore(initVal, alloc);
      }
    }
  }
  // 全局变量声明
  else {
    // 数组的初始化、值为表达式等等情况下，直接生成全局变量比较难，这里用函数去初始化全局变量
    // 1. 创建全局变量并进行零初始化
    bool isConstant = obj->type->qual.const_;
    /* 请注意如果是常量，通过函数的方式初始化显然不行，因为常量创建之后就不能通过store赋值了。。。*/
    llvm::GlobalVariable* gloVar = new llvm::GlobalVariable(
      mMod, ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, obj->name);
    gloVar->setInitializer(llvm::Constant::getNullValue(ty));
    if (obj->init != nullptr) {
      // 2. 创建函数为全局变量进行初始化逻辑
      llvm::Function* ctorFunc = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(mCtx), false),
        llvm::GlobalValue::PrivateLinkage,
        obj->name + "_ctor",
        mMod);
      // 创建基本块
      llvm::BasicBlock* entryBlock =
        llvm::BasicBlock::Create(mCtx, "entry", ctorFunc);
      mCurIrb->SetInsertPoint(entryBlock);
      // 初始化全局变量，对初始化列表特殊处理
      if (dynamic_cast<InitListExpr*>(obj->init) != nullptr) {
        // 递归处理初始化列表的函数
        std::vector<llvm::Value*> initVals;
        // 是否用0填充，如果出现ImplicitInitExpr则用0填充
        bool zeroFill = false;
        std::function<void(InitListExpr * initListExpr)> handleInitListExpr =
          [&](InitListExpr* initListExpr) -> void {
          for (auto&& expr : initListExpr->list) {
            if (expr->dcst<ImplicitInitExpr>() != nullptr) {
              zeroFill = true;
              continue;
            }
            // 注意二维数组的InitListExpr的形式
            else if (expr->dcst<InitListExpr>() != nullptr)
              handleInitListExpr(expr->dcst<InitListExpr>());
            else
              initVals.push_back(self(expr));
          }
        };
        // 初始化列表初始化，这里一般对应数组类型
        handleInitListExpr(dynamic_cast<InitListExpr*>(obj->init));
        int initValNum = initVals.size();

        auto arrType = llvm::dyn_cast<llvm::ArrayType>(ty);
        uint64_t rowNum = arrType->getNumElements();
        // 一维数组和二维数组分开处理
        llvm::Type* elementType = arrType->getElementType();
        if (elementType->isArrayTy()) {
          // 二维数组
          uint64_t colNum = elementType->getArrayNumElements();
          if (initValNum > rowNum * colNum)
            ABORT();
          if (zeroFill) {
            while (initValNum < rowNum * colNum) {
              initVals.push_back(mCurIrb->getInt32(0));
              initValNum++;
            }
          }
          for (int i = 0; i < initValNum; i++) {
            llvm::Value* initVal = initVals[i];
            int r = i / colNum, c = i % colNum;
            llvm::Value* index[] = { mCurIrb->getInt64(0),
                                     mCurIrb->getInt64(r),
                                     mCurIrb->getInt64(c) };
            llvm::Value* ptr =
              mCurIrb->CreateInBoundsGEP(arrType, gloVar, index);
            mCurIrb->CreateStore(initVal, ptr);
          }
        } else {
          // 一维数组
          if (initValNum > rowNum)
            ABORT();
          for (int i = 0; i < initValNum; i++) {
            llvm::Value* initVal = initVals[i];
            llvm::Value* index[] = { mCurIrb->getInt64(0),
                                     mCurIrb->getInt64(i) };
            llvm::Value* ptr =
              mCurIrb->CreateInBoundsGEP(arrType, gloVar, index);
            mCurIrb->CreateStore(initVal, ptr);
          }
        }
      } else {
        // 普通的变量声明并初始化
        llvm::Value* initVal = self(obj->init);
        mCurIrb->CreateStore(self(obj->init), gloVar);
      }
      llvm::appendToGlobalCtors(mMod, ctorFunc, 0);
      mCurIrb->CreateRetVoid();
    }
    obj->any = gloVar;
  }
}

void
EmitIR::operator()(FunctionDecl* obj)
{
  // 创建函数
  auto fty = llvm::dyn_cast<llvm::FunctionType>(self(obj->type));
  auto func = llvm::Function::Create(
    fty, llvm::GlobalVariable::ExternalLinkage, obj->name, mMod);

  obj->any = func;

  if (obj->body == nullptr)
    return;
  // 为函数创建基本块
  auto entryBb = llvm::BasicBlock::Create(mCtx, "entry", func);
  // 并将IR插入点改为entry基本块
  mCurIrb = std::make_unique<llvm::IRBuilder<>>(entryBb);
  auto& entryIrb = *mCurIrb;

  // TODO: 添加对函数参数的处理
  auto argBegin = func->arg_begin();
  int k = 0;
  while (argBegin != func->arg_end()) {
    argBegin->setName(obj->params[k]->name);
    llvm::Argument* arg = &(*argBegin);
    llvm::AllocaInst* allocaInst = entryIrb.CreateAlloca(
      arg->getType(), nullptr, obj->params[k]->name + ".addr");
    entryIrb.CreateStore(arg, allocaInst);
    ++argBegin;
    ++k;
  }

  // 翻译函数体
  llvm::Function* storeF = mCurFunc;
  while (!mBlockStack.empty())
    mBlockStack.pop();
  mCurFunc = func;
  self(obj->body);
  auto& exitIrb = *mCurIrb;

  // 这里只处理空返回值的情况就可以了，因为其他类型返回值的情况在ReturnStmt中已经处理了
  if (fty->getReturnType()->isVoidTy())
    exitIrb.CreateRetVoid();
  else if (mCurIrb->GetInsertBlock()->getTerminator() == nullptr) {
    exitIrb.CreateRet(llvm::Constant::getNullValue(fty->getReturnType()));
  }
  // else
  //   exitIrb.CreateUnreachable();
  mCurFunc = storeF;
}
