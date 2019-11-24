#include "JsonReader.h"
#include <clocale>
#include <sstream>
#include <stdarg.h>

#ifdef USE_WINAPI
#include <windows.h>
#endif

#define FILE_BUFFER 1048576
#define RESIZE_FACTOR 1.2f

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class JsonReader

JsonReader::JsonReader() { clear(); }

JsonReader::~JsonReader()
{
    clear();
}

void JsonReader::clear()
{
    m_useLocale = false;
    m_currentPublisher = nullptr;
    m_input.release();
    clearStrings();
    unsubscribe();
}

void JsonReader::clearStrings()
{
    m_elemName.clear();
    m_elemValue.clear();
    m_path.clear();
    m_currentElemName.clear();
}

void JsonReader::unsubscribe()
{
    m_onObjectBegin.unsubscribe();
    m_onObjectEnd.unsubscribe();
    m_onArrayBegin.unsubscribe();
    m_onArrayEnd.unsubscribe();
    m_onArrayItem.unsubscribe();
    m_onPair.unsubscribe();
}

void JsonReader::updateCurrentPath(size_t& pathLen)
{
    if (m_elemName.length > 0)
    {
        memcpy((char*)(m_path.str + pathLen), m_elemName.str, m_elemName.length);
        pathLen += m_elemName.length;
        if (pathLen >= m_path.capacity)
            m_path.resize((size_t)(pathLen * RESIZE_FACTOR));
        m_path.setLength(pathLen);
        m_path.isAscii &= m_elemName.isAscii;
    }
}

void JsonReader::parseObject(size_t pathLen, size_t namePos, size_t elemNameLen)
{
    m_path.str[pathLen++] = '{';

    notify(&m_onObjectBegin, namePos, elemNameLen, pathLen);

    while (m_input.getNextChar() != '}')
    {
        parseString(m_elemName);
        m_input.getNextChar();
        parseValue(pathLen);
    }
    m_elemName.clear();
    m_elemValue.clear();
    notify(&m_onObjectEnd, namePos, elemNameLen, pathLen);
}

void JsonReader::parseArray(size_t pathLen, size_t namePos, size_t elemNameLen)
{
    m_path.str[pathLen++] = '[';

    notify(&m_onArrayBegin, namePos, elemNameLen, pathLen);

    while (m_input.getNextChar() != ']')
    {
        m_elemName.clear();
        m_elemValue.clear();
        m_arrayItem.clear();
        parseValue(pathLen, &m_arrayItem);
        notify(&m_onArrayItem, namePos, elemNameLen, pathLen, m_arrayItem.getValue());
    }
    m_elemName.clear();
    m_elemValue.clear();
    m_arrayItem.clear();
    notify(&m_onArrayEnd, namePos, elemNameLen, pathLen);
}

void JsonReader::parseValue(size_t pathLen, ARRAY_ITEM* arrayItem)
{
    size_t namePos = pathLen;
    size_t elemNameLen = m_elemName.length;
    bool isPathAscii = m_path.isAscii;
    STR* elemValue = &m_elemValue;

    updateCurrentPath(pathLen);
    char ch = m_input.getCurrentChar();

    if (ch == '{')
        parseObject(pathLen, namePos, elemNameLen);
    else if (ch == '[')
        parseArray(pathLen, namePos, elemNameLen);
    else
    {
        if (ch == '\"') // key / value pair.
            parseString(m_elemValue);
        else if ((ch >= '0' && ch <= '9') || ch == '-') // number.
            parseNumber(m_elemValue);
        else if (parseTrue())
            m_elemValue.copy("true", 4);
        else if (parseFalse())
            m_elemValue.copy("false", 5);
        else if (parseNull())
            elemValue = nullptr;
        else
            throwException("Unexpected character '%c'.", ch);
        if (!arrayItem)
            notify(&m_onPair, namePos, elemNameLen, pathLen, elemValue);
        else
            arrayItem->setValue(elemValue);
    }
    m_path.isAscii = isPathAscii;
}

