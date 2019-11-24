#pragma once

#include <codecvt>
#include <cstring>
#include <fstream>
#include <functional>
#include <locale>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>


#ifdef _MSC_VER
#define USE_WINAPI
#endif

// Reads JSON data and notifies its elements and values to the client.
class JsonReader
{
public:
    // Converts the encoding of the characters in a string.
    // This class is public so it can be used externally as JsonReader::TextConverter (note that it is not thread-safe).
    class TextConverter
    {
    public:
        TextConverter();
        ~TextConverter();

        // The following functions receive a string and its length and return the converted string (null terminated).
        // If not NULL, the output length is stored in the 'outLen' argument (not including the null terminator).
        // The term 'MultiByte' here refers to a non-Unicode charset, where characters can be encoded with one
        // or more bytes according to the current locale, such as CP1252 or GB18030.
        const char* MultiByteToUtf8(const char* multibyte, const size_t multibyteLen, size_t* outLen = nullptr);
        const char* Utf8ToMultiByte(const char* utf8, const size_t utf8Len, size_t* outLen = nullptr);
        void Utf8ToMultiByte(const std::string utf8, std::string& str);
        const char* WideToUtf8(const wchar_t* wide, const size_t wideLen, size_t* outLen = nullptr);
        const wchar_t* Utf8ToWide(const char* utf8, const size_t utf8Len, size_t* outLen = nullptr);
        void Utf8ToWide(const std::string utf8, std::wstring& wstr);
        // Encodes a 4-byte Unicode code point into UTF-8.
        const char* CodePointToUtf8(const uint32_t codePoint, size_t& outLen);

    protected:
        char* m_narrowString;  // Holds the last decoded narrow string.
        size_t m_narrowMaxLen; // Maximum length of the narrow string.
        wchar_t* m_wideString; // Holds the last decoded wide string.
        size_t m_wideMaxLen;   // Maximum length of the wide string.

#ifndef USE_WINAPI
        // NOTE: The <codecvt> header has been deprecated in C++17.
        // Used here until a standard replacement is available.
        std::wstring_convert<std::codecvt_utf8<wchar_t>> m_codecvt;
        // Auxiliary variables, made member by convenience:
        std::string m_str;
        std::wstring m_wstr;
#endif
    };

protected:
    // Represents a string.
    struct STR
    {
        STR();
        STR(const wchar_t* source, bool useLocale = false);
        ~STR() { release(); }

        // Methods
        void resize(size_t newCapacity);     // Allocs more memory for the string.
        void setLength(size_t newLength);    // Sets the current length of the string. It does not allocate memory.
        void clear();                        // Clears the string.
        void release();                      // Frees the memory allocated for the string.
        const char* toNarrow();              // It may return the string encoded as multibyte or UTF-8.
        const char* toUtf8() { return str; } // Returns the internal string as UTF-8.
        const wchar_t* toWide()              // Returns the internal string as a wide character string.
        {
            return converter.Utf8ToWide(str, length);
        }
        void copy(const char* source, size_t sourceLen, // Sets the content of the internal string.
            bool checkCapacity = false, bool checkEncoding = false);

        // Variables
        char* str;       // The string is internally stored as UTF-8.
        size_t length;   // Current length of the string.
        size_t capacity; // Number of allocated bytes to store the string.
        bool isAscii;    // True if the string contains ASCII characters exclusively.
        bool isQuoted;   // True if the string was enclosed between quotation marks in the JSON structure.
        bool useLocale;  // If true, the method 'toNarrow()' returns the string as multibyte according to the locale.

    private:
        static const int CAPACITY_DEFAULT = 1024; // Default amount of bytes available.
        TextConverter converter;                  // Used to convert character encodings.
    };

    // Stores the value of the current array item when an array is being parsed.
    struct ARRAY_ITEM
    {
        void setValue(STR* itemValue)
        {
            value = itemValue;
            isValue = true;
        }
        STR* getValue() { return isValue ? value : nullptr; }
        void clear()
        {
            value = nullptr;
            isValue = false;
        }
        STR* value;
        bool isValue; // True if the value is of type string, number, boolean or null.
    } m_arrayItem;

