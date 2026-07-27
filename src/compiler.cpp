#include "compiler.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

HydroCompileError::HydroCompileError()
    : hasError(false), line(0), column(0), message("") {
}

HydroCompileResult::HydroCompileResult()
    : success(false), generatedCpp(""), logLines(), error() {
}

static std::string trim(const std::string& text) {
    std::size_t start = 0;
    while (
        start < text.size() &&
        std::isspace(static_cast<unsigned char>(text[start]))
    ) {
        start++;
    }

    std::size_t end = text.size();
    while (
        end > start &&
        std::isspace(static_cast<unsigned char>(text[end - 1]))
    ) {
        end--;
    }

    return text.substr(start, end - start);
}

static bool startsWith(
    const std::string& text,
    const std::string& prefix
) {
    return text.size() >= prefix.size() &&
        text.compare(0, prefix.size(), prefix) == 0;
}

static bool endsWith(
    const std::string& text,
    const std::string& suffix
) {
    return text.size() >= suffix.size() &&
        text.compare(
            text.size() - suffix.size(),
            suffix.size(),
            suffix
        ) == 0;
}

static std::string escapeCppString(const std::string& text) {
    std::string result;
    std::size_t i;

    for (i = 0; i < text.size(); i++) {
        unsigned char c =
            static_cast<unsigned char>(text[i]);

        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\n') result += "\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result.push_back(static_cast<char>(c));
    }

    return result;
}

static void indent(
    std::stringstream& output,
    int depth
) {
    int i;
    for (i = 0; i < depth; i++) {
        output << "    ";
    }
}

enum ETokenType {
    ET_STRING,
    ET_NUMBER,
    ET_VARIABLE,
    ET_INPUT,
    ET_WORD,
    ET_LIST_ACCESS,
    ET_PLUS,
    ET_MINUS,
    ET_STAR,
    ET_DIVIDE,
    ET_MODULO,
    ET_LEFT_PAREN,
    ET_RIGHT_PAREN,
    ET_EQUAL,
    ET_NOT_EQUAL,
    ET_LESS,
    ET_GREATER,
    ET_LESS_EQUAL,
    ET_GREATER_EQUAL,
    ET_END
};

struct EToken {
    ETokenType type;
    std::string value;
};

static bool isWordStart(char c) {
    return std::isalpha(
        static_cast<unsigned char>(c)
    ) || c == '_';
}

static bool isWordPart(char c) {
    return std::isalnum(
        static_cast<unsigned char>(c)
    ) || c == '_';
}

static bool tokenizeExpression(
    const std::string& source,
    std::vector<EToken>& tokens,
    std::string& error
) {
    tokens.clear();
    std::size_t i = 0;

    while (i < source.size()) {
        char c = source[i];

        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }

        if (c == '"') {
            std::string value;
            bool closed = false;
            i++;

            while (i < source.size()) {
                if (source[i] == '"') {
                    i++;
                    closed = true;
                    break;
                }

                if (
                    source[i] == '\\' &&
                    i + 1 < source.size()
                ) {
                    i++;
                    char escaped = source[i];

                    if (escaped == 'n') value.push_back('\n');
                    else if (escaped == 't') value.push_back('\t');
                    else if (escaped == 'r') value.push_back('\r');
                    else value.push_back(escaped);

                    i++;
                    continue;
                }

                value.push_back(source[i]);
                i++;
            }

            if (!closed) {
                error = "Lezaratlan szoveg.";
                return false;
            }

            EToken token;
            token.type = ET_STRING;
            token.value = value;
            tokens.push_back(token);
            continue;
        }

        if (c == '\'') {
            std::string value;
            bool closed = false;
            i++;

            while (i < source.size()) {
                if (source[i] == '\'') {
                    i++;
                    closed = true;
                    break;
                }

                value.push_back(source[i]);
                i++;
            }

            if (!closed || value.empty()) {
                error =
                    "A valtozonevet ' jelek koze kell tenni.";
                return false;
            }

            EToken token;
            token.type = ET_VARIABLE;
            token.value = value;
            tokens.push_back(token);
            continue;
        }

        if (
            std::isdigit(static_cast<unsigned char>(c)) ||
            (
                c == '.' &&
                i + 1 < source.size() &&
                std::isdigit(
                    static_cast<unsigned char>(source[i + 1])
                )
            )
        ) {
            std::string value;
            bool dot = false;

            while (i < source.size()) {
                char current = source[i];

                if (
                    std::isdigit(
                        static_cast<unsigned char>(current)
                    )
                ) {
                    value.push_back(current);
                    i++;
                }
                else if (current == '.' && !dot) {
                    dot = true;
                    value.push_back(current);
                    i++;
                }
                else {
                    break;
                }
            }

            EToken token;
            token.type = ET_NUMBER;
            token.value = value;
            tokens.push_back(token);
            continue;
        }

        if (isWordStart(c)) {
            std::string value;

            while (
                i < source.size() &&
                isWordPart(source[i])
            ) {
                value.push_back(source[i]);
                i++;
            }

            if (i < source.size() && source[i] == '&') {
                value.push_back('&');
                i++;

                EToken token;
                token.type = ET_INPUT;
                token.value = value;
                tokens.push_back(token);
            }
            else {
                // Listaelem kifejezesben: listaT[1], listaT['i'], listaT['i' - 1]
                if (i < source.size() && source[i] == '[' &&
                    !value.empty() && value[value.size() - 1] == 'T') {
                    std::size_t start = ++i;
                    int depth = 1;
                    bool inString = false;
                    bool inVariable = false;
                    while (i < source.size() && depth > 0) {
                        char ch = source[i];
                        if (ch == '"' && !inVariable) inString = !inString;
                        else if (ch == '\'' && !inString) inVariable = !inVariable;
                        else if (!inString && !inVariable) {
                            if (ch == '[') depth++;
                            else if (ch == ']') depth--;
                        }
                        if (depth > 0) i++;
                    }
                    if (depth != 0) {
                        error = "Hianyzik a ] jel a listaindex vegen.";
                        return false;
                    }
                    std::string selector = source.substr(start, i - start);
                    i++;
                    EToken token;
                    token.type = ET_LIST_ACCESS;
                    token.value = value + "\n" + selector;
                    tokens.push_back(token);
                }
                else {
                    EToken token;
                    token.type = ET_WORD;
                    token.value = value;
                    tokens.push_back(token);
                }
            }

            continue;
        }

        EToken token;
        token.value = std::string(1, c);

        if (c == '+') token.type = ET_PLUS;
        else if (c == '-') token.type = ET_MINUS;
        else if (c == '*') token.type = ET_STAR;
        else if (c == '/' || static_cast<unsigned char>(c) == 247) {
            token.type = ET_DIVIDE;
        }
        else if (c == '%') token.type = ET_MODULO;
        else if (c == '(') token.type = ET_LEFT_PAREN;
        else if (c == ')') token.type = ET_RIGHT_PAREN;
        else if (
            c == '=' &&
            i + 1 < source.size() &&
            source[i + 1] == '='
        ) {
            token.type = ET_EQUAL;
            token.value = "==";
            i++;
        }
        else if (
            c == '!' &&
            i + 1 < source.size() &&
            source[i + 1] == '='
        ) {
            token.type = ET_NOT_EQUAL;
            token.value = "!=";
            i++;
        }
        else if (
            c == '<' &&
            i + 1 < source.size() &&
            source[i + 1] == '='
        ) {
            token.type = ET_LESS_EQUAL;
            token.value = "<=";
            i++;
        }
        else if (
            c == '>' &&
            i + 1 < source.size() &&
            source[i + 1] == '='
        ) {
            token.type = ET_GREATER_EQUAL;
            token.value = ">=";
            i++;
        }
        else if (c == '<') token.type = ET_LESS;
        else if (c == '>') token.type = ET_GREATER;
        else {
            error = "Ismeretlen kifejezes-karakter: ";
            error.push_back(c);
            return false;
        }

        tokens.push_back(token);
        i++;
    }

    EToken end;
    end.type = ET_END;
    end.value = "";
    tokens.push_back(end);

    return true;
}

class ExpressionCompiler {
private:
    const std::vector<EToken>& tokens;
    std::size_t position;
    std::string& error;

    const EToken& current() const {
        return tokens.at(position);
    }

    void advance() {
        if (position + 1 < tokens.size()) {
            position++;
        }
    }

    std::string primary() {
        if (current().type == ET_STRING) {
            std::string result =
                "EValue::fromString(\"" +
                escapeCppString(current().value) +
                "\")";
            advance();
            return result;
        }

        if (current().type == ET_NUMBER) {
            std::string value = current().value;
            advance();

            if (value.find('.') != std::string::npos) {
                return "EValue::fromFloat(" + value + ")";
            }

            return "EValue::fromInt(" + value + ")";
        }

        if (current().type == ET_VARIABLE) {
            std::string result =
                "getVar(\"" +
                escapeCppString(current().value) +
                "\")";
            advance();
            return result;
        }

        if (current().type == ET_INPUT) {
            std::string result =
                "getInput(\"" +
                escapeCppString(current().value) +
                "\")";
            advance();
            return result;
        }

        if (current().type == ET_LIST_ACCESS) {
            std::string packed = current().value;
            std::size_t split = packed.find('\n');
            if (split == std::string::npos) {
                error = "Hibas listaelem-hivatkozas.";
                return "";
            }
            std::string listName = packed.substr(0, split);
            std::string selectorText = packed.substr(split + 1);
            std::vector<EToken> selectorTokens;
            std::string selectorError;
            if (!tokenizeExpression(selectorText, selectorTokens, selectorError)) {
                error = selectorError;
                return "";
            }
            ExpressionCompiler selectorCompiler(selectorTokens, selectorError);
            std::string selectorCode = selectorCompiler.compileValue();
            if (selectorCode.empty()) {
                error = selectorError.empty() ? "Hibas listaindex." : selectorError;
                return "";
            }
            advance();
            return "eListGetOne(\"" + escapeCppString(listName) +
                   "\", eListIndexFromValue(" + selectorCode + "))";
        }

        if (current().type == ET_WORD) {
            std::string result =
                "EValue::fromString(\"" +
                escapeCppString(current().value) +
                "\")";
            advance();
            return result;
        }

        if (current().type == ET_LEFT_PAREN) {
            advance();
            std::string result = expression();

            if (current().type != ET_RIGHT_PAREN) {
                error = "Hianyzik a ) jel.";
                return "";
            }

            advance();
            return result;
        }

        error = "Ervenytelen kifejezes.";
        return "";
    }

    std::string unary() {
        if (current().type == ET_MINUS) {
            advance();
            std::string right = unary();

            if (right.empty()) {
                return "";
            }

            return
                "eSub(EValue::fromInt(0), " +
                right +
                ")";
        }

        return primary();
    }

    std::string multiplication() {
        std::string left = unary();

        if (left.empty()) {
            return "";
        }

        while (
            current().type == ET_STAR ||
            current().type == ET_DIVIDE ||
            current().type == ET_MODULO
        ) {
            ETokenType operation = current().type;
            advance();

            std::string right = unary();

            if (right.empty()) {
                return "";
            }

            const char* functionName =
                operation == ET_STAR
                ? "eMul"
                : (
                    operation == ET_DIVIDE
                    ? "eDiv"
                    : "eMod"
                );

            left =
                std::string(functionName) +
                "(" +
                left +
                ", " +
                right +
                ")";
        }

        return left;
    }

    std::string expression() {
        std::string left = multiplication();

        if (left.empty()) {
            return "";
        }

        while (
            current().type == ET_PLUS ||
            current().type == ET_MINUS
        ) {
            ETokenType operation = current().type;
            advance();

            std::string right = multiplication();

            if (right.empty()) {
                return "";
            }

            left =
                std::string(
                    operation == ET_PLUS
                    ? "eAdd("
                    : "eSub("
                ) +
                left +
                ", " +
                right +
                ")";
        }

        return left;
    }

public:
    ExpressionCompiler(
        const std::vector<EToken>& inputTokens,
        std::string& outputError
    )
        : tokens(inputTokens),
          position(0),
          error(outputError) {
    }

    std::string compileValue() {
        std::string result = expression();

        if (
            !result.empty() &&
            current().type != ET_END
        ) {
            error = "Felesleges resz a kifejezes vegen.";
            return "";
        }

        return result;
    }

    std::string compileCondition() {
        return logicalOr();
    }

private:
    std::string comparison() {
        if (
            current().type == ET_WORD &&
            current().value == "tyet"
        ) {
            advance();
            std::string value = comparison();

            if (value.empty()) {
                return "";
            }

            return "(!(" + value + "))";
        }

        std::string left = expression();

        if (left.empty()) {
            return "";
        }

        ETokenType operation = current().type;

        if (
            operation != ET_EQUAL &&
            operation != ET_NOT_EQUAL &&
            operation != ET_LESS &&
            operation != ET_GREATER &&
            operation != ET_LESS_EQUAL &&
            operation != ET_GREATER_EQUAL
        ) {
            return "eTruthy(" + left + ")";
        }

        advance();
        std::string right = expression();

        if (right.empty()) {
            return "";
        }

        const char* functionName = "eEqual";

        if (operation == ET_NOT_EQUAL) {
            functionName = "eNotEqual";
        }
        else if (operation == ET_LESS) {
            functionName = "eLess";
        }
        else if (operation == ET_GREATER) {
            functionName = "eGreater";
        }
        else if (operation == ET_LESS_EQUAL) {
            functionName = "eLessEqual";
        }
        else if (operation == ET_GREATER_EQUAL) {
            functionName = "eGreaterEqual";
        }

        return
            std::string(functionName) +
            "(" +
            left +
            ", " +
            right +
            ")";
    }

    std::string logicalAnd() {
        std::string left = comparison();

        if (left.empty()) {
            return "";
        }

        while (
            current().type == ET_WORD &&
            current().value == "fer"
        ) {
            advance();
            std::string right = comparison();

            if (right.empty()) {
                return "";
            }

            left =
                "((" +
                left +
                ") && (" +
                right +
                "))";
        }

        return left;
    }