void JsonReader::parseString(STR& text)
{
    char ch = 0;
    text.length = 0;
    text.isAscii = true;
    text.isQuoted = true;

    m_input.goToNextQuote();
    while ((ch = m_input.getNextChar(true)) != '\"')
    {
        if (ch == '\\')
            m_input.readEscapeSequence(text);
        else
        {
            if (text.length >= text.capacity)
                text.resize((size_t)(text.length * RESIZE_FACTOR));
            if (static_cast<unsigned char>(ch) > 0x7F)
                text.isAscii = false;
            text.str[text.length++] = ch;
        }
    }
    text.str[text.length] = 0;
}

void JsonReader::parseNumber(STR& number)
{
    number.length = 0;
    number.isAscii = true;
    number.isQuoted = false;

    while (isNumericCharacter(m_input.getCurrentChar()))
    {
        if (number.length >= number.capacity)
            number.resize((size_t)(number.length * RESIZE_FACTOR));
        number.str[number.length++] = m_input.getCurrentChar();
        m_input.getNextChar(true);
    }
    m_input.goToPreviousChar();
    number.str[number.length] = 0;
}

bool JsonReader::isNumericCharacter(char ch)
{
    if ((ch < '0' || ch > '9') && ch != '.' && ch != '+' && ch != '-' && ch != 'e' && ch != 'E')
        return false;
    return true;
}

bool JsonReader::parseTrue()
{
    return (m_input.getCurrentChar() == 't' && m_input.getNextChar(true) == 'r' &&
        m_input.getNextChar(true) == 'u' && m_input.getNextChar(true) == 'e');
}

bool JsonReader::parseFalse()
{
    return (m_input.getCurrentChar() == 'f' && m_input.getNextChar(true) == 'a' &&
        m_input.getNextChar(true) == 'l' && m_input.getNextChar(true) == 's' && m_input.getNextChar(true) == 'e');
}

bool JsonReader::parseNull()
{
    return (m_input.getCurrentChar() == 'n' && m_input.getNextChar(true) == 'u' &&
        m_input.getNextChar(true) == 'l' && m_input.getNextChar(true) == 'l');
}

bool JsonReader::readFile(const char* fileFullPath) { return read(fileFullPath, true); }

bool JsonReader::readBuffer(const char* bufferUtf8) { return read(bufferUtf8, false); }

bool JsonReader::read(const char* source, bool isFile, std::set<std::wstring>* pathList)
{
    bool succeeded = true;
    try
    {
        clearStrings();

        m_input.init(source, isFile);

        if (pathList)
        {
            // Store unique JSON paths in array 'pathList'.
            std::function<void()> callback = [pathList, this]() { pathList->insert(getCurrentElementPathWide()); };
            std::function<void(const wchar_t*)> funcPair = [pathList, this](const wchar_t*)
            {
                pathList->insert(getCurrentElementPathWide());
            };
            onObjectBegin((const char*)nullptr, callback);
            onArrayBegin((const char*)nullptr, callback);
            onPair((const char*)nullptr, funcPair);
        }

        m_input.getNextChar();
        if (!m_input.isEOF())
            parseValue(0);
    }
    catch (std::exception& e)
    {
        std::ostringstream description;
        description << e.what();
        if (m_input.getPosition() > 0)
            description << " Byte Position: " << m_input.getPosition() << ".";
        if (strlen(m_path.str) > 0)
            description << " JSON path: '" << m_path.str << "'.";
        m_errDescription = description.str();
        succeeded = false;
    }
    clear();
    return succeeded;
}

std::string JsonReader::getCurrentElementPath() { return std::string(m_path.toNarrow()); }

std::wstring JsonReader::getCurrentElementPathWide() { return std::wstring(m_path.toWide()); }

