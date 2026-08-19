#include <napi.h>
#include "byv_native_profiler.hpp"

#include <cstdint>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

namespace byv
{

    /*
     * ============================================================
     * BYV CORE
     * Experimental generation
     *
     * Grammar:
     *
     * @BYV
     *
     * key -> value
     *
     * object ::
     *     key -> value
     *
     * array ::
     *     -"value"
     *     -"value"
     *
     * object array ::
     *     +::
     *         id -> 1
     *         name -> "Alice"
     *
     * ============================================================
     */

    static constexpr uint8_t VERSION = 7;
    static constexpr uint32_t MAX_COLLECTION_ITEMS = 1000000;
    static constexpr uint64_t MAX_DICTIONARY_ENTRIES = 1000000;
    static constexpr size_t MAX_DEPTH = 512;

    struct ArgumentError : std::runtime_error
    {
        explicit ArgumentError(const std::string &message)
            : std::runtime_error(message) {}
    };

    static inline void checkDepth(
        size_t depth,
        const char *errorMessage)
    {
        if (depth >= MAX_DEPTH)
            throw std::runtime_error(errorMessage);
    }

#ifdef BYV_ENABLE_PROFILE
    class ProfileGuard
    {
    public:
        explicit ProfileGuard(const char *title)
            : title_(title) {}

        void begin()
        {
            byv_profile::begin();
            active_ = true;
        }

        void finish()
        {
            if (!active_)
                return;

            byv_profile::end();
            byv_profile::report(title_);
            active_ = false;
        }

        ~ProfileGuard()
        {
            if (active_)
                byv_profile::end();
        }

    private:
        const char *title_;
        bool active_ = false;
    };
#endif

    enum class Type : uint8_t
    {
        Null = 0,
        Bool = 1,
        Int = 2,
        Float = 3,
        String = 4,
        Object = 5,
        Array = 6
    };

    struct Value
    {
        Type type = Type::Null;

        bool boolean = false;
        int64_t integer = 0;
        double floating = 0.0;

        std::string string;

        std::vector<std::pair<std::string, Value>> object;
        std::vector<Value> array;
    };

    struct Token
    {
        enum class Kind
        {
            Identifier,
            String,
            Integer,
            Float,
            True,
            False,
            Null,

            Arrow,
            Scope,
            ArrayItem,
            ObjectItem,

            Header,
            End
        };

        Kind kind;
        std::string text;
        int line = 1;

        // Posisi horizontal token pada source.
        // BYV menggunakan indentation sebagai bagian dari grammar.
        int column = 1;
    };

    class Lexer
    {
    public:
        explicit Lexer(std::string_view source)
            : source_(source) {}

        std::vector<Token> tokenize()
        {
            std::vector<Token> tokens;

            while (!eof())
            {
                skipWhitespace();

                if (eof())
                    break;

                int tokenColumn = column_;

                if (eof())
                    break;

                if (peek() == '#')
                {
                    skipComment();
                    continue;
                }

                if (match("@BYV"))
                {
                    tokens.push_back({Token::Kind::Header,
                                      "@BYV",
                                      line_,
                                      tokenColumn});
                    continue;
                }

                // +::
                if (match("+::"))
                {
                    tokens.push_back({Token::Kind::ObjectItem,
                                      "+::",
                                      line_,
                                      tokenColumn});
                    continue;
                }

                // ->
                if (match("->"))
                {
                    tokens.push_back({Token::Kind::Arrow,
                                      "->",
                                      line_,
                                      tokenColumn});
                    continue;
                }

                // ::
                if (match("::"))
                {
                    tokens.push_back({Token::Kind::Scope,
                                      "::",
                                      line_,
                                      tokenColumn});
                    continue;
                }

                if (peek() == '-')
                {
                    /*
                     * '-' is array append.
                     * Negative numbers are handled separately.
                     */
                    if (pos_ + 1 < source_.size() &&
                        (source_[pos_ + 1] == '"' ||
                         source_[pos_ + 1] == '\''))
                    {

                        advance();

                        tokens.push_back({Token::Kind::ArrayItem,
                                          "-",
                                          line_,
                                          tokenColumn});

                        continue;
                    }
                }

                if (peek() == '"' || peek() == '\'')
                {
                    tokens.push_back(readString());
                    continue;
                }

                if (isNumberStart())
                {
                    tokens.push_back(readNumber());
                    continue;
                }

                if (isIdentifierStart(peek()))
                {
                    tokens.push_back(readIdentifier());
                    continue;
                }

                throw error("Unexpected character");
            }

            tokens.push_back({Token::Kind::End,
                              "",
                              line_});

            return tokens;
        }

    private:
        std::string_view source_;

        size_t pos_ = 0;

        int line_ = 1;

        // Kolom 1-based.
        int column_ = 1;

        bool eof() const
        {
            return pos_ >= source_.size();
        }

        char peek() const
        {
            if (eof())
                return '\0';

            return source_[pos_];
        }

        char peekNext() const
        {
            if (pos_ + 1 >= source_.size())
                return '\0';

            return source_[pos_ + 1];
        }

        void advance()
        {
            if (eof())
                return;

            char c = source_[pos_];

            pos_++;

            if (c == '\n')
            {
                line_++;
                column_ = 1;
            }
            else if (c == '\t')
            {
                // BYV: tab dianggap 4 spasi.
                column_ += 4;
            }
            else
            {
                column_++;
            }
        }

        bool match(std::string_view value)
        {
            if (source_.substr(pos_, value.size()) == value)
            {
                for (char c : value)
                    advance();
                return true;
            }

            return false;
        }

        void skipWhitespace()
        {
            while (!eof())
            {
                char c = peek();

                if (c == ' ' || c == '\t' || c == '\r')
                {
                    advance();
                    continue;
                }

                if (c == '\n')
                {
                    advance();
                    continue;
                }

                break;
            }
        }

        void skipComment()
        {
            while (!eof() && peek() != '\n')
                advance();
        }

        bool isIdentifierStart(char c) const
        {
            return (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   c == '_' ||
                   c == '$';
        }

        bool isIdentifierChar(char c) const
        {
            return isIdentifierStart(c) ||
                   (c >= '0' && c <= '9') ||
                   c == '-' ||
                   c == '.';
        }

        bool isNumberStart() const
        {
            char c = peek();

            if ((c >= '0' && c <= '9'))
                return true;

            if (c == '-' &&
                peekNext() >= '0' &&
                peekNext() <= '9')
                return true;

            return false;
        }

        Token readIdentifier()
        {
            int startColumn = column_;
            size_t start = pos_;

            while (!eof() && isIdentifierChar(peek()))
                advance();

            std::string value(source_.substr(start, pos_ - start));

            if (value == "true")
            {
                return {
                    Token::Kind::True,
                    value,
                    line_,
                    startColumn};
            }

            if (value == "false")
                return {Token::Kind::False, value, line_};

            if (value == "null")
                return {Token::Kind::Null, value, line_};

            return {
                Token::Kind::Identifier,
                value,
                line_,
                startColumn};
        }

        Token readNumber()
        {
            int startColumn = column_;
            size_t start = pos_;
            bool floating = false;

            if (peek() == '-')
                advance();

            while (!eof() &&
                   peek() >= '0' &&
                   peek() <= '9')
            {
                advance();
            }

            if (peek() == '.')
            {
                floating = true;
                advance();

                while (!eof() &&
                       peek() >= '0' &&
                       peek() <= '9')
                {
                    advance();
                }
            }

            if (peek() == 'e' || peek() == 'E')
            {
                floating = true;
                advance();

                if (peek() == '+' || peek() == '-')
                    advance();

                while (!eof() &&
                       peek() >= '0' &&
                       peek() <= '9')
                {
                    advance();
                }
            }

            std::string value(source_.substr(start, pos_ - start));

            return {
                floating ? Token::Kind::Float : Token::Kind::Integer,
                value,
                line_,
                startColumn};
        }

        Token readString()
        {
            int startColumn = column_;
            char quote = peek();
            advance();

            std::string result;

            while (!eof())
            {
                char c = peek();

                if (c == quote)
                {
                    advance();

                    return {
                        Token::Kind::String,
                        result,
                        line_,
                        startColumn};
                }

                if (c == '\\')
                {
                    advance();

                    if (eof())
                        break;

                    char escaped = peek();
                    advance();

                    switch (escaped)
                    {
                    case 'n':
                        result.push_back('\n');
                        break;

                    case 'r':
                        result.push_back('\r');
                        break;

                    case 't':
                        result.push_back('\t');
                        break;

                    case '\\':
                        result.push_back('\\');
                        break;

                    case '"':
                        result.push_back('"');
                        break;

                    case '\'':
                        result.push_back('\'');
                        break;

                    default:
                        result.push_back(escaped);
                        break;
                    }

                    continue;
                }

                result.push_back(c);
                advance();
            }

            throw error("Unterminated string");
        }

        std::runtime_error error(const std::string &message) const
        {
            return std::runtime_error(
                "BYV lexer error at line " +
                std::to_string(line_) +
                ": " +
                message);
        }
    };

