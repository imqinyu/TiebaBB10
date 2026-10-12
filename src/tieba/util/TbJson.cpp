#include "TbJson.hpp"

#include "util/TbTr.hpp"

#include <QStringList>
#include <stdlib.h>

namespace tieba {

namespace {

/*! 判断是否为数字字面量允许的字符 */
inline bool isNumberChar(char c)
{
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.'
        || c == 'e' || c == 'E';
}

/*! 解析 uXXXX 转义，返回码点。失败返回 -1 */
int readHex4(const QByteArray &s, int &pos)
{
    if (pos + 4 > s.size())
        return -1;
    int value = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = s.at(pos + i);
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return -1;
        value = value * 16 + digit;
    }
    pos += 4;
    return value;
}

} // anonymous namespace

/* ============================ 解析器 ============================ */

class TbJson::Parser
{
public:
    Parser(const QByteArray &text)
        : m_text(text), m_pos(0), m_errorPos(0) {}

    TbJson parse()
    {
        skipWhitespace();
        TbJson root = parseValue(0);
        if (!m_error.isEmpty())
            return makeError();

        skipWhitespace();
        if (m_pos != m_text.size()) {
            m_errorPos = m_pos;
            m_error = TbTr("E9A1B6E5B182E580BCE4B98BE5908EE8BF98E69C89E58685E5AEB9");
            return makeError();
        }
        return root;
    }

    QString error() const { return m_error; }
    int errorPos() const { return m_errorPos; }

private:
    static const int kMaxDepth = 128;

    QByteArray m_text;
    int m_pos;
    int m_errorPos;
    QString m_error;

    TbJson makeError() const
    {
        TbJson out;
        out.m_valid = false;
        out.m_error = TbTr("4A534F4E20E8A7A3E69E90E5A4B1E8B4A528E5818FE7A7BB202531293A2532")
                          .arg(m_errorPos)
                          .arg(m_error);
        return out;
    }

    void setError(const QString &message)
    {
        if (m_error.isEmpty()) {
            m_error = message;
            m_errorPos = m_pos;
        }
    }

    void skipWhitespace()
    {
        while (m_pos < m_text.size()) {
            const char c = m_text.at(m_pos);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++m_pos;
            else
                break;
        }
    }

    char peek() const
    {
        return m_pos < m_text.size() ? m_text.at(m_pos) : '\0';
    }