void JsonReader::getCurrentElementPath(std::string& elementPath) { elementPath = m_path.toNarrow(); }

void JsonReader::getCurrentElementPathWide(std::wstring& elementPath) { elementPath = m_path.toWide(); }

std::string JsonReader::getCurrentElementName()
{
    std::string str;
    getCurrentElementName(str);
    return str;
}

std::wstring JsonReader::getCurrentElementNameWide()
{
    std::wstring wstr;
    getCurrentElementNameWide(wstr);
    return wstr;
}

void JsonReader::getCurrentElementName(std::string& elementName)
{
    if (m_currentPublisher)
    {
        m_currentPublisher->getCurrentElementName(m_currentElemName);
        elementName.assign(m_currentElemName.toNarrow());
    }
    else
        elementName.clear();
}

void JsonReader::getCurrentElementNameWide(std::wstring& elementName)
{
    if (m_currentPublisher)
    {
        m_currentPublisher->getCurrentElementName(m_currentElemName);
        elementName.assign(m_currentElemName.toWide());
    }
    else
        elementName.clear();
}

bool JsonReader::getPathsFromFile(const char* fileFullPath, std::set<std::wstring>& paths)
{
    return read(fileFullPath, true, &paths);
}

bool JsonReader::getPathsFromBuffer(const char* bufferUtf8, std::set<std::wstring>& paths)
{
    return read(bufferUtf8, false, &paths);
}

void JsonReader::throwException(const char* format, ...)
{
    char buffer[2048];
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(buffer, 2048, format, argptr);
    va_end(argptr);
    throw std::runtime_error(std::string(buffer));
}

void JsonReader::useLocale(bool useLocale, const char* locale)
{
    m_useLocale = useLocale;
    m_elemName.useLocale = useLocale;
    m_elemValue.useLocale = useLocale;
    m_path.useLocale = useLocale;
    m_currentElemName.useLocale = useLocale;

    if (locale == NULL)
        setlocale(LC_ALL, "");
    else if (setlocale(LC_ALL, locale) == NULL)
        throwException("Locale '%s' not found.", locale);
}

void JsonReader::notify(Publisher* publisher, size_t namePos, size_t nameLen, size_t pathLen, STR* value)
{
    m_path.setLength(pathLen);
    m_currentPublisher = publisher;
    publisher->notify(m_path.str, namePos, nameLen, pathLen, value);
}