    /*
     * ============================================================
     * PARSER
     * ============================================================
     */

    class Parser
    {
    public:
        explicit Parser(std::vector<Token> tokens)
            : tokens_(std::move(tokens)) {}

        Value parse()
        {
            Value root;

            root.type = Type::Object;

            if (current().kind == Token::Kind::Header)
                advance();

            while (current().kind != Token::Kind::End)
            {
                parseObjectEntry(root, 0);
            }

            return root;
        }

    private:
        std::vector<Token> tokens_;
        size_t pos_ = 0;

        const Token &current() const
        {
            return tokens_[pos_];
        }

        const Token &next() const
        {
            if (pos_ + 1 >= tokens_.size())
                return tokens_.back();

            return tokens_[pos_ + 1];
        }

        void advance()
        {
            if (pos_ + 1 < tokens_.size())
                pos_++;
        }

        void expect(Token::Kind kind)
        {
            if (current().kind != kind)
            {
                throw std::runtime_error(
                    "BYV parser error at line " +
                    std::to_string(current().line));
            }

            advance();
        }

        void parseObjectEntry(Value &object, size_t depth)
        {
            checkDepth(
                depth,
                "BYV nesting too deep");

            if (current().kind != Token::Kind::Identifier)
            {
                throw std::runtime_error(
                    "BYV parser: expected identifier at line " +
                    std::to_string(current().line));
            }

            std::string key = current().text;
            int currentKeyColumn = current().column;
            advance();

            if (current().kind == Token::Kind::Arrow)
            {
                advance();

                Value value = parseScalar();

                object.object.emplace_back(
                    std::move(key),
                    std::move(value));

                return;
            }

            if (current().kind == Token::Kind::Scope)
            {
                advance();

                Value value = parseScope(
                    currentKeyColumn,
                    depth + 1);

                object.object.emplace_back(
                    std::move(key),
                    std::move(value));

                return;
            }

            throw std::runtime_error(
                "BYV parser: expected -> or :: after key");
        }

        Value parseScalar()
        {
            Value value;

            switch (current().kind)
            {

            case Token::Kind::String:
                value.type = Type::String;
                value.string = current().text;
                advance();
                return value;

            case Token::Kind::Integer:
                value.type = Type::Int;

                {
                    const std::string &literal =
                        current().text;

                    const auto parsed =
                        std::from_chars(
                            literal.data(),
                            literal.data() + literal.size(),
                            value.integer);

                    if (parsed.ec == std::errc::result_out_of_range)
                    {
                        throw std::runtime_error(
                            "BYV parser error at line " +
                            std::to_string(current().line) +
                            ": integer literal out of range: " +
                            literal);
                    }

                    if (parsed.ec != std::errc() ||
                        parsed.ptr !=
                            literal.data() + literal.size())
                    {
                        throw std::runtime_error(
                            "BYV parser error at line " +
                            std::to_string(current().line) +
                            ": malformed integer literal: " +
                            literal);
                    }
                }
                advance();
                return value;

            case Token::Kind::Float:
                value.type = Type::Float;
                try
                {
                    value.floating = std::stod(current().text);
                }
                catch (const std::out_of_range &)
                {
                    throw std::runtime_error(
                        "BYV parser error at line " +
                        std::to_string(current().line) +
                        ": float literal out of range: " +
                        current().text);
                }
                catch (const std::invalid_argument &)
                {
                    throw std::runtime_error(
                        "BYV parser error at line " +
                        std::to_string(current().line) +
                        ": malformed float literal: " +
                        current().text);
                }
                advance();
                return value;

            case Token::Kind::True:
                value.type = Type::Bool;
                value.boolean = true;
                advance();
                return value;

            case Token::Kind::False:
                value.type = Type::Bool;
                value.boolean = false;
                advance();
                return value;

            case Token::Kind::Null:
                value.type = Type::Null;
                advance();
                return value;

            default:
                throw std::runtime_error(
                    "BYV parser: expected value at line " +
                    std::to_string(current().line));
            }
        }

        Value parseScope(int parentColumn, size_t depth)
        {
            checkDepth(
                depth,
                "BYV nesting too deep");

            /*
             * A scope is indentation-sensitive.  The first token of a
             * child must be indented farther than its parent.  This keeps
             * sibling objects at the correct level instead of accidentally
             * nesting them inside the previous object.
             */
            if (current().kind == Token::Kind::ArrayItem)
                return parseScalarArray(depth);

            if (current().kind == Token::Kind::ObjectItem)
                return parseObjectArray(depth);

            Value object;
            object.type = Type::Object;

            while (current().kind == Token::Kind::Identifier &&
                   current().column > parentColumn)
            {
                parseObjectEntry(object, depth);
            }

            return object;
        }

        Value parseScalarArray(size_t depth)
        {
            checkDepth(
                depth,
                "BYV nesting too deep");

            Value array;

            array.type = Type::Array;

            while (current().kind == Token::Kind::ArrayItem)
            {
                advance();

                array.array.push_back(parseScalar());
            }

            return array;
        }

        Value parseObjectArray(size_t depth)
        {
            checkDepth(
                depth,
                "BYV nesting too deep");

            Value array;
            array.type = Type::Array;

            while (current().kind == Token::Kind::ObjectItem)
            {
                int markerColumn = current().column;
                advance();

                Value object;
                object.type = Type::Object;

                while (current().kind == Token::Kind::Identifier &&
                       current().column > markerColumn)
                {
                    parseObjectEntry(object, depth + 1);
                }

                array.array.push_back(std::move(object));
            }

            return array;
        }
    };

    /*
     * ============================================================
     * BINARY FORMAT
     * ============================================================
     *
     * Header:
     *
     * 4 bytes  magic = BYV7
     * 1 byte   version
     * 1 byte   flags
     * 4 bytes  payload size
     *
     * payload:
     *
     * recursive values
     *
     * ============================================================
     */

    class BinaryWriter
    {
    public:
        std::vector<uint8_t> data;

        void byte(uint8_t value)
        {
            data.push_back(value);
        }

        void u32(uint32_t value)
        {
            data.push_back(static_cast<uint8_t>(value));
            data.push_back(static_cast<uint8_t>(value >> 8));
            data.push_back(static_cast<uint8_t>(value >> 16));
            data.push_back(static_cast<uint8_t>(value >> 24));
        }

        void u64(uint64_t value)
        {
            for (int i = 0; i < 8; i++)
            {
                data.push_back(
                    static_cast<uint8_t>(
                        value >> (i * 8)));
            }
        }

        void bytes(const void *ptr, size_t size)
        {
            const auto *p =
                static_cast<const uint8_t *>(ptr);

            data.insert(
                data.end(),
                p,
                p + size);
        }

        void string(const std::string &value)
        {
            u32(static_cast<uint32_t>(value.size()));

            if (!value.empty())
                bytes(value.data(), value.size());
        }
    };