    TbJson parseValue(int depth)
    {
        if (depth > kMaxDepth) {
            setError(TbTr("E5B58CE5A597E5B182E695B0E8BF87E6B7B1"));
            return TbJson();
        }

        skipWhitespace();
        if (m_pos >= m_text.size()) {
            setError(TbTr("E8BE93E585A5E6848FE5A496E7BB93E69D9F"));
            return TbJson();
        }

        switch (peek()) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
        case '"':
            return parseStringValue();
        case 't':
        case 'f':
            return parseBool();
        case 'n':
            return parseNull();
        default:
            return parseNumber();
        }
    }

    TbJson parseObject(int depth)
    {
        ++m_pos; // '{'
        TbJson out;
        out.m_valid = true;
        out.m_type = TbJson::Object;

        skipWhitespace();
        if (peek() == '}') {
            ++m_pos;
            return out;
        }

        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                setError(TbTr("253120E79A84E994AEE5BF85E9A1BBE698AFE5AD97E7ACA6E4B8B2"));
                return TbJson();
            }
            const QString key = parseString();
            if (!m_error.isEmpty())
                return TbJson();

            skipWhitespace();
            if (peek() != ':') {
                setError(TbTr("E994AE20253120E5908EE5BF85E9A1BBE698AF203A"));
                return TbJson();
            }
            ++m_pos;

            const TbJson value = parseValue(depth + 1);
            if (!m_error.isEmpty())
                return TbJson();

            out.m_members.insert(key, value);

            skipWhitespace();
            const char c = peek();
            if (c == ',') {
                ++m_pos;
                continue;
            }
            if (c == '}') {
                ++m_pos;
                return out;
            }
            setError(TbTr("253120E5A484E69C9FE69C9B202C20E68896207D"));
            return TbJson();
        }
    }

    TbJson parseArray(int depth)
    {
        ++m_pos; // '['
        TbJson out;
        out.m_valid = true;
        out.m_type = TbJson::Array;

        skipWhitespace();
        if (peek() == ']') {
            ++m_pos;
            return out;
        }

        while (true) {
            const TbJson value = parseValue(depth + 1);
            if (!m_error.isEmpty())
                return TbJson();
            out.m_items.append(value);

            skipWhitespace();
            const char c = peek();
            if (c == ',') {
                ++m_pos;
                continue;
            }
            if (c == ']') {
                ++m_pos;
                return out;
            }
            setError(TbTr("253120E5A484E69C9FE69C9B202C20E68896205D"));
            return TbJson();
        }
    }

    TbJson parseStringValue()
    {
        const QString s = parseString();
        if (!m_error.isEmpty())
            return TbJson();
        TbJson out;
        out.m_valid = true;
        out.m_type = TbJson::String;
        out.m_stringValue = s;
        return out;
    }

    /*! 调用前必须确认当前字符是双引号 */
    QString parseString()
    {
        ++m_pos; // 开引号

        QByteArray raw;
        while (true) {
            if (m_pos >= m_text.size()) {
                setError(TbTr("E5AD97E7ACA6E4B8B2E6B2A1E69C89E7BB93E69D9FE5BC95E58FB7"));
                return QString();
            }
            const char c = m_text.at(m_pos);

            if (c == '"') {
                ++m_pos;
                break;
            }
            if (c != '\\') {
                // 快路径：普通字节直接拷
                raw.append(c);
                ++m_pos;
                continue;
            }

            // 转义
            ++m_pos;
            if (m_pos >= m_text.size()) {
                setError(TbTr("E8BDACE4B989E5BA8FE58897E6B2A1E69C89E5AE8CE68890"));
                return QString();
            }
            const char esc = m_text.at(m_pos++);
            switch (esc) {
            case '"':  raw.append('"');  break;
            case '\\': raw.append('\\'); break;
            case '/':  raw.append('/');  break;
            case 'b':  raw.append('\b'); break;
            case 'f':  raw.append('\f'); break;
            case 'n':  raw.append('\n'); break;
            case 'r':  raw.append('\r'); break;
            case 't':  raw.append('\t'); break;
            case 'u': {
                const int code = readHex4(m_text, m_pos);
                if (code < 0) {
                    setError(TbTr("E697A0E69588E79A84205C7520E8BDACE4B989"));
                    return QString();
                }
                unsigned int cp = (unsigned int)code;

                // 代理对：高位 D800-DBFF 后面必须跟低位 DC00-DFFF
                if (cp >= 0xD800u && cp <= 0xDBFFu) {
                    if (m_pos + 1 < m_text.size()
                            && m_text.at(m_pos) == '\\'
                            && m_text.at(m_pos + 1) == 'u') {
                        m_pos += 2;
                        const int low = readHex4(m_text, m_pos);
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000u
                                 + ((cp - 0xD800u) << 10)
                                 + ((unsigned int)low - 0xDC00u);
                        } else {
                            setError(TbTr("E697A0E69588E79A84E4BD8EE4BBA3E79086E9A1B9"));
                            return QString();
                        }
                    } else {
                        setError(TbTr("E69CAAE9858DE5AFB9E79A84E9AB98E4BBA3E79086E9A1B9"));
                        return QString();
                    }
                } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                    setError(TbTr("E69CAAE9858DE5AFB9E79A84E4BD8EE4BBA3E79086E9A1B9"));
                    return QString();
                }

                // 编码为 UTF-8 字节，最后统一走 fromUtf8
                if (cp < 0x80u) {
                    raw.append((char)cp);
                } else if (cp < 0x800u) {
                    raw.append((char)(0xC0u | (cp >> 6)));
                    raw.append((char)(0x80u | (cp & 0x3Fu)));
                } else if (cp < 0x10000u) {
                    raw.append((char)(0xE0u | (cp >> 12)));
                    raw.append((char)(0x80u | ((cp >> 6) & 0x3Fu)));
                    raw.append((char)(0x80u | (cp & 0x3Fu)));
                } else {
                    raw.append((char)(0xF0u | (cp >> 18)));
                    raw.append((char)(0x80u | ((cp >> 12) & 0x3Fu)));
                    raw.append((char)(0x80u | ((cp >> 6) & 0x3Fu)));
                    raw.append((char)(0x80u | (cp & 0x3Fu)));
                }
                break;
            }
            default:
                setError(TbTr("E69CAAE79FA5E79A84E8BDACE4B989E5BA8FE58897"));
                return QString();
            }
        }
        return QString::fromUtf8(raw.constData(), raw.size());
    }

    TbJson parseBool()
    {
        if (m_text.mid(m_pos, 4) == QByteArray("true")) {
            m_pos += 4;
            TbJson out;
            out.m_valid = true;
            out.m_type = TbJson::Bool;
            out.m_boolValue = true;
            out.m_stringValue = QLatin1String("true");
            return out;
        }
        if (m_text.mid(m_pos, 5) == QByteArray("false")) {
            m_pos += 5;
            TbJson out;
            out.m_valid = true;
            out.m_type = TbJson::Bool;
            out.m_boolValue = false;
            out.m_stringValue = QLatin1String("false");
            return out;
        }
        setError(TbTr("E697A0E69588E79A84E5AD97E99DA2E9878F28E5BA94E4B8BA20747275652F66616C736529"));
        return TbJson();
    }

    TbJson parseNull()
    {
        if (m_text.mid(m_pos, 4) == QByteArray("null")) {
            m_pos += 4;
            TbJson out;
            out.m_valid = true;
            out.m_type = TbJson::Null;
            return out;
        }
        setError(TbTr("E697A0E69588E79A84E5AD97E99DA2E9878F28E5BA94E4B8BA206E756C6C29"));
        return TbJson();
    }

    TbJson parseNumber()
    {
        const int start = m_pos;
        while (m_pos < m_text.size() && isNumberChar(m_text.at(m_pos)))
            ++m_pos;

        if (m_pos == start) {
            setError(TbTr("E6848FE5A496E79A84E5AD97E7ACA63A202531")
                         .arg(QChar(m_text.at(m_pos))));
            return TbJson();
        }

        const QByteArray literal = m_text.mid(start, m_pos - start);
        bool isReal = literal.contains('.') || literal.contains('e')
                      || literal.contains('E');

        TbJson out;
        out.m_valid = true;
        out.m_type = TbJson::Number;
        out.m_stringValue = QString::fromLatin1(literal.constData(), literal.size());
        out.m_numberIsReal = isReal;

        if (isReal) {
            out.m_realValue = literal.toDouble();
        } else {
            bool ok = false;
            out.m_intValue = literal.toLongLong(&ok);
            if (!ok) {
                // 超长整数（例如某些 id）：退化为 double，但字符串形式仍精确保留
                out.m_numberIsReal = true;
                out.m_realValue = literal.toDouble();
            }
        }
        return out;
    }
};