    // Encapsulates the JSON input source, which can be a file or a buffer.
    class JsonInput
    {
    public:
        JsonInput();
        virtual ~JsonInput();

        // If 'isFile' is true, 'source' is assumed to be the full path of a JSON file.
        // Otherwise, it's assumed to be a buffer and makes an internal copy.
        void init(const char* source, bool isFile);
        // Releases the internal buffer and closes the file if open.
        void release();

        // Returns the next character to process.
        // If 'verbatim' is true, returns exactly the next character (for example, while reading strings or numbers).
        // Otherwise, discards irrelevant characters such as spaces or new lines between elements.
        const char getNextChar(bool verbatim = false);
        // Returns the current character read.
        const char getCurrentChar() { return m_buffer[m_idx]; };
        // Called when an escape sequence is found.
        void readEscapeSequence(STR& text);
        // Moves the buffer's index one position back.
        void goToPreviousChar() { m_idx--; }
        // Moves the buffer's index forward until a quotation mark is found.
        void goToNextQuote();
        // Returns true if no more data can be read from the source.
        bool isEOF() { return m_isEOF; }
        // Returns the current absolute position in the input source.
        size_t getPosition() { return m_position; }

    protected:
        bool openFile(const char* fileFullPath);
        bool setBuffer(const char* bufferUtf8);
        void fillBuffer();
        void getEscapedCodePoint(STR& text);
        char charToHex(char input);

    protected:
        char* m_buffer;            // Buffer containing all or part of the input source.
        size_t m_maxLen;           // Number of bytes available in the buffer.
        size_t m_idx;              // Index of the current character in the buffer.
        size_t m_position;         // Absolute character position of the input data.
        std::ifstream m_file;      // The input file, in case we are reading from a file.
        bool m_isEOF;              // True when the end has been reached.
        TextConverter m_converter; // Used to decode Unicode code points.
    };

    // Wrappers of the client's callback target (function, lambda expression, etc.) that will receive the events.
    // A specialized class is used depending on the number of arguments sent.
    // -> Callback's base class.
    class Callback
    {
    public:
        Callback() {};
        virtual ~Callback() {};
        virtual void notify(STR* value) = 0; // Executes a client's callback, optionally passing one string.
    };
    // -> Callback without arguments.
    class Callback0 : public Callback
    {
    public:
        Callback0(std::function<void()>& callback) { m_func = callback; }
        void notify(STR*) { m_func(); }

    protected:
        std::function<void()> m_func;
    };
    // -> Callback receiving one string.
    class Callback1 : public Callback
    {
    public:
        Callback1(std::function<void(const char*)>& callback);
        Callback1(std::function<void(const wchar_t*)>& callback);
        void notify(STR* value);

    protected:
        bool m_narrow; // True if the string is to be sent as a narrow string (multibyte or UTF-8).
        std::function<void(const char*)> m_funcNarrow;  // The argument is passed as a narrow string.
        std::function<void(const wchar_t*)> m_funcWide; // The argument is passed as a wide string.
    };

    // Notifies the client about one type of event (new object found, new array found, etc.).
    class Publisher
    {
        struct comparer
        {
            bool operator()(char const* str1, char const* str2) const { return std::strcmp(str1, str2) < 0; }
        };

        // Map used to retrieve the callback to notify when a specified element is found.
        // The key is the name or path of the element.
        typedef std::map<const char*, Callback*, comparer> CALLBACK_MAP;

    public:
        Publisher();

        // Subscribes a callback to one type of event related to a specific element.
        void subscribe(const wchar_t* element, Callback* callback);
        void subscribe(const char* elementUtf8, Callback* callback);
        // Unsubscribes all callbacks related to one event type.
        void unsubscribe();
        // Looks for any callbacks associated to the name or path of the current element.
        void notify(char* path, size_t namePos, size_t nameLen, size_t pathLen, STR* value = nullptr);
        // Returns the name of the current element.
        void getCurrentElementName(STR& elemName);

    protected:
        // Finds a callback associated to the element described by 'nameOrPath' and, if found, calls it passing 'value'.
        void notify(CALLBACK_MAP* map, char* nameOrPath, STR* value);