    static constexpr size_t HEADER_SIZE = 10;
    static constexpr size_t HEADER_PAYLOAD_OFFSET = 6;

    static size_t writeHeader(
        BinaryWriter &writer,
        uint8_t flags)
    {
        writer.byte('B');
        writer.byte('Y');
        writer.byte('V');
        writer.byte('7');
        writer.byte(VERSION);
        writer.byte(flags);
        writer.u32(0);

        return writer.data.size();
    }

    static void patchPayloadSize(
        BinaryWriter &writer,
        size_t payloadStart)
    {
        const size_t payloadSize =
            writer.data.size() - payloadStart;

        if (payloadSize > UINT32_MAX)
            throw std::runtime_error(
                "BYV payload too large");

        const uint32_t checkedSize =
            static_cast<uint32_t>(payloadSize);

        for (int i = 0; i < 4; ++i)
        {
            writer.data[HEADER_PAYLOAD_OFFSET + i] =
                static_cast<uint8_t>(
                    checkedSize >> (i * 8));
        }
    }

    class BinaryReader
    {
    public:
        BinaryReader(
            const uint8_t *data,
            size_t size)
            : data_(data),
              size_(size) {}

        uint8_t byte()
        {
            ensure(1);

            return data_[pos_++];
        }

        uint32_t u32()
        {
            ensure(4);

            uint32_t value =
                static_cast<uint32_t>(data_[pos_]) |
                (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                (static_cast<uint32_t>(data_[pos_ + 3]) << 24);

            pos_ += 4;

            return value;
        }

        uint64_t u64()
        {
            ensure(8);

            uint64_t value = 0;

            for (int i = 0; i < 8; i++)
            {
                value |=
                    static_cast<uint64_t>(
                        data_[pos_ + i])
                    << (i * 8);
            }

            pos_ += 8;

            return value;
        }

        std::string string()
        {
            uint32_t length = u32();

            ensure(length);

            std::string value(
                reinterpret_cast<const char *>(
                    data_ + pos_),
                length);

            if (!isValidUTF8(value))
                throw std::runtime_error(
                    "BYV invalid UTF-8 string");

            pos_ += length;

            return value;
        }

        size_t position() const
        {
            return pos_;
        }

        size_t size() const
        {
            return size_;
        }

        size_t remaining() const
        {
            return size_ - pos_;
        }

    private:
        const uint8_t *data_;
        size_t size_;
        size_t pos_ = 0;

        static bool isValidUTF8(const std::string &value)
        {
            size_t i = 0;

            while (i < value.size())
            {
                uint8_t first =
                    static_cast<uint8_t>(value[i]);

                if (first <= 0x7F)
                {
                    i++;
                    continue;
                }

                size_t length;
                uint32_t codePoint;

                if (first >= 0xC2 && first <= 0xDF)
                {
                    length = 2;
                    codePoint = first & 0x1F;
                }
                else if (first >= 0xE0 && first <= 0xEF)
                {
                    length = 3;
                    codePoint = first & 0x0F;
                }
                else if (first >= 0xF0 && first <= 0xF4)
                {
                    length = 4;
                    codePoint = first & 0x07;
                }
                else
                {
                    return false;
                }

                if (i + length > value.size())
                    return false;

                for (size_t j = 1; j < length; j++)
                {
                    uint8_t continuation =
                        static_cast<uint8_t>(value[i + j]);

                    if ((continuation & 0xC0) != 0x80)
                        return false;

                    codePoint =
                        (codePoint << 6) |
                        (continuation & 0x3F);
                }

                if ((length == 3 && codePoint < 0x800) ||
                    (length == 4 && codePoint < 0x10000) ||
                    codePoint > 0x10FFFF ||
                    (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                {
                    return false;
                }

                i += length;
            }

            return true;
        }

        void ensure(size_t amount)
        {
            if (amount > size_ - pos_)
            {
                throw std::runtime_error(
                    "BYV binary buffer truncated");
            }
        }
    };

    static uint32_t readLittleEndianU32(
        const uint8_t *data)
    {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }

    struct Header
    {
        uint8_t flags;
        uint32_t payloadSize;
    };

    static Header validateHeader(
        const uint8_t *data,
        size_t size)
    {
        if (size < HEADER_SIZE)
            throw std::runtime_error(
                "Invalid BYV buffer");

        if (
            data[0] != 'B' ||
            data[1] != 'Y' ||
            data[2] != 'V' ||
            data[3] != '7')
        {
            throw std::runtime_error(
                "Invalid BYV magic");
        }

        if (data[4] != VERSION)
        {
            throw std::runtime_error(
                "Unsupported BYV version");
        }

        const uint32_t payloadSize =
            readLittleEndianU32(
                data + HEADER_PAYLOAD_OFFSET);

        if (payloadSize != size - HEADER_SIZE)
        {
            throw std::runtime_error(
                "BYV payload size mismatch");
        }

        return {
            data[5],
            payloadSize};
    }

    static bool flagsValid(uint8_t flags)
    {
        return (flags & ~0x03) == 0 &&
               ((flags & 0x02) == 0 ||
                (flags & 0x01) != 0);
    }

    static void validateFlags(uint8_t flags)
    {
        if ((flags & ~0x03) != 0)
            throw std::runtime_error(
                "BYV unknown flags");

        if ((flags & 0x02) != 0 &&
            (flags & 0x01) == 0)
        {
            throw std::runtime_error(
                "BYV string dictionary flag requires packed keys");
        }
    }

    static size_t clampedReserve(
        size_t count,
        const BinaryReader &reader)
    {
        return std::min<size_t>(
            count,
            reader.remaining());
    }

    static void ensureNoTrailingData(
        const BinaryReader &reader)
    {
        if (reader.position() != reader.size())
            throw std::runtime_error(
                "BYV trailing data after root value");
    }

    static uint32_t readCollectionCount(
        BinaryReader &reader,
        const char *errorMessage)
    {
        const uint32_t count =
            reader.u32();

        if (count > MAX_COLLECTION_ITEMS)
            throw std::runtime_error(errorMessage);

        return count;
    }

    static bool readBool(BinaryReader &reader)
    {
        const uint8_t rawBool =
            reader.byte();

        if (rawBool > 1)
            throw std::runtime_error(
                "BYV invalid boolean value");

        return rawBool != 0;
    }

    static uint64_t doubleToBits(double value)
    {
        uint64_t raw;
        std::memcpy(
            &raw,
            &value,
            sizeof(double));
        return raw;
    }

    static double bitsToDouble(uint64_t raw)
    {
        double value;
        std::memcpy(
            &value,
            &raw,
            sizeof(double));
        return value;
    }

    static bool tryGetInt64(
        double number,
        int64_t &result)
    {
        if (!std::isfinite(number) ||
            std::floor(number) != number ||
            number < static_cast<double>(INT64_MIN) ||
            number > static_cast<double>(INT64_MAX))
        {
            return false;
        }

        result = static_cast<int64_t>(number);
        return true;
    }

    static void requireArg(
        const Napi::CallbackInfo &info,
        const char *message)
    {
        if (info.Length() < 1)
            throw ArgumentError(message);
    }

    static Napi::Value requireStringArg(
        const Napi::CallbackInfo &info,
        const char *message)
    {
        if (
            info.Length() < 1 ||
            !info[0].IsString())
        {
            throw ArgumentError(message);
        }

        return info[0];
    }

    static Napi::Buffer<uint8_t> requireBufferArg(
        const Napi::CallbackInfo &info,
        const char *message)
    {
        if (
            info.Length() < 1 ||
            !info[0].IsBuffer())
        {
            throw ArgumentError(message);
        }

        return info[0].As<Napi::Buffer<uint8_t>>();
    }

    static Napi::Buffer<uint8_t> copyBuffer(
        Napi::Env env,
        const std::vector<uint8_t> &data)
    {
        return Napi::Buffer<uint8_t>::Copy(
            env,
            data.data(),
            data.size());
    }

    template <typename Fn>
    static Napi::Value guarded(
        Napi::Env env,
        Fn body)
    {
        try
        {
            return body();
        }
        catch (const Napi::Error &e)
        {
            e.ThrowAsJavaScriptException();
            return env.Null();
        }
        catch (const ArgumentError &e)
        {
            Napi::TypeError::New(
                env,
                e.what())
                .ThrowAsJavaScriptException();

            return env.Null();
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(
                env,
                e.what())
                .ThrowAsJavaScriptException();

            return env.Null();
        }
        catch (...)
        {
            Napi::Error::New(
                env,
                "BYV internal error")
                .ThrowAsJavaScriptException();

            return env.Null();
        }
    }

    static void writeValue(
        BinaryWriter &writer,
        const Value &value,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep");

        if (value.type == Type::Object &&
            value.object.size() > MAX_COLLECTION_ITEMS)
        {
            throw std::runtime_error(
                "BYV object too large");
        }

        if (value.type == Type::Array &&
            value.array.size() > MAX_COLLECTION_ITEMS)
        {
            throw std::runtime_error(
                "BYV array too large");
        }

        writer.byte(
            static_cast<uint8_t>(value.type));

        switch (value.type)
        {

        case Type::Null:
            break;

        case Type::Bool:
            writer.byte(
                value.boolean ? 1 : 0);
            break;

        case Type::Int:
            writer.u64(
                static_cast<uint64_t>(
                    value.integer));
            break;

        case Type::Float:
            writer.u64(
                doubleToBits(value.floating));
            break;

        case Type::String:
            writer.string(value.string);
            break;

        case Type::Object:
            writer.u32(
                static_cast<uint32_t>(
                    value.object.size()));

            for (const auto &entry : value.object)
            {
                writer.string(entry.first);
                writeValue(
                    writer,
                    entry.second,
                    depth + 1);
            }

            break;

        case Type::Array:
            writer.u32(
                static_cast<uint32_t>(
                    value.array.size()));

            for (const auto &item : value.array)
                writeValue(
                    writer,
                    item,
                    depth + 1);

            break;
        }
    }

    static Value readValue(
        BinaryReader &reader,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep");

        Value value;

        value.type =
            static_cast<Type>(
                reader.byte());

        switch (value.type)
        {

        case Type::Null:
            break;

        case Type::Bool:
            value.boolean = readBool(reader);
            break;

        case Type::Int:
            value.integer =
                static_cast<int64_t>(
                    reader.u64());
            break;

        case Type::Float:
            value.floating =
                bitsToDouble(reader.u64());
            break;

        case Type::String:
            value.string =
                reader.string();
            break;

        case Type::Object:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV object too large");

            value.object.reserve(
                clampedReserve(
                    count,
                    reader));

            for (uint32_t i = 0; i < count; i++)
            {
                std::string key =
                    reader.string();

                Value child =
                    readValue(
                        reader,
                        depth + 1);

                value.object.emplace_back(
                    std::move(key),
                    std::move(child));
            }

            break;
        }

        case Type::Array:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV array too large");

            value.array.reserve(
                clampedReserve(
                    count,
                    reader));

            for (uint32_t i = 0; i < count; i++)
                value.array.push_back(
                    readValue(
                        reader,
                        depth + 1));

            break;
        }

        default:
            throw std::runtime_error(
                "Unknown BYV type");
        }

        return value;
    }