// Methods for subscribing to events.
// -> Wide character string versions.
void JsonReader::onObjectBegin(const wchar_t* element, std::function<void()> callback)
{
    m_onObjectBegin.subscribe(element, new Callback0(callback));
}
void JsonReader::onObjectEnd(const wchar_t* element, std::function<void()> callback)
{
    m_onObjectEnd.subscribe(element, new Callback0(callback));
}
void JsonReader::onArrayBegin(const wchar_t* element, std::function<void()> callback)
{
    m_onArrayBegin.subscribe(element, new Callback0(callback));
}
void JsonReader::onArrayEnd(const wchar_t* element, std::function<void()> callback)
{
    m_onArrayEnd.subscribe(element, new Callback0(callback));
}
void JsonReader::onArrayItem(const wchar_t* element, std::function<void(const char*)> callback)
{
    m_onArrayItem.subscribe(element, new Callback1(callback));
}
void JsonReader::onArrayItem(const wchar_t* element, std::function<void(const wchar_t*)> callback)
{
    m_onArrayItem.subscribe(element, new Callback1(callback));
}
void JsonReader::onPair(const wchar_t* element, std::function<void(const char*)> callback)
{
    m_onPair.subscribe(element, new Callback1(callback));
}
void JsonReader::onPair(const wchar_t* element, std::function<void(const wchar_t*)> callback)
{
    m_onPair.subscribe(element, new Callback1(callback));
}
// -> UTF-8 string versions.
void JsonReader::onObjectBegin(const char* elementUtf8, std::function<void()> callback)
{
    m_onObjectBegin.subscribe(elementUtf8, new Callback0(callback));
}
void JsonReader::onObjectEnd(const char* elementUtf8, std::function<void()> callback)
{
    m_onObjectEnd.subscribe(elementUtf8, new Callback0(callback));
}
void JsonReader::onArrayBegin(const char* elementUtf8, std::function<void()> callback)
{
    m_onArrayBegin.subscribe(elementUtf8, new Callback0(callback));
}
void JsonReader::onArrayEnd(const char* elementUtf8, std::function<void()> callback)
{
    m_onArrayEnd.subscribe(elementUtf8, new Callback0(callback));
}
void JsonReader::onArrayItem(const char* elementUtf8, std::function<void(const char*)> callback)
{
    m_onArrayItem.subscribe(elementUtf8, new Callback1(callback));
}
void JsonReader::onArrayItem(const char* elementUtf8, std::function<void(const wchar_t*)> callback)
{
    m_onArrayItem.subscribe(elementUtf8, new Callback1(callback));
}
void JsonReader::onPair(const char* elementUtf8, std::function<void(const char*)> callback)
{
    m_onPair.subscribe(elementUtf8, new Callback1(callback));
}
void JsonReader::onPair(const char* elementUtf8, std::function<void(const wchar_t*)> callback)
{
    m_onPair.subscribe(elementUtf8, new Callback1(callback));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class JsonReader::TextConverter

JsonReader::TextConverter::TextConverter()
{
    m_narrowMaxLen = 1;
    m_narrowString = new char[m_narrowMaxLen + 1];
    m_narrowString[0] = 0;
    m_wideMaxLen = 1;
    m_wideString = new wchar_t[m_wideMaxLen + 1];
    m_wideString[0] = 1;
}

JsonReader::TextConverter::~TextConverter()
{
    if (m_narrowString)
        delete[] m_narrowString;
    if (m_wideString)
        delete[] m_wideString;
}

const char* JsonReader::TextConverter::MultiByteToUtf8(const char* multibyte, const size_t multibyteLen, size_t* outLen)
{
    if (!multibyte)
        return nullptr;

    if (multibyteLen == 0)
    {
        m_narrowString[0] = 0;
        return m_narrowString;
    }

    size_t length = 0;
    mbstate_t state = { 0 };
#ifdef USE_WINAPI
    mbsrtowcs_s(&length, nullptr, 0, &multibyte, multibyteLen, &state);
#else
    length = std::mbsrtowcs(nullptr, &multibyte, multibyteLen, &state);
#endif

    if (length == 0 || length == static_cast<size_t>(-1))
        return nullptr;

    if (length > m_wideMaxLen)
    {
        if (m_wideString)
            delete[] m_wideString;
        m_wideMaxLen = length;
        m_wideString = new wchar_t[m_wideMaxLen + 1];
        m_wideString[0] = 0;
    }

    state = { 0 };
#ifdef USE_WINAPI
    mbsrtowcs_s(&length, m_wideString, length, &multibyte, multibyteLen, &state);
    length--; // do not include the null terminator.
#else
    const char* src = &(multibyte[0]);
    std::mbsrtowcs(m_wideString, (const char**)&src, multibyteLen, &state);
#endif
    m_wideString[length] = 0;
    return WideToUtf8(m_wideString, length, outLen);
}

const char* JsonReader::TextConverter::Utf8ToMultiByte(const char* utf8, const size_t utf8Len, size_t* outLen)
{
    if (!utf8)
        return nullptr;

    if (utf8Len == 0)
    {
        m_narrowString[0] = 0;
        return m_narrowString;
    }

    size_t length = 0;
    size_t wideLen = 0;

    const wchar_t* wideString = Utf8ToWide(utf8, utf8Len, &wideLen);

#ifdef USE_WINAPI
    wcstombs_s(&length, nullptr, 0, wideString, 0);
#else
    mbstate_t state = { 0 };
    length = std::wcsrtombs(nullptr, (const wchar_t**)&wideString, wideLen, &state);
#endif

    if (length == 0 || length == static_cast<size_t>(-1))
        return nullptr;

    if (length > m_narrowMaxLen)
    {
        if (m_narrowString)
            delete[] m_narrowString;
        m_narrowMaxLen = length;
        m_narrowString = new char[m_narrowMaxLen + 1];
        m_narrowString[0] = 0;
    }

#ifdef USE_WINAPI
    wcstombs_s(&length, m_narrowString, m_narrowMaxLen, wideString, length);
    length--; // do not include the null terminator.
#else
    state = { 0 };
    std::wcsrtombs(m_narrowString, (const wchar_t**)&wideString, length, &state);
#endif
    m_narrowString[length] = 0;
    if (outLen)
        (*outLen) = length - 1;
    return m_narrowString;
}

void JsonReader::TextConverter::Utf8ToMultiByte(const std::string utf8, std::string& str)
{
    const char* multibyte = Utf8ToMultiByte(utf8.c_str(), (int)utf8.length());
    if (multibyte)
        str = multibyte;
    else
        str.clear();
}

const char* JsonReader::TextConverter::WideToUtf8(const wchar_t* wide, const size_t wideLen, size_t* outLen)
{
    if (!wide)
        return nullptr;

    if (wideLen == 0)
    {
        m_narrowString[0] = 0;
        return m_narrowString;
    }

    size_t length = 0;
#ifdef USE_WINAPI
    length = WideCharToMultiByte(CP_UTF8, 0, wide, (int)wideLen, nullptr, 0, nullptr, nullptr);
#else
    auto p = reinterpret_cast<const wchar_t*>(wide);
    m_str = m_codecvt.to_bytes(p, p + wideLen);
    length = m_str.length();
#endif

    if (length == 0)
        return nullptr;

    if (length > m_narrowMaxLen)
    {
        if (m_narrowString)
            delete[] m_narrowString;
        m_narrowMaxLen = length;
        m_narrowString = new char[m_narrowMaxLen + 1];
    }

    if (outLen)
        (*outLen) = length - 1;

#ifdef USE_WINAPI
    WideCharToMultiByte(CP_UTF8, 0, wide, (int)wideLen, m_narrowString, (int)length, nullptr, nullptr);
    m_narrowString[length] = 0;
    return m_narrowString;
#else
    return m_str.c_str();
#endif
}

const wchar_t* JsonReader::TextConverter::Utf8ToWide(const char* utf8, const size_t utf8Len, size_t* outLen)
{
    if (!utf8)
        return nullptr;

    if (utf8Len == 0)
    {
        m_wideString[0] = 0;
        return m_wideString;
    }

    size_t length = 0;
#ifdef USE_WINAPI
    length = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)utf8Len, nullptr, 0);
