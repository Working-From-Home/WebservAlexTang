#include "Uri.hpp"

URI::URI(std::string uri){
	size_t i = 0;
	std::cout << "uri: " << uri << std::endl;
	std::cout << "uri.size(): " << uri.size() << std::endl;
	for (size_t j = 0; j < uri.size(); j++) {
		if (path.size() == 0 && (j + 1 == uri.size() || uri.at(j) == '?')) {
			path = uri.substr(i, j - i + (uri.at(j) == '?'? 0 : 1));
			i = j + 1;
		}
		if (path.size() && query.size() == 0 && (uri.at(j) == '#' || j + 1 == uri.size())) {
			query = uri.substr(i, j - i + (uri.at(j) == '#'? 0 : 1));
			parseQuery();
			if (j < uri.size() && uri.at(j) == '#')
				fragment = uri.substr(j, uri.size() - j);
			break;
		}
	}
	std::cout << "Path: " << path << std::endl;
	std::cout << "Query: " << query << std::endl;
	std::cout << "Fragment: " << fragment << std::endl;
}

void URI::parseQuery() {
	size_t j = 0;
	for (size_t i = 0; i < query.size(); i++) {
		if (query.at(i) == '='){
			queryName.push_back(query.substr(j, i - j));
			j = i + 1;
			while (i < query.size() && query.at(i) != '&')
				i++;
			queryValue.push_back(query.substr(j, i - j));
			if (i == query.size())
				break;
			i++;
			j = i;
		}
	}
	for (size_t i = 0 ;  i < queryName.size() ; i++) {
     	std::cout << queryName.at(i);
	 	std::cout << " = ";
	 	std::cout << queryValue.at(i);
  	 	std::cout << '\n';
	}
}