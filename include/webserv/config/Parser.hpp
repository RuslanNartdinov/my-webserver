#ifndef WEBSERV_PARSER_HPP
#define WEBSERV_PARSER_HPP

#include "webserv/config/Config.hpp"
#include "webserv/config/Lexer.hpp"

namespace ws {

class Parser {
public:
    explicit Parser(Lexer& lx);
    Config parse();

private:
    Lexer& L;
    Token  cur;

    void   expect(TokenType t, const char* what);
    bool   accept(TokenType t);
    void   next();

    void parseServer(Config& cfg);
    void parseServerBody(ServerConfig& srv);
    void parseLocation(ServerConfig& srv);

    static bool toBool(const std::string& s);
};

} // namespace ws
#endif