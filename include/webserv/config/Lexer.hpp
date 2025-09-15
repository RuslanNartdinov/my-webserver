#ifndef WEBSERV_LEXER_HPP
#define WEBSERV_LEXER_HPP

#include <string>
#include <vector>

namespace ws {

enum TokenType {
    T_EOF, T_LBRACE, T_RBRACE, T_SEMI, T_IDENTIFIER, T_STRING
};

struct Token {
    TokenType type;
    std::string text;
    size_t line, col;
};

class Lexer {
public:
    explicit Lexer(const std::string& input);
    Token next();           // получить следующий токен
    Token peek();           // посмотреть текущий (без сдвига)

private:
    const std::string _s;
    size_t _i, _line, _col;
    void skipSpacesAndComments();
    Token makeIdentifierOrString();
};

} // namespace ws
#endif