    std::string logicalOr() {
        std::string left = logicalAnd();

        if (left.empty()) {
            return "";
        }

        while (
            current().type == ET_WORD &&
            current().value == "LL"
        ) {
            advance();
            std::string right = logicalAnd();

            if (right.empty()) {
                return "";
            }

            left =
                "((" +
                left +
                ") || (" +
                right +
                "))";
        }

        if (current().type != ET_END) {
            error =
                "Ervenytelen logikai feltetel.";
            return "";
        }

        return left;
    }
};

static bool compileValueExpression(
    const std::string& source,
    std::string& generated,
    std::string& error
) {
    std::vector<EToken> tokens;

    if (!tokenizeExpression(source, tokens, error)) {
        return false;
    }

    ExpressionCompiler compiler(tokens, error);
    generated = compiler.compileValue();

    return !generated.empty();
}

static bool compileConditionExpression(
    const std::string& source,
    std::string& generated,
    std::string& error
) {
    std::vector<EToken> tokens;

    if (!tokenizeExpression(source, tokens, error)) {
        return false;
    }

    ExpressionCompiler compiler(tokens, error);
    generated = compiler.compileCondition();

    return !generated.empty();
}

static std::vector<std::string> splitRandomItems(
    const std::string& text
) {
    std::vector<std::string> items;
    std::string current;
    bool inString = false;
    std::size_t i;

    for (i = 0; i < text.size(); i++) {
        char c = text[i];

        if (c == '"') {
            inString = !inString;
            current.push_back(c);
        }
        else if (c == ';' && !inString) {
            items.push_back(trim(current));
            current.clear();
        }
        else {
            current.push_back(c);
        }
    }

    if (!trim(current).empty()) {
        items.push_back(trim(current));
    }

    return items;
}

static bool isIntegerText(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    std::size_t i = 0;

    if (text[0] == '-') {
        i = 1;
    }

    if (i >= text.size()) {
        return false;
    }

    for (; i < text.size(); i++) {
        if (!std::isdigit(
            static_cast<unsigned char>(text[i])
        )) {
            return false;
        }
    }

    return true;
}

static std::string runtimeHeader() {
    return R"CPP(
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

enum ERuntimeType {
    TYPE_KOCSI,
    TYPE_HAJO,
    TYPE_LET,
    TYPE_FELHO,
    TYPE_HANG
};

struct EValue {
    ERuntimeType type;
    std::string text;
    long long integerValue;
    double floatValue;
    char charValue;

    EValue()
        : type(TYPE_HANG),
          text(""),
          integerValue(0),
          floatValue(0.0),
          charValue('\0') {
    }

    static EValue fromString(const std::string& value) {
        EValue result;
        result.type = TYPE_HAJO;
        result.text = value;
        return result;
    }

    static EValue fromInt(long long value) {
        EValue result;
        result.type = TYPE_LET;
        result.integerValue = value;
        result.floatValue = static_cast<double>(value);
        return result;
    }

    static EValue fromFloat(double value) {
        EValue result;
        result.type = TYPE_FELHO;
        result.floatValue = value;
        result.integerValue = static_cast<long long>(value);
        return result;
    }

    static EValue fromChar(char value) {
        EValue result;
        result.type = TYPE_KOCSI;
        result.charValue = value;
        result.text.assign(1, value);
        return result;
    }
};

static std::map<std::string, EValue> eVariables;
static std::map<std::string, EValue> eInputs;
static std::vector<EValue> eLastRandom;

struct EList {
    ERuntimeType elementType;
    long long maximumSize;
    std::vector<EValue> values;
};

static std::map<std::string, EList> eLists;

static std::map<std::string, bool> eWatchedKeys;

// Forward declaration: eObservedKey() uses the window message pump
// before its full definition appears later in the generated runtime.
static void eIkonPump();

static HWND eIkonWindow = NULL;
static bool eIkonClosedByUser = false;
static COLORREF eIkonFontColor = RGB(255,255,255);
static COLORREF eIkonBackground = RGB(0,0,0);
static int eIkonTextY = 10;
struct EIkonRect { int x1; int x2; int y1; int y2; COLORREF color; };
struct EIkonText { int x; int y; std::string text; COLORREF color; };
// Dinamikus tarolok: nincs 32 objektumos programozott korlat.
// Az objektumok szamat csak a gep rendelkezesre allo memoriaja korlatozza.
static std::vector<EIkonRect> eIkonRects;
static std::vector<EIkonText> eIkonTexts;
static bool eIkonDirty = false;
static bool eIkonInputActive = false;
static bool eIkonInputFinished = false;
static std::string eIkonInputBuffer;
static int eIkonInputTextIndex = -1;

static int eKeyCode(const std::string& name) {
    if (name.size()==1) {
        char c=name[0];
        if (c>='a'&&c<='z') c=(char)(c-'a'+'A');
        return (unsigned char)c;
    }
    if (name=="AltGr") return VK_RMENU;
    if (name=="Alt") return VK_MENU;
    if (name=="Ctrl" || name=="Control") return VK_CONTROL;
    if (name=="Shift") return VK_SHIFT;
    if (name=="Enter") return VK_RETURN;
    if (name=="Space") return VK_SPACE;
    if (name=="Escape" || name=="Esc") return VK_ESCAPE;
    if (name=="Up") return VK_UP;
    if (name=="Down") return VK_DOWN;
    if (name=="Left") return VK_LEFT;
    if (name=="Right") return VK_RIGHT;
    if (name=="F1") return VK_F1; if (name=="F2") return VK_F2;
    if (name=="F3") return VK_F3; if (name=="F4") return VK_F4;
    if (name=="F5") return VK_F5; if (name=="F6") return VK_F6;
    if (name=="F7") return VK_F7; if (name=="F8") return VK_F8;
    if (name=="F9") return VK_F9; if (name=="F10") return VK_F10;
    if (name=="F11") return VK_F11; if (name=="F12") return VK_F12;
    return 0;
}
static std::string eKeySignature(const std::vector<std::string>& keys) {
    std::string r; for(size_t i=0;i<keys.size();++i){if(i)r+="+";r+=keys[i];} return r;
}
static bool eKeysDown(const std::vector<std::string>& keys) {
    if(keys.empty()) return false;
    for(size_t i=0;i<keys.size();++i){int vk=eKeyCode(keys[i]); if(!vk || !(GetAsyncKeyState(vk)&0x8000)) return false;}
    return true;
}
static bool eWaitKeyOnce(const std::vector<std::string>& keys) {
    while(!eKeysDown(keys)) Sleep(8);
    while(eKeysDown(keys)) Sleep(8);
    return true;
}
static bool eWatchKey(const std::vector<std::string>& keys) {
    eWatchedKeys[eKeySignature(keys)] = true;
    return eKeysDown(keys);
}
static void eStopWatchKey(const std::vector<std::string>& keys) {
    eWatchedKeys.erase(eKeySignature(keys));
}
static bool eObservedKey(const std::vector<std::string>& keys) {
    eIkonPump();
    if (eIkonClosedByUser) return false;
    return eKeysDown(keys);
}

static COLORREF eTempleColor(const std::string& n) {
    if(n=="black") return RGB(0,0,0); if(n=="blue") return RGB(0,0,170);
    if(n=="green") return RGB(0,170,0); if(n=="cyan") return RGB(0,170,170);
    if(n=="red") return RGB(170,0,0); if(n=="magenta"||n=="purple") return RGB(170,0,170);
    if(n=="brown") return RGB(170,85,0); if(n=="lightgray") return RGB(170,170,170);
    if(n=="darkgray") return RGB(85,85,85); if(n=="lightblue") return RGB(85,85,255);
    if(n=="lightgreen") return RGB(85,255,85); if(n=="lightcyan") return RGB(85,255,255);
    if(n=="lightred") return RGB(255,85,85); if(n=="pink") return RGB(255,85,255);
    if(n=="yellow") return RGB(255,255,85); return RGB(255,255,255);
}
static COLORREF eMixColors(const std::vector<std::string>& names) {
    if(names.empty()) return RGB(255,255,255); int r=0,g=0,b=0;
    for(size_t i=0;i<names.size();++i){COLORREF c=eTempleColor(names[i]);r+=GetRValue(c);g+=GetGValue(c);b+=GetBValue(c);} 
    return RGB(r/(int)names.size(),g/(int)names.size(),b/(int)names.size());
}
static LRESULT CALLBACK eIkonProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_PAINT){
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT client; GetClientRect(h,&client);
        HDC mem=CreateCompatibleDC(dc); HBITMAP bmp=CreateCompatibleBitmap(dc,client.right-client.left,client.bottom-client.top);
        HGDIOBJ oldBmp=SelectObject(mem,bmp);
        HBRUSH background=CreateSolidBrush(eIkonBackground); FillRect(mem,&client,background); DeleteObject(background);
        int cx=client.right/2, cy=client.bottom/2;
        for(size_t i=0;i<eIkonRects.size();++i){
            const EIkonRect& q=eIkonRects[i]; HBRUSH b=CreateSolidBrush(q.color);
            RECT box={cx+q.x1,cy-q.y2,cx+q.x2+1,cy-q.y1+1}; FillRect(mem,&box,b); DeleteObject(b);
        }
        SetBkColor(mem,eIkonBackground);
        for(size_t i=0;i<eIkonTexts.size();++i){
            SetTextColor(mem,eIkonTexts[i].color); TextOutA(mem,eIkonTexts[i].x,eIkonTexts[i].y,eIkonTexts[i].text.c_str(),(int)eIkonTexts[i].text.size());
        }
        BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,oldBmp); DeleteObject(bmp); DeleteDC(mem); EndPaint(h,&ps); return 0;
    }
    if(m==WM_CHAR && eIkonInputActive){
        if(w==13){
            eIkonInputFinished=true;
            eIkonInputActive=false;
            return 0;
        }
        if(w==8){
            if(!eIkonInputBuffer.empty()) eIkonInputBuffer.erase(eIkonInputBuffer.size()-1);
        }
        else if(w>=32 && w<=255){
            eIkonInputBuffer.push_back((char)w);
        }
        if(eIkonInputTextIndex>=0 && eIkonInputTextIndex<(int)eIkonTexts.size())
            eIkonTexts[(size_t)eIkonInputTextIndex].text=std::string("> ")+eIkonInputBuffer+"_";
        eIkonDirty=true;
        InvalidateRect(h,NULL,FALSE);
        return 0;
    }
    if(m==WM_CLOSE){eIkonClosedByUser=true;DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){eIkonWindow=NULL;PostQuitMessage(0);return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void eIkonInit(){
    if(eIkonClosedByUser) ExitProcess(0);
    if(eIkonWindow) return; FreeConsole();
    HINSTANCE hi=GetModuleHandleA(NULL); WNDCLASSA wc; ZeroMemory(&wc,sizeof(wc));
    wc.lpfnWndProc=eIkonProc; wc.hInstance=hi; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(eIkonBackground); wc.lpszClassName="E_IKONAIN2_WINDOW";
    RegisterClassA(&wc); eIkonWindow=CreateWindowA(wc.lpszClassName,"E - IKONAIN2",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,900,650,NULL,NULL,hi,NULL);
    ShowWindow(eIkonWindow,SW_SHOW); UpdateWindow(eIkonWindow);
}
static void eIkonPresent(){
    if(!eIkonWindow || !eIkonDirty) return;
    InvalidateRect(eIkonWindow,NULL,FALSE);
    UpdateWindow(eIkonWindow);
    eIkonDirty=false;
}
// Egy uj kepkocka kezdese. Csak a kirajzolt objektumokat es szovegeket torli.
// A hatterszin es a betuszin szandekosan megmarad az mlsd. utan is.
static void eIkonClearFrame(){
    if(!eIkonWindow) return;
    eIkonRects.clear();
    eIkonTexts.clear();
    eIkonTextY=10;
    eIkonDirty=true;
}
static void eIkonSetFont(const std::vector<std::string>& c){eIkonFontColor=eMixColors(c);}
static void eIkonSetBackground(const std::vector<std::string>& c){
    eIkonBackground=eMixColors(c);
    eIkonClearFrame();
}
static void eIkonPrint(const std::string& text,const std::vector<std::string>& c){if(!eIkonWindow)eIkonInit();EIkonText t={10,eIkonTextY,text,c.empty()?eIkonFontColor:eMixColors(c)};eIkonTexts.push_back(t);eIkonTextY+=20;eIkonDirty=true;}
static std::string eIkonReadLine(){
    if(!eIkonWindow) eIkonInit();
    eIkonInputBuffer.clear();
    eIkonInputFinished=false;
    eIkonInputActive=true;
    EIkonText inputLine={10,eIkonTextY,"> _",eIkonFontColor};
    eIkonTexts.push_back(inputLine);
    eIkonInputTextIndex=(int)eIkonTexts.size()-1;
    eIkonTextY+=20;
    eIkonDirty=true;
    SetFocus(eIkonWindow);
    eIkonPresent();
    while(!eIkonInputFinished && !eIkonClosedByUser){
        eIkonPump();
        eIkonPresent();
        Sleep(8);
    }
    if(eIkonInputTextIndex>=0 && eIkonInputTextIndex<(int)eIkonTexts.size())
        eIkonTexts[(size_t)eIkonInputTextIndex].text=std::string("> ")+eIkonInputBuffer;
    eIkonInputTextIndex=-1;
    eIkonDirty=true;
    eIkonPresent();
    return eIkonInputBuffer;
}
static void eIkonDrawHorizontal(int x1,int x2,int y,const std::string& color){if(!eIkonWindow)eIkonInit();RECT r;GetClientRect(eIkonWindow,&r);int cx=r.right/2,cy=r.bottom/2;HDC dc=GetDC(eIkonWindow);HPEN p=CreatePen(PS_SOLID,1,eTempleColor(color));HGDIOBJ old=SelectObject(dc,p);MoveToEx(dc,cx+x1,cy-y,NULL);LineTo(dc,cx+x2,cy-y);SelectObject(dc,old);DeleteObject(p);ReleaseDC(eIkonWindow,dc);}
static double toNumber(const EValue& value);
static void eIkonDrawRect(const EValue& xv1,const EValue& xv2,const EValue& yv1,const EValue& yv2,const std::string& color){if(!eIkonWindow)eIkonInit();int x1=(int)toNumber(xv1),x2=(int)toNumber(xv2),y1=(int)toNumber(yv1),y2=(int)toNumber(yv2);if(x1>x2)std::swap(x1,x2);if(y1>y2)std::swap(y1,y2);EIkonRect q={x1,x2,y1,y2,eTempleColor(color)};eIkonRects.push_back(q);eIkonDirty=true;}
static void eIkonPump(){MSG msg;while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){eIkonWindow=NULL;eIkonClosedByUser=true;ExitProcess(0);}TranslateMessage(&msg);DispatchMessageA(&msg);}if(eIkonClosedByUser)ExitProcess(0);}
static void eIkonLoop(){if(!eIkonWindow)return;eIkonPresent();MSG msg;while(GetMessageA(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}}

static std::string typeName(ERuntimeType type) {
    if (type == TYPE_KOCSI) return "kocsi";
    if (type == TYPE_HAJO) return "hajo";
    if (type == TYPE_LET) return "let";
    if (type == TYPE_FELHO) return "felho";
    return "hang";
}

static std::string toText(const EValue& value) {
    std::stringstream stream;

    if (value.type == TYPE_KOCSI) {
        stream << value.charValue;
    }
    else if (value.type == TYPE_HAJO) {
        stream << value.text;
    }
    else if (value.type == TYPE_LET) {
        stream << value.integerValue;
    }
    else if (value.type == TYPE_FELHO) {
        stream << value.floatValue;
    }

    return stream.str();
}

static double toNumber(const EValue& value) {
    if (value.type == TYPE_LET) {
        return static_cast<double>(value.integerValue);
    }

    if (value.type == TYPE_FELHO) {
        return value.floatValue;
    }

    if (value.type == TYPE_KOCSI) {
        return static_cast<unsigned char>(value.charValue);
    }

    throw std::runtime_error(
        "E tipushiba: szoveg nem hasznalhato szamkent."
    );
}

static EValue convertDeclaration(
    ERuntimeType type,
    const EValue& value
) {
    if (type == TYPE_HAJO) {
        if (value.type == TYPE_HAJO) return value;
        return EValue::fromString(toText(value));
    }

    if (type == TYPE_KOCSI) {
        std::string text = toText(value);

        if (text.size() != 1) {
            throw std::runtime_error(
                "E tipushiba: a kocsi pontosan egy karakter."
            );
        }

        return EValue::fromChar(text[0]);
    }

    if (type == TYPE_LET) {
        if (value.type == TYPE_LET) return value;

        throw std::runtime_error(
            "E tipushiba: a let csak egesz szamot fogad."
        );
    }

    if (type == TYPE_FELHO) {
        if (value.type == TYPE_FELHO) return value;
        if (value.type == TYPE_LET) {
            return EValue::fromFloat(
                static_cast<double>(value.integerValue)
            );
        }

        throw std::runtime_error(
            "E tipushiba: a felho csak szamot fogad."
        );
    }

    return EValue();
}


static void eDeclareList(
    const std::string& name,
    ERuntimeType type,
    long long declaredSize,
    const std::vector<EValue>& initialValues
) {
    if (name.empty() || name[name.size() - 1] != 'T') {
        throw std::runtime_error(
            "E listahiba: a lista neve T betuvel vegzodjon."
        );
    }

    if (eLists.count(name) != 0 || eVariables.count(name) != 0) {
        throw std::runtime_error(
            "E listahiba: mar letezik ilyen nev: " + name
        );
    }

    if (declaredSize >= 0 &&
        static_cast<long long>(initialValues.size()) > declaredSize) {
        throw std::runtime_error(
            "E listahiba: tobb kezdoelem van, mint a megadott listameret."
        );
    }

    EList list;
    list.elementType = type;
    list.maximumSize = declaredSize;

    for (std::size_t i = 0; i < initialValues.size(); ++i) {
        list.values.push_back(
            convertDeclaration(type, initialValues[i])
        );
    }

    if (declaredSize >= 0) {
        EValue empty;
        if (type == TYPE_HAJO) empty = EValue::fromString("");
        else if (type == TYPE_KOCSI) empty = EValue::fromChar(' ');
        else if (type == TYPE_LET) empty = EValue::fromInt(0);
        else if (type == TYPE_FELHO) empty = EValue::fromFloat(0.0);

        while (static_cast<long long>(list.values.size()) < declaredSize) {
            list.values.push_back(empty);
        }
    }

    eLists[name] = list;
}

static void eDeleteList(const std::string& name) {
    if (eLists.erase(name) == 0) {
        throw std::runtime_error(
            "E listahiba: nem letezo lista: " + name
        );
    }
}

static long long eListIndexFromValue(const EValue& value) {
    if (value.type == TYPE_FELHO || value.type == TYPE_LET) {
        double number = value.type == TYPE_LET
            ? static_cast<double>(value.integerValue)
            : value.floatValue;
        long long integer = static_cast<long long>(number);
        if (number != static_cast<double>(integer)) {
            throw std::runtime_error(
                "E listahiba: a lista indexe csak egesz szam lehet."
            );
        }
        return integer;
    }
    throw std::runtime_error(
        "E listahiba: a lista indexe szam tipusu legyen."
    );
}

static std::size_t eCheckedListIndex(
    const std::string& name,
    long long oneBasedIndex
) {
    std::map<std::string, EList>::iterator found = eLists.find(name);
    if (found == eLists.end()) {
        throw std::runtime_error(
            "E listahiba: nem letezo lista: " + name
        );
    }

    if (oneBasedIndex < 1 ||
        oneBasedIndex > static_cast<long long>(found->second.values.size())) {
        std::stringstream message;
        message << "E listahiba: a(z) " << name
                << " lista indexe 1 es "
                << found->second.values.size()
                << " kozott lehet.";
        throw std::runtime_error(message.str());
    }

    return static_cast<std::size_t>(oneBasedIndex - 1);
}

static EValue eListGetOne(
    const std::string& name,
    long long oneBasedIndex
) {
    std::size_t index = eCheckedListIndex(name, oneBasedIndex);
    return eLists[name].values[index];
}

static EValue eListLength(const std::string& name) {
    std::map<std::string, EList>::iterator found = eLists.find(name);
    if (found == eLists.end()) {
        throw std::runtime_error(
            "E listahiba: nem letezo lista: " + name
        );
    }

    return EValue::fromInt(
        static_cast<long long>(found->second.values.size())
    );
}

static EValue eListRead(
    const std::string& name,
    const std::vector<long long>& indices,
    bool all
) {
    std::map<std::string, EList>::iterator found = eLists.find(name);
    if (found == eLists.end()) {
        throw std::runtime_error(
            "E listahiba: nem letezo lista: " + name
        );
    }

    std::stringstream text;
    if (all) {
        for (std::size_t i = 0; i < found->second.values.size(); ++i) {
            if (i != 0) text << " ";
            text << toText(found->second.values[i]);
        }
    }
    else {
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (i != 0) text << " ";
            text << toText(eListGetOne(name, indices[i]));
        }
    }

    return EValue::fromString(text.str());
}