    /*
     * ============================================================
     * BYV PACKED KEY DICTIONARY FORMAT
     * ============================================================
     *
     * flags bit 0 = 1 => packed key dictionary payload
     *
     * payload:
     *   varuint dictionaryCount
     *   dictionaryCount x string
     *   packed root value
     *
     * Packed object:
     *   type
     *   u32 count
     *   repeated: varuint keyId + packed value
     *
     * Other values keep the existing v7 representation.
     * This keeps the format self-describing while removing repeated
     * object key bytes and their 4-byte length fields.
     * ============================================================
     */

    static void writeVarUInt(BinaryWriter &writer, uint64_t value)
    {
        while (value >= 0x80)
        {
            writer.byte(static_cast<uint8_t>(value) | 0x80);
            value >>= 7;
        }

        writer.byte(static_cast<uint8_t>(value));
    }

    static uint64_t readVarUInt(BinaryReader &reader)
    {
        uint64_t result = 0;

        for (int i = 0; i < 10; i++)
        {
            uint8_t byte = reader.byte();

            if (i == 9 && ((byte & 0x80) != 0 ||
                           (byte & 0x7E) != 0))
            {
                throw std::runtime_error(
                    "BYV varuint overflow");
            }

            result |=
                static_cast<uint64_t>(byte & 0x7F) <<
                (i * 7);

            if ((byte & 0x80) == 0)
                return result;
        }

        throw std::runtime_error("BYV varuint overflow");
    }

    template <typename Container, typename MakeEntry>
    static void readDictionary(
        BinaryReader &reader,
        Container &dictionary,
        const char *errorMessage,
        MakeEntry makeEntry)
    {
        const uint64_t count =
            readVarUInt(reader);

        if (count > MAX_DICTIONARY_ENTRIES)
            throw std::runtime_error(errorMessage);

        dictionary.reserve(
            clampedReserve(
                static_cast<size_t>(count),
                reader));

        for (uint64_t i = 0; i < count; ++i)
            dictionary.push_back(
                makeEntry(reader));
    }

    static void collectKeys(
        const Value &value,
        std::unordered_map<std::string, uint32_t> &ids,
        std::vector<std::string> &dictionary,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep; a cyclic structure may be the cause");

        if (value.type == Type::Object)
        {
            for (const auto &entry : value.object)
            {
                if (ids.find(entry.first) == ids.end())
                {
                    uint32_t id = static_cast<uint32_t>(dictionary.size());
                    ids.emplace(entry.first, id);
                    dictionary.push_back(entry.first);
                }

                collectKeys(
                    entry.second,
                    ids,
                    dictionary,
                    depth + 1);
            }
        }
        else if (value.type == Type::Array)
        {
            for (const auto &item : value.array)
                collectKeys(
                    item,
                    ids,
                    dictionary,
                    depth + 1);
        }
    }

    static void collectStrings(
        const Value &value,
        std::unordered_map<std::string, uint64_t> &counts,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep; a cyclic structure may be the cause");

        if (value.type == Type::String)
        {
            counts[value.string]++;
            return;
        }

        if (value.type == Type::Object)
        {
            for (const auto &entry : value.object)
                collectStrings(
                    entry.second,
                    counts,
                    depth + 1);
        }
        else if (value.type == Type::Array)
        {
            for (const auto &item : value.array)
                collectStrings(
                    item,
                    counts,
                    depth + 1);
        }
    }

    static void buildStringDictionary(
        const std::unordered_map<std::string, uint64_t> &counts,
        std::unordered_map<std::string, uint32_t> &ids,
        std::vector<std::string> &dictionary)
    {
        for (const auto &entry : counts)
        {
            const std::string &value = entry.first;
            const uint64_t count = entry.second;

            // String encoding with dictionary reference costs:
            //   1 marker byte + varuint(id) per occurrence
            // Dictionary entry costs:
            //   4-byte length + raw bytes
            // Only intern when it actually saves bytes.
            if (count < 2)
                continue;

            const size_t rawCost = 4 + value.size();
            const size_t refCost = 1 + 1; // marker + 1-byte id (initial dictionary is small)
            const size_t dictCost = 4 + value.size();

            if (count * rawCost > dictCost + count * refCost)
            {
                uint32_t id = static_cast<uint32_t>(dictionary.size());
                ids.emplace(value, id);
                dictionary.push_back(value);
            }
        }
    }

