// Game/script/script_compiler.cpp - level-script source -> Program. See script_compiler.h.
#include "Game/script/script_compiler.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace Game::Script {

Value defaultValue(const std::string& type)
{
    if (type == "real")
        return Value::real(0.0);
    if (type == "boolean")
        return Value::boolean(false);
    if (type == "string")
        return Value::string("");
    if (type == "entityptr" || type == "matrixptr" || type == "effectptr" || type == "mhfxptr")
        return Value::nil();
    if (type == "vec3d" || type == "rgbaint")
        return Value::vec3({0.0f, 0.0f, 0.0f});
    return Value::integer(0); // integer, enumerations and the engine's enum types
}

bool loadConstants(const std::string& path, ConstantTable& out)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss(line);
        std::string name;
        int64 value = 0;
        if (ss >> name >> value)
            out[name] = value;
    }
    return true;
}

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------------------------------------
// Lexer

enum class Tok { End, Ident, Int, Real, String, Sym };

struct Token {
    Tok kind = Tok::End;
    std::string text; // identifiers lower case, strings verbatim, symbols as written
    int64 i = 0;
    double64 r = 0.0;
    uint32 line = 1;
};

std::expected<std::vector<Token>, std::string> tokenize(const std::string& src)
{
    std::vector<Token> out;
    uint32 line = 1;
    std::size_t p = 0;
    const std::size_t n = src.size();
    auto peekc = [&](std::size_t k) { return p + k < n ? src[p + k] : '\0'; };
    while (p < n)
    {
        const char c = src[p];
        if (c == '\n')
        {
            ++line;
            ++p;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            ++p;
            continue;
        }
        if (c == '{') // comment or {$directive}
        {
            // They nest: Estate_Ext's willies_door_(sd)01 comments out a handler holding
            // {OPEN THE DOORS}, and the game's compiler kept only its OnCreate.
            int32 depth = 0;
            for (; p < n; ++p)
            {
                if (src[p] == '{')
                    ++depth;
                else if (src[p] == '}' && --depth == 0)
                    break;
                line += src[p] == '\n';
            }
            ++p;
            continue;
        }
        if (c == '(' && peekc(1) == '*')
        {
            p += 2;
            while (p + 1 < n && !(src[p] == '*' && src[p + 1] == ')'))
                line += src[p++] == '\n';
            p += 2;
            continue;
        }
        if (c == '/' && peekc(1) == '/')
        {
            while (p < n && src[p] != '\n')
                ++p;
            continue;
        }
        Token t;
        t.line = line;
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            const std::size_t s = p;
            while (p < n && (std::isalnum(static_cast<unsigned char>(src[p])) || src[p] == '_'))
                ++p;
            t.kind = Tok::Ident;
            t.text = lower(src.substr(s, p - s));
        }
        else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '$' && std::isxdigit(static_cast<unsigned char>(peekc(1)))))
        {
            const std::size_t s = p;
            if (c == '$')
            {
                ++p;
                while (p < n && std::isxdigit(static_cast<unsigned char>(src[p])))
                    ++p;
                t.kind = Tok::Int;
                t.i = std::strtoll(src.substr(s + 1, p - s - 1).c_str(), nullptr, 16);
            }
            else
            {
                while (p < n && std::isdigit(static_cast<unsigned char>(src[p])))
                    ++p;
                bool real = false;
                if (p + 1 < n && src[p] == '.' && std::isdigit(static_cast<unsigned char>(src[p + 1])))
                {
                    real = true;
                    ++p;
                    while (p < n && std::isdigit(static_cast<unsigned char>(src[p])))
                        ++p;
                }
                if (p < n && (src[p] == 'e' || src[p] == 'E') &&
                    (std::isdigit(static_cast<unsigned char>(peekc(1))) || ((peekc(1) == '-' || peekc(1) == '+') && std::isdigit(static_cast<unsigned char>(peekc(2))))))
                {
                    real = true;
                    p += 2;
                    while (p < n && std::isdigit(static_cast<unsigned char>(src[p])))
                        ++p;
                }
                const std::string num = src.substr(s, p - s);
                t.text = num;
                t.kind = real ? Tok::Real : Tok::Int;
                if (real)
                    t.r = std::strtod(num.c_str(), nullptr);
                else
                    t.i = std::strtoll(num.c_str(), nullptr, 10);
            }
        }
        else if (c == '\'')
        {
            ++p;
            t.kind = Tok::String;
            for (;;)
            {
                if (p >= n)
                    return std::unexpected("line " + std::to_string(line) + ": unterminated string");
                if (src[p] == '\'')
                {
                    if (peekc(1) == '\'')
                    {
                        t.text += '\'';
                        p += 2;
                        continue;
                    }
                    ++p;
                    break;
                }
                t.text += src[p++];
            }
        }
        else
        {
            t.kind = Tok::Sym;
            static const char* two[] = {":=", "<>", "<=", ">=", ".."};
            bool matched = false;
            for (const char* s : two)
                if (c == s[0] && peekc(1) == s[1])
                {
                    t.text = s;
                    p += 2;
                    matched = true;
                    break;
                }
            if (!matched)
            {
                t.text = std::string(1, c);
                ++p;
            }
        }
        out.push_back(std::move(t));
    }
    Token end;
    end.line = line;
    out.push_back(end);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Compiler