static void eListSet(
    const std::string& name,
    long long oneBasedIndex,
    const EValue& value
) {
    std::map<std::string, EList>::iterator found = eLists.find(name);
    if (found == eLists.end()) {
        throw std::runtime_error(
            "E listahiba: nem letezo lista: " + name
        );
    }
    if (oneBasedIndex < 1) {
        throw std::runtime_error(
            "E listahiba: a lista indexelese 1-tol indul."
        );
    }

    EList& list = found->second;
    long long currentSize = static_cast<long long>(list.values.size());
    if (oneBasedIndex <= currentSize) {
        list.values[static_cast<std::size_t>(oneBasedIndex - 1)] =
            convertDeclaration(list.elementType, value);
        return;
    }

    if (oneBasedIndex != currentSize + 1) {
        throw std::runtime_error(
            "E listahiba: uj elemet csak a lista kovetkezo helyere lehet tenni."
        );
    }
    if (list.maximumSize >= 0 && oneBasedIndex > list.maximumSize) {
        throw std::runtime_error(
            "E listahiba: az uj elem nem fer bele a megadott listameretbe."
        );
    }

    list.values.push_back(convertDeclaration(list.elementType, value));
}

static void eListDeleteItems(
    const std::string& name,
    std::vector<long long> indices
) {
    std::map<std::string, EList>::iterator found = eLists.find(name);
    if (found == eLists.end()) {
        throw std::runtime_error(
            "E listahiba: nem letezo lista: " + name
        );
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for (std::vector<long long>::reverse_iterator it = indices.rbegin();
         it != indices.rend(); ++it) {
        std::size_t index = eCheckedListIndex(name, *it);
        found->second.values.erase(found->second.values.begin() + index);
    }
}

static EValue detectInputType(const std::string& text) {
    if (text.empty()) {
        return EValue::fromString("");
    }

    char* end = NULL;
    long long integerValue =
        std::strtoll(text.c_str(), &end, 10);

    if (end != NULL && *end == '\0') {
        return EValue::fromInt(integerValue);
    }

    end = NULL;
    double floatValue =
        std::strtod(text.c_str(), &end);

    if (end != NULL && *end == '\0') {
        return EValue::fromFloat(floatValue);
    }

    if (text.size() == 1) {
        return EValue::fromChar(text[0]);
    }

    return EValue::fromString(text);
}

static EValue getVar(const std::string& name) {
    std::map<std::string, EValue>::iterator found =
        eVariables.find(name);

    if (found == eVariables.end()) {
        throw std::runtime_error(
            "Nem letezo valtozo: " + name
        );
    }

    return found->second;
}

static EValue getInput(const std::string& name) {
    std::map<std::string, EValue>::iterator found =
        eInputs.find(name);

    if (found == eInputs.end()) {
        throw std::runtime_error(
            "Nem letezo & valtozo: " + name
        );
    }

    return found->second;
}

static void declareVar(
    const std::string& name,
    ERuntimeType type,
    const EValue& value
) {
    if (eVariables.find(name) != eVariables.end()) {
        throw std::runtime_error(
            "A valtozo mar letezik: " + name
        );
    }

    eVariables[name] =
        convertDeclaration(type, value);
}

static void assignVar(
    const std::string& name,
    const EValue& value
) {
    std::map<std::string, EValue>::iterator found =
        eVariables.find(name);

    if (found == eVariables.end()) {
        throw std::runtime_error(
            "Nem letezo valtozo: " + name
        );
    }

    ERuntimeType expected = found->second.type;

    if (
        expected == TYPE_FELHO &&
        value.type == TYPE_LET
    ) {
        found->second = EValue::fromFloat(
            static_cast<double>(value.integerValue)
        );
        return;
    }

    if (expected != value.type) {
        throw std::runtime_error(
            "E tipushiba: a '" +
            name +
            "' valtozo " +
            typeName(expected) +
            ", de a kapott ertek " +
            typeName(value.type) +
            "."
        );
    }

    found->second = value;
}

static void changeType(
    const std::string& name,
    ERuntimeType type,
    const EValue& value
) {
    if (eVariables.find(name) == eVariables.end()) {
        throw std::runtime_error(
            "Nem letezo valtozo: " + name
        );
    }

    eVariables[name] =
        convertDeclaration(type, value);
}

static void deleteVar(const std::string& name) {
    eVariables.erase(name);
}

static EValue eAdd(
    const EValue& left,
    const EValue& right
) {
    if (
        left.type == TYPE_HAJO ||
        right.type == TYPE_HAJO ||
        left.type == TYPE_KOCSI ||
        right.type == TYPE_KOCSI
    ) {
        return EValue::fromString(
            toText(left) + toText(right)
        );
    }

    if (
        left.type == TYPE_FELHO ||
        right.type == TYPE_FELHO
    ) {
        return EValue::fromFloat(
            toNumber(left) + toNumber(right)
        );
    }

    return EValue::fromInt(
        static_cast<long long>(
            toNumber(left) + toNumber(right)
        )
    );
}

static EValue eSub(
    const EValue& left,
    const EValue& right
) {
    if (
        left.type == TYPE_FELHO ||
        right.type == TYPE_FELHO
    ) {
        return EValue::fromFloat(
            toNumber(left) - toNumber(right)
        );
    }

    return EValue::fromInt(
        static_cast<long long>(
            toNumber(left) - toNumber(right)
        )
    );
}

static EValue eMul(
    const EValue& left,
    const EValue& right
) {
    if (
        left.type == TYPE_FELHO ||
        right.type == TYPE_FELHO
    ) {
        return EValue::fromFloat(
            toNumber(left) * toNumber(right)
        );
    }

    return EValue::fromInt(
        static_cast<long long>(
            toNumber(left) * toNumber(right)
        )
    );
}

static EValue eDiv(
    const EValue& left,
    const EValue& right
) {
    double divisor = toNumber(right);

    if (divisor == 0.0) {
        throw std::runtime_error("Nullaval osztas.");
    }

    return EValue::fromFloat(
        toNumber(left) / divisor
    );
}

static EValue eMod(
    const EValue& left,
    const EValue& right
) {
    long long divisor =
        static_cast<long long>(toNumber(right));

    if (divisor == 0) {
        throw std::runtime_error(
            "Nullaval nem lehet maradekos osztast vegezni."
        );
    }

    long long dividend =
        static_cast<long long>(toNumber(left));

    return EValue::fromInt(dividend % divisor);
}

static bool eEqual(
    const EValue& left,
    const EValue& right
) {
    if (
        left.type == TYPE_HAJO ||
        right.type == TYPE_HAJO ||
        left.type == TYPE_KOCSI ||
        right.type == TYPE_KOCSI
    ) {
        return toText(left) == toText(right);
    }

    return std::fabs(
        toNumber(left) - toNumber(right)
    ) < 0.0000001;
}

static bool eNotEqual(
    const EValue& left,
    const EValue& right
) {
    return !eEqual(left, right);
}

static bool eLess(
    const EValue& left,
    const EValue& right
) {
    return toNumber(left) < toNumber(right);
}

static bool eGreater(
    const EValue& left,
    const EValue& right
) {
    return toNumber(left) > toNumber(right);
}

static bool eLessEqual(
    const EValue& left,
    const EValue& right
) {
    return toNumber(left) <= toNumber(right);
}

static bool eGreaterEqual(
    const EValue& left,
    const EValue& right
) {
    return toNumber(left) >= toNumber(right);
}

static bool eTruthy(const EValue& value) {
    if (value.type == TYPE_HAJO) {
        return !value.text.empty();
    }

    if (value.type == TYPE_KOCSI) {
        return value.charValue != '\0';
    }

    if (value.type == TYPE_LET) {
        return value.integerValue != 0;
    }

    if (value.type == TYPE_FELHO) {
        return value.floatValue != 0.0;
    }

    return false;
}

static void incrementVar(
    const std::string& name,
    const EValue& amount
) {
    EValue current = getVar(name);
    assignVar(name, eAdd(current, amount));
}

static void decrementVar(
    const std::string& name,
    const EValue& amount
) {
    EValue current = getVar(name);
    assignVar(name, eSub(current, amount));
}

static EValue getAnyValue(
    const std::string& name,
    bool inputValue
) {
    return inputValue ? getInput(name) : getVar(name);
}

static std::string annotationEscape(const std::string& text) {
    static const char* digits = "0123456789ABCDEF";
    std::string result;

    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        result.push_back(digits[(ch >> 4) & 0x0F]);
        result.push_back(digits[ch & 0x0F]);
    }

    return result;
}

