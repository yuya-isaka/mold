#include "mold.h"

namespace mold::macho {

struct Token {
  enum { STRING = 1, INDENT, DEDENT, RESET, END };

  u8 kind = 0;
  std::string_view str;
};

class YamlParser {
public:
  YamlParser(std::string_view input) : input(input) {}

  YamlNode parse(Context &ctx);

private:
  std::vector<Token> tokenize(Context &ctx);
  i64 get_indent(std::string_view str);

  std::string_view
  tokenize_bare_string(Context &ctx, std::vector<Token> &tokens,
                       std::string_view str);

  std::string_view
  tokenize_list(Context &ctx, std::vector<Token> &tokens, std::string_view str);

  std::string_view
  tokenize_string(Context &ctx, std::vector<Token> &tokens,
                  std::string_view str, char end);

  std::string_view input;
};

std::vector<Token> YamlParser::tokenize(Context &ctx) {
  std::vector<Token> tokens;
  std::vector<i64> indents;
  std::string_view str = input;

  while (!str.empty()) {
    if (str.starts_with("---")) {
      tokens.push_back({Token::RESET, str.substr(0, 3)});
      str = str.substr(str.find('\n'));
      continue;
    }

    if (str.starts_with("- ")) {
      tokens.push_back({'-', str.substr(0, 1)});
      str = str.substr(1);
      str = str.substr(str.find_first_not_of(' '));
      if (!str.starts_with("\n"))
        indents.push_back(get_indent(str));
      continue;
    }

    if (str.starts_with('[')) {
      str = tokenize_list(ctx, tokens, str);
      continue;
    }

    if (str.starts_with('\'')) {
      str = tokenize_string(ctx, tokens, str, '\'');
      continue;
    }

    if (str.starts_with('"')) {
      str = tokenize_string(ctx, tokens, str, '"');
      continue;
    }

    if (str.starts_with(',') || str.starts_with('\n')) {
      tokens.push_back({(u8)str[0], str.substr(0, 1)});
      str = str.substr(1);
      continue;
    }

    if (str.starts_with('#')) {
      str = str.substr(str.find('\n'));
      continue;
    }

    if (str.starts_with(':')) {
      tokens.push_back({':', str.substr(0, 1)});
      str = str.substr(1);
      str = str.substr(str.find_first_not_of(' '));
      continue;
    }

    if (str.starts_with(' ')) {
      size_t pos = str.find_first_not_of(' ');
      if (str[pos] == '#') {
        str = str.substr(str.find('\n'));
        continue;
      }

      if (str[pos] == '\n') {
        str = str.substr(pos + 1);
        continue;
      }

      if (indents.empty() || indents.back() < pos) {
        tokens.push_back({Token::INDENT, str.substr(0, pos)});
        str = str.substr(pos);
        continue;
      }

      if (indents.back() == pos) {
        str = str.substr(pos);
        continue;
      }

      assert(indents.back() > pos);
      indents.pop_back();

      tokens.push_back({Token::DEDENT, str.substr(0, pos)});
      str = str.substr(pos);

      while (!indents.empty()) {
        if (indents.back() == pos)
          break;
        if (indents.back() < pos)
          Fatal(ctx) << "bad indentation";
        tokens.push_back(tokens.back());
      }
      continue;
    }

    str = tokenize_bare_string(ctx, tokens, str);
  }

  tokens.push_back({Token::END, str});
  return tokens;
}

i64 YamlParser::get_indent(std::string_view str) {
  u8 *p = (u8 *)str.data();
  u8 *begin = (u8 *)input.data();

  assert(begin <= p && p < begin + input.size());

  std::string_view s = input.substr(0, p - begin);
  size_t pos = s.rfind('\n');
  if (pos == s.npos)
    return s.size();
  return s.size() - pos;
}

std::string_view
YamlParser::tokenize_list(Context &ctx, std::vector<Token> &tokens,
                          std::string_view str) {
  tokens.push_back({'[', str.substr(0, 1)});
  str = str.substr(1);

  while (!str.empty() && str[0] != ']') {
    if (str[0] == ' ' || str[0] == '\n') {
      str = str.substr( str.find_first_not_of(" \n"));
      continue;
    }

    if (str.starts_with('\'')) {
      str = tokenize_string(ctx, tokens, str, '\'');
      continue;
    }

    if (str.starts_with('"')) {
      str = tokenize_string(ctx, tokens, str, '"');
      continue;
    }

    if (str.starts_with(',')) {
      tokens.push_back({',', str.substr(0, 1)});
      str = str.substr(1);
      continue;
    }

    str = tokenize_bare_string(ctx, tokens, str);
  }

  if (str.empty())
    Error(ctx) << "unclosed list";
  tokens.push_back({']', str.substr(0, 1)});
  return str.substr(1);
}

std::string_view
YamlParser::tokenize_string(Context &ctx, std::vector<Token> &tokens,
                            std::string_view str, char end) {
  str = str.substr(1);
  size_t pos = str.find(end);
  if (pos == str.npos)
    Fatal(ctx) << "unterminated string literal";
  tokens.push_back({Token::STRING, str.substr(1, pos - 1)});
  return str.substr(pos + 1);
}

std::string_view
YamlParser::tokenize_bare_string(Context &ctx, std::vector<Token> &tokens,
                                 std::string_view str) {
  size_t pos = str.find_first_not_of(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.");
  tokens.push_back({Token::STRING, str.substr(0, pos)});
  return str.substr(pos);
}

YamlNode YamlParser::parse(Context &ctx) {
  std::vector<Token> tokens = tokenize(ctx);

  for (Token &tok : tokens) {
    switch (tok.kind) {
    case Token::STRING:
      SyncOut(ctx) << "\"" << tok.str << "\"";
      break;
    case Token::INDENT:
      SyncOut(ctx) << "INDENT " << tok.str;
      break;
    case Token::DEDENT:
      SyncOut(ctx) << "DEDENT " << tok.str;
      break;
    case Token::RESET:
      SyncOut(ctx) << "RESET " << tok.str;
      break;
    case Token::END:
      SyncOut(ctx) << "END " << tok.str;
      break;
    case '\n':
      SyncOut(ctx) << "'\\n'";
      break;
    default:
      SyncOut(ctx) << "'" << (char)tok.kind << "'";
      break;
    }
  }

  return {"foo"};
}

YamlNode parse_yaml(Context &ctx, std::string_view str) {
  assert(!str.empty());
  assert(str[str.size() - 1] == '\n');

  return YamlParser(str).parse(ctx);
}

} // namespace mold::macho