struct CompileError {
    std::string message;
};

struct Symbol {
    Scope scope = Scope::Local;
    uint32 slot = 0;
    std::string type;
};

Field fieldFor(const std::string& name)
{
    if (name == "x" || name == "red")
        return Field::X;
    if (name == "y" || name == "green")
        return Field::Y;
    if (name == "z" || name == "blue")
        return Field::Z;
    if (name == "alpha" || name == "w")
        return Field::W;
    throw CompileError{"unknown field ." + name};
}

class Compiler {
public:
    Compiler(std::vector<Token> tokens, const ConstantTable& constants) : t_(std::move(tokens)), shared_(constants) {}

    Program run()
    {
        expectWord("scriptmain");
        prog_.name = ident();
        expectSym(";");
        while (!isWord("end"))
        {
            if (acceptWord("entity"))
                entitySection();
            else if (acceptWord("const"))
                constSection();
            else if (acceptWord("type"))
                typeSection();
            else if (acceptWord("var"))
                varSection(nullptr);
            else if (isWord("procedure") || isWord("function"))
                routine();
            else if (acceptWord("script"))
                script();
            else
                fail("unexpected '" + cur().text + "' at the top level");
        }
        expectWord("end");
        link();
        return std::move(prog_);
    }

private:
    // --- token helpers ---
    const Token& cur() const { return t_[pos_]; }
    const Token& ahead(std::size_t k) const { return t_[std::min(pos_ + k, t_.size() - 1)]; }
    void advance() { if (pos_ + 1 < t_.size()) ++pos_; }
    [[noreturn]] void fail(const std::string& what) const
    {
        throw CompileError{"line " + std::to_string(cur().line) + ": " + what};
    }
    bool isWord(const char* w) const { return cur().kind == Tok::Ident && cur().text == w; }
    bool isSym(const char* s) const { return cur().kind == Tok::Sym && cur().text == s; }
    bool acceptWord(const char* w)
    {
        if (!isWord(w))
            return false;
        advance();
        return true;
    }
    bool acceptSym(const char* s)
    {
        if (!isSym(s))
            return false;
        advance();
        return true;
    }
    void expectWord(const char* w)
    {
        if (!acceptWord(w))
            fail(std::string("expected '") + w + "', found '" + cur().text + "'");
    }
    void expectSym(const char* s)
    {
        if (!acceptSym(s))
            fail(std::string("expected '") + s + "', found '" + cur().text + "'");
    }
    std::string ident()
    {
        if (cur().kind != Tok::Ident)
            fail("expected a name, found '" + cur().text + "'");
        std::string s = cur().text;
        advance();
        return s;
    }
    static bool isKeyword(const std::string& w)
    {
        static const char* kw[] = {"begin", "end", "var", "const", "type", "procedure", "function", "script", "entity",
                                   "if", "then", "else", "while", "do", "for", "to", "downto", "repeat", "until",
                                   "case", "of"};
        return std::any_of(std::begin(kw), std::end(kw), [&](const char* k) { return w == k; });
    }