static std::map<int, std::string> runtimeAnnotations;

static void flushAnnotations() {
    const char* temporaryPath = "e_annotations.new";
    const char* finalPath = "e_annotations.tmp";

    {
        std::ofstream output(
            temporaryPath,
            std::ios::binary | std::ios::trunc
        );

        if (!output.is_open()) {
            return;
        }

        for (
            std::map<int, std::string>::const_iterator it =
                runtimeAnnotations.begin();
            it != runtimeAnnotations.end();
            ++it
        ) {
            output
                << it->first
                << "|"
                << annotationEscape(it->second)
                << "\n";
        }

        output.flush();
    }

    // A teljes pillanatkepet csereljuk le egyszerre. Igy az IDE nem
    // torolhet ki veletlenul egy olyan var?/var??? eredmenyt, amelyet
    // a program eppen akkor irt ki.
    DeleteFileA(finalPath);
    MoveFileExA(
        temporaryPath,
        finalPath,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    );
}

static void clearAnnotations() {
    runtimeAnnotations.clear();
    DeleteFileA("e_annotations.new");
    DeleteFileA("e_annotations.tmp");
    flushAnnotations();
}

static void writeAnnotation(int sourceLine, const std::string& comment) {
    runtimeAnnotations[sourceLine] = comment;
    flushAnnotations();
}

static std::string valueCommentText(const EValue& value) {
    std::string body;

    if (
        value.type == TYPE_HAJO ||
        value.type == TYPE_KOCSI
    ) {
        body = "\"" + toText(value) + "\"";
    }
    else {
        body = toText(value);
    }

    return "/*" + body + "*\\";
}

static void printTypeComment(int sourceLine, const EValue& value) {
    writeAnnotation(
        sourceLine,
        "/*" + typeName(value.type) + "*\\"
    );
}

static void printValueComment(int sourceLine, const EValue& value) {
    writeAnnotation(sourceLine, valueCommentText(value));
}

static EValue hajoLength(const EValue& value) {
    return EValue::fromInt(
        static_cast<long long>(toText(value).size())
    );
}

static std::string hajoSliceText(
    const EValue& value,
    char mode,
    int count
) {
    std::string text = toText(value);
    if (count <= 0 || text.empty()) return "";
    if (count >= static_cast<int>(text.size())) return text;

    if (mode == 'j') {
        return text.substr(text.size() - count);
    }

    if (mode == 'd') {
        std::size_t start = text.size() / 2;
        return text.substr(start, count);
    }

    return text.substr(0, count);
}

static EValue hajoSlice(
    const EValue& value,
    char mode,
    int count
) {
    return EValue::fromString(
        hajoSliceText(value, mode, count)
    );
}

static EValue hajoConcat(
    const std::vector<EValue>& values
) {
    std::string result;
    std::size_t i;
    for (i = 0; i < values.size(); i++) {
        if (i > 0 && !result.empty()) result += " ";
        result += toText(values[i]);
    }
    return EValue::fromString(result);
}

static EValue hajoCompare(
    const std::vector<EValue>& values
) {
    if (values.size() < 2) return EValue::fromInt(0);
    const std::string first = toText(values[0]);
    std::size_t i;
    for (i = 1; i < values.size(); i++) {
        if (toText(values[i]) != first) {
            return EValue::fromInt(1);
        }
    }
    return EValue::fromInt(0);
}

static EValue hajoContains(
    const EValue& source,
    const EValue& searched
) {
    return EValue::fromInt(
        toText(source).find(toText(searched)) !=
        std::string::npos ? 0 : 1
    );
}

static EValue hajoCharacters(
    const EValue& source,
    const std::vector<EValue>& searched
) {
    const std::string text = toText(source);
    std::size_t i;
    for (i = 0; i < searched.size(); i++) {
        const std::string part = toText(searched[i]);
        if (
            part.empty() ||
            text.find(part[0]) == std::string::npos
        ) {
            return EValue::fromInt(1);
        }
    }
    return EValue::fromInt(0);
}

static EValue hajoStroke(const EValue& value) {
    const std::string text = toText(value);
    std::string result;
    std::string token;
    std::size_t i;

    for (i = 0; i <= text.size(); i++) {
        char c = i < text.size() ? text[i] : ';';
        bool separator =
            c == ',' || c == ';' ||
            c == ' ' || c == '\t' ||
            c == '\n' || c == '\r';

        if (separator) {
            if (!token.empty()) {
                if (!result.empty()) result += "\n";
                result += token;
                token.clear();
            }
        }
        else {
            token += c;
        }
    }

    return EValue::fromString(result);
}

static void hajoCopyToInput(
    const std::string& target,
    const EValue& value
) {
    eInputs[target] = value;
}

static void hajoCopyToVar(
    const std::string& target,
    const EValue& value
) {
    std::map<std::string, EValue>::iterator found =
        eVariables.find(target);

    if (found == eVariables.end()) {
        throw std::runtime_error(
            "Nem letezo hajokopi celvaltozo: " + target
        );
    }

    ERuntimeType expected = found->second.type;
    found->second = convertDeclaration(expected, value);
}

static void readInput(const std::string& name) {
    std::string value;
    if (eIkonWindow) value = eIkonReadLine();
    else std::getline(std::cin, value);
    eInputs[name] = detectInputType(value);
}

static void readAndDiscard() {
    if (eIkonWindow) {
        (void)eIkonReadLine();
        return;
    }
    std::string value;
    std::getline(std::cin, value);
}

static void setRandom(
    std::vector<EValue> options,
    int count,
    bool uniqueValues
) {
    if (options.empty()) {
        throw std::runtime_error(
            "Az rnd lista ures."
        );
    }

    if (count < 1) {
        throw std::runtime_error(
            "Az rnd darabszama legalabb 1."
        );
    }

    if (
        uniqueValues &&
        count > static_cast<int>(options.size())
    ) {
        throw std::runtime_error(
            "rnd_tyet mellett tul sok elemet kertel."
        );
    }

    eLastRandom.clear();

    if (uniqueValues) {
        std::random_shuffle(
            options.begin(),
            options.end()
        );

        int i;
        for (i = 0; i < count; i++) {
            eLastRandom.push_back(options[i]);
        }
    }
    else {
        int i;

        for (i = 0; i < count; i++) {
            int index =
                std::rand() %
                static_cast<int>(options.size());

            eLastRandom.push_back(options[index]);
        }
    }
}

static EValue eRandomRange(const EValue& minimumValue, const EValue& maximumValue) {
    long long minimum = static_cast<long long>(toNumber(minimumValue));
    long long maximum = static_cast<long long>(toNumber(maximumValue));
    if (minimum > maximum) std::swap(minimum, maximum);
    unsigned long long span = static_cast<unsigned long long>(maximum - minimum) + 1ULL;
    long long value = minimum + static_cast<long long>(static_cast<unsigned long long>(std::rand()) % span);
    return EValue::fromInt(value);
}

static EValue randomResultValue() {
    if (eLastRandom.empty()) {
        throw std::runtime_error(
            "Meg nem futott rnd utasitas."
        );
    }

    if (eLastRandom.size() == 1) {
        return eLastRandom[0];
    }

    std::string joined;
    std::size_t i;

    for (i = 0; i < eLastRandom.size(); i++) {
        if (i > 0) joined += " ";
        joined += toText(eLastRandom[i]);
    }

    return EValue::fromString(joined);
}

static void saveRandomInput(
    const std::string& name
) {
    eInputs[name] = randomResultValue();
}

static void waitForInteraction() {
    std::string ignored;
    std::getline(std::cin, ignored);
}

static void waitMilliseconds(
    unsigned long milliseconds
) {
    eIkonPresent();
    unsigned long elapsed = 0;
    while (elapsed < milliseconds) {
        eIkonPump();
        unsigned long step = (milliseconds - elapsed > 10UL) ? 10UL : (milliseconds - elapsed);
        Sleep(step);
        elapsed += step;
    }
}

static void waitForever() {
    eIkonPresent();
    for (;;) {
        eIkonPump();
        Sleep(10);
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::srand(
        static_cast<unsigned int>(std::time(NULL))
    );

    clearAnnotations();

    try {
)CPP";
}

static std::string runtimeFooter() {
    return R"CPP(
    }
    catch (const std::exception& error) {
        std::cerr
            << "E futasi hiba: "
            << error.what()
            << std::endl;

        return 1;
    }

    eIkonLoop();
    return 0;
}
)CPP";
}

class ESourceCompiler {
private:
    std::vector<std::string> lines;
    std::size_t position;
    HydroCompileResult& result;
    bool studioLoaded;
    bool hajoStudioLoaded;
    bool gombLoaded;
    bool ikonainLoaded;
    std::map<std::string, std::string> knownTypes;
    std::map<std::string, std::string> knownLists;

    bool fail(
        int line,
        const std::string& message
    ) {
        result.error.hasError = true;
        result.error.line = line;
        result.error.column = 1;
        result.error.message = message;
        return false;
    }

    static std::string removeEndDot(
        const std::string& text
    ) {
        std::string result = trim(text);

        if (!result.empty() && result[result.size() - 1] == '.') {
            result.erase(result.size() - 1);
        }

        return trim(result);
    }

    static bool parseQuotedName(
        const std::string& text,
        std::size_t start,
        std::string& name,
        std::size_t& after
    ) {
        if (
            start >= text.size() ||
            text[start] != '\''
        ) {
            return false;
        }

        std::size_t end =
            text.find('\'', start + 1);

        if (end == std::string::npos) {
            return false;
        }

        name = text.substr(
            start + 1,
            end - start - 1
        );

        after = end + 1;
        return !name.empty();
    }

    static const char* runtimeType(
        const std::string& type
    ) {
        if (type == "kocsi") return "TYPE_KOCSI";
        if (type == "hajo") return "TYPE_HAJO";
        if (type == "let") return "TYPE_LET";
        if (type == "felho") return "TYPE_FELHO";
        return "TYPE_HANG";
    }

    static std::vector<std::string> splitHajoItems(
        const std::string& text,
        char separator
    ) {
        std::vector<std::string> result;
        std::string currentItem;
        bool inString = false;
        bool inVariable = false;
        int parentheses = 0;
        std::size_t i;

        for (i = 0; i < text.size(); i++) {
            char c = text[i];
            if (c == '"' && !inVariable) inString = !inString;
            if (c == '\'' && !inString) inVariable = !inVariable;
            if (!inString && !inVariable) {
                if (c == '(') parentheses++;
                else if (c == ')' && parentheses > 0) parentheses--;
            }

            if (
                c == separator && !inString &&
                !inVariable && parentheses == 0
            ) {
                result.push_back(trim(currentItem));
                currentItem.clear();
            }
            else {
                currentItem += c;
            }
        }

        result.push_back(trim(currentItem));
        return result;
    }

    bool requireHajoStudio(int line) {
        if (hajoStudioLoaded) return true;
        return fail(
            line,
            "Ehhez kell: #kontarb :hajostudio.j:"
        );
    }

    bool compileSelector(
        std::string text,
        std::string& valueCode,
        char& mode,
        int& count,
        int line
    ) {
        text = trim(text);
        mode = 'b';
        count = -1;

        std::size_t left = text.rfind('(');
        if (
            left != std::string::npos &&
            !text.empty() && text[text.size()-1] == ')'
        ) {
            std::string selector = trim(
                text.substr(left + 1, text.size() - left - 2)
            );
            text = trim(text.substr(0, left));
            std::size_t at = selector.find('@');
            if (at != std::string::npos) {
                if (!selector.empty()) mode = selector[0];
                selector = selector.substr(at + 1);
            }
            if (!isIntegerText(trim(selector))) {
                return fail(line, "Hibas hajo karaktervalaszto.");
            }
            count = std::atoi(trim(selector).c_str());
        }

        return compileExpression(text, valueCode, line);
    }