/* ============================ 公有接口 ============================ */

TbJson::TbJson()
    : m_type(Null)
    , m_valid(true)
    , m_boolValue(false)
    , m_numberIsReal(false)
    , m_intValue(0)
    , m_realValue(0.0)
{
}

TbJson::TbJson(const TbJson &other)
    : m_type(other.m_type)
    , m_valid(other.m_valid)
    , m_error(other.m_error)
    , m_boolValue(other.m_boolValue)
    , m_stringValue(other.m_stringValue)
    , m_numberIsReal(other.m_numberIsReal)
    , m_intValue(other.m_intValue)
    , m_realValue(other.m_realValue)
    , m_items(other.m_items)
    , m_members(other.m_members)
{
}

TbJson &TbJson::operator=(const TbJson &other)
{
    if (this != &other) {
        m_type = other.m_type;
        m_valid = other.m_valid;
        m_error = other.m_error;
        m_boolValue = other.m_boolValue;
        m_stringValue = other.m_stringValue;
        m_numberIsReal = other.m_numberIsReal;
        m_intValue = other.m_intValue;
        m_realValue = other.m_realValue;
        m_items = other.m_items;
        m_members = other.m_members;
    }
    return *this;
}

TbJson::~TbJson()
{
}

TbJson TbJson::parse(const QByteArray &utf8)
{
    Parser parser(utf8);
    return parser.parse();
}