    protected:
        CALLBACK_MAP m_callbacksName; // Callbacks associated to element names.
        CALLBACK_MAP m_callbacksPath; // Callbacks associated to element paths.
        Callback* m_callbackAll;      // Callback used to notify an event on all elements that apply.

        size_t m_numSubscribersByName; // Number of callbacks subscribed using the element's name.
        size_t m_numSubscribersByPath; // Number of callbacks subscribed using the element's path.

        char* m_name;     // Name of the current element being notified.
        size_t m_nameLen; // Length of the name of the current element being notified.

        CALLBACK_MAP::iterator m_iterator; // Auxiliary variable made member for convenience.
    };

public:
    // Main class declarations.

    JsonReader();
    ~JsonReader();

    // Methods to process the JSON structure from a file or a buffer.

    bool readFile(const char* fileFullPath);
    bool readBuffer(const char* bufferUtf8); // The buffer must be null terminated and encoded in UTF-8.

    // Methods to subscribe to event types related to specific JSON elements (object found, array found, etc.).

    // The argument 'element' determines the JSON element by its name or its path.
    // Paths are built appending the names and the opening curly and square brackets found from the root up to the
    // element (without quotes).
    // Additionally, object and array paths end with an opening curly and square bracket respectively.
    // For example, '{users[{id' targets the key 'id' contained in the object '{users[{' in the array '{users[' in the
    // root object '{'. To receive notifications about the array 'users' apply the name 'users' or the path '{users['.
    // However, paths must be used in case elements with the same name are found in different contexts in the JSON data.
    // Use NULL to subscribe to an event type for all elements (e.g. to get notified when any object is found).
    // The argument 'callback' is a callable object that will be triggered when the event on the target element happens.

    // Notifies that the definition of a new object has started.
    void onObjectBegin(const wchar_t* element, std::function<void()> callback);
    // Notifies that the definition of the current object has finished.
    void onObjectEnd(const wchar_t* element, std::function<void()> callback);
    // Notifies that the definition of a new array has started.
    void onArrayBegin(const wchar_t* element, std::function<void()> callback);
    // Notifies that the definition of the current array has finished.
    void onArrayEnd(const wchar_t* element, std::function<void()> callback);
    // Notifies that an array's item has been found, passing the item's value (if applies) as an argument.
    // This argument is a pointer to a string if the item is of type string, number or boolean. Otherwise it is NULL.
    void onArrayItem(const wchar_t* element, std::function<void(const char* value)> callback);    // Value as narrow.
    void onArrayItem(const wchar_t* element, std::function<void(const wchar_t* value)> callback); // Value as wide.
    // Notifies that a pair (key/value) has been found, passing the key's value as an argument.
    // This argument is a pointer to a string if the value is of type string, number or boolean. Otherwise it is NULL.
    void onPair(const wchar_t* element, std::function<void(const char* value)> callback);    // Value as narrow.
    void onPair(const wchar_t* element, std::function<void(const wchar_t* value)> callback); // Value as wide.
    // Same functions passing the 'element' argument encoded in UTF-8.
    void onObjectBegin(const char* elementUtf8, std::function<void()> callback);
    void onObjectEnd(const char* elementUtf8, std::function<void()> callback);
    void onArrayBegin(const char* elementUtf8, std::function<void()> callback);
    void onArrayEnd(const char* elementUtf8, std::function<void()> callback);
    void onArrayItem(const char* elementUtf8, std::function<void(const char*)> callback);
    void onArrayItem(const char* elementUtf8, std::function<void(const wchar_t*)> callback);
    void onPair(const char* elementUtf8, std::function<void(const char*)> callback);
    void onPair(const char* elementUtf8, std::function<void(const wchar_t*)> callback);

    // Methods that return a list of unique paths of all elements found in a JSON data.
    // They may help to find out the exact element's path in order to subscribe to its events.

    bool getPathsFromFile(const char* fileFullPath, std::set<std::wstring>& paths);
    bool getPathsFromBuffer(const char* bufferUtf8, std::set<std::wstring>& paths);

    // Methods that provide additional information from within the callback functions.