#else
    m_wstr = m_codecvt.from_bytes(utf8);
    length = m_wstr.length();
#endif

    if (length == 0)
        return nullptr;

    if (length > m_wideMaxLen)
    {
        if (m_wideString)
            delete[] m_wideString;
        m_wideMaxLen = length;
        m_wideString = new wchar_t[m_wideMaxLen + 1];
        m_wideString[0] = 0;
    }

    if (outLen)
        (*outLen) = length - 1;

#ifdef USE_WINAPI
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, m_wideString, (int)length);
    m_wideString[length] = 0;
    return m_wideString;
#else
    return m_wstr.c_str();
#endif
}

void JsonReader::TextConverter::Utf8ToWide(const std::string utf8, std::wstring& wstr)
{
#ifdef USE_WINAPI
    const wchar_t* wide = nullptr;
    wide = Utf8ToWide(utf8.c_str(), utf8.length());
    if (wide)
        wstr = wide;
    else
        wstr.clear();
#else
    wstr = m_codecvt.from_bytes(utf8);
#endif
}

const char* JsonReader::TextConverter::CodePointToUtf8(const uint32_t codePoint, size_t& outLen)
{
#ifdef USE_WINAPI
    outLen = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)&codePoint, 2, NULL, 0, NULL, NULL);

    if (outLen == 0)
        return nullptr;

    if (outLen > m_narrowMaxLen)
    {
        if (m_narrowString)
            delete[] m_narrowString;
        m_narrowMaxLen = outLen;
        m_narrowString = new char[m_narrowMaxLen + 1];
        m_narrowString[0] = 0;
    }

    WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)&codePoint, 2, m_narrowString, (int)outLen, NULL, NULL);
    outLen--; // do not include the null terminator.
    return m_narrowString;