    bool compileHajoCall(
        const std::string& text,
        std::string& generated,
        int line
    ) {
        std::string value = trim(text);

        if (startsWith(value, "hajohos_")) {
            if (!requireHajoStudio(line)) return false;
            std::string target = value.substr(8);
            if (!endsWith(target, "&")) target += "&";
            generated = "hajoLength(getInput(\"" +
                escapeCppString(target) + "\"))";
            return true;
        }

        if (startsWith(value, "hajohos ")) {
            if (!requireHajoStudio(line)) return false;
            std::string inner;
            if (!compileExpression(value.substr(8), inner, line)) return false;
            generated = "hajoLength(" + inner + ")";
            return true;
        }

        const char* names[] = {
            "hajotictil", "hajomtictil", "hajomdvd",
            "hajomndvd", "hajostroke", "hajohajo",
            "hajokocsi"
        };
        int nameIndex;
        for (nameIndex = 0; nameIndex < 7; nameIndex++) {
            std::string name = names[nameIndex];
            if (!startsWith(value, name + "(")) continue;
            if (!requireHajoStudio(line)) return false;
            if (value[value.size()-1] != ')') {
                return fail(line, "Hianyzik a hajo fuggveny ) jele.");
            }
            std::string body = value.substr(
                name.size()+1, value.size()-name.size()-2
            );

            if (name == "hajostroke") {
                std::string arg;
                if (!compileExpression(body, arg, line)) return false;
                generated = "hajoStroke(" + arg + ")";
                return true;
            }

            if (name == "hajohajo") {
                std::vector<std::string> parts = splitHajoItems(body, ';');
                if (parts.size() != 2) return fail(line, "A hajohajo ket elemet ker.");
                std::string a,b;
                if (!compileExpression(parts[0],a,line) || !compileExpression(parts[1],b,line)) return false;
                generated = "hajoContains("+a+", "+b+")";
                return true;
            }

            if (name == "hajokocsi") {
                std::vector<std::string> halves = splitHajoItems(body, ';');
                if (halves.size() < 2) return fail(line, "A hajokocsi forrast es karaktereket ker.");
                std::string source;
                if (!compileExpression(halves[0],source,line)) return false;
                std::vector<std::string> chars;
                std::size_t h;
                for (h=1; h<halves.size(); h++) {
                    std::vector<std::string> more=splitHajoItems(halves[h], ';');
                    chars.insert(chars.end(),more.begin(),more.end());
                }
                std::stringstream out;
                out << "hajoCharacters(" << source << ", std::vector<EValue>{";
                for (h=0; h<chars.size(); h++) {
                    std::string item=trim(chars[h]);
                    if (startsWith(item,"j@")||startsWith(item,"b@")||startsWith(item,"d@")) item=trim(item.substr(2));
                    std::string c;
                    if (!compileExpression(item,c,line)) return false;
                    if (h) out << ", "; out << c;
                }
                out << "})"; generated=out.str(); return true;
            }

            std::vector<std::string> mainParts = splitHajoItems(body, ';');
            std::vector<std::string> items = splitHajoItems(mainParts[0], '-');
            char globalMode='b'; int globalCount=-1;
            if ((name=="hajomndvd") && mainParts.size()>1) {
                std::string selector=trim(mainParts[1]);
                std::size_t at=selector.find('@');
                if(at!=std::string::npos){globalMode=selector[0];selector=selector.substr(at+1);} 
                if(!isIntegerText(trim(selector))) return fail(line,"Hibas hajomndvd karakterszam.");
                globalCount=std::atoi(trim(selector).c_str());
            }
            std::stringstream out;
            out << (name=="hajomdvd"||name=="hajomndvd" ? "hajoCompare(" : "hajoConcat(");
            out << "std::vector<EValue>{";
            std::size_t i;
            for(i=0;i<items.size();i++){
                std::string code; char mode='b'; int count=-1;
                if(name=="hajomtictil") {
                    if(!compileSelector(items[i],code,mode,count,line)) return false;
                } else {
                    if(!compileExpression(items[i],code,line)) return false;
                    if(name=="hajomndvd"){mode=globalMode;count=globalCount;}
                }
                if(count>=0) code="hajoSlice("+code+", '"+std::string(1,mode)+"', "+std::to_string(count)+")";
                if(i) out << ", "; out << code;
            }
            out << "})"; generated=out.str(); return true;
        }
        return false;
    }

