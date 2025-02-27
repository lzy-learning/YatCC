#include "lex.hpp"
#include <iostream>

void
print_token();

namespace lex {

static const char* kTokenNames[] = {
  "identifier",   "numeric_constant",   "string_literal",
  "int",          "return",             "l_brace",
  "r_brace",      "l_square",           "r_square",
  "l_paren",      "r_paren",            "semi",
  "equal",        "plus",               "comma",
  "minus", "star", "slash","percent",
  "less", "greater", "lessequal", "greaterequal",
  "equalequal", "exclaimequal", "ampamp", "pipepipe", 
  "exclaim",
  "if", "else", "while", "break", "continue",
  "const", "void"
};

const char*
id2str(Id id)
{
  static char sCharBuf[2] = { 0, 0 };
  if (id == Id::YYEOF) {
    return "eof";
  }
  else if (id < Id::IDENTIFIER) {
    sCharBuf[0] = char(id);
    return sCharBuf;
  }
  return kTokenNames[int(id) - int(Id::IDENTIFIER)];
}

G g;

int
come(int tokenId, const char* yytext, int yyleng, int yylineno)
{
  g.mId = Id(tokenId);
  g.mText = { yytext, std::size_t(yyleng) };
  // g.mLine = yylineno;

  print_token();
  g.mStartOfLine = false;
  g.mLeadingSpace = false;

  return tokenId;
}

/**
 * 根据预处理信息获取文件名
*/
std::string get_filename(const std::string& line){
  size_t filename_start = line.find('"');
  size_t filename_end = line.find('"', filename_start+1);
  return line.substr(filename_start+1, filename_end-filename_start-1);
}

/**
 * 根据预处理信息获取行号，一般是#后面的第一个数字
*/
int get_line_number(const std::string& line)
{
  size_t line_number_start = line.find(' ');
  size_t line_number_end = line.find(' ', line_number_start+1);
  std::string line_number_str = line.substr(line_number_start+1, line_number_end-line_number_start-1);
  return std::stoi(line_number_str);
}

/**
 * 处理预处理信息
*/
int handle_preprocessed_info(const char* yytext, int yyleng){
  std::string line{yytext, std::size_t(yyleng)};
  std::string filename =get_filename(line);
  // 判断是不是文件名，可以根据后缀来判断，这里使用一个track，因为大多数预处理信息都是"<...>"的形式，所以判断'<'符号在不在字符串中即可
  if(filename.find('<')==std::string::npos){
    // 处理文件名和行号，第一个数字就是起始行号
    g.mFile = get_filename(line);
    g.mLine = get_line_number(line)-1;  // 这里-1是因为后面会识别到换行符，为了抵消它的作用
  }
  return 1;
}

/**
 * 处理换行和空格、制表符等空白字符
*/
int handle_wrap_and_space(const char* yytext, int yyleng){
  std::string text{yytext, std::size_t(yyleng)};
  if(text == "\n"){
    g.mStartOfLine = true;
    g.mLine+=1;
    g.mColumn = 1;
  }else{
    g.mLeadingSpace = true;
  }
  return 1;
}
} // namespace lex