#else
    m_str = m_codecvt.to_bytes(codePoint);
    outLen = m_str.length();
    return m_str.c_str();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// struct JsonReader::STR

JsonReader::STR::STR()
{
    capacity = CAPACITY_DEFAULT;
    str = new char[capacity];
    setLength(0);
    isAscii = true;
    isQuoted = false;
    useLocale = false;
}

JsonReader::STR::STR(const wchar_t* source, bool useLocale) : STR()
{
    const char* utf8 = converter.WideToUtf8(source, wcslen(source));
    if (utf8)
        copy(utf8, (int)strlen(utf8), true);
    this->useLocale = useLocale;
}

void JsonReader::STR::copy(const char* source, size_t sourceLen, bool checkCapacity, bool checkEncoding)
{
    if (checkCapacity)
        resize(sourceLen);
    if (sourceLen > 0)
        memcpy((void*)str, source, sourceLen);
    setLength(sourceLen);
    isAscii = true;
    if (checkEncoding)
        for (size_t i = 0; i < sourceLen; i++)
            if (static_cast<unsigned char>(source[i]) > 0x7F)
            {
                isAscii = false;
            }
    isQuoted = false;
}

void JsonReader::STR::clear()
{
    if (length > 0)
        setLength(0);
    isAscii = true;
    isQuoted = false;
}

void JsonReader::STR::resize(size_t newCapacity)
{
    if (newCapacity > capacity)
    {
        capacity = newCapacity;
        char* newString = new char[capacity];
        if (str != nullptr)
        {
            memcpy(newString, str, length);
            newString[length] = 0;
            delete[] str;
        }
        str = newString;
    }
}

void JsonReader::STR::setLength(size_t newLength)
{
    if (newLength < capacity && str)
    {
        length = newLength;
        str[length] = 0;
    }
}

void JsonReader::STR::release()
{
    if (str != nullptr)
        delete[] str;
    str = nullptr;
}