    static void writePackedValue(
        BinaryWriter &writer,
        const Value &value,
        const std::unordered_map<std::string, uint32_t> &ids,
        const std::unordered_map<std::string, uint32_t> &stringIds,
        bool stringDictionaryEnabled,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep; a cyclic structure may be the cause");

        if (value.type == Type::Object &&
            value.object.size() > MAX_COLLECTION_ITEMS)
        {
            throw std::runtime_error(
                "BYV object too large");
        }

        if (value.type == Type::Array &&
            value.array.size() > MAX_COLLECTION_ITEMS)
        {
            throw std::runtime_error(
                "BYV array too large");
        }

        writer.byte(static_cast<uint8_t>(value.type));

        switch (value.type)
        {
        case Type::Null:
            break;

        case Type::Bool:
            writer.byte(value.boolean ? 1 : 0);
            break;

        case Type::Int:
            writer.u64(static_cast<uint64_t>(value.integer));
            break;

        case Type::Float:
            writer.u64(doubleToBits(value.floating));
            break;

        case Type::String:
        {
            if (stringDictionaryEnabled)
            {
                auto it = stringIds.find(value.string);
                if (it != stringIds.end())
                {
                    writer.byte(1); // dictionary reference
                    writeVarUInt(writer, it->second);
                    break;
                }

                writer.byte(0); // raw string
            }

            writer.string(value.string);
            break;
        }

        case Type::Object:
            writer.u32(static_cast<uint32_t>(value.object.size()));

            for (const auto &entry : value.object)
            {
                auto it = ids.find(entry.first);
                if (it == ids.end())
                    throw std::runtime_error("BYV packed key missing from dictionary");

                writeVarUInt(writer, it->second);
                writePackedValue(
                    writer,
                    entry.second,
                    ids,
                    stringIds,
                    stringDictionaryEnabled,
                    depth + 1);
            }
            break;

        case Type::Array:
            writer.u32(static_cast<uint32_t>(value.array.size()));

            for (const auto &item : value.array)
                writePackedValue(
                    writer,
                    item,
                    ids,
                    stringIds,
                    stringDictionaryEnabled,
                    depth + 1);
            break;
        }
    }

    static Value readPackedValue(
        BinaryReader &reader,
        const std::vector<std::string> &dictionary,
        const std::vector<std::string> &stringDictionary,
        bool stringDictionaryEnabled,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep");

        Value value;
        value.type = static_cast<Type>(reader.byte());

        switch (value.type)
        {
        case Type::Null:
            break;

        case Type::Bool:
            value.boolean = readBool(reader);
            break;

        case Type::Int:
            value.integer = static_cast<int64_t>(reader.u64());
            break;

        case Type::Float:
            value.floating = bitsToDouble(reader.u64());
            break;

        case Type::String:
        {
            if (stringDictionaryEnabled)
            {
                uint8_t marker = reader.byte();

                if (marker == 1)
                {
                    uint64_t rawId = readVarUInt(reader);
                    if (rawId >= stringDictionary.size())
                        throw std::runtime_error("BYV packed string id out of range");

                    value.string = stringDictionary[static_cast<size_t>(rawId)];
                    break;
                }

                if (marker != 0)
                    throw std::runtime_error("BYV invalid string marker");
            }

            value.string = reader.string();
            break;
        }

        case Type::Object:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV object too large");

            value.object.reserve(
                clampedReserve(
                    count,
                    reader));

            for (uint32_t i = 0; i < count; i++)
            {
                uint64_t rawId = readVarUInt(reader);
                if (rawId >= dictionary.size())
                    throw std::runtime_error("BYV packed key id out of range");

                Value child = readPackedValue(
                    reader,
                    dictionary,
                    stringDictionary,
                    stringDictionaryEnabled,
                    depth + 1);

                value.object.emplace_back(
                    dictionary[static_cast<size_t>(rawId)],
                    std::move(child));
            }
            break;
        }

        case Type::Array:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV array too large");

            value.array.reserve(
                clampedReserve(
                    count,
                    reader));

            for (uint32_t i = 0; i < count; i++)
                value.array.push_back(
                    readPackedValue(
                        reader,
                        dictionary,
                        stringDictionary,
                        stringDictionaryEnabled,
                        depth + 1));
            break;
        }

        default:
            throw std::runtime_error("Unknown BYV packed type");
        }

        return value;
    }

    static void writePackedPayload(
        BinaryWriter &writer,
        const Value &root,
        bool enableStringDictionary)
    {
        std::unordered_map<std::string, uint32_t> ids;
        std::vector<std::string> dictionary;
        collectKeys(root, ids, dictionary);

        writeVarUInt(writer, dictionary.size());
        for (const auto &key : dictionary)
            writer.string(key);

        std::unordered_map<std::string, uint32_t> stringIds;
        std::vector<std::string> stringDictionary;

        if (enableStringDictionary)
        {
            std::unordered_map<std::string, uint64_t> counts;
            collectStrings(root, counts);
            buildStringDictionary(counts, stringIds, stringDictionary);
        }

        writeVarUInt(writer, stringDictionary.size());
        for (const auto &value : stringDictionary)
            writer.string(value);

        writePackedValue(
            writer,
            root,
            ids,
            stringIds,
            enableStringDictionary);
    }

    /*
     * ============================================================
     * BYV COMPILER
     * ============================================================
     */

    static std::vector<uint8_t> compile(
        std::string_view source)
    {
        Lexer lexer(source);

        auto tokens =
            lexer.tokenize();

        Parser parser(
            std::move(tokens));

        Value root =
            parser.parse();

        BinaryWriter writer;

        size_t payloadStart =
            writeHeader(writer, 3);

        writePackedPayload(writer, root, true);

        patchPayloadSize(
            writer,
            payloadStart);

        return std::move(writer.data);
    }

    static Value deserialize(
        const uint8_t *data,
        size_t size)
    {
        Header header =
            validateHeader(data, size);

        BinaryReader reader(
            data + HEADER_SIZE,
            header.payloadSize);

        const uint8_t flags = header.flags;
        validateFlags(flags);

        if ((flags & 0x01) != 0)
        {
            std::vector<std::string> dictionary;
            readDictionary(
                reader,
                dictionary,
                "BYV dictionary too large",
                [](BinaryReader &source)
                {
                    return source.string();
                });

            std::vector<std::string> stringDictionary;
            bool hasStringDictionary = (flags & 0x02) != 0;

            if (hasStringDictionary)
            {
                readDictionary(
                    reader,
                    stringDictionary,
                    "BYV string dictionary too large",
                    [](BinaryReader &source)
                    {
                        return source.string();
                    });
            }

            Value root = readPackedValue(
                reader,
                dictionary,
                stringDictionary,
                hasStringDictionary);

            ensureNoTrailingData(reader);

            return root;
        }

        Value root = readValue(reader);

        ensureNoTrailingData(reader);

        return root;
    }

    /*
     * ============================================================
     * NAPI CONVERSION
     * ============================================================
     */

    static Napi::Value toJS(
        Napi::Env env,
        const Value &value,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep");

        switch (value.type)
        {

        case Type::Null:
            return env.Null();

        case Type::Bool:
            return Napi::Boolean::New(
                env,
                value.boolean);

        case Type::Int:
            return Napi::Number::New(
                env,
                static_cast<double>(
                    value.integer));

        case Type::Float:
            return Napi::Number::New(
                env,
                value.floating);

        case Type::String:
            return Napi::String::New(
                env,
                value.string);

        case Type::Object:
        {
            Napi::Object object =
                Napi::Object::New(env);

            for (const auto &entry :
                 value.object)
            {

                Napi::Value child;

                {
                    BYV_PROFILE_SCOPE("tojs.object.child");
                    child = toJS(
                        env,
                        entry.second,
                        depth + 1);
                }

                {
                    BYV_PROFILE_SCOPE("tojs.object.set");
                    object.Set(
                        entry.first,
                        child);
                }
            }

            return object;
        }

        case Type::Array:
        {
            Napi::Array array =
                Napi::Array::New(
                    env,
                    value.array.size());

            for (
                size_t i = 0;
                i < value.array.size();
                i++)
            {
                Napi::Value child;

                {
                    BYV_PROFILE_SCOPE("tojs.array.child");
                    child = toJS(
                        env,
                        value.array[i],
                        depth + 1);
                }

                {
                    BYV_PROFILE_SCOPE("tojs.array.set");
                    array.Set(
                        i,
                        child);
                }
            }

            return array;
        }
        }

        throw std::runtime_error(
            "Unknown BYV type");
    }

