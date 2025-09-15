#include "webserv/config/Parser.hpp"
#include <sstream>

namespace ws {

Parser::Parser(Lexer& lx) : L(lx) { cur = L.next(); }

void Parser::next() { cur = L.next(); }
bool Parser::accept(TokenType t) { if (cur.type==t) { next(); return true; } return false; }

void Parser::expect(TokenType t, const char* what) {
    if (!accept(t)) {
        std::ostringstream oss;
        oss << "expected " << what;
        throw ConfigError(oss.str(), cur.line, cur.col);
    }
}

bool Parser::toBool(const std::string& s) {
    return s=="on" || s=="true" || s=="1" || s=="yes";
}

Config Parser::parse() {
    Config cfg;
    while (cur.type != T_EOF) {
        if (cur.type == T_IDENTIFIER && cur.text == "server") {
            parseServer(cfg);
        } else {
            throw ConfigError("expected 'server' block", cur.line, cur.col);
        }
    }
    if (cfg.servers.empty())
        throw ConfigError("no server blocks found", cur.line, cur.col);
    return cfg;
}

void Parser::parseServer(Config& cfg) {
    next(); // consume 'server'
    expect(T_LBRACE, "'{' after server");
    ServerConfig srv;

    parseServerBody(srv);

    cfg.servers.push_back(srv);
}

static bool isTokenIdent(const Token& t, const char* s) {
    return t.type==T_IDENTIFIER && t.text==s;
}

void Parser::parseServerBody(ServerConfig& srv) {
    while (!accept(T_RBRACE)) {
        if (isTokenIdent(cur, "listen")) {
            next();
            // listen 0.0.0.0:8080;
            if (cur.type != T_IDENTIFIER) throw ConfigError("listen expects host:port", cur.line, cur.col);
            std::string hp = cur.text; next();
            expect(T_SEMI, "';'");
            // parse host:port
            size_t colon = hp.find(':');
            if (colon == std::string::npos) throw ConfigError("listen requires host:port", cur.line, cur.col);
            srv.host = hp.substr(0, colon);
            srv.port = std::atoi(hp.substr(colon+1).c_str());
            continue;
        }
        if (isTokenIdent(cur, "server_name")) {
            next();
            // server_name a b c;
            while (cur.type==T_IDENTIFIER || cur.type==T_STRING) {
                srv.server_names.push_back(cur.text);
                next();
            }
            expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "root")) {
            next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("root expects path", cur.line, cur.col);
            srv.root = cur.text; next();
            expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "index")) {
            next();
            std::vector<std::string> idx;
            while (cur.type==T_IDENTIFIER || cur.type==T_STRING) { idx.push_back(cur.text); next(); }
            expect(T_SEMI, "';'");
            // применим как default index в location "/" если захотим позже; пока игнор на уровне сервера
            continue;
        }
        if (isTokenIdent(cur, "error_page")) {
            next();
            // error_page 404 /errors/404.html;
            if (cur.type!=T_IDENTIFIER) throw ConfigError("error_page expects code", cur.line, cur.col);
            int code = std::atoi(cur.text.c_str()); next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("error_page expects path", cur.line, cur.col);
            srv.error_pages[code] = cur.text; next();
            expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "client_max_body_size")) {
            next();
            if (cur.type!=T_IDENTIFIER) throw ConfigError("client_max_body_size expects size", cur.line, cur.col);
            srv.client_max_body_size = parseSizeWithUnits(cur.text, cur.line, cur.col);
            next();
            expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "location")) {
            parseLocation(srv);
            continue;
        }

        // неизвестная директива
        std::string unk = cur.text;
        throw ConfigError("unknown directive in server: " + unk, cur.line, cur.col);
    }

    // валидации
    if (srv.host.empty()) throw ConfigError("server: missing listen host", cur.line, cur.col);
    if (srv.port<=0)      throw ConfigError("server: invalid listen port", cur.line, cur.col);
    if (srv.root.empty()) srv.root = "."; // допустим дефолт
}

void Parser::parseLocation(ServerConfig& srv) {
    // location /path {
    expect(T_IDENTIFIER, "'location'"); // уже проверено выше, просто сдвигаем
    if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING)
        throw ConfigError("location expects path", cur.line, cur.col);
    Location loc; loc.path = cur.text; next();
    expect(T_LBRACE, "'{' after location");

    while (!accept(T_RBRACE)) {
        if (isTokenIdent(cur, "allow_methods")) {
            next();
            while (cur.type==T_IDENTIFIER) { loc.allow_methods.push_back(cur.text); next(); }
            expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "root")) {
            next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("root expects path", cur.line, cur.col);
            loc.root = cur.text; next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "alias")) {
            next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("alias expects path", cur.line, cur.col);
            loc.alias = cur.text; next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "index")) {
            next();
            while (cur.type==T_IDENTIFIER || cur.type==T_STRING) { loc.index.push_back(cur.text); next(); }
            expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "autoindex")) {
            next();
            if (cur.type!=T_IDENTIFIER) throw ConfigError("autoindex expects on/off", cur.line, cur.col);
            loc.autoindex = toBool(cur.text); next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "upload_enable")) {
            next();
            if (cur.type!=T_IDENTIFIER) throw ConfigError("upload_enable expects on/off", cur.line, cur.col);
            loc.upload_enable = toBool(cur.text); next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "upload_store")) {
            next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("upload_store expects path", cur.line, cur.col);
            loc.upload_store = cur.text; next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "return")) {
            next();
            if (cur.type!=T_IDENTIFIER) throw ConfigError("return expects code", cur.line, cur.col);
            loc.return_code = std::atoi(cur.text.c_str()); next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("return expects url", cur.line, cur.col);
            loc.return_url = cur.text; next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "cgi_ext")) {
            next();
            if (cur.type!=T_IDENTIFIER) throw ConfigError("cgi_ext expects extension", cur.line, cur.col);
            loc.cgi_ext = cur.text; next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "cgi_bin")) {
            next();
            if (cur.type!=T_IDENTIFIER && cur.type!=T_STRING) throw ConfigError("cgi_bin expects path", cur.line, cur.col);
            loc.cgi_bin = cur.text; next(); expect(T_SEMI, "';'");
            continue;
        }
        if (isTokenIdent(cur, "client_max_body_size")) {
            next();
            if (cur.type!=T_IDENTIFIER) throw ConfigError("client_max_body_size expects size", cur.line, cur.col);
            loc.client_max_body_size = parseSizeWithUnits(cur.text, cur.line, cur.col);
            next(); expect(T_SEMI, "';'");
            continue;
        }

        std::string unk = cur.text;
        throw ConfigError("unknown directive in location: " + unk, cur.line, cur.col);
    }

    // простые проверки
    if (loc.path.empty() || loc.path[0] != '/')
        throw ConfigError("location path must start with '/'", cur.line, cur.col);

    // если методов не указано — по умолчанию все три (реализуем на Этапе 7)
    if (loc.allow_methods.empty()) {
        loc.allow_methods.push_back("GET");
        loc.allow_methods.push_back("POST");
        loc.allow_methods.push_back("DELETE");
    }

    srv.locations.push_back(loc);
}

} // namespace ws