    // Return the current element's path.
    std::string getCurrentElementPath();
    std::wstring getCurrentElementPathWide();
    void getCurrentElementPath(std::string& elementPath);
    void getCurrentElementPathWide(std::wstring& elementPath);
    // Return the current element's name.
    std::string getCurrentElementName();
    std::wstring getCurrentElementNameWide();
    void getCurrentElementName(std::string& elementName);
    void getCurrentElementNameWide(std::wstring& elementName);
    // Returns true if the value passed to the callback function was originally quoted.
    // Used to distinguish strings from booleans and numbers (e.g. 123 and "123" are both passed as "123").
    bool isValueQuoted() { return m_elemValue.isQuoted; }
    // Returns true if the current path contains only ASCII characters.
    bool isPathAscii() { return m_path.isAscii; }
    // Returns true if the current array item is of type string, number, boolean or null.
    bool isArrayItemValue() { return m_arrayItem.isValue; }

    // Method to receive narrow string values in a non-Unicode multibyte encoding, such as CP1252 or GB18030.
    // This feature, which is turned off by default, does not apply when the values are notified as wide chars.
    // If 'useLocale' is true, 'locale' specifies the locale to use for encoding (make sure the locale is installed).
    void useLocale(bool useLocale, const char* locale = nullptr);

    // Methods to get a description when an error is found.

    std::string getErrorDescription() { return m_errDescription; }
    std::wstring getErrorDescriptionWide()
    {
        std::wstring wsTmp(m_errDescription.begin(), m_errDescription.end());
        return wsTmp;
    }

protected:
    // Reads a file or buffer containing the JSON data encoded in UTF-8.
    // If 'isFile' is true, 'source' is the full path of the input file. Otherwise, it's a pointer to a UTF-8 buffer.
    // The optional argument 'pathList' returns a list of unique paths of all the elements found.
    bool read(const char* source, bool isFile, std::set<std::wstring>* pathList = nullptr);

    // Methods to reset the state.
    void clear();
    void clearStrings();
    void unsubscribe(); // Removes all callbacks.

    // Methods used for parsing.
    void parseValue(size_t pathLen, ARRAY_ITEM* arrayItem = nullptr);
    void parseObject(size_t pathLen, size_t namePos, size_t elemNameLen);
    void parseArray(size_t pathLen, size_t namePos, size_t elemNameLen);
    void parseString(STR& text);
    void parseNumber(STR& number);
    bool parseTrue();
    bool parseFalse();
    bool parseNull();
    bool isNumericCharacter(char ch); // True if 'ch' may be part of a number (digit, decimal, sign...).
    void updateCurrentPath(size_t& pathLen); // Updates the length of the current path according to the context.

    // Notifies an event.
    // The argument 'publisher' determines the type of event (new object, new array...).
    // The arguments 'namePos', 'nameLen' and 'pathLen' describe the element that raised the event.
    // If applicable, the argument 'value' contains the string to be sent to the client. Otherwise it is NULL.
    void notify(Publisher* publisher, size_t namePos, size_t nameLen, size_t pathLen, STR* value = nullptr);

    // Throws a runtime exception from a variable argument list.
    static void throwException(const char* format, ...);

protected:
    // Represents the JSON input stream.
    JsonInput m_input;

    // Publishers used to notify one type of event (new object, new array, etc.) to their subscribed callbacks.
    Publisher m_onObjectBegin;
    Publisher m_onObjectEnd;
    Publisher m_onArrayBegin;
    Publisher m_onArrayEnd;
    Publisher m_onArrayItem;
    Publisher m_onPair;
    Publisher* m_currentPublisher; // Points to the publisher that is currently sending a notification.

    // Strings used during the parsing of the JSON data.
    STR m_elemName;        // Stores the name of the last element found.
    STR m_elemValue;       // Stores the value of the last value (string, number or boolean) found.
    STR m_path;            // Stores the path of the current element being parsed.
    STR m_currentElemName; // Auxiliary string that stores the name of the current element being notified.
                           // It is only updated when the client requests the current element's name.

    // If true, UTF-8 strings are provided in a localized non-Unicode multibyte encoding.
    bool m_useLocale;

    // Stores the description of the last error.
    std::string m_errDescription;
};