    /*
     * ============================================================
     * NAPI API
     * ============================================================
     */

    // Optimized JS -> Value conversion.
    //
    // Key goals versus the previous std::function-based implementation:
    // - no type-erased recursive std::function calls
    // - each array element is fetched exactly once
    // - each object key is fetched exactly once
    // - each object value is fetched exactly once
    // - preserve the production packed/string-dictionary format
    static Value convertJSValue(
        const Napi::Value &input,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep; a cyclic structure may be the cause");

        Value result;

        if (input.IsNull() || input.IsUndefined())
        {
            result.type = Type::Null;
            return result;
        }

        if (input.IsBoolean())
        {
            result.type = Type::Bool;
            result.boolean = input.As<Napi::Boolean>().Value();
            return result;
        }

        if (input.IsNumber())
        {
            const double number =
                input.As<Napi::Number>().DoubleValue();

            int64_t integer;

            if (tryGetInt64(number, integer))
            {
                result.type = Type::Int;
                result.integer = integer;
            }
            else
            {
                result.type = Type::Float;
                result.floating = number;
            }

            return result;
        }

        if (input.IsString())
        {
            result.type = Type::String;
            result.string =
                input.As<Napi::String>().Utf8Value();
            return result;
        }

        if (input.IsArray())
        {
            result.type = Type::Array;

            const Napi::Array array =
                input.As<Napi::Array>();

            const uint32_t length = array.Length();
            result.array.reserve(length);

            for (uint32_t i = 0; i < length; ++i)
            {
                // Fetch each element exactly once.
                Napi::Value item = array.Get(i);
                result.array.emplace_back(
                    convertJSValue(
                        item,
                        depth + 1));
            }

            return result;
        }

        if (input.IsObject())
        {
            result.type = Type::Object;

            const Napi::Object object =
                input.As<Napi::Object>();

            const Napi::Array keys =
                object.GetPropertyNames();

            const uint32_t length = keys.Length();
            result.object.reserve(length);

            for (uint32_t i = 0; i < length; ++i)
            {
                // Fetch the property-name value once.
                const Napi::Value keyValue =
                    keys.Get(i);

                const Napi::String keyString =
                    keyValue.As<Napi::String>();

                std::string key =
                    keyString.Utf8Value();

                // Fetch the property value once.
                Napi::Value child =
                    object.Get(keyString);

                result.object.emplace_back(
                    std::move(key),
                    convertJSValue(
                        child,
                        depth + 1));
            }

            return result;
        }

        throw std::runtime_error(
            "Unsupported JavaScript value");
    }

    static void writeJSValueDirect(
        BinaryWriter &writer,
        Napi::Value input,
        std::unordered_map<std::string, uint32_t> &ids,
        std::vector<std::string> &dictionary)
    {
        if (input.IsNull() || input.IsUndefined())
        {
            writer.byte(static_cast<uint8_t>(Type::Null));
            return;
        }

        if (input.IsBoolean())
        {
            writer.byte(static_cast<uint8_t>(Type::Bool));
            writer.byte(input.As<Napi::Boolean>().Value() ? 1 : 0);
            return;
        }

        if (input.IsNumber())
        {
            double number = input.As<Napi::Number>().DoubleValue();

            int64_t integer;

            if (tryGetInt64(number, integer))
            {
                writer.byte(static_cast<uint8_t>(Type::Int));
                writer.u64(static_cast<uint64_t>(integer));
            }
            else
            {
                writer.byte(static_cast<uint8_t>(Type::Float));
                writer.u64(doubleToBits(number));
            }
            return;
        }

        if (input.IsString())
        {
            writer.byte(static_cast<uint8_t>(Type::String));
            writer.byte(0); // flags=1: no string dictionary
            std::string value = input.As<Napi::String>().Utf8Value();
            writer.string(value);
            return;
        }

        if (input.IsArray())
        {
            writer.byte(static_cast<uint8_t>(Type::Array));
            Napi::Array array = input.As<Napi::Array>();
            uint32_t length = array.Length();
            writer.u32(length);

            for (uint32_t i = 0; i < length; ++i)
                writeJSValueDirect(writer, array.Get(i), ids, dictionary);

            return;
        }

        if (input.IsObject())
        {
            writer.byte(static_cast<uint8_t>(Type::Object));
            Napi::Object object = input.As<Napi::Object>();
            Napi::Array keys = object.GetPropertyNames();
            uint32_t length = keys.Length();
            writer.u32(length);

            for (uint32_t i = 0; i < length; ++i)
            {
                Napi::Value keyValue = keys.Get(i);
                std::string key = keyValue.As<Napi::String>().Utf8Value();

                auto it = ids.find(key);
                uint32_t id;
                if (it == ids.end())
                {
                    id = static_cast<uint32_t>(dictionary.size());
                    ids.emplace(key, id);
                    dictionary.push_back(key);
                }
                else
                {
                    id = it->second;
                }

                writeVarUInt(writer, id);
                writeJSValueDirect(writer, object.Get(keyValue), ids, dictionary);
            }

            return;
        }

        throw std::runtime_error("Unsupported JavaScript value");
    }

    static std::vector<uint8_t> serializeJSDirectPacked(
        Napi::Value input)
    {
        std::unordered_map<std::string, uint32_t> ids;
        std::vector<std::string> dictionary;

        // The payload is written directly from JS values. This avoids
        // constructing the intermediate C++ Value tree entirely.
        BinaryWriter payload;
        writeJSValueDirect(payload, input, ids, dictionary);

        BinaryWriter writer;
        writer.data.reserve(10 + dictionary.size() * 8 + payload.data.size());

        size_t payloadStart =
            writeHeader(writer, 1);

        writeVarUInt(writer, dictionary.size());
        for (const auto &key : dictionary)
            writer.string(key);

        writer.bytes(payload.data.data(), payload.data.size());

        patchPayloadSize(
            writer,
            payloadStart);

        return std::move(writer.data);
    }

    /*
     * ============================================================
     * FAST JS -> BYV v7 PACKED SERIALIZER V2
     * ============================================================
     *
     * Important optimization:
     * - Do NOT traverse the JS object twice.
     * - Convert JS -> Value exactly once.
     * - During that same traversal collect:
     *     * key dictionary
     *     * string frequencies
     * - After that, all remaining work is native C++ memory.
     *
     * Compared with serialize()/writePackedPayload(), this removes
     * the separate collectKeys() and collectStrings() tree walks.
     *
     * Output remains BYV v7 flags=3.
     * ============================================================
     */