    // --- emission ---
    uint32 emit(Op op, uint32 a = 0, uint32 b = 0, Scope scope = Scope::Local, Field field = Field::X)
    {
        Instr in;
        in.op = op;
        in.a = a;
        in.b = b;
        in.scope = scope;
        in.field = field;
        in.line = cur().line;
        routine_->code.push_back(in);
        return static_cast<uint32>(routine_->code.size() - 1);
    }
    uint32 here() const { return static_cast<uint32>(routine_->code.size()); }
    void patch(uint32 at, uint32 target) { routine_->code[at].a = target; }
    uint32 constant(const Value& v)
    {
        prog_.constants.push_back(v);
        return static_cast<uint32>(prog_.constants.size() - 1);
    }
    uint32 nativeName(const std::string& name)
    {
        const auto it = std::find(prog_.natives.begin(), prog_.natives.end(), name);
        if (it != prog_.natives.end())
            return static_cast<uint32>(it - prog_.natives.begin());
        prog_.natives.push_back(name);
        return static_cast<uint32>(prog_.natives.size() - 1);
    }
    uint32 tempSlot(const std::string& type)
    {
        routine_->slotTypes.push_back(type);
        return static_cast<uint32>(routine_->slotTypes.size() - 1);
    }

    // --- declarations ---
    std::string typeName()
    {
        std::string type = ident();
        if (acceptSym("["))
        {
            while (!isSym("]") && cur().kind != Tok::End)
                advance();
            expectSym("]");
        }
        if (const auto alias = typeAliases_.find(type); alias != typeAliases_.end())
            return alias->second;
        return type;
    }

    // ENTITY name : type; - names are model names and may contain parentheses
    // ("Gen_MainPower_Lever_(S)"), so a name is every token up to the colon.
    void entitySection()
    {
        while (cur().kind != Tok::End && !(cur().kind == Tok::Ident && isKeyword(cur().text)))
        {
            std::string name;
            while (cur().kind != Tok::End && !isSym(":"))
            {
                name += cur().text;
                advance();
            }
            expectSym(":");
            const std::string type = ident();
            expectSym(";");
            if (prog_.entity.empty())
            {
                prog_.entity = name;
                prog_.entityType = type;
            }
        }
    }

    Value constExpr()
    {
        bool negative = false;
        if (acceptSym("-"))
            negative = true;
        else
            acceptSym("+");
        Value v;
        if (cur().kind == Tok::Int)
            v = Value::integer(cur().i);
        else if (cur().kind == Tok::Real)
            v = Value::real(cur().r);
        else if (cur().kind == Tok::String)
            v = Value::string(cur().text);
        else if (cur().kind == Tok::Ident)
        {
            if (!resolveConstant(cur().text, v))
                fail("'" + cur().text + "' is not a constant");
        }
        else
            fail("expected a constant");
        advance();
        if (negative)
        {
            if (v.type == ValueType::Real)
                v.r = -v.r;
            else
                v.i = -v.i;
        }
        return v;
    }

    void constSection()
    {
        while (cur().kind == Tok::Ident && !isKeyword(cur().text))
        {
            const std::string name = ident();
            expectSym("=");
            consts_[name] = constExpr();
            expectSym(";");
        }
    }

