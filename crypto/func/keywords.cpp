/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#include "func.h"

namespace funC {

/*
 * 
 *   KEYWORD DEFINITION
 * 
 */

void define_keywords() {
  sym::symbols.add_kw_char(false, '+')
      .add_kw_char(false, '-')
      .add_kw_char(false, '*')
      .add_kw_char(false, '/')
      .add_kw_char(false, '%')
      .add_kw_char(false, '?')
      .add_kw_char(false, ':')
      .add_kw_char(false, ',')
      .add_kw_char(false, ';')
      .add_kw_char(false, '(')
      .add_kw_char(false, ')')
      .add_kw_char(false, '[')
      .add_kw_char(false, ']')
      .add_kw_char(false, '{')
      .add_kw_char(false, '}')
      .add_kw_char(false, '=')
      .add_kw_char(false, '_')
      .add_kw_char(false, '<')
      .add_kw_char(false, '>')
      .add_kw_char(false, '&')
      .add_kw_char(false, '|')
      .add_kw_char(false, '^')
      .add_kw_char(false, '~');

  using Kw = funC::Keyword;
  sym::symbols.add_keyword(false, "==", Kw::_Eq)
      .add_keyword(false, "!=", Kw::_Neq)
      .add_keyword(false, "<=", Kw::_Leq)
      .add_keyword(false, ">=", Kw::_Geq)
      .add_keyword(false, "<=>", Kw::_Spaceship)
      .add_keyword(false, "<<", Kw::_Lshift)
      .add_keyword(false, ">>", Kw::_Rshift)
      .add_keyword(false, "~>>", Kw::_RshiftR)
      .add_keyword(false, "^>>", Kw::_RshiftC)
      .add_keyword(false, "~/", Kw::_DivR)
      .add_keyword(false, "^/", Kw::_DivC)
      .add_keyword(false, "~%", Kw::_ModR)
      .add_keyword(false, "^%", Kw::_ModC)
      .add_keyword(false, "/%", Kw::_DivMod)
      .add_keyword(false, "+=", Kw::_PlusLet)
      .add_keyword(false, "-=", Kw::_MinusLet)
      .add_keyword(false, "*=", Kw::_TimesLet)
      .add_keyword(false, "/=", Kw::_DivLet)
      .add_keyword(false, "~/=", Kw::_DivRLet)
      .add_keyword(false, "^/=", Kw::_DivCLet)
      .add_keyword(false, "%=", Kw::_ModLet)
      .add_keyword(false, "~%=", Kw::_ModRLet)
      .add_keyword(false, "^%=", Kw::_ModCLet)
      .add_keyword(false, "<<=", Kw::_LshiftLet)
      .add_keyword(false, ">>=", Kw::_RshiftLet)
      .add_keyword(false, "~>>=", Kw::_RshiftRLet)
      .add_keyword(false, "^>>=", Kw::_RshiftCLet)
      .add_keyword(false, "&=", Kw::_AndLet)
      .add_keyword(false, "|=", Kw::_OrLet)
      .add_keyword(false, "^=", Kw::_XorLet);

  sym::symbols.add_keyword(false, "return", Kw::_Return)
      .add_keyword(false, "var", Kw::_Var)
      .add_keyword(false, "repeat", Kw::_Repeat)
      .add_keyword(false, "do", Kw::_Do)
      .add_keyword(false, "while", Kw::_While)
      .add_keyword(false, "until", Kw::_Until)
      .add_keyword(false, "try", Kw::_Try)
      .add_keyword(false, "catch", Kw::_Catch)
      .add_keyword(false, "if", Kw::_If)
      .add_keyword(false, "ifnot", Kw::_Ifnot)
      .add_keyword(false, "then", Kw::_Then)
      .add_keyword(false, "else", Kw::_Else)
      .add_keyword(false, "elseif", Kw::_Elseif)
      .add_keyword(false, "elseifnot", Kw::_Elseifnot);

  sym::symbols.add_keyword(false, "int", Kw::_Int)
      .add_keyword(false, "cell", Kw::_Cell)
      .add_keyword(false, "slice", Kw::_Slice)
      .add_keyword(false, "builder", Kw::_Builder)
      .add_keyword(false, "cont", Kw::_Cont)
      .add_keyword(false, "tuple", Kw::_Tuple)
      .add_keyword(false, "type", Kw::_Type)
      .add_keyword(false, "->", Kw::_Mapsto)
      .add_keyword(false, "forall", Kw::_Forall);

  sym::symbols.add_keyword(false, "extern", Kw::_Extern)
      .add_keyword(false, "global", Kw::_Global)
      .add_keyword(false, "asm", Kw::_Asm)
      .add_keyword(false, "impure", Kw::_Impure)
      .add_keyword(false, "inline", Kw::_Inline)
      .add_keyword(false, "inline_ref", Kw::_InlineRef)
      .add_keyword(false, "auto_apply", Kw::_AutoApply)
      .add_keyword(false, "method_id", Kw::_MethodId)
      .add_keyword(false, "operator", Kw::_Operator)
      .add_keyword(false, "infix", Kw::_Infix)
      .add_keyword(false, "infixl", Kw::_Infixl)
      .add_keyword(false, "infixr", Kw::_Infixr)
      .add_keyword(false, "const", Kw::_Const);

  sym::symbols.add_keyword(false, "#pragma", Kw::_PragmaHashtag).add_keyword(false, "#include", Kw::_IncludeHashtag);
}

}  // namespace funC
