#include "webserv/config/Lexer.hpp"

namespace ws {

Lexer::Lexer(const std::string& input)
: _s(input), _i(0), _line(1), _col(1) {}

static bool isIdentStart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='.' || c=='/' || c=='-' || c==':';
}
static bool isIdent(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='.' || c=='/' || c=='-' || c==':';
}

void Lexer::skipSpacesAndComments() {
    while (_i < _s.size()) {
        char c = _s[_i];
        if (c==' ' || c=='\t' || c=='\r') { ++_i; ++_col; continue; }
        if (c=='\n') { ++_i; _col=1; ++_line; continue; }
        if (c=='#') { // комментарий до конца строки
            while (_i < _s.size() && _s[_i]!='\n') { ++_i; ++_col; }
            continue;
        }
        break;
    }
}

Token Lexer::makeIdentifierOrString() {
    Token t; t.line=_line; t.col=_col;
    if (_s[_i]=='"') {
        // строка в кавычках
        ++_i; ++_col;
        std::string buf;
        while (_i < _s.size() && _s[_i]!='"') {
            char c = _s[_i++];
            ++_col;
            if (c=='\\' && _i<_s.size()) { // простые экранирования
                char n = _s[_i++]; ++_col;
                if (n=='"' || n=='\\') buf.push_back(n);
                else { buf.push_back('\\'); buf.push_back(n); }
            } else buf.push_back(c);
        }
        if (_i<_s.size() && _s[_i]=='"') { ++_i; ++_col; }
        t.type = T_STRING; t.text = buf;
        return t;
    } else {
        // идентификатор (включая пути и числа)
        std::string buf;
        while (_i < _s.size() && isIdent(_s[_i])) { buf.push_back(_s[_i++]); ++_col; }
        t.type = T_IDENTIFIER; t.text = buf;
        return t;
    }
}

Token Lexer::peek() {
    size_t save_i=_i, save_l=_line, save_c=_col;
    Token t = next();
    _i=save_i; _line=save_l; _col=save_c;
    return t;
}

Token Lexer::next() {
    skipSpacesAndComments();
    Token t; t.line=_line; t.col=_col; t.text="";

    if (_i >= _s.size()) { t.type = T_EOF; return t; }
    char c = _s[_i];

    if (c=='{') { ++_i; ++_col; t.type=T_LBRACE; return t; }
    if (c=='}') { ++_i; ++_col; t.type=T_RBRACE; return t; }
    if (c==';') { ++_i; ++_col; t.type=T_SEMI;   return t; }

    // строки в кавычках или идентификаторы
    if (c=='"' || isIdentStart(c)) return makeIdentifierOrString();

    // неизвестный символ — съедим его как идентификатор-одиночку
    ++_i; ++_col; t.type=T_IDENTIFIER; t.text=std::string(1,c);
    return t;
}

} // namespace ws