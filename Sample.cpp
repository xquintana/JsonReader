/*
    Example of usage of the JsonReader class:
    - Shows the paths of all JSON elements.
    - Extracts data from an array composed by objects (users).
    - Extracts data from an array composed by strings (colors).
    - Enumerates the names of all arrays found in the JSON data.
*/

#include "JsonReader.h"
#include <iostream>
#include <vector>

// JSON data (UTF-8).
const char* data = R"s(
    {
    "data": {
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
        ]
    }
})s";

int main()
{
    // User info includes a name and an identifier.
    struct USER
    {
        std::string name;
        int id;
    } userInfo;

    std::vector<USER> users;             // List of users.
    std::vector<std::wstring> colors;    // List of colors, stored as wide character strings.
    std::vector<std::string> arrayNames; // Name of the arrays found in the JSON structure.
    std::set<std::wstring> jsonPaths;    // Unique JSON paths found.
    JsonReader reader;                   // The JSON reader instance.

    // For demo purposes, first print the unique paths of all JSON elements.
    // Paths are built by appending the names and the opening curly and square brackets found from the root up to the
    // element (without quotes).
    // Additionally, object and array paths must end with an opening curly or square bracket, respectively.

    reader.getPathsFromBuffer(data, jsonPaths);
    std::cout << "Element's Unique Paths:" << std::endl;
    for (const auto& path : jsonPaths)
        std::wcout << L"\t" << path << std::endl;

    // Extract the data.
    // For simplicity, input arguments are not checked for null.

    // Get user names.
    reader.onPair("name", [&userInfo](const char* name)
    {
        userInfo.name = name;
    });
    // Get user identifiers.
    reader.onPair("id", [&userInfo](const char* id)
    {
        userInfo.id = std::atoi(id);
    });
    // Called once a new user has been parsed.
    reader.onArrayItem("users", [&users, &userInfo](const char*)
    {
        users.push_back(userInfo); // Store user info.
    });
    // Read the colors as wide char strings using the path of the 'colors' array instead of its name.
    // Note that the path ends with an opening square bracket since it locates an array.
    reader.onArrayItem("{data{colors[", [&colors](const wchar_t* color)
    {
        colors.push_back(color);
    });
    // Get all array names.
    reader.onArrayBegin((const char*)nullptr, [&arrayNames, &reader]()
    {
        arrayNames.push_back(reader.getCurrentElementName());
    });

    // Once the callbacks have been set, start processing the JSON data.
    // For simplicity, the returned value is not checked.
    reader.readBuffer(data);

    // Print the user info.
    std::cout << "Users:" << std::endl;
    for (const auto& user : users)
        std::cout << "\tname: " << user.name << "\t - id: " << user.id << std::endl;

    // Print the color names.
    std::cout << "Colors:" << std::endl;
    for (const auto& color : colors)
        std::wcout << L"\t" << color << std::endl;

    // Print the name of the arrays found.
    std::cout << "Arrays:" << std::endl;
    for (const auto& name : arrayNames)
        std::cout << "\t" << name << std::endl;

    std::cout << std::endl;
    return 0;
}

/**************  OUTPUT  ************************

    Element's Unique Paths:
            {
            {data{
            {data{colors[
            {data{users[
            {data{users[{
            {data{users[{id
            {data{users[{name
    Users:
            name: Alice      - id: 1
            name: Bob        - id: 2
            name: Charlie    - id: 3
    Colors:
            red
            green
            blue
    Arrays:
            users
            colors

*************************************************/
