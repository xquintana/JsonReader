# JsonReader

_JsonReader_ is a portable lightweight event-driven JSON data reader with a simple API written in C++.  
It is especially suitable for reading large amounts of JSON text, since data is not stored in memory.  
Extracting values using a callback-based approach instead of traversing the DOM tree should make the code more efficient and compact.  
It is based on the same idea as the [SAX](https://en.wikipedia.org/wiki/Simple_API_for_XML) XML parsers.


## The API

_JsonReader_ is implemented as a C++ class. The API provides a set of methods that associate a callback with an event related to a JSON element (for example, when a certain key is found):

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **onObjectBegin(** _element_ , _callback_ **);** // The definition of the object _element_ starts.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **onObjectEnd(** _element_ , _callback_ **);** // The definition of the object _element_ finishes.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **onArrayBegin(** _element_ , _callback_ **);** // The definition of the array _element_ starts.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **onArrayEnd(** _element_ , _callback_ **);** // The definition of the array _element_ finishes.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **onArrayItem**( _element_ , _callback_ **);** // An item of the array _element_ is found.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **onPair(** _element_ , _callback_ **);** // A key/value pair (where _element_ is the key) is found.  

+ **_element_** is the name or path of the element we want to receive the event from, passed as a UTF-8 or a wide character string. This argument can be null, meaning that we want to receive this event from all elements that apply (e.g. to get notified when any object is found).
+ **_callback_** represents the code we want to get called when that event on that element happens. It is of type [std::function](https://en.cppreference.com/w/cpp/utility/functional/function) so it can be any callable target such as a function or a lambda expression. The execution of the callback is synchronous, and therefore the next JSON element will not be processed until it is done.

See the [header file](JsonReader.h) for the actual C++ syntax.

Example:

```    
jsonReader.onArrayBegin("users", []()
{
    // Do something when the array 'users' has just been found.    
});
```

In case the name of the target element can exist in different contexts (e.g. keys with the same name in different objects) or the element has no name (e.g. an unnamed object), the element's path must be used instead. Paths uniquely locate elements in the JSON data. See [below](#using-the-elements-path) for an explanation about how paths are built.

The methods **onArrayItem** and **onPair** respectively supply the array item's value and the key's value to the callback as a string pointer, provided that the value is of type string, number or boolean. Otherwise this pointer is null.

Values can be received as:

+ UTF-8 strings.
+ Wide character strings.
+ Non-Unicode multibyte strings, encoded according to the current locale (CP1252, GB18030, etc.).

Example:
```    
jsonReader.onPair("id", [](const char* value)
{
    // Print the value of the key 'id'.
    if (value)
        std::cout << "id: " << value << std::endl;
});
```    

Once the callbacks have been defined, the next step is to start processing the JSON data. This is done by calling the methods **readFile** or **readBuffer** for reading from a file or a null terminated buffer:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bool **readFile(** const char* _fileFullPath_ **);**  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bool **readBuffer(** const char* _bufferUtf8_ **);**  

In either case, the JSON data must be encoded in UTF-8.  
The buffer is accessed directly, so it must not be modified until the process is finished.  
If a problem occurs while parsing the data, these methods return _false_ and a description of the error can be obtained by calling the method **getErrorDescription**.  

From inside the callback it is possible to get information about the current context through the following methods:

+ **getCurrentElementPath** and **getCurrentElementName**. If the callback was not associated to a specific element (by setting the first argument '_element_' to null, as explained before), these methods can be used to find out the JSON path or the name of the current element being notified. There are different versions available: returning narrow or wide strings, by reference or by value.
+ **isValueQuoted**. Returns _true_ if the value was quoted in the JSON data. Since values are notified as strings, this method allows to distinguish between, for example, reading the number 123 and the string "123", or reading the boolean _true_ and the string "true". Anyway, this distinction should not be necessary, as the type of the data passed to the callback is supposed to be known in advance.
+ **isPathAscii**. Returns _true_ if the current path contains only ASCII characters.
+ **isArrayItemValue**. Returns _true_ if the array item being notified is of type string, number, boolean or null. Therefore, it returns _false_ if this item is an object or an array.

In the following example, we get notified every time an item from any array is found. Then, we find out the name of each array and check if the current item is an actual value (string, number, boolean or null) or not (object or array).
```
// Set first argument to null to receive this event from any array.
// Capture the JSON reader in the lambda expression so we can use it.
jsonReader.onArrayItem((const char*)nullptr, [&jsonReader](const char* value)
{
    std::string arrayName = jsonReader.getCurrentElementName();
    bool isValue = jsonReader.isArrayItemValue();
    std::cout << "Array name: " << arrayName << std::endl;
    if (isValue)
    {
        if (value) 
        {
            // The array item is a string, a number or a boolean.
            // We could use the method 'isValueQuoted' to discriminate.
            std::cout << "Item is " << value << std::endl;
        }
        else // The array item is 'null'
        {
            std::cout << "Item is null" << std::endl;
        }
    }
    else
    {
        std::cout << "Item is an object or a nested array" << std::endl;
    }
});
```

If one or more callbacks handle the input values as narrow strings, these are passed as UTF-8 by default. However, by calling the method **useLocale**, values can be received as non-Unicode multibyte strings, according to the current locale: 

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;void **useLocale(** bool _useLocale_, const char* _locale_ **);**

The boolean _useLocale_ enables or disables the localization of the strings extracted from the JSON data. If _true_, the argument _locale_ specifies the identifier of the locale to use for the conversion. Please make sure that the proper identifier is used, and that the locale is installed in your computer.  
This method must be called before reading the JSON data.  
 
### Full Example

The following example reads and displays user data (name and identifier) from a set of key/value pairs and it also enumerates the colors contained in a list.

The method **onPair** is used to associate callbacks with the events of finding the keys '_name_' and '_id_'.  
In the same way, the method **onArrayItem** is used to set a callback that will be triggered for each item found in the '_colors_' array.  
These callbacks receive a string with the corresponding value.

```
// Input data
const char* json = R"s(
{
"users": [
    {
        "name": "Alice",
        "id": 1
    },
    {
        "name": "Bob",
        "id": 2
    },
    {
        "name": "Charlie",
        "id": 3
    }
],
"colors": [
    "red",
    "green",
    "blue"
]})s";

// Create an instance of the JSON reader.
JsonReader jsonReader;

// Associate callbacks with the following events:

// Find user names.
jsonReader.onPair("name", [](const char* value)
{
    std::cout << "User name: " << value << std::endl;
});
// Find user identifiers.
jsonReader.onPair("id", [](const char* value)
{
    std::cout << "User id: " << value << std::endl;
});    
// Find color names.
jsonReader.onArrayItem("colors", [](const char* value)
{
    std::cout << "Color: " << value << std::endl;
});

// Start processing the JSON data.
jsonReader.readBuffer(json);
```
The output is:
```
User name: Alice
User id: 1
User name: Bob
User id: 2
User name: Charlie
User id: 3
Color: red
Color: green
Color: blue
```

By capturing variables in the lambda expressions you can do more interesting things such as storing the received values in member variables.  
See another example [here](Sample.cpp).

### Using the element's path

As mentioned before, paths uniquely locate elements in the JSON data, and should be employed when the use of the element's name may lead to ambiguity.  
In this implementation, paths are built by appending the names and the opening curly and square brackets found from the root up to the element (without quotes).   
Additionally, object and array paths must end with an opening curly or square bracket, respectively.

In the previous example, the path of the key '_id_' is `{users[{id` because, beginning from the root object '_{_' (with path `{` ), we find the array '_users_' (path is now `{users[` ), and then an unnamed object '_{_' (path is now `{users[{` ) to finally reach the key '_id_' (final path is `{users[{id` ). Supposing that another key named '_id_' was also used in a different context, the ambiguity would be solved by using its path:

```
jsonReader.onPair("{users[{id", [](const char* value)
{
    std::cout << "User id: " << value << std::endl;
});
```

In order to help finding out the path of an element, the following methods are provided:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bool **getPathsFromFile(** const char* _fileFullPath_, std::set`<std::wstring>`& _paths_ **);**  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bool **getPathsFromBuffer(** const char* _bufferUtf8_, std::set`<std::wstring>`& _paths_ **);**

These return the unique paths of all elements found in the JSON data contained in a file or UTF-8 buffer. 
In the previous example, they would return the following paths:
```
{            // The root object.
{colors[     // The array 'colors'.
{users[      // The array 'users'.
{users[{     // An unnamed object contained in the 'users' array.
{users[{id   // The user's key 'id'.
{users[{name // The user's key 'name'.
```
Once the needed paths have been found out, the call to these methods can be removed.

## Also, a text encoding converter

The class _JsonReader_ uses a nested class named _TextConverter_ to convert text to different encodings:

* Multibyte string to UTF-8 string.
* UTF-8 string to Multibyte string.
* Wide character string to UTF-8 string.
* UTF-8 string to Wide character string.
* Unicode 4-bytes code point to UTF-8 character.

The term 'multibyte' refers here to a non-Unicode multibyte string encoded according to the current locale.  

This class is public so it can be used externally and even independently of the main class.  

Example:  
```
// Convert a wide string to UTF-8.
JsonReader::TextConverter converter;    
std::wstring input = L"café";
std::string output = converter.WideToUtf8(input.c_str(), input.length());

for (const unsigned char &ch : output)
    printf("0x%02X ", ch); // UTF-8 output is 0x63 0x61 0x66 0xC3 0xA9 ('cafÃ©')
```

See the [header file](JsonReader.h) for more details.


## Constraints

* JSON data must be encoded in UTF-8.
* The compiler must support C++ 11.
* This code is not thread-safe.  


## Portability

It has been tested on Windows 10 (Visual Studio 2017), Ubuntu 14.04 LTS (g++ 5.5.0) and Mint 19.2 (g++ 7.4.0).  

Notes on Linux:

* Use _g++_ version 5.5 or later, and compile with the flag _-std=c++11_.
* The _codecvt_ header is used to perform the text encoding. This header has been deprecated from C++17, but there is no standard replacement yet. If this is an issue, you can replace it with your own code or with a third party library such as [Boost](https://www.boost.org/).


## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

