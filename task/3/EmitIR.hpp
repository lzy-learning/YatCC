#include "asg.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include<stack>
#include<unordered_set>

class EmitIR
{
public:
  Obj::Mgr& mMgr;
  llvm::Module mMod;

  EmitIR(Obj::Mgr& mgr, llvm::LLVMContext& ctx, llvm::StringRef mid = "-");

  llvm::Module& operator()(asg::TranslationUnit* tu);

private:
  llvm::LLVMContext& mCtx;

  llvm::Type* mIntTy;
  llvm::FunctionType* mCtorTy;

  llvm::Function* mCurFunc;
  std::unique_ptr<llvm::IRBuilder<>> mCurIrb;

  std::stack<llvm::BasicBlock*> mBlockStack;  // 用于IfStmt的解析，主要是结束块的层层跳转
  std::stack<llvm::BasicBlock*> mBreakStack;   // 用于break语句，主要保存它们所在的基本块，在endBlock生成的时候取出并生成br语句
  std::stack<llvm::BasicBlock*> mContinueStack; // 用于continue语句
  std::unordered_set<llvm::BasicBlock*> mPrepareToEndBlock;   // 用于保存那些已经在mBreakStack和mContinueStack中的基本块，防止给它们添加终结指令

  std::stack<llvm::Value*> mShortCircuitAndValue;
  std::stack<llvm::Value*> mShortCircuitOrValue;
  std::stack<llvm::BasicBlock*> mShortCircuitAndBlock;
  std::stack<llvm::BasicBlock*> mShortCircuitOrBlock;

  std::stack<llvm::Value*> mLandLhsValues;
  std::stack<llvm::BasicBlock*> mLandLhsBlocks;  // 这两个数据结构是为了解决短路求值中land.lhs.*基本块没有结束语句的问题

  //============================================================================
  // 类型
  //============================================================================

  llvm::Type* operator()(const asg::Type* type);

  //============================================================================
  // 表达式
  //============================================================================

  llvm::Value* operator()(asg::Expr* obj);

  llvm::Constant* operator()(asg::IntegerLiteral* obj);

  // TODO: 添加表达式处理相关声明
  llvm::Value* operator()(asg::StringLiteral* obj);
  llvm::Value* operator()(asg::DeclRefExpr* obj);
  llvm::Value* operator()(asg::ParenExpr* obj);
  llvm::Value* operator()(asg::UnaryExpr* obj);
  llvm::Value* operator()(asg::BinaryExpr* obj);
  llvm::Value* operator()(asg::CallExpr* obj);
  llvm::Value* operator()(asg::InitListExpr* obj);
  llvm::Value* operator()(asg::ImplicitInitExpr* obj);
  llvm::Value* operator()(asg::ImplicitCastExpr* obj);

  //============================================================================
  // 语句
  //============================================================================

  void operator()(asg::Stmt* obj);

  void operator()(asg::CompoundStmt* obj);

  void operator()(asg::ReturnStmt* obj);

  void operator()(asg::NullStmt* obj);
  void operator()(asg::DeclStmt* obj);
  void operator()(asg::ExprStmt* obj);
  void operator()(asg::IfStmt* obj);
  void operator()(asg::WhileStmt* obj);
  void operator()(asg::BreakStmt* obj);
  void operator()(asg::ContinueStmt* obj);

  // TODO: 添加语句处理相关声明

  //============================================================================
  // 声明
  //============================================================================

  void operator()(asg::Decl* obj);

  void operator()(asg::FunctionDecl* obj);

  // TODO: 添加声明处理相关声明
  void operator()(asg::VarDecl* obj);
};