    void typeSection()
    {
        while (cur().kind == Tok::Ident && !isKeyword(cur().text))
        {
            const std::string name = ident();
            expectSym("=");
            if (acceptSym("("))
            {
                int64 index = 0;
                do
                    consts_[ident()] = Value::integer(index++);
                while (acceptSym(","));
                expectSym(")");
                typeAliases_[name] = "integer";
            }
            else
                typeAliases_[name] = typeName();
            expectSym(";");
        }
    }

    // A VAR section: program globals (routine == nullptr) or a routine's locals.
    void varSection(Routine* owner)
    {
        for (;;)
        {
            while (acceptSym(";")) // tolerated: "vecPos : Vec3d;;"
                ;
            if (cur().kind != Tok::Ident || isKeyword(cur().text))
                break;
            std::vector<std::string> names{ident()};
            while (acceptSym(","))
                names.push_back(ident());
            expectSym(":");
            Scope scope = owner ? Scope::Local : Scope::Global;
            if (acceptWord("level_var") || acceptWord("game_var"))
                scope = Scope::Level;
            const std::string type = typeName();
            expectSym(";");
            for (const std::string& n : names)
            {
                Symbol s;
                s.type = type;
                s.scope = scope;
                if (scope == Scope::Level)
                {
                    s.slot = static_cast<uint32>(prog_.levelVars.size());
                    prog_.levelVars.push_back(n);
                    prog_.levelVarTypes.push_back(type);
                    globals_[n] = s;
                }
                else if (owner)
                {
                    s.slot = static_cast<uint32>(owner->slotTypes.size());
                    owner->slotTypes.push_back(type);
                    locals_[n] = s;
                }
                else
                {
                    s.slot = static_cast<uint32>(prog_.globalTypes.size());
                    prog_.globalTypes.push_back(type);
                    prog_.globalNames.push_back(n);
                    globals_[n] = s;
                }
            }
        }
    }

    Routine& routineNamed(const std::string& name)
    {
        if (const auto it = prog_.routineIndex.find(name); it != prog_.routineIndex.end())
            return prog_.routines[it->second];
        prog_.routineIndex[name] = static_cast<uint32>(prog_.routines.size());
        prog_.routines.push_back(Routine{});
        prog_.routines.back().name = name;
        return prog_.routines.back();
    }

    void routine()
    {
        const bool isFunction = isWord("function");
        advance();
        const std::string name = ident();
        const std::size_t index = [&] { routineNamed(name); return prog_.routineIndex[name]; }();
        std::vector<std::pair<std::string, std::string>> params; // name, type
        std::vector<bool> byRef;
        bool hasHeader = false;
        if (acceptSym("("))
        {
            hasHeader = true;
            while (!isSym(")"))
            {
                const bool ref = acceptWord("var");
                std::vector<std::string> names{ident()};
                while (acceptSym(","))
                    names.push_back(ident());
                expectSym(":");
                const std::string type = typeName();
                for (const auto& n : names)
                {
                    params.emplace_back(n, type);
                    byRef.push_back(ref);
                }
                if (!acceptSym(";"))
                    break;
            }
            expectSym(")");
        }
        std::string resultType;
        if (acceptSym(":"))
            resultType = typeName();
        expectSym(";");

        Routine& r = prog_.routines[index];
        r.isFunction = r.isFunction || isFunction;
        if (hasHeader || r.slotTypes.empty())
        {
            r.paramCount = static_cast<uint32>(params.size());
            r.paramByRef = byRef;
            r.slotTypes.clear();
            for (const auto& [n, type] : params)
                r.slotTypes.push_back(type);
            r.resultSlot = r.paramCount;
            r.slotTypes.push_back(resultType.empty() ? "integer" : resultType); // result (unused by procedures)
            paramNames_[name] = params;
        }
        else if (!resultType.empty())
            r.slotTypes[r.resultSlot] = resultType;

        if (acceptWord("forward"))
        {
            expectSym(";");
            return; // an explicit forward declaration
        }
        if (!(isWord("var") || isWord("const") || isWord("begin")))
            return; // a forward declaration; the body follows later
        body(index, paramNames_[name]);
    }