const char* JsonReader::STR::toNarrow()
{
    if (length == 0 || isAscii || !useLocale)
        return str;
    return converter.Utf8ToMultiByte(str, length);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class JsonReader::JsonSource

JsonReader::JsonInput::JsonInput()
{
    m_idx = (size_t)-1;
    m_maxLen = 0;
    m_buffer = nullptr;
    m_isEOF = false;
    m_position = 0;
}

JsonReader::JsonInput::~JsonInput() { release(); }

void JsonReader::JsonInput::init(const char* source, bool isFile)
{
    if (isFile)
    {
        if (!openFile(source))
        {
            throwException("Cannot open file.");
        }
    }
    else
    {
        if (!setBuffer(source))
        {
            throwException("Cannot set buffer.");
        }
    }
}

void JsonReader::JsonInput::release()
{
    if (m_file.is_open())
    {
        m_file.close();
        if (m_buffer)
        {
            delete[] m_buffer;
            m_buffer = nullptr;
        }
    }

    m_idx = 0;
    m_maxLen = 0;
    m_position = 0;
    m_isEOF = false;
}

bool JsonReader::JsonInput::openFile(const char* fileFullPath)
{
    m_file.open(fileFullPath, std::ifstream::in | std::ios::binary);
    if (m_file.is_open())
    {
        m_buffer = new char[FILE_BUFFER];
        m_isEOF = false;
        fillBuffer();
        m_idx = (size_t)-1;
        return true;
    }
    return false;
}

bool JsonReader::JsonInput::setBuffer(const char* bufferUtf8)
{
    m_maxLen = strlen(bufferUtf8); // The input buffer must be null terminated.
    // Constness must be removed due to the assignment, but the content will not be modified.
    m_buffer = const_cast<char*>(bufferUtf8);
    m_idx = (size_t)-1;
    return true;
}

void JsonReader::JsonInput::fillBuffer()
{
    m_idx = 0;
    m_maxLen = 0;

    if (m_isEOF == true)
        throwException("Unexpected end of file.");

    if (m_file.is_open()) // The buffer is only refilled if the source is a file.
    {
        m_file.read(m_buffer, FILE_BUFFER);
        m_maxLen = (size_t)m_file.gcount();
    }
}

const char JsonReader::JsonInput::getNextChar(bool verbatim)
{
    do
    {
        if (++m_idx >= m_maxLen)
            fillBuffer();
        if (m_maxLen == 0)
        {
            m_isEOF = true;
            return 0;
        }
        m_position++;
    } while (!verbatim &&
        (m_buffer[m_idx] == ' ' || m_buffer[m_idx] == '\r' || m_buffer[m_idx] == '\n' || m_buffer[m_idx] == '\t' ||
            m_buffer[m_idx] == ':' || m_buffer[m_idx] == ',' || m_buffer[m_idx] == '\0'));
    return m_buffer[m_idx];
}

char JsonReader::JsonInput::charToHex(char input)
{
    if (input >= '0' && input <= '9')
        return (input - '0');
    else if (input >= 'a' && input <= 'f')
        return (input - 'a' + 10);
    else if (input >= 'A' && input <= 'F')
        return (input - 'A' + 10);
    else
        throwException("Invalid hex digit '%c'.", input);
    return 0;
}

void JsonReader::JsonInput::readEscapeSequence(STR& text)
{
    switch (getNextChar(true))
    {
    case '\"':
        text.str[text.length++] = '\"';
        break;
    case '\\':
        text.str[text.length++] = '\\';
        break;
    case '/':
        text.str[text.length++] = '/';
        break;
    case 'b':
        text.str[text.length++] = '\b';
        break;
    case 'f':
        text.str[text.length++] = '\f';
        break;
    case 'n':
        text.str[text.length++] = '\n';
        break;
    case 'r':
        text.str[text.length++] = '\r';
        break;
    case 't':
        text.str[text.length++] = '\t';
        break;
    case 'u':
        getEscapedCodePoint(text);
        break;
    default:
        throwException("Invalid escape sequence '\\%c'.", m_buffer[m_idx]);
    }
}

void JsonReader::JsonInput::getEscapedCodePoint(STR& text)
{
    uint16_t escapeSequence = 0; // Format: \uXXXX.
    for (int i = 0; i < 4; i++)
    {
        escapeSequence <<= 4;
        escapeSequence |= charToHex(getNextChar(true));
    }
    size_t utf8Len = 0;
    const char* utf8 = m_converter.CodePointToUtf8(escapeSequence, utf8Len);
    if ((text.length + utf8Len) >= text.capacity)
        text.resize((size_t)(text.length * RESIZE_FACTOR));
    memcpy(text.str + text.length, utf8, utf8Len);
    text.length += utf8Len;
    text.isAscii = false;
}

void JsonReader::JsonInput::goToNextQuote()
{
    while (m_buffer[m_idx] != '\"')
    {
        if (++m_idx == m_maxLen)
            fillBuffer();
        m_position++;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class JsonReader::Callback1

JsonReader::Callback1::Callback1(std::function<void(const char*)>& callback)
{
    m_funcNarrow = callback;
    m_narrow = true;
}

JsonReader::Callback1::Callback1(std::function<void(const wchar_t*)>& callback)
{
    m_funcWide = callback;
    m_narrow = false;
}

void JsonReader::Callback1::notify(STR* value)
{
    if (m_narrow)
    {
        const char* str = nullptr;
        if (value)
            str = value->toNarrow();
        m_funcNarrow(str);
    }
    else
    {
        const wchar_t* wstr = nullptr;
        if (value)
            wstr = value->toWide();
        m_funcWide(wstr);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class JsonReader::Publisher

JsonReader::Publisher::Publisher()
{
    m_callbackAll = nullptr;
    m_numSubscribersByName = 0;
    m_numSubscribersByPath = 0;
    m_name = nullptr;
    m_nameLen = 0;
}

void JsonReader::Publisher::subscribe(const wchar_t* element, Callback* callback)
{
    if (element)
    {
        // Convert input string to UTF-8.
        STR elementStr(element);
        subscribe(elementStr.toUtf8(), callback);
    }
    else
        subscribe((const char*)nullptr, callback);
}

void JsonReader::Publisher::subscribe(const char* elementUtf8, Callback* callback)
{
    if (!elementUtf8)
    {
        m_callbackAll = callback;
        return;
    }

    // Detect whether it's an element's name or path.
    bool isPath = false;
    size_t length = strlen(elementUtf8);
    for (size_t i = 0; i < length; i++)
    {
        if (elementUtf8[i] == '{' || elementUtf8[i] == '[')
        {
            isPath = true;
            break;
        }
    }

    // Set the map key as a copy of the element.
    char* key = new char[length + 1];
    if (length > 0)
        memcpy(key, elementUtf8, length);
    key[length] = 0;

    if (isPath)
        m_callbacksPath[key] = callback;
    else
        m_callbacksName[key] = callback;

    m_numSubscribersByName = m_callbacksName.size();
    m_numSubscribersByPath = m_callbacksPath.size();
}

void JsonReader::Publisher::unsubscribe()
{
    for (auto item : m_callbacksName)
    {
        delete[] item.first;
        delete item.second;
    }
    m_callbacksName.clear();
    for (auto item : m_callbacksPath)
    {
        delete[] item.first;
        delete item.second;
    }
    m_callbacksPath.clear();
    m_numSubscribersByName = m_numSubscribersByPath = 0;
    if (m_callbackAll)
    {
        delete m_callbackAll;
        m_callbackAll = nullptr;
    }
}

void JsonReader::Publisher::notify(char* path, size_t namePos, size_t nameLen, size_t pathLen, STR* value)
{
    m_name = path + namePos;
    m_nameLen = nameLen;

    if (m_numSubscribersByName) // notify by name.
    {
        // Extract the element's name from the path without copying memory.
        // This requires the temporary manipulation of the next character after the name.
        char backupChar = m_name[m_nameLen];
        m_name[m_nameLen] = 0;
        notify(&m_callbacksName, m_name, value);
        m_name[m_nameLen] = backupChar;
    }

    if (pathLen > 0 && m_numSubscribersByPath) // notify by path.
        notify(&m_callbacksPath, path, value);

    if (m_callbackAll) // notify on all elements.
        m_callbackAll->notify(value);
}

void JsonReader::Publisher::notify(CALLBACK_MAP* map, char* nameOrPath, STR* value)
{
    m_iterator = map->find(nameOrPath);
    if (m_iterator != map->end())
    {
        Callback* callback = m_iterator->second;
        if (callback)
            callback->notify(value);
    }
}

void JsonReader::Publisher::getCurrentElementName(STR& elemName) { elemName.copy(m_name, m_nameLen, true, true); }
