%{
#include "lex.hpp"
/* 所有代码全部抽离出来，放到 lex.hpp 和 lex.cpp 里 */

using namespace lex;

#define ADDCOL() g.mColumn += yyleng;
#define COME(id) return come(id, yytext, yyleng, yylineno)
#define HANDLE_PREPROCESSED_INFO() handle_preprocessed_info(yytext, yyleng)
#define HANDLE_WRAP_AND_SPACE() handle_wrap_and_space(yytext, yyleng)
%}

%option 8bit warn noyywrap yylineno

D     [0-9]
L     [a-zA-Z_]
IS    ((u|U)|(u|U)?(l|L|ll|LL)|(l|L|ll|LL)(u|U))

%%

"int"       { ADDCOL(); COME(INT); }
"return"    { ADDCOL(); COME(RETURN); }

"("         { ADDCOL(); COME(L_PAREN); }
")"         { ADDCOL(); COME(R_PAREN); }
"["         { ADDCOL(); COME(L_SQUARE); }
"]"         { ADDCOL(); COME(R_SQUARE); }
"{"         { ADDCOL(); COME(L_BRACE); }
"}"         { ADDCOL(); COME(R_BRACE); }

"+"         { ADDCOL(); COME(PLUS); }

";"         { ADDCOL(); COME(SEMI); }
","         { ADDCOL(); COME(COMMA); }

"="         { ADDCOL(); COME(EQUAL); }

"-"         { ADDCOL(); COME(MINUS); }
"*"         { ADDCOL(); COME(STAR); }
"/"         { ADDCOL(); COME(SLASH); }
"%"         { ADDCOL(); COME(PERCENT); }
"<"         { ADDCOL(); COME(LESS); }
">"         { ADDCOL(); COME(GREATER); }
"<="         { ADDCOL(); COME(LESSEQUAL); }
">="         { ADDCOL(); COME(GREATEREQUAL); }
"=="         { ADDCOL(); COME(EQUALEQUAL); }
"!="         { ADDCOL(); COME(EXCLAIMEQUAL); }
"&&"         { ADDCOL(); COME(AMPAMP); }
"||"         { ADDCOL(); COME(PIPEPIPE); }
"!"         { ADDCOL(); COME(EXCLAIM); }

"if"         { ADDCOL(); COME(IF); }
"else"         { ADDCOL(); COME(ELSE); }
"while"         { ADDCOL(); COME(WHILE); }
"break"         { ADDCOL(); COME(BREAK); }
"continue"         { ADDCOL(); COME(CONTINUE); }
"const"         { ADDCOL(); COME(CONST); }
"void"         { ADDCOL(); COME(VOID); }

{L}({L}|{D})*         { ADDCOL(); COME(IDENTIFIER); }

L?\"(\\.|[^\\"\n])*\" { ADDCOL(); COME(STRING_LITERAL); }

0b[01]*{IS}?          { ADDCOL(); COME(CONSTANT); }     /* 识别二进制数 */
0x[0-9a-fA-F]*{IS}?   { ADDCOL(); COME(CONSTANT); }     /* 识别十六进制数 */
0[0-7]*{IS}?          { ADDCOL(); COME(CONSTANT); }     /* 识别0或者八进制数 */
[1-9]{D}*{IS}?        { ADDCOL(); COME(CONSTANT); }     /* 识别十进制数 */

^#[^\n]*              { return /*~YYEOF*/HANDLE_PREPROCESSED_INFO(); } /* 预处理信息处理，可以从预处理信息中获得文件名以及行号 */

[ \t\v\n\f]           { ADDCOL(); return /*~YYEOF*/HANDLE_WRAP_AND_SPACE(); } /* 需要处理行号和列号信息 */

<<EOF>>     { ADDCOL(); COME(YYEOF); }

%%

/* about symbols avaliable (yytext, yyleng etc.) in the context of Flex:
 * https://ftp.gnu.org/old-gnu/Manuals/flex-2.5.4/html_node/flex_14.html
 * https://ftp.gnu.org/old-gnu/Manuals/flex-2.5.4/html_node/flex_15.html
*/