    void script()
    {
        const std::string name = ident();
        expectSym(";");
        const std::size_t index = [&] { routineNamed(name); return prog_.routineIndex[name]; }();
        Routine& r = prog_.routines[index];
        r.isScript = true;
        r.slotTypes = {"integer"}; // result slot, unused
        r.resultSlot = 0;
        body(index, {});
    }

    void body(std::size_t index, const std::vector<std::pair<std::string, std::string>>& params)
    {
        routine_ = &prog_.routines[index];
        // The result slot keeps its type; locals follow it.
        routine_->slotTypes.resize(routine_->paramCount + 1u);
        locals_.clear();
        for (uint32 i = 0; i < params.size(); ++i)
            locals_[params[i].first] = Symbol{Scope::Local, i, params[i].second};
        if (routine_->isFunction)
            locals_[routine_->name] = Symbol{Scope::Local, routine_->resultSlot, routine_->slotTypes[routine_->resultSlot]};
        routine_->code.clear();
        for (;;)
        {
            if (acceptWord("var"))
                varSection(routine_);
            else if (acceptWord("const"))
                constSection();
            else
                break;
        }
        expectWord("begin");
        statementList({"end"});
        expectWord("end");
        expectSym(";");
        emit(Op::Return);
        routine_->defined = true;
        routine_ = nullptr;
        locals_.clear();
    }

    // --- symbols ---
    const Symbol* variable(const std::string& name) const
    {
        if (routine_)
            if (const auto it = locals_.find(name); it != locals_.end())
                return &it->second;
        if (const auto it = globals_.find(name); it != globals_.end())
            return &it->second;
        return nullptr;
    }
    bool resolveConstant(const std::string& name, Value& out) const
    {
        if (name == "true" || name == "false")
        {
            out = Value::boolean(name == "true");
            return true;
        }
        if (name == "nil")
        {
            out = Value::nil();
            return true;
        }
        if (const auto it = consts_.find(name); it != consts_.end())
        {
            out = it->second;
            return true;
        }
        if (const auto it = shared_.find(name); it != shared_.end())
        {
            out = Value::integer(it->second);
            return true;
        }
        return false;
    }

    // --- statements ---
    bool atAny(std::initializer_list<const char*> words) const
    {
        return std::any_of(words.begin(), words.end(), [&](const char* w) { return isWord(w); });
    }

    void statementList(std::initializer_list<const char*> terminators)
    {
        for (;;)
        {
            if (!atAny(terminators))
                statement();
            if (!acceptSym(";"))
                break;
        }
    }

    void statement()
    {
        if (acceptWord("begin"))
        {
            statementList({"end"});
            expectWord("end");
        }
        else if (acceptWord("if"))
        {
            expression();
            expectWord("then");
            const uint32 jf = emit(Op::JumpIfFalse);
            statement();
            if (acceptWord("else"))
            {
                const uint32 j = emit(Op::Jump);
                patch(jf, here());
                statement();
                patch(j, here());
            }
            else
                patch(jf, here());
        }
        else if (acceptWord("while"))
        {
            const uint32 top = here();
            expression();
            expectWord("do");
            const uint32 jf = emit(Op::JumpIfFalse);
            statement();
            emit(Op::Jump, top);
            patch(jf, here());
        }
        else if (acceptWord("repeat"))
        {
            const uint32 top = here();
            statementList({"until"});
            expectWord("until");
            expression();
            emit(Op::JumpIfFalse, top);
        }
        else if (acceptWord("for"))
            forStatement();
        else if (acceptWord("case"))
            caseStatement();
        else if (cur().kind == Tok::Ident && !isKeyword(cur().text))
            identStatement();
        // else: the empty statement
    }

