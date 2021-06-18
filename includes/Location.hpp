#ifndef LOCATION_HPP
#define LOCATION_HPP

#include <vector>
#include <string>

class Location {
    public:
        std::vector<std::string> method;
        std::string root;
        std::string index;
        bool auto_index;
        std::string cgi;
        std::string cgi_path;
        std::string max_body;
        std::string auth;
};

#endif