    static bool isOuterParenthesized(const std::string& text) {
        if (text.size() < 2 || text[0] != '(' || text[text.size()-1] != ')') return false;
        bool inString = false, inVariable = false;
        int depth = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '"' && !inVariable && (i == 0 || text[i-1] != '\\')) inString = !inString;
            else if (c == '\'' && !inString) inVariable = !inVariable;
            if (inString || inVariable) continue;
            if (c == '(') ++depth;
            else if (c == ')') {
                --depth;
                if (depth == 0 && i + 1 < text.size()) return false;
            }
        }
        return depth == 0;
    }

    static std::size_t findTopLevelOperator(
        const std::string& text,
        const std::vector<std::string>& operators
    ) {
        bool inString = false, inVariable = false;
        int depth = 0;
        for (std::size_t pos = text.size(); pos-- > 0;) {
            char c = text[pos];
            if (c == '"' && !inVariable && (pos == 0 || text[pos-1] != '\\')) inString = !inString;
            else if (c == '\'' && !inString) inVariable = !inVariable;
            if (inString || inVariable) continue;
            if (c == ')') { ++depth; continue; }
            if (c == '(') { --depth; continue; }
            if (depth != 0) continue;
            for (std::size_t i = 0; i < operators.size(); ++i) {
                const std::string& op = operators[i];
                if (pos + op.size() <= text.size() && text.compare(pos, op.size(), op) == 0) {
                    if ((op == "-" || op == "+") &&
                        (pos == 0 || text[pos-1] == '(' || text[pos-1] == '+' || text[pos-1] == '-' ||
                         text[pos-1] == '*' || text[pos-1] == '/' || text[pos-1] == '%' ||
                         text[pos-1] == '=' || text[pos-1] == '!' || text[pos-1] == '<' || text[pos-1] == '>')) {
                        continue;
                    }
                    return pos;
                }
            }
        }
        return std::string::npos;
    }


    bool requireGomb(int line) { if(gombLoaded) return true; return fail(line,"Ehhez kell: #kontarb :gomb.j:"); }
    bool requireIkonain(int line) { if(ikonainLoaded) return true; return fail(line,"Az IKONAIN2 konyvtarat elobb le kell tolteni az E Books fulon, majd be kell tolteni."); }
    bool compileKeyList(const std::string& text, std::string& generated, int line) {
        std::size_t l=text.find('('), r=text.rfind(')');
        if(l==std::string::npos||r==std::string::npos||r<=l) return fail(line,"Hibas gmb forma.");
        std::string body=text.substr(l+1,r-l-1); std::vector<std::string> names; std::size_t i=0;
        while(i<body.size()){std::size_t a=body.find('"',i); if(a==std::string::npos)break; std::size_t b=body.find('"',a+1); if(b==std::string::npos)return fail(line,"Lezaratlan gombnev."); names.push_back(body.substr(a+1,b-a-1));i=b+1;}
        if(names.empty()) return fail(line,"A gmb legalabb egy gombot ker.");
        generated="std::vector<std::string>{"; for(std::size_t n=0;n<names.size();++n){if(n)generated+=",";generated+="\""+escapeCppString(names[n])+"\"";} generated+="}"; return true;
    }
    bool compileColorList(const std::string& text, std::string& generated, int line) {
        std::size_t l=text.find('('), r=text.rfind(')'); if(l==std::string::npos||r==std::string::npos||r<=l)return fail(line,"Hibas szinlista.");
        std::vector<std::string> cs=splitHajoItems(text.substr(l+1,r-l-1),';'); if(cs.empty())return fail(line,"Legalabb egy szin kell.");
        generated="std::vector<std::string>{"; for(std::size_t i=0;i<cs.size();++i){std::string c=trim(cs[i]);if(i)generated+=",";generated+="\""+escapeCppString(c)+"\"";}generated+="}";return true;
    }
    bool parseListReference(
        const std::string& text,
        std::string& name,
        std::vector<std::string>& selectors,
        bool& all,
        int line
    ) {
        std::string clean = trim(text);
        std::size_t left = clean.find('[');
        std::size_t right = clean.rfind(']');
        if (left == std::string::npos || right == std::string::npos || right < left) {
            return false;
        }

        name = trim(clean.substr(0, left));
        if (name.empty() || name[name.size() - 1] != 'T') {
            return false;
        }

        if (knownLists.count(name) == 0) {
            fail(line, "Nem letezo lista: " + name);
            return false;
        }

        if (!trim(clean.substr(right + 1)).empty()) {
            fail(line, "Hibas listaelem-hivatkozas.");
            return false;
        }

        std::string body = trim(clean.substr(left + 1, right - left - 1));
        all = body == "-";
        selectors.clear();
        if (all) return true;
        if (body.empty()) {
            fail(line, "A lista [] reszebe index kell.");
            return false;
        }

        selectors = splitHajoItems(body, ';');
        for (std::size_t i = 0; i < selectors.size(); ++i) {
            selectors[i] = trim(selectors[i]);
            if (selectors[i].empty()) {
                fail(line, "A lista indexe nem lehet ures.");
                return false;
            }
        }
        return true;
    }

    bool compileExpression(
        const std::string& text,
        std::string& generated,
        int line
    ) {
        std::string error;
        std::string cleanText = trim(text);

        if (startsWith(cleanText, "listaT_hossz ")) {
            std::string listName = trim(cleanText.substr(13));
            if (listName.empty()) {
                return fail(line, "A listaT_hossz utan listanev kell.");
            }
            if (listName[listName.size() - 1] != 'T') {
                return fail(line, "A lista neve T betuvel vegzodjon.");
            }
            if (knownLists.count(listName) == 0) {
                return fail(line, "Nem letezo lista: " + listName);
            }
            generated = "eListLength(\"" +
                escapeCppString(listName) + "\")";
            return true;
        }

        {
            std::string listName;
            std::vector<std::string> selectors;
            bool all = false;
            if (cleanText.find('[') != std::string::npos &&
                cleanText.size() > 2 &&
                cleanText[cleanText.size() - 1] == ']') {
                bool parsed = parseListReference(
                    cleanText, listName, selectors, all, line
                );
                if (result.error.hasError) return false;
                if (parsed) {
                    std::vector<std::string> selectorCodes;
                    for (std::size_t i = 0; i < selectors.size(); ++i) {
                        std::string selectorCode;
                        if (!compileExpression(selectors[i], selectorCode, line)) {
                            return false;
                        }
                        selectorCodes.push_back(
                            "eListIndexFromValue(" + selectorCode + ")"
                        );
                    }
                    if (!all && selectorCodes.size() == 1) {
                        generated = "eListGetOne(\"" +
                            escapeCppString(listName) + "\", " +
                            selectorCodes[0] + ")";
                    }
                    else {
                        generated = "eListRead(\"" +
                            escapeCppString(listName) +
                            "\", std::vector<long long>{";
                        for (std::size_t i = 0; i < selectorCodes.size(); ++i) {
                            if (i) generated += ",";
                            generated += selectorCodes[i];
                        }
                        generated += "}, ";
                        generated += all ? "true)" : "false)";
                    }
                    return true;
                }
            }
        }
        if (startsWith(cleanText, "sslen gmb(")) { if(!requireGomb(line)) return false; std::string k; if(!compileKeyList(cleanText,k,line))return false; generated="eWaitKeyOnce("+k+")"; return true; }
        if (startsWith(cleanText, "gomb gmb(")) { if(!requireGomb(line)) return false; std::string k; if(!compileKeyList(cleanText,k,line))return false; generated="eObservedKey("+k+")"; return true; }
        while (isOuterParenthesized(cleanText)) {
            cleanText = trim(cleanText.substr(1, cleanText.size()-2));
        }

        if (startsWith(cleanText, "rnd") && cleanText.size() >= 6) {
            std::size_t rndOpen = cleanText.find('(');
            if (rndOpen != std::string::npos && trim(cleanText.substr(3, rndOpen - 3)).empty() && cleanText[cleanText.size()-1] == ')') {
            std::string inside = cleanText.substr(rndOpen + 1, cleanText.size() - rndOpen - 2);
            bool inString = false, inVariable = false;
            int depth = 0;
            std::size_t separator = std::string::npos;
            for (std::size_t i = 0; i < inside.size(); ++i) {
                char c = inside[i];
                if (c == '"' && !inVariable && (i == 0 || inside[i-1] != '\\')) inString = !inString;
                else if (c == '\'' && !inString) inVariable = !inVariable;
                if (inString || inVariable) continue;
                if (c == '(') ++depth;
                else if (c == ')') --depth;
                else if (c == ';' && depth == 0) { separator = i; break; }
            }
            if (separator == std::string::npos) return fail(line, "Az rnd(min; max) alakban hianyzik a ; jel.");
            std::string minimumCode, maximumCode;
            if (!compileExpression(inside.substr(0, separator), minimumCode, line)) return false;
            if (!compileExpression(inside.substr(separator + 1), maximumCode, line)) return false;
            generated = "eRandomRange(" + minimumCode + ", " + maximumCode + ")";
            return true;
            }
        }

        std::size_t opPos = findTopLevelOperator(cleanText, std::vector<std::string>{"+", "-"});
        if (opPos != std::string::npos) {
            std::string left, right;
            if (!compileExpression(cleanText.substr(0, opPos), left, line)) return false;
            if (!compileExpression(cleanText.substr(opPos + 1), right, line)) return false;
            generated = std::string(cleanText[opPos] == '+' ? "eAdd(" : "eSub(") + left + ", " + right + ")";
            return true;
        }

        opPos = findTopLevelOperator(cleanText, std::vector<std::string>{"*", "/", "%"});
        if (opPos != std::string::npos) {
            std::string left, right;
            if (!compileExpression(cleanText.substr(0, opPos), left, line)) return false;
            if (!compileExpression(cleanText.substr(opPos + 1), right, line)) return false;
            const char* fn = cleanText[opPos] == '*' ? "eMul" : (cleanText[opPos] == '/' ? "eDiv" : "eMod");
            generated = std::string(fn) + "(" + left + ", " + right + ")";
            return true;
        }

        if (startsWith(cleanText, "hajo")) {
            if (compileHajoCall(cleanText, generated, line)) return true;
            if (result.error.hasError) return false;
        }

        if (!compileValueExpression(cleanText, generated, error)) {
            return fail(line, error);
        }
        return true;
    }

    bool compileCondition(
        const std::string& text,
        std::string& generated,
        int line
    ) {
        std::string cleanText = trim(text);
        if (startsWith(cleanText, "sslen gmb(")) { if(!requireGomb(line))return false; std::string k;if(!compileKeyList(cleanText,k,line))return false;generated="eWaitKeyOnce("+k+")";return true; }
        if (startsWith(cleanText, "gomb gmb(")) { if(!requireGomb(line))return false; std::string k;if(!compileKeyList(cleanText,k,line))return false;generated="eObservedKey("+k+")";return true; }
        while (isOuterParenthesized(cleanText)) cleanText = trim(cleanText.substr(1, cleanText.size()-2));

        std::size_t pos = findTopLevelOperator(cleanText, std::vector<std::string>{" LL "});
        if (pos != std::string::npos) {
            std::string left, right;
            if (!compileCondition(cleanText.substr(0,pos), left, line)) return false;
            if (!compileCondition(cleanText.substr(pos+4), right, line)) return false;
            generated = "((" + left + ") || (" + right + "))";
            return true;
        }
        pos = findTopLevelOperator(cleanText, std::vector<std::string>{" fer "});
        if (pos != std::string::npos) {
            std::string left, right;
            if (!compileCondition(cleanText.substr(0,pos), left, line)) return false;
            if (!compileCondition(cleanText.substr(pos+5), right, line)) return false;
            generated = "((" + left + ") && (" + right + "))";
            return true;
        }
        if (startsWith(cleanText, "tyet ")) {
            std::string inner;
            if (!compileCondition(cleanText.substr(5), inner, line)) return false;
            generated = "!(" + inner + ")";
            return true;
        }

        const std::vector<std::string> comparisons{"==", "!=", "<=", ">=", "<", ">"};
        pos = findTopLevelOperator(cleanText, comparisons);
        if (pos != std::string::npos) {
            std::string op;
            for (std::size_t i=0;i<comparisons.size();++i) if (cleanText.compare(pos, comparisons[i].size(), comparisons[i])==0) { op=comparisons[i]; break; }
            std::string left, right;
            if (!compileExpression(cleanText.substr(0,pos), left, line)) return false;
            if (!compileExpression(cleanText.substr(pos+op.size()), right, line)) return false;
            const char* fn = op=="==" ? "eEqual" :
                (op=="!=" ? "eNotEqual" :
                (op=="<=" ? "eLessEqual" :
                (op==">=" ? "eGreaterEqual" :
                (op=="<" ? "eLess" : "eGreater"))));
            generated = std::string(fn) + "(" + left + ", " + right + ")";
            return true;
        }

        std::string value;
        if (!compileExpression(cleanText, value, line)) return false;
        generated = "eTruthy(" + value + ")";
        return true;
    }

    bool parseBlock(
        std::stringstream& output,
        int depth,
        bool stopAtBrace
    ) {
        while (position < lines.size()) {
            std::string raw = lines[position];
            std::string line = trim(raw);
            int sourceLine =
                static_cast<int>(position) + 1;

            if (
                line.empty() ||
                startsWith(line, "//")
            ) {
                position++;
                continue;
            }

            if (line == "}" || line == "}.") {
                if (!stopAtBrace) {
                    return fail(
                        sourceLine,
                        "Varatlan } blokkzaras."
                    );
                }

                position++;
                return true;
            }

            if (startsWith(line, "elz")) {
                if (stopAtBrace) {
                    return true;
                }

                return fail(
                    sourceLine,
                    "Az elz csak kozvetlenul ahf utan allhat."
                );
            }

            if (!compileStatement(
                output,
                depth,
                line,
                sourceLine
            )) {
                return false;
            }
        }

        if (stopAtBrace) {
            return fail(
                static_cast<int>(lines.size()),
                "Hianyzik a blokk zaro } jele."
            );
        }

        return true;
    }

    bool compileIf(
        std::stringstream& output,
        int depth,
        const std::string& line,
        int sourceLine
    ) {
        std::size_t marker = line.rfind("()");

        if (marker == std::string::npos) {
            return fail(
                sourceLine,
                "Az ahf feltetel utan () szukseges."
            );
        }

        std::string conditionText =
            trim(line.substr(3, marker - 3));

        std::string condition;

        if (!compileCondition(
            conditionText,
            condition,
            sourceLine
        )) {
            return false;
        }

        if (line.find('{', marker + 2) == std::string::npos) {
            return fail(
                sourceLine,
                "Az ahf sor vegen { szukseges."
            );
        }

        indent(output, depth);
        output << "if (" << condition << ") {\n";

        position++;

        if (!parseBlock(output, depth + 1, true)) {
            return false;
        }

        indent(output, depth);
        output << "}";

        std::size_t saved = position;

        while (
            position < lines.size() &&
            trim(lines[position]).empty()
        ) {
            position++;
        }

        if (
            position < lines.size() &&
            startsWith(trim(lines[position]), "elz")
        ) {
            std::string elseLine =
                trim(lines[position]);

            if (elseLine.find('{') == std::string::npos) {
                return fail(
                    static_cast<int>(position) + 1,
                    "Az elz sor vegen { szukseges."
                );
            }

            output << " else {\n";
            position++;

            if (!parseBlock(
                output,
                depth + 1,
                true
            )) {
                return false;
            }

            indent(output, depth);
            output << "}\n";
        }
        else {
            position = saved;
            output << "\n";
        }

        return true;
    }

    bool compileAdcu(
        std::stringstream& output,
        int depth,
        const std::string& line,
        int sourceLine
    ) {
        if (
            startsWith(line, "adcu -") &&
            line.find("(veg)") != std::string::npos
        ) {
            if (line.find('{') == std::string::npos) {
                return fail(
                    sourceLine,
                    "Az adcu blokkhoz { szukseges."
                );
            }

            indent(output, depth);
            output << "for (;;) {\n";
            position++;

            if (!parseBlock(
                output,
                depth + 1,
                true
            )) {
                return false;
            }

            indent(output, depth);
            output << "}\n";
            return true;
        }

        std::size_t nam =
            line.find("(nam)");

        if (nam != std::string::npos) {
            std::string countText =
                trim(line.substr(4, nam - 4));

            if (!isIntegerText(countText)) {
                return fail(
                    sourceLine,
                    "Az adcu nam elott egesz szam kell."
                );
            }

            indent(output, depth);
            output
                << "for (long long eRepeat = 0; "
                << "eRepeat < "
                << countText
                << "; ++eRepeat) {\n";

            position++;

            if (!parseBlock(
                output,
                depth + 1,
                true
            )) {
                return false;
            }

            indent(output, depth);
            output << "}\n";
            return true;
        }

        bool whileTrue =
            line.find("(adcu_tyet)") !=
            std::string::npos;

        std::string marker =
            whileTrue
            ? "(adcu_tyet)"
            : "()";

        std::size_t markerPosition =
            line.rfind(marker);

        if (markerPosition == std::string::npos) {
            return fail(
                sourceLine,
                "Hibas adcu forma."
            );
        }

        std::string conditionText =
            trim(
                line.substr(
                    4,
                    markerPosition - 4
                )
            );

        std::string condition;

        if (!compileCondition(
            conditionText,
            condition,
            sourceLine
        )) {
            return false;
        }

        if (
            line.find(
                '{',
                markerPosition + marker.size()
            ) == std::string::npos
        ) {
            return fail(
                sourceLine,
                "Az adcu blokkhoz { szukseges."
            );
        }

        indent(output, depth);

        if (whileTrue) {
            output << "while (" << condition << ") {\n";
        }
        else {
            output << "while (!(" << condition << ")) {\n";
        }

        position++;

        if (!parseBlock(
            output,
            depth + 1,
            true
        )) {
            return false;
        }

        indent(output, depth);
        output << "}\n";

        return true;
    }

    bool compileRnd(
        std::stringstream& output,
        int depth,
        const std::string& line,
        int sourceLine
    ) {
        std::string body = removeEndDot(line);
        std::size_t cursor = 3;

        while (
            cursor < body.size() &&
            std::isspace(
                static_cast<unsigned char>(body[cursor])
            )
        ) {
            cursor++;
        }

        bool unique = false;

        if (
            body.compare(
                cursor,
                10,
                "(rnd_tyet)"
            ) == 0
        ) {
            unique = true;
            cursor += 10;
        }

        while (
            cursor < body.size() &&
            std::isspace(
                static_cast<unsigned char>(body[cursor])
            )
        ) {
            cursor++;
        }

        if (
            cursor >= body.size() ||
            body[cursor] != '('
        ) {
            return fail(
                sourceLine,
                "Az rnd listaja ( ) kozott legyen."
            );
        }

        std::size_t listEnd =
            body.find(')', cursor + 1);

        if (listEnd == std::string::npos) {
            return fail(
                sourceLine,
                "Az rnd listaja nincs lezarva."
            );
        }

        std::string listText =
            trim(
                body.substr(
                    cursor + 1,
                    listEnd - cursor - 1
                )
            );

        std::size_t countStart =
            body.find('(', listEnd + 1);

        std::size_t countEnd =
            countStart == std::string::npos
            ? std::string::npos
            : body.find(')', countStart + 1);

        if (
            countStart == std::string::npos ||
            countEnd == std::string::npos
        ) {
            return fail(
                sourceLine,
                "Az rnd masodik (darab) resze hianyzik."
            );
        }

        std::string countText =
            trim(
                body.substr(
                    countStart + 1,
                    countEnd - countStart - 1
                )
            );

        if (!isIntegerText(countText)) {
            return fail(
                sourceLine,
                "Az rnd darabszama egesz szam legyen."
            );
        }

        std::vector<std::string> itemTexts =
            splitRandomItems(listText);

        if (itemTexts.empty()) {
            return fail(
                sourceLine,
                "Az rnd lista ures."
            );
        }

        std::vector<std::string> generatedItems;

        if (
            itemTexts.size() == 1 &&
            itemTexts[0].find('-') !=
                std::string::npos
        ) {
            std::size_t dash =
                itemTexts[0].find('-');

            std::string first =
                trim(itemTexts[0].substr(0, dash));

            std::string last =
                trim(itemTexts[0].substr(dash + 1));

            if (
                isIntegerText(first) &&
                isIntegerText(last)
            ) {
                int startValue = std::atoi(first.c_str());
                int endValue = std::atoi(last.c_str());
                int step =
                    startValue <= endValue
                    ? 1
                    : -1;

                int value;

                for (
                    value = startValue;
                    ;
                    value += step
                ) {
                    std::stringstream item;
                    item
                        << "EValue::fromInt("
                        << value
                        << ")";

                    generatedItems.push_back(
                        item.str()
                    );

                    if (value == endValue) {
                        break;
                    }
                }
            }
        }

        if (generatedItems.empty()) {
            std::size_t i;

            for (i = 0; i < itemTexts.size(); i++) {
                std::string generated;
                std::string item = trim(itemTexts[i]);

                if (
                    !item.empty() &&
                    item[0] != '"' &&
                    item[0] != '\'' &&
                    !std::isdigit(
                        static_cast<unsigned char>(item[0])
                    ) &&
                    item[0] != '-'
                ) {
                    generated =
                        "EValue::fromString(\"" +
                        escapeCppString(item) +
                        "\")";
                }
                else if (!compileExpression(
                    item,
                    generated,
                    sourceLine
                )) {
                    return false;
                }

                generatedItems.push_back(generated);
            }
        }

        indent(output, depth);
        output << "{ std::vector<EValue> eRndOptions; ";

        std::size_t i;

        for (i = 0; i < generatedItems.size(); i++) {
            output
                << "eRndOptions.push_back("
                << generatedItems[i]
                << "); ";
        }

        output
            << "setRandom(eRndOptions, "
            << countText
            << ", "
            << (unique ? "true" : "false")
            << "); }\n";

        position++;
        return true;
    }

    bool compileTiw(
        std::stringstream& output,
        int depth,
        const std::string& line,
        int sourceLine
    ) {
        std::string body =
            removeEndDot(trim(line.substr(3)));

        if (body == "()") {
            indent(output, depth);
            output << "waitForInteraction();\n";
            position++;
            return true;
        }

        if (
            body.find("(tiw_veg)") !=
            std::string::npos
        ) {
            indent(output, depth);
            output << "waitForever();\n";
            position++;
            return true;
        }

        std::size_t marker = body.find("()");

        if (marker == std::string::npos) {
            return fail(
                sourceLine,
                "Hibas tiw forma."
            );
        }

        std::string duration =
            trim(body.substr(0, marker));

        unsigned long multiplier = 0;
        std::string numberText;

        if (endsWith(duration, "min")) {
            multiplier = 60000UL;
            numberText =
                duration.substr(
                    0,
                    duration.size() - 3
                );
        }
        else if (endsWith(duration, "ms")) {
            multiplier = 1UL;
            numberText =
                duration.substr(
                    0,
                    duration.size() - 2
                );
        }
        else if (endsWith(duration, "s")) {
            multiplier = 1000UL;
            numberText =
                duration.substr(
                    0,
                    duration.size() - 1
                );
        }
        else if (endsWith(duration, "h")) {
            multiplier = 3600000UL;
            numberText =
                duration.substr(
                    0,
                    duration.size() - 1
                );
        }
        else {
            return fail(
                sourceLine,
                "A tiw mertekegysege ms, s, min vagy h."
            );
        }

        numberText = trim(numberText);

        if (!isIntegerText(numberText)) {
            return fail(
                sourceLine,
                "A tiw ideje egesz szam legyen."
            );
        }

        indent(output, depth);
        output
            << "waitMilliseconds("
            << numberText
            << "UL * "
            << multiplier
            << "UL);\n";

        position++;
        return true;
    }

    bool compileStatement(
        std::stringstream& output,
        int depth,
        const std::string& line,
        int sourceLine
    ) {
        if (startsWith(line, "#kontarb")) {
            if (line == "#kontarb :studio.j:") {
                studioLoaded = true;
            }
            else if (line == "#kontarb :hajostudio.j:") { hajoStudioLoaded = true; }
            else if (line == "#kontarb :gomb.j:") { gombLoaded = true; }
            else if (line == "#kontarb :IKONAIN2:") { ikonainLoaded = true; studioLoaded = true; indent(output,depth); output << "eIkonInit();\n"; }
            else {
                return fail(
                    sourceLine,
                    "Ismeretlen konyvtar. Hasznalhato: :studio.j:, :hajostudio.j:, :gomb.j: vagy :IKONAIN2:"
                );
            }

            position++;
            return true;
        }


        if (startsWith(line, "list ")) {
            std::string header = trim(line.substr(5));
            std::size_t typeEnd = header.find(' ');
            if (typeEnd == std::string::npos) {
                return fail(sourceLine, "A list utan adattipus es listanev kell.");
            }

            std::string type = trim(header.substr(0, typeEnd));
            if (type != "kocsi" && type != "hajo" &&
                type != "let" && type != "felho") {
                return fail(sourceLine, "Ismeretlen lista-adattipus.");
            }

            std::string rest = trim(header.substr(typeEnd + 1));
            std::size_t bracketLeft = rest.find('[');
            std::size_t bracketRight = rest.find(']');
            if (bracketLeft == std::string::npos || bracketRight == std::string::npos ||
                bracketRight < bracketLeft) {
                return fail(sourceLine, "A lista meretet [] jelek koze kell irni.");
            }

            std::string name = trim(rest.substr(0, bracketLeft));
            if (name.empty() || name[name.size() - 1] != 'T') {
                return fail(sourceLine, "A lista neve T betuvel vegzodjon.");
            }
            if (knownLists.count(name) || knownTypes.count(name)) {
                return fail(sourceLine, "Mar letezik ilyen nev: " + name);
            }

            std::string sizeText = trim(rest.substr(
                bracketLeft + 1, bracketRight - bracketLeft - 1
            ));
            long long declaredSize = -1;
            if (!sizeText.empty()) {
                if (!isIntegerText(sizeText)) {
                    return fail(sourceLine, "A lista merete egesz szam legyen.");
                }
                declaredSize = std::strtoll(sizeText.c_str(), NULL, 10);
                if (declaredSize < 0) {
                    return fail(sourceLine, "A lista merete nem lehet negativ.");
                }
            }

            if (rest.find('{', bracketRight) == std::string::npos) {
                return fail(sourceLine, "A lista elemei { } blokkba keruljenek.");
            }

            std::vector<std::string> valueCodes;
            std::size_t next = position + 1;
            bool closed = false;
            while (next < lines.size()) {
                std::string itemLine = trim(lines[next]);
                if (itemLine == "}") {
                    closed = true;
                    break;
                }
                if (!itemLine.empty()) {
                    if (itemLine[itemLine.size() - 1] != ';') {
                        return fail(static_cast<int>(next) + 1,
                            "A lista minden eleme ; jellel vegzodjon.");
                    }
                    itemLine = trim(itemLine.substr(0, itemLine.size() - 1));
                    std::string code;
                    if (!compileExpression(itemLine, code, static_cast<int>(next) + 1)) {
                        return false;
                    }
                    valueCodes.push_back(code);
                }
                ++next;
            }

            if (!closed) {
                return fail(sourceLine, "Hianyzik a lista zaró } jele.");
            }
            if (declaredSize >= 0 &&
                static_cast<long long>(valueCodes.size()) > declaredSize) {
                return fail(sourceLine,
                    "Tobb listaelem van, mint a [] reszben megadott meret.");
            }

            indent(output, depth);
            output << "eDeclareList(\"" << escapeCppString(name) << "\", "
                   << runtimeType(type) << ", " << declaredSize
                   << ", std::vector<EValue>{";
            for (std::size_t i = 0; i < valueCodes.size(); ++i) {
                if (i) output << ", ";
                output << valueCodes[i];
            }
            output << "});\n";

            knownLists[name] = type;
            position = next + 1;
            return true;
        }

        if (startsWith(line, "nib ") && line.find('T') != std::string::npos &&
            line.find("gomb gmb(") == std::string::npos) {
            std::string body = removeEndDot(trim(line.substr(4)));
            std::size_t left = body.find('[');
            if (left == std::string::npos) {
                if (knownLists.count(body) != 0) {
                    indent(output, depth);
                    output << "eDeleteList(\"" << escapeCppString(body) << "\");\n";
                    knownLists.erase(body);
                    position++;
                    return true;
                }
            }
            else {
                std::string name;
                std::vector<std::string> selectors;
                bool all = false;
                if (!parseListReference(body, name, selectors, all, sourceLine)) {
                    if (result.error.hasError) return false;
                }
                else {
                    if (all) {
                        indent(output, depth);
                        output << "eDeleteList(\"" << escapeCppString(name) << "\");\n";
                        knownLists.erase(name);
                    }
                    else {
                        indent(output, depth);
                        output << "eListDeleteItems(\"" << escapeCppString(name)
                               << "\", std::vector<long long>{";
                        for (std::size_t i = 0; i < selectors.size(); ++i) {
                            std::string selectorCode;
                            if (!compileExpression(selectors[i], selectorCode, sourceLine)) {
                                return false;
                            }
                            if (i) output << ",";
                            output << "eListIndexFromValue(" << selectorCode << ")";
                        }
                        output << "});\n";
                    }
                    position++;
                    return true;
                }
            }
        }

        if (!line.empty() && line.find('[') != std::string::npos &&
            line.find('=') != std::string::npos) {
            std::string body = removeEndDot(line);
            std::size_t equal = body.find('=');
            std::string target = trim(body.substr(0, equal));
            std::string name;
            std::vector<std::string> selectors;
            bool all = false;
            if (parseListReference(target, name, selectors, all, sourceLine)) {
                if (all || selectors.size() != 1) {
                    return fail(sourceLine,
                        "Ertekadaskor pontosan egy listaindexet adj meg.");
                }
                std::string valueCode;
                if (!compileExpression(body.substr(equal + 1), valueCode, sourceLine)) {
                    return false;
                }
                indent(output, depth);
                std::string selectorCode;
                if (!compileExpression(selectors[0], selectorCode, sourceLine)) {
                    return false;
                }
                output << "eListSet(\"" << escapeCppString(name) << "\", "
                       << "eListIndexFromValue(" << selectorCode << "), "
                       << valueCode << ");\n";
                position++;
                return true;
            }
            if (result.error.hasError) return false;
        }


        if (startsWith(line,"gomb gmb(")) { if(!requireGomb(sourceLine))return false; std::string k;if(!compileKeyList(line,k,sourceLine))return false;indent(output,depth);output<<"eWatchKey("<<k<<");\n";position++;return true; }
        if (startsWith(line,"sslen gmb(")) { if(!requireGomb(sourceLine))return false; std::string k;if(!compileKeyList(line,k,sourceLine))return false;indent(output,depth);output<<"eWaitKeyOnce("<<k<<");\n";position++;return true; }
        if (startsWith(line,"nib gomb gmb(")) { if(!requireGomb(sourceLine))return false; std::string k;if(!compileKeyList(line,k,sourceLine))return false;indent(output,depth);output<<"eStopWatchKey("<<k<<");\n";position++;return true; }
        if (startsWith(line,"clr ")) {
            if(!requireIkonain(sourceLine))return false;
            std::string k;
            if (startsWith(line,"clr (")) { if(!compileColorList(line,k,sourceLine))return false; }
            else { std::string c=trim(removeEndDot(line).substr(4)); if(c.empty())return fail(sourceLine,"A clr utan szin kell."); k="std::vector<std::string>{\""+escapeCppString(c)+"\"}"; }
            indent(output,depth); output<<"eIkonSetBackground("<<k<<");\n"; position++; return true;
        }
        if (startsWith(line,"clrfont ")) {
            if(!requireIkonain(sourceLine))return false;
            std::string k;
            if (startsWith(line,"clrfont (")) { if(!compileColorList(line,k,sourceLine))return false; }
            else { std::string c=trim(removeEndDot(line).substr(8)); if(c.empty())return fail(sourceLine,"A clrfont utan szin kell."); k="std::vector<std::string>{\""+escapeCppString(c)+"\"}"; }
            indent(output,depth); output<<"eIkonSetFont("<<k<<");\n"; position++; return true;
        }
        if (startsWith(line,"clrplautz ")) {
            if(!requireIkonain(sourceLine))return false;
            std::string k;
            if (startsWith(line,"clrplautz (")) { if(!compileColorList(line,k,sourceLine))return false; }
            else { std::string c=trim(removeEndDot(line).substr(10)); if(c.empty())return fail(sourceLine,"A clrplautz utan szin kell."); k="std::vector<std::string>{\""+escapeCppString(c)+"\"}"; }
            indent(output,depth); output<<"eIkonSetBackground("<<k<<");\n"; position++; return true;
        }
        if (line=="mlsd" || line=="mlsd.") { if(!requireIkonain(sourceLine))return false;indent(output,depth);output<<"eIkonClearFrame();\n";position++;return true; }
        if (startsWith(line,"Blender_Objekt") || startsWith(line,"IKONAIN_Reag")) { if(!requireIkonain(sourceLine))return false; if(line.find('{')==std::string::npos)return fail(sourceLine,"Hianyzik a { jel.");indent(output,depth);output<<"{\n";position++;if(!parseBlock(output,depth+1,true))return false;indent(output,depth);output<<"}\n";return true; }

        // IKONAIN2 rajzolas: barmilyen objektumnev, valtozok es kifejezesek.
        // Pelda:
        // fej.x = ('fejX') - ('fejX' + 9); green
        // fej.y = ('fejY') - ('fejY' + 9); green
        {
            std::size_t dotX = line.find(".x =");
            if (dotX != std::string::npos && dotX > 0) {
                if (!requireIkonain(sourceLine)) return false;
                std::string objectName = trim(line.substr(0, dotX));
                std::string xBody = trim(line.substr(dotX + 4));
                std::size_t xSemi = xBody.rfind(';');
                if (xSemi == std::string::npos) return fail(sourceLine,"Az x koordinata utan kell ; es szin.");
                std::string xRange = trim(xBody.substr(0,xSemi));
                std::string color = trim(xBody.substr(xSemi+1));
                while(!color.empty() && (color.back()=='.'||color.back()==';')) color.pop_back();
                std::size_t xMinus = findTopLevelOperator(xRange,std::vector<std::string>{"-"});
                std::string xLeftText=xRange, xRightText=xRange;
                if(xMinus!=std::string::npos){xLeftText=trim(xRange.substr(0,xMinus));xRightText=trim(xRange.substr(xMinus+1));}

                std::size_t next=position+1;
                while(next<lines.size() && trim(lines[next]).empty()) next++;
                if(next>=lines.size()) return fail(sourceLine,"Hianyzik az objektum y koordinataja.");
                std::string yLine=removeEndDot(trim(lines[next]));
                std::string yPrefix=objectName+".y =";
                if(!startsWith(yLine,yPrefix)) return fail(static_cast<int>(next)+1,"Az x sor utan ugyanannak az objektumnak a y sora kell.");
                std::string yBody=trim(yLine.substr(yPrefix.size()));
                std::size_t ySemi=yBody.rfind(';');
                if(ySemi==std::string::npos) return fail(static_cast<int>(next)+1,"Az y koordinata utan kell ; es szin.");
                std::string yRange=trim(yBody.substr(0,ySemi));
                std::size_t yMinus=findTopLevelOperator(yRange,std::vector<std::string>{"-"});
                std::string yLeftText=yRange, yRightText=yRange;
                if(yMinus!=std::string::npos){yLeftText=trim(yRange.substr(0,yMinus));yRightText=trim(yRange.substr(yMinus+1));}

                std::string x1,x2,y1,y2;
                if(!compileExpression(xLeftText,x1,sourceLine) || !compileExpression(xRightText,x2,sourceLine) ||
                   !compileExpression(yLeftText,y1,static_cast<int>(next)+1) || !compileExpression(yRightText,y2,static_cast<int>(next)+1)) return false;
                indent(output,depth);
                output<<"eIkonDrawRect("<<x1<<","<<x2<<","<<y1<<","<<y2<<",\""<<escapeCppString(color)<<"\");\n";
                position=next+1;
                return true;
            }
        }
        if (startsWith(line, "ahf ")) {
            return compileIf(
                output,
                depth,
                line,
                sourceLine
            );
        }

        if (startsWith(line, "adcu ")) {
            return compileAdcu(
                output,
                depth,
                line,
                sourceLine
            );
        }

        if (startsWith(line, "rnd ")) {
            return compileRnd(
                output,
                depth,
                line,
                sourceLine
            );
        }

        if (startsWith(line, "tiw")) {
            return compileTiw(
                output,
                depth,
                line,
                sourceLine
            );
        }

        if (startsWith(line, "var??? ")) {
            std::string target = removeEndDot(trim(line.substr(7)));
            std::string generated;
            if (!compileExpression(target, generated, sourceLine)) return false;
            indent(output, depth);
            output << "printValueComment(" << sourceLine << ", " << generated << ");\n";
            position++;
            return true;
        }

        if (startsWith(line, "var? ")) {
            std::string target = removeEndDot(trim(line.substr(5)));
            std::string generated;
            if (!compileExpression(target, generated, sourceLine)) return false;
            indent(output, depth);
            output << "printTypeComment(" << sourceLine << ", " << generated << ");\n";
            position++;
            return true;
        }

        if (startsWith(line, "hajohos_")) {
            if (!requireHajoStudio(sourceLine)) return false;
            std::string target = removeEndDot(trim(line.substr(8)));
            if (!endsWith(target, "&")) target += "&";
            indent(output, depth);
            output << "printValueComment(" << sourceLine
                   << ", hajoLength(getInput(\""
                   << escapeCppString(target) << "\")));\n";
            position++;
            return true;
        }

        if (
            startsWith(line, "hajokopi(") ||
            startsWith(line, "hajomkopi(")
        ) {
            if (!requireHajoStudio(sourceLine)) return false;
            bool partial = startsWith(line, "hajomkopi(");
            std::size_t left=line.find('('), right=line.rfind(')');
            if(right==std::string::npos||right<=left) return fail(sourceLine,"Hibas hajo masolas.");
            std::string body=line.substr(left+1,right-left-1);
            std::vector<std::string> halves=splitHajoItems(body,';');
            std::vector<std::string> items=splitHajoItems(halves[0],'-');
            if(items.size()<2) return fail(sourceLine,"A hajo masolashoz forras es cel kell.");
            std::string sourceCode;
            if(!compileExpression(items[0],sourceCode,sourceLine)) return false;
            if(partial){
                if(halves.size()!=2) return fail(sourceLine,"A hajomkopi utan karakterszam kell.");
                std::string selector=trim(halves[1]); char mode='b';
                std::size_t at=selector.find('@');
                if(at!=std::string::npos){mode=selector[0];selector=selector.substr(at+1);}
                if(!isIntegerText(trim(selector))) return fail(sourceLine,"Hibas hajomkopi karakterszam.");
                sourceCode="hajoSlice("+sourceCode+", '"+std::string(1,mode)+"', "+trim(selector)+")";
            }
            std::size_t i;
            for(i=1;i<items.size();i++){
                std::string target=trim(items[i]);
                indent(output,depth);
                if (endsWith(target, "&")) {
                    output << "hajoCopyToInput(\"" << escapeCppString(target) << "\", " << sourceCode << ");\n";
                }
                else {
                    if(target.size()<2||target[0]!='\''||target[target.size()-1]!='\'') return fail(sourceLine,"A hajokopi celja 'valtozo' vagy skiyF valtozo& legyen.");
                    target=target.substr(1,target.size()-2);
                    output << "hajoCopyToVar(\"" << escapeCppString(target) << "\", " << sourceCode << ");\n";
                }
            }
            position++;
            return true;
        }

        if (
            startsWith(line, "kocsi ") ||
            startsWith(line, "hajo ") ||
            startsWith(line, "let ") ||
            startsWith(line, "felho ") ||
            startsWith(line, "hang ")
        ) {
            std::size_t space = line.find(' ');
            std::string type = line.substr(0, space);

            if (type == "hang") {
                return fail(
                    sourceLine,
                    "A hang nem tarol valtozoerteket."
                );
            }

            std::string body =
                removeEndDot(
                    trim(line.substr(space + 1))
                );

            std::string name;
            std::size_t after = 0;

            if (!parseQuotedName(
                body,
                0,
                name,
                after
            )) {
                return fail(
                    sourceLine,
                    "A valtozonevet ' jelek koze kell tenni."
                );
            }

            std::size_t equal =
                body.find('=', after);

            if (equal == std::string::npos) {
                return fail(
                    sourceLine,
                    "Hianyzik az = jel."
                );
            }

            std::string generated;

            if (!compileExpression(
                body.substr(equal + 1),
                generated,
                sourceLine
            )) {
                return false;
            }

            indent(output, depth);
            output
                << "declareVar(\""
                << escapeCppString(name)
                << "\", "
                << runtimeType(type)
                << ", "
                << generated
                << ");\n";

            knownTypes[name] = type;
            position++;
            return true;
        }

        if (startsWith(line, "listaT_hossz ")) {
            std::string expression = removeEndDot(line);
            std::string generated;
            if (!compileExpression(expression, generated, sourceLine)) return false;
            indent(output, depth);
            if (ikonainLoaded) {
                output << "eIkonPrint(toText(" << generated
                       << "), std::vector<std::string>{});\n";
            }
            else {
                output << "std::cout << toText(" << generated
                       << ") << std::endl;\n";
            }
            position++;
            return true;
        }

        if (startsWith(line, "tascuF")) {
            if (!studioLoaded) return fail(sourceLine,"A tascuF elott kell: #kontarb :studio.j:");
            std::size_t left=line.find('('), right=line.rfind(')');
            if(left==std::string::npos||right==std::string::npos||right<=left)return fail(sourceLine,"Hibas tascuF forma.");
            std::string expression=line.substr(left+1,right-left-1);
            std::vector<std::string> colors;
            if(ikonainLoaded) {
                std::size_t marker=expression.rfind("\"(");
                if(marker!=std::string::npos && expression[expression.size()-1]==')') {
                    std::string colorBody=expression.substr(marker+2,expression.size()-marker-3);
                    colors=splitHajoItems(colorBody,';'); expression=expression.substr(0,marker+1);
                }
            }
            std::string generated;
            if(!compileExpression(expression,generated,sourceLine))return false;
            indent(output,depth);
            if(ikonainLoaded) {
                output<<"eIkonPrint(toText("<<generated<<"), std::vector<std::string>{";
                for(std::size_t i=0;i<colors.size();++i){if(i)output<<",";output<<"\""<<escapeCppString(trim(colors[i]))<<"\"";}
                output<<"});\n";
            } else output<<"std::cout << toText("<<generated<<") << std::endl;\n";
            position++; return true;
        }

        if (startsWith(line, "skiyF")) {
            if (!studioLoaded) {
                return fail(
                    sourceLine,
                    "A skiyF elott kell: #kontarb :studio.j:"
                );
            }

            std::string body =
                removeEndDot(
                    trim(line.substr(5))
                );

            if (startsWith(body, "(rnd)")) {
                std::string inputName =
                    trim(body.substr(5));

                if (
                    inputName.empty() ||
                    !endsWith(inputName, "&")
                ) {
                    return fail(
                        sourceLine,
                        "A skiyF (rnd) utan & valtozo kell."
                    );
                }

                indent(output, depth);
                output
                    << "saveRandomInput(\""
                    << escapeCppString(inputName)
                    << "\");\n";

                position++;
                return true;
            }

            if (body.empty()) {
                indent(output, depth);
                output << "readAndDiscard();\n";
                position++;
                return true;
            }

            if (
                body.size() >= 2 &&
                body[0] == '"' &&
                body[body.size() - 1] == '"'
            ) {
                std::string question =
                    body.substr(
                        1,
                        body.size() - 2
                    );

                indent(output, depth);
                if (ikonainLoaded) {
                    output << "eIkonPrint(\""
                           << escapeCppString(question)
                           << "\", std::vector<std::string>());\n";
                } else {
                    output << "std::cout << \""
                           << escapeCppString(question)
                           << "\" << std::endl;\n";
                }

                indent(output, depth);
                output << "readAndDiscard();\n";

                position++;
                return true;
            }

            std::size_t semicolon =
                body.find(';');

            std::string inputName =
                trim(
                    semicolon == std::string::npos
                    ? body
                    : body.substr(0, semicolon)
                );

            if (!endsWith(inputName, "&")) {
                return fail(
                    sourceLine,
                    "A skiyF tarolo neve & jellel vegzodjon."
                );
            }

            if (semicolon != std::string::npos) {
                std::string question =
                    trim(body.substr(semicolon + 1));

                if (
                    question.size() < 2 ||
                    question[0] != '"' ||
                    question[question.size() - 1] != '"'
                ) {
                    return fail(
                        sourceLine,
                        "A skiyF kerdese idezojeles legyen."
                    );
                }

                question =
                    question.substr(
                        1,
                        question.size() - 2
                    );

                indent(output, depth);
                if (ikonainLoaded) {
                    output << "eIkonPrint(\""
                           << escapeCppString(question)
                           << "\", std::vector<std::string>());\n";
                } else {
                    output << "std::cout << \""
                           << escapeCppString(question)
                           << "\" << std::endl;\n";
                }
            }

            indent(output, depth);
            output
                << "readInput(\""
                << escapeCppString(inputName)
                << "\");\n";

            position++;
            return true;
        }

        if (startsWith(line, "nib ")) {
            std::string body =
                removeEndDot(
                    trim(line.substr(4))
                );

            std::string name;
            std::size_t after = 0;

            if (!parseQuotedName(
                body,
                0,
                name,
                after
            )) {
                return fail(
                    sourceLine,
                    "A nib utan 'valtozonev' kell."
                );
            }

            indent(output, depth);
            output
                << "deleteVar(\""
                << escapeCppString(name)
                << "\");\n";

            knownTypes.erase(name);
            position++;
            return true;
        }

        if (!line.empty() && line[0] == '\'') {
            std::string body =
                removeEndDot(line);

            std::string name;
            std::size_t after = 0;

            if (!parseQuotedName(
                body,
                0,
                name,
                after
            )) {
                return fail(
                    sourceLine,
                    "Hibas valtozonev."
                );
            }

            std::string rest =
                trim(body.substr(after));

            if (startsWith(rest, "//")) {
                rest = trim(rest.substr(2));

                std::size_t space =
                    rest.find(' ');

                std::size_t equal =
                    rest.find('=');

                if (
                    equal == std::string::npos
                ) {
                    return fail(
                        sourceLine,
                        "A tipusvaltasnal = kell."
                    );
                }

                std::string type =
                    trim(rest.substr(0, equal));

                if (
                    type != "kocsi" &&
                    type != "hajo" &&
                    type != "let" &&
                    type != "felho"
                ) {
                    return fail(
                        sourceLine,
                        "Ismeretlen uj valtozotipus."
                    );
                }

                std::string generated;

                if (!compileExpression(
                    rest.substr(equal + 1),
                    generated,
                    sourceLine
                )) {
                    return false;
                }

                indent(output, depth);
                output
                    << "changeType(\""
                    << escapeCppString(name)
                    << "\", "
                    << runtimeType(type)
                    << ", "
                    << generated
                    << ");\n";

                knownTypes[name] = type;
                position++;
                return true;
            }

            if (startsWith(rest, "++")) {
                std::string generated;

                if (!compileExpression(
                    rest.substr(2),
                    generated,
                    sourceLine
                )) {
                    return false;
                }

                indent(output, depth);
                output
                    << "incrementVar(\""
                    << escapeCppString(name)
                    << "\", "
                    << generated
                    << ");\n";

                position++;
                return true;
            }

            if (startsWith(rest, "--")) {
                std::string generated;

                if (!compileExpression(
                    rest.substr(2),
                    generated,
                    sourceLine
                )) {
                    return false;
                }

                indent(output, depth);
                output
                    << "decrementVar(\""
                    << escapeCppString(name)
                    << "\", "
                    << generated
                    << ");\n";

                position++;
                return true;
            }

            if (startsWith(rest, "=")) {
                std::string generated;

                if (!compileExpression(
                    rest.substr(1),
                    generated,
                    sourceLine
                )) {
                    return false;
                }

                indent(output, depth);
                output
                    << "assignVar(\""
                    << escapeCppString(name)
                    << "\", "
                    << generated
                    << ");\n";

                position++;
                return true;
            }

            return fail(
                sourceLine,
                "A valtozo utan =, ++, -- vagy // kell."
            );
        }

        return fail(
            sourceLine,
            "Ismeretlen E utasitas."
        );
    }