    void forStatement()
    {
        const std::string name = ident();
        const Symbol* v = variable(name);
        if (!v)
            fail("for loop variable '" + name + "' is not declared");
        const Symbol var = *v;
        expectSym(":=");
        expression();
        emit(Op::Store, var.slot, 0, var.scope);
        const bool down = acceptWord("downto");
        if (!down)
            expectWord("to");
        expression();
        const uint32 limit = tempSlot("integer");
        emit(Op::Store, limit);
        expectWord("do");
        const uint32 top = here();
        emit(Op::Load, var.slot, 0, var.scope);
        emit(Op::Load, limit);
        emit(down ? Op::Ge : Op::Le);
        const uint32 jf = emit(Op::JumpIfFalse);
        statement();
        emit(Op::Load, var.slot, 0, var.scope);
        emit(Op::PushConst, constant(Value::integer(1)));
        emit(down ? Op::Sub : Op::Add);
        emit(Op::Store, var.slot, 0, var.scope);
        emit(Op::Jump, top);
        patch(jf, here());
    }

    void caseStatement()
    {
        expression();
        const uint32 sel = tempSlot("integer");
        emit(Op::Store, sel);
        expectWord("of");
        std::vector<uint32> toEnd;
        while (!isWord("end") && !isWord("else"))
        {
            uint32 labels = 0;
            do
            {
                emit(Op::Load, sel);
                emit(Op::PushConst, constant(constExpr()));
                emit(Op::Eq);
                if (labels++)
                    emit(Op::Or);
            } while (acceptSym(","));
            expectSym(":");
            const uint32 jf = emit(Op::JumpIfFalse);
            statement();
            toEnd.push_back(emit(Op::Jump));
            patch(jf, here());
            if (!acceptSym(";"))
                break;
        }
        if (acceptWord("else"))
        {
            acceptSym(":"); // tolerated: "else: begin ... end"
            statementList({"end"});
        }
        expectWord("end");
        for (uint32 j : toEnd)
            patch(j, here());
    }

    void identStatement()
    {
        const std::string name = ident();
        const Symbol* v = variable(name);
        if (v && acceptSym(":="))
        {
            const Symbol var = *v;
            expression();
            emit(Op::Store, var.slot, 0, var.scope);
            return;
        }
        if (v && isSym(".") && ahead(1).kind == Tok::Ident)
        {
            const Symbol var = *v;
            advance();
            const Field f = fieldFor(ident());
            expectSym(":=");
            expression();
            emit(Op::StoreField, var.slot, 0, var.scope, f);
            return;
        }
        call(name);
        emit(Op::Pop);
    }

    // A call of a routine or native: name [ '(' args ')' ]. Bare variables are passed by
    // reference so VAR parameters and native out-arguments (SetVector) can write them.
    void call(const std::string& name)
    {
        uint32 argc = 0;
        if (acceptSym("("))
        {
            while (!isSym(")"))
            {
                const Symbol* v = cur().kind == Tok::Ident ? variable(cur().text) : nullptr;
                if (v && (ahead(1).text == "," || ahead(1).text == ")"))
                {
                    emit(Op::PushRef, v->slot, 0, v->scope);
                    advance();
                }
                else
                    expression();
                ++argc;
                if (!acceptSym(","))
                    break;
            }
            expectSym(")");
        }
        emit(Op::CallNative, nativeName(name), argc);
    }

    // --- expressions (Pascal precedence) ---
    void expression()
    {
        simpleExpression();
        static const std::pair<const char*, Op> rel[] = {{"=", Op::Eq}, {"<>", Op::Ne}, {"<", Op::Lt},
                                                         {">", Op::Gt}, {"<=", Op::Le}, {">=", Op::Ge}};
        for (const auto& [sym, op] : rel)
            if (acceptSym(sym))
            {
                simpleExpression();
                emit(op);
                return;
            }
    }