TbJson TbJson::at(int index) const
{
    if (m_type != Array || index < 0 || index >= m_items.size())
        return TbJson();
    return m_items.at(index);
}

TbJson TbJson::member(const QString &key) const
{
    if (m_type != Object)
        return TbJson();
    QMap<QString, TbJson>::const_iterator it = m_members.find(key);
    if (it == m_members.end())
        return TbJson();
    return it.value();
}

QString TbJson::toString(const QString &def) const
{
    switch (m_type) {
    case Null:
        return def;
    case String:
    case Number:
        return m_stringValue;
    case Bool:
        return m_boolValue ? QLatin1String("true") : QLatin1String("false");
    default:
        return def;
    }
}

qint64 TbJson::toLongLong(qint64 def) const
{
    switch (m_type) {
    case Number:
        if (m_numberIsReal)
            return (qint64)m_realValue;
        return m_intValue;
    case Bool:
        return m_boolValue ? 1 : 0;
    case String: {
        // 贴吧大量使用字符串数字，且偶尔带空白
        bool ok = false;
        const qint64 value = m_stringValue.trimmed().toLongLong(&ok);
        return ok ? value : def;
    }
    default:
        return def;
    }
}

int TbJson::toInt(int def) const
{
    const qint64 value = toLongLong(def);
    return (int)value;
}

double TbJson::toDouble(double def) const
{
    switch (m_type) {
    case Number:
        return m_numberIsReal ? m_realValue : (double)m_intValue;
    case Bool:
        return m_boolValue ? 1.0 : 0.0;
    case String: {
        bool ok = false;
        const double value = m_stringValue.trimmed().toDouble(&ok);
        return ok ? value : def;
    }
    default:
        return def;
    }
}

bool TbJson::toBool(bool def) const
{
    switch (m_type) {
    case Bool:
        return m_boolValue;
    case Number:
        return m_numberIsReal ? (m_realValue != 0.0) : (m_intValue != 0);
    case String: {
        const QString s = m_stringValue.trimmed().toLower();
        if (s.isEmpty())
            return def;
        if (s == QLatin1String("0") || s == QLatin1String("false")
                || s == QLatin1String("null") || s == QLatin1String("no"))
            return false;
        if (s == QLatin1String("1") || s == QLatin1String("true")
                || s == QLatin1String("yes"))
            return true;
        // 其他非空字符串一律为真
        return true;
    }
    default:
        return def;
    }
}

QString TbJson::str(const TbJson &node, const QString &key, const QString &def)
{
    return node.member(key).toString(def);
}

QString TbJson::str2(const TbJson &node, const QString &key1,
                     const QString &key2, const QString &def)
{
    const TbJson first = node.member(key1);
    if (!first.isMissing())
        return first.toString(def);
    return node.member(key2).toString(def);
}

qint64 TbJson::integer(const TbJson &node, const QString &key, qint64 def)
{
    const TbJson value = node.member(key);
    if (value.isMissing())
        return def;
    return value.toLongLong(def);
}

int TbJson::intValue(const TbJson &node, const QString &key, int def)
{
    const TbJson value = node.member(key);
    if (value.isMissing())
        return def;
    return value.toInt(def);
}

bool TbJson::boolean(const TbJson &node, const QString &key, bool def)
{
    const TbJson value = node.member(key);
    if (value.isMissing())
        return def;
    return value.toBool(def);
}

TbJson TbJson::object(const TbJson &node, const QString &key)
{
    const TbJson value = node.member(key);
    return value.isObject() ? value : TbJson();
}

TbJson TbJson::array(const TbJson &node, const QString &key)
{
    const TbJson value = node.member(key);
    return value.isArray() ? value : TbJson();
}

QStringList TbJson::stringList(const TbJson &node, const QString &key)
{
    QStringList out;
    const TbJson value = node.member(key);
    if (!value.isArray())
        return out;
    for (int i = 0; i < value.size(); ++i)
        out.append(value.at(i).toString());
    return out;
}

} // namespace tieba
