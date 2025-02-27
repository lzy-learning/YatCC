#pragma once

#include <string>
#include <string_view>
#include <cstring>

namespace lex {

enum Id
{
  YYEMPTY = -2,
  YYEOF = 0,     /* "end of file"  */
  YYerror = 256, /* error  */
  YYUNDEF = 257, /* "invalid token"  */
  IDENTIFIER,
  CONSTANT,
  STRING_LITERAL,
  INT,
  RETURN,
  L_BRACE,
  R_BRACE,
  L_SQUARE,
  R_SQUARE,
  L_PAREN,
  R_PAREN,
  SEMI,
  EQUAL,
  PLUS,
  COMMA,
  MINUS,
  STAR,
  SLASH,    // /
  PERCENT,  // %
  LESS,   // <
  GREATER,    // >
  LESSEQUAL,    // <=
  GREATEREQUAL, // >=
  EQUALEQUAL, // ==
  EXCLAIMEQUAL, // !=
  AMPAMP,   // &&
  PIPEPIPE, // ||
  EXCLAIM,    // !
  IF,
  ELSE,
  WHILE,
  BREAK,
  CONTINUE,
  CONST,   // 关键字 const
  VOID
};

const char*
id2str(Id id);

struct G
{
  Id mId{ YYEOF };              // 词号
  std::string_view mText;       // 对应文本
  std::string mFile;            // 文件路径
  int mLine{ 1 }, mColumn{ 1 }; // 行号、列号
  bool mStartOfLine{ true };    // 是否是行首
  bool mLeadingSpace{ false };  // 是否有前导空格
};

extern G g;

int
come(int tokenId, const char* yytext, int yyleng, int yylineno);

std::string get_filename(const std::string& line);
int get_line_number(const std::string& line);
int handle_preprocessed_info(const char* yytext, int yyleng);
int handle_wrap_and_space(const char* yytext, int yyleng);

} // namespace lex