    void simpleExpression()
    {
        term();
        for (;;)
        {
            if (acceptSym("+"))
            {
                term();
                emit(Op::Add);
            }
            else if (acceptSym("-"))
            {
                term();
                emit(Op::Sub);
            }
            else if (acceptWord("or"))
            {
                term();
                emit(Op::Or);
            }
            else
                return;
        }
    }

    void term()
    {
        factor();
        for (;;)
        {
            if (acceptSym("*"))
            {
                factor();
                emit(Op::Mul);
            }
            else if (acceptSym("/"))
            {
                factor();
                emit(Op::Div);
            }
            else if (acceptWord("div"))
            {
                factor();
                emit(Op::IntDiv);
            }
            else if (acceptWord("mod"))
            {
                factor();
                emit(Op::Mod);
            }
            else if (acceptWord("and"))
            {
                factor();
                emit(Op::And);
            }
            else
                return;
        }
    }

    void factor()
    {
        const Token& t = cur();
        if (t.kind == Tok::Int)
        {
            emit(Op::PushConst, constant(Value::integer(t.i)));
            advance();
        }
        else if (t.kind == Tok::Real)
        {
            emit(Op::PushConst, constant(Value::real(t.r)));
            advance();
        }
        else if (t.kind == Tok::String)
        {
            emit(Op::PushConst, constant(Value::string(t.text)));
            advance();
        }
        else if (acceptSym("("))
        {
            expression();
            expectSym(")");
        }
        else if (acceptWord("not"))
        {
            factor();
            emit(Op::Not);
        }
        else if (acceptSym("-"))
        {
            factor();
            emit(Op::Neg);
        }
        else if (acceptSym("+"))
            factor();
        else if (t.kind == Tok::Ident && !isKeyword(t.text))
        {
            const std::string name = ident();
            Value c;
            if (const Symbol* v = variable(name); v && !isSym("("))
            {
                emit(Op::Load, v->slot, 0, v->scope);
                if (isSym(".") && ahead(1).kind == Tok::Ident)
                {
                    advance();
                    emit(Op::LoadField, 0, 0, Scope::Local, fieldFor(ident()));
                }
            }
            else if (!isSym("(") && resolveConstant(name, c))
                emit(Op::PushConst, constant(c));
            else
            {
                call(name);
                if (isSym(".") && ahead(1).kind == Tok::Ident) // GetEntityPosition(p).y
                {
                    advance();
                    emit(Op::LoadField, 0, 0, Scope::Local, fieldFor(ident()));
                }
            }
        }
        else
            fail("unexpected '" + t.text + "' in an expression");
    }

    // Calls compiled as natives that name a routine of this program become user calls.
    void link()
    {
        for (Routine& r : prog_.routines)
            for (Instr& in : r.code)
                if (in.op == Op::CallNative)
                    if (const auto it = prog_.routineIndex.find(prog_.natives[in.a]); it != prog_.routineIndex.end() &&
                                                                                      !prog_.routines[it->second].isScript)
                    {
                        in.op = Op::CallUser;
                        in.a = it->second;
                    }
        for (const Routine& r : prog_.routines)
            if (!r.defined)
                throw CompileError{"routine '" + r.name + "' is declared but never defined"};
    }

    std::vector<Token> t_;
    std::size_t pos_ = 0;
    const ConstantTable& shared_;
    Program prog_;
    std::unordered_map<std::string, Value> consts_;
    std::unordered_map<std::string, std::string> typeAliases_;
    std::unordered_map<std::string, Symbol> globals_;
    std::unordered_map<std::string, Symbol> locals_;
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> paramNames_;
    Routine* routine_ = nullptr;
};

} // namespace

std::expected<Program, std::string> compile(const std::string& source, const ConstantTable& constants)
{
    auto tokens = tokenize(source);
    if (!tokens)
        return std::unexpected(tokens.error());
    try
    {
        Compiler c(std::move(*tokens), constants);
        return c.run();
    }
    catch (const CompileError& e)
    {
        return std::unexpected(e.message);
    }
}

} // namespace Game::Script