    static Value convertCollectFast(
        Napi::Value input,
        std::unordered_map<std::string, uint32_t> &keyIds,
        std::vector<std::string> &keyDictionary,
        std::unordered_map<std::string, uint64_t> &stringCounts,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep; a cyclic structure may be the cause");

        Value result;

        if (input.IsNull() || input.IsUndefined())
        {
            result.type = Type::Null;
            return result;
        }

        if (input.IsBoolean())
        {
            result.type = Type::Bool;
            result.boolean =
                input.As<Napi::Boolean>().Value();
            return result;
        }

        if (input.IsNumber())
        {
            double number =
                input.As<Napi::Number>().DoubleValue();

            int64_t integer;

            if (tryGetInt64(number, integer))
            {
                result.type = Type::Int;
                result.integer = integer;
            }
            else
            {
                result.type = Type::Float;
                result.floating = number;
            }

            return result;
        }

        if (input.IsString())
        {
            result.type = Type::String;
            result.string =
                input.As<Napi::String>().Utf8Value();

            ++stringCounts[result.string];
            return result;
        }

        if (input.IsArray())
        {
            result.type = Type::Array;

            Napi::Array array =
                input.As<Napi::Array>();

            const uint32_t length =
                array.Length();

            result.array.reserve(length);

            for (uint32_t i = 0; i < length; ++i)
            {
                result.array.emplace_back(
                    convertCollectFast(
                        array.Get(i),
                        keyIds,
                        keyDictionary,
                        stringCounts,
                        depth + 1));
            }

            return result;
        }

        if (input.IsObject())
        {
            result.type = Type::Object;

            Napi::Object object =
                input.As<Napi::Object>();

            Napi::Array keys =
                object.GetPropertyNames();

            const uint32_t length =
                keys.Length();

            result.object.reserve(length);

            for (uint32_t i = 0; i < length; ++i)
            {
                Napi::Value keyValue = keys.Get(i);

                std::string key =
                    keyValue
                        .As<Napi::String>()
                        .Utf8Value();

                if (keyIds.find(key) == keyIds.end())
                {
                    uint32_t id =
                        static_cast<uint32_t>(
                            keyDictionary.size());

                    keyIds.emplace(key, id);
                    keyDictionary.push_back(key);
                }

                Napi::Value child =
                    object.Get(keyValue);

                result.object.emplace_back(
                    std::move(key),
                    convertCollectFast(
                        child,
                        keyIds,
                        keyDictionary,
                        stringCounts,
                        depth + 1));
            }

            return result;
        }

        throw std::runtime_error(
            "Unsupported JavaScript value");
    }

    static std::vector<uint8_t> serializeJSFastPacked(
        Napi::Value input)
    {
        std::unordered_map<std::string, uint32_t> keyIds;
        std::vector<std::string> keyDictionary;
        std::unordered_map<std::string, uint64_t> stringCounts;

        /*
         * ONE N-API traversal:
         *   JS -> Value tree
         *   + key dictionary
         *   + string frequencies
         */
        Value root =
            convertCollectFast(
                input,
                keyIds,
                keyDictionary,
                stringCounts);

        std::unordered_map<std::string, uint32_t> stringIds;
        std::vector<std::string> stringDictionary;

        buildStringDictionary(
            stringCounts,
            stringIds,
            stringDictionary);

        BinaryWriter writer;

        writer.data.reserve(
            10 +
            keyDictionary.size() * 8 +
            stringDictionary.size() * 16 +
            1024);

        const size_t payloadStart =
            writeHeader(writer, 3);

        writeVarUInt(
            writer,
            keyDictionary.size());

        for (const auto &key : keyDictionary)
            writer.string(key);

        writeVarUInt(
            writer,
            stringDictionary.size());

        for (const auto &value : stringDictionary)
            writer.string(value);

        writePackedValue(
            writer,
            root,
            keyIds,
            stringIds,
            true);

        patchPayloadSize(
            writer,
            payloadStart);

        return std::move(writer.data);
    }