public:
    ESourceCompiler(
        const std::string& source,
        HydroCompileResult& compileResult
    )
        : lines(),
          position(0),
          result(compileResult),
          studioLoaded(false),
          hajoStudioLoaded(false),
          gombLoaded(false),
          ikonainLoaded(false),
          knownTypes(),
          knownLists() {
        std::stringstream stream(source);
        std::string line;

        bool inBlockComment = false;
        while (std::getline(stream, line)) {
            if (!line.empty() && line[line.size()-1] == '\r') {
                line.erase(line.size()-1);
            }

            std::string cleaned;
            std::size_t i = 0;
            while (i < line.size()) {
                if (inBlockComment) {
                    std::size_t end = line.find("*\\", i);
                    if (end == std::string::npos) {
                        i = line.size();
                    }
                    else {
                        inBlockComment = false;
                        i = end + 2;
                    }
                }
                else {
                    std::size_t start = line.find("/*", i);
                    if (start == std::string::npos) {
                        cleaned += line.substr(i);
                        break;
                    }
                    cleaned += line.substr(i, start-i);
                    inBlockComment = true;
                    i = start + 2;
                }
            }
            lines.push_back(cleaned);
        }
    }

    bool compile(std::string& generatedCpp) {
        std::stringstream output;
        output << runtimeHeader();

        if (!parseBlock(output, 2, false)) {
            return false;
        }

        output << runtimeFooter();
        generatedCpp = output.str();

        return true;
    }
};

HydroCompileResult hydroCompileSource(
    const std::string& source
) {
    HydroCompileResult result;

    result.logLines.push_back(
        "E v4.12 fordito: forraselemzes..."
    );

    ESourceCompiler compiler(source, result);

    if (!compiler.compile(result.generatedCpp)) {
        result.logLines.push_back(
            "E v4.12 fordito: hiba"
        );

        return result;
    }

    result.logLines.push_back(
        "E v4.12 fordito: sikeres"
    );

    result.logLines.push_back(
        "C++ koztes kod generalasa: sikeres"
    );

    result.success = true;
    return result;
}