    static Napi::Value SerializeFast(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                requireArg(
                    info,
                    "serializeFast() requires argument");

                std::vector<uint8_t> bytes =
                    serializeJSFastPacked(info[0]);

                return copyBuffer(
                    env,
                    bytes);
            });
    }

    Napi::Value Serialize(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                requireArg(
                    info,
                    "serialize() requires argument");

#ifdef BYV_ENABLE_PROFILE
                ProfileGuard profile(
                    "BYV SERIALIZE PROFILE");
                profile.begin();
#endif

                Value root;

                {
                    BYV_PROFILE_SCOPE("serialize.convert");
                    root = convertJSValue(info[0]);
                }

                BinaryWriter writer;
                size_t payloadStart;

                {
                    BYV_PROFILE_SCOPE("serialize.header");

                    payloadStart =
                        writeHeader(
                            writer,
                            3);
                }

                {
                    BYV_PROFILE_SCOPE("serialize.packed_payload");
                    writePackedPayload(
                        writer,
                        root,
                        true);
                }

                {
                    BYV_PROFILE_SCOPE("serialize.payload_size");

                    patchPayloadSize(
                        writer,
                        payloadStart);
                }

                Napi::Value output;

                {
                    BYV_PROFILE_SCOPE("serialize.buffer_copy");

                    output =
                        copyBuffer(
                            env,
                            writer.data);
                }

#ifdef BYV_ENABLE_PROFILE
                profile.finish();
#endif

                return output;
            });
    }

    Napi::Value Parse(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env =
            info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                const Napi::String sourceValue =
                    requireStringArg(
                        info,
                        "parse() requires BYV string")
                        .As<Napi::String>();

                std::string source =
                    sourceValue.Utf8Value();

                Lexer lexer(source);

                Parser parser(
                    lexer.tokenize());

                Value root =
                    parser.parse();

                return toJS(
                    env,
                    root);
            });
    }

    Napi::Value Compile(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env =
            info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                const Napi::String sourceValue =
                    requireStringArg(
                        info,
                        "compile() requires BYV source")
                        .As<Napi::String>();

                std::string source =
                    sourceValue.Utf8Value();

                std::vector<uint8_t> result =
                    compile(source);

                return copyBuffer(
                    env,
                    result);
            });
    }

    /*
     * ============================================================
     * FAST DESERIALIZER: BYV -> JS DIRECT
     * ============================================================
     *
     * This path intentionally does NOT build the intermediate
     * C++ Value tree used by deserialize() + toJS().
     *
     * It reads the existing BYV v7 wire format and creates JS
     * objects/arrays directly through Node-API.
     *
     * Supported:
     *   flags bit 0 = packed key dictionary
     *   flags bit 1 = string dictionary
     *   flags 0      = legacy v7 payload
     *
     * The existing deserialize() API is left untouched so this
     * fast path can be benchmarked independently.
     */

    static Napi::Value readFastLegacyValue(
        BinaryReader &reader,
        Napi::Env env,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep");

        Type type =
            static_cast<Type>(reader.byte());

        switch (type)
        {
        case Type::Null:
            return env.Null();

        case Type::Bool:
        {
            return Napi::Boolean::New(
                env,
                readBool(reader));
        }

        case Type::Int:
            return Napi::Number::New(
                env,
                static_cast<double>(
                    static_cast<int64_t>(
                        reader.u64())));

        case Type::Float:
        {
            return Napi::Number::New(
                env,
                bitsToDouble(reader.u64()));
        }

        case Type::String:
        {
            std::string value =
                reader.string();

            return Napi::String::New(
                env,
                value);
        }

        case Type::Object:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV object too large");

            Napi::Object object =
                Napi::Object::New(env);

            for (uint32_t i = 0; i < count; ++i)
            {
                std::string key =
                    reader.string();

                Napi::Value child =
                    readFastLegacyValue(
                        reader,
                        env,
                        depth + 1);

                object.Set(
                    key,
                    child);
            }

            return object;
        }

        case Type::Array:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV array too large");

            Napi::Array array =
                Napi::Array::New(
                    env,
                    clampedReserve(
                        count,
                        reader));

            for (uint32_t i = 0; i < count; ++i)
            {
                Napi::Value child =
                    readFastLegacyValue(
                        reader,
                        env,
                        depth + 1);

                array.Set(
                    i,
                    child);
            }

            return array;
        }

        default:
            throw std::runtime_error(
                "Unknown BYV type");
        }
    }

    static Napi::Value readFastPackedValue(
        BinaryReader &reader,
        Napi::Env env,
        const std::vector<Napi::String> &jsKeys,
        const std::vector<Napi::String> &jsStrings,
        bool stringDictionaryEnabled,
        size_t depth = 0)
    {
        checkDepth(
            depth,
            "BYV nesting too deep");

        Type type =
            static_cast<Type>(reader.byte());

        switch (type)
        {
        case Type::Null:
            return env.Null();

        case Type::Bool:
        {
            return Napi::Boolean::New(
                env,
                readBool(reader));
        }

        case Type::Int:
            return Napi::Number::New(
                env,
                static_cast<double>(
                    static_cast<int64_t>(
                        reader.u64())));

        case Type::Float:
        {
            return Napi::Number::New(
                env,
                bitsToDouble(reader.u64()));
        }

        case Type::String:
        {
            if (stringDictionaryEnabled)
            {
                uint8_t marker =
                    reader.byte();

                if (marker == 1)
                {
                    uint64_t rawId =
                        readVarUInt(reader);

                    if (rawId >= jsStrings.size())
                    {
                        throw std::runtime_error(
                            "BYV packed string id out of range");
                    }

                    return jsStrings[static_cast<size_t>(rawId)];
                }

                if (marker != 0)
                {
                    throw std::runtime_error(
                        "BYV invalid string marker");
                }
            }

            std::string value =
                reader.string();

            return Napi::String::New(
                env,
                value);
        }

        case Type::Object:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV object too large");

            Napi::Object object =
                Napi::Object::New(env);

            for (uint32_t i = 0; i < count; ++i)
            {
                uint64_t rawId =
                    readVarUInt(reader);

                if (rawId >= jsKeys.size())
                {
                    throw std::runtime_error(
                        "BYV packed key id out of range");
                }

                Napi::Value child =
                    readFastPackedValue(
                        reader,
                        env,
                        jsKeys,
                        jsStrings,
                        stringDictionaryEnabled,
                        depth + 1);

                object.Set(
                    jsKeys[static_cast<size_t>(rawId)],
                    child);
            }

            return object;
        }

        case Type::Array:
        {
            uint32_t count =
                readCollectionCount(
                    reader,
                    "BYV array too large");

            Napi::Array array =
                Napi::Array::New(
                    env,
                    clampedReserve(
                        count,
                        reader));

            for (uint32_t i = 0; i < count; ++i)
            {
                Napi::Value child =
                    readFastPackedValue(
                        reader,
                        env,
                        jsKeys,
                        jsStrings,
                        stringDictionaryEnabled,
                        depth + 1);

                array.Set(
                    i,
                    child);
            }

            return array;
        }

        default:
            throw std::runtime_error(
                "Unknown BYV packed type");
        }
    }

    static Napi::Value deserializeFastJS(
        const uint8_t *data,
        size_t size,
        Napi::Env env)
    {
        Header header =
            validateHeader(data, size);

        BinaryReader reader(
            data + HEADER_SIZE,
            header.payloadSize);

        const uint8_t flags = header.flags;
        validateFlags(flags);

        if ((flags & 0x01) == 0)
        {
            // Legacy v7 payload: no dictionaries.
            Napi::Value result = readFastLegacyValue(
                reader,
                env);

            ensureNoTrailingData(reader);

            return result;
        }

        std::vector<Napi::String> jsKeys;
        readDictionary(
            reader,
            jsKeys,
            "BYV dictionary too large",
            [&env](BinaryReader &source)
            {
                return Napi::String::New(
                    env,
                    source.string());
            });

        const bool hasStringDictionary =
            (flags & 0x02) != 0;

        std::vector<Napi::String> jsStrings;

        if (hasStringDictionary)
        {
            readDictionary(
                reader,
                jsStrings,
                "BYV string dictionary too large",
                [&env](BinaryReader &source)
                {
                    return Napi::String::New(
                        env,
                        source.string());
                });
        }

        Napi::Value result = readFastPackedValue(
            reader,
            env,
            jsKeys,
            jsStrings,
            hasStringDictionary);

        ensureNoTrailingData(reader);

        return result;
    }

    Napi::Value DeserializeFast(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env =
            info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                Napi::Buffer<uint8_t> buffer =
                    requireBufferArg(
                        info,
                        "deserializeFast() requires Buffer");

#ifdef BYV_ENABLE_PROFILE
                ProfileGuard profile(
                    "BYV DESERIALIZE FAST PROFILE");
                profile.begin();
#endif

                Napi::Value result;

                {
                    BYV_PROFILE_SCOPE(
                        "deserializeFast.direct_js");

                    result =
                        deserializeFastJS(
                            buffer.Data(),
                            buffer.Length(),
                            env);
                }

#ifdef BYV_ENABLE_PROFILE
                profile.finish();
#endif

                return result;
            });
    }

    Napi::Value Deserialize(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env =
            info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                Napi::Buffer<uint8_t> buffer =
                    requireBufferArg(
                        info,
                        "deserialize() requires Buffer");

#ifdef BYV_ENABLE_PROFILE
                ProfileGuard profile(
                    "BYV DESERIALIZE PROFILE");
                profile.begin();
#endif

                Value root;

                {
                    BYV_PROFILE_SCOPE("deserialize.decode");
                    root = deserialize(
                        buffer.Data(),
                        buffer.Length());
                }

                Napi::Value result;
                {
                    BYV_PROFILE_SCOPE("deserialize.to_js");
                    result = toJS(
                        env,
                        root);
                }

#ifdef BYV_ENABLE_PROFILE
                profile.finish();
#endif

                return result;
            });
    }

    Napi::Value Inspect(
        const Napi::CallbackInfo &info)
    {
        Napi::Env env =
            info.Env();

        return guarded(
            env,
            [&]() -> Napi::Value
            {
                Napi::Buffer<uint8_t> buffer =
                    requireBufferArg(
                        info,
                        "inspect() requires Buffer");

                Napi::Object result =
                    Napi::Object::New(env);

                bool valid =
                    buffer.Length() >= 10 &&
                    buffer[0] == 'B' &&
                    buffer[1] == 'Y' &&
                    buffer[2] == 'V' &&
                    buffer[3] == '7' &&
                    buffer[4] == VERSION &&
                    readLittleEndianU32(
                        buffer.Data() + HEADER_PAYLOAD_OFFSET) ==
                        buffer.Length() - 10 &&
                    flagsValid(buffer[5]);

                result.Set(
                    "valid",
                    valid);

                result.Set(
                    "version",
                    buffer.Length() >= 5
                        ? buffer[4]
                        : 0);

                result.Set(
                    "byteLength",
                    static_cast<double>(
                        buffer.Length()));

                if (buffer.Length() >= 10)
                {

                    uint32_t payload =
                        readLittleEndianU32(
                            buffer.Data() + HEADER_PAYLOAD_OFFSET);

                    result.Set(
                        "payloadSize",
                        payload);

                    result.Set(
                        "flags",
                        buffer[5]);
                }

                return result;
            });
    }

    Napi::Value Version(
        const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(
            info.Env(),
            VERSION);
    }

    /*
     * ============================================================
     * MODULE INITIALIZATION
     * ============================================================
     */

    Napi::Object Init(
        Napi::Env env,
        Napi::Object exports)
    {
        exports.Set(
            "version",
            Napi::Function::New(
                env,
                Version));

        exports.Set(
            "parse",
            Napi::Function::New(
                env,
                Parse));

        exports.Set(
            "compile",
            Napi::Function::New(
                env,
                Compile));

        exports.Set(
            "serialize",
            Napi::Function::New(
                env,
                Serialize));

        exports.Set(
            "serializeFast",
            Napi::Function::New(
                env,
                SerializeFast));

        exports.Set(
            "deserialize",
            Napi::Function::New(
                env,
                Deserialize));

        exports.Set(
            "deserializeFast",
            Napi::Function::New(
                env,
                DeserializeFast));

        exports.Set(
            "inspect",
            Napi::Function::New(
                env,
                Inspect));

        return exports;
    }

} // namespace byv

/*
 * ============================================================
 * GLOBAL NODE-API INITIALIZER
 * ============================================================
 *
 * Jangan menggunakan:
 *
 * NODE_API_MODULE(byv_core, byv::Init)
 *
 * karena beberapa versi node-addon-api/node-gyp/MSVC
 * bermasalah ketika initializer berada di namespace.
 *
 * Kita buat wrapper global.
 */

static Napi::Object BYVInit(
    Napi::Env env,
    Napi::Object exports)
{
    return byv::Init(env, exports);
}

NODE_API_MODULE(byv_core, BYVInit)