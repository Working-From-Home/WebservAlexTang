#include "Reponse.hpp"

Reponse::Reponse(Request r, Server s, int cfd): request(r), locations(s.locations), clientfd(cfd), server(s) {
	std::string tmpUri = request.uri;

	autoindex = 0;
	if (tmpUri.size() > 1 && tmpUri[tmpUri.size() - 1] == '/')
		tmpUri.erase(--tmpUri.end());
	while (tmpUri.size()) {
		for (std::map<std::string, Location>::iterator it = locations.begin() ; it != locations.end(); ++it)
			if (tmpUri == it->first){
				makeReponse(request, it->second, tmpUri);
				return ;
			}
		tmpUri.erase(--tmpUri.end());
		while (tmpUri.size() && tmpUri[tmpUri.size() - 1] != '/')
			tmpUri.erase(--tmpUri.end());
		if (tmpUri.size() > 1)
			tmpUri.erase(--tmpUri.end());
	}
	//std::cerr << "No location" << std::endl;
	std::map<std::string, std::string> info;

	info["Content-Type"] = "text/html";
	methodError(info, 403);
}

void Reponse::makeReponse(Request request, Location location, std::string tmpUri){
	std::map<std::string, std::string> info;
	std::string body;
	struct stat buf;

	if (location.redirection.first)
		return getRedirection(location);
	info["Content-Type"] = "text/html";
	header = "HTTP/1.1 200 OK\n";
	if (request.uri.size() > 2 && request.uri[request.uri.size() - 1] == '/')
		request.uri.erase(request.uri.end());
	URI uri(request.uri, tmpUri, location.root);
	if (tmpUri == request.uri) {
		info["path"] = location.root;
		if (location.auto_index)
			autoindex = 1;
		else {
			info["path"] += "/";
			info["path"] += location.index;
		}
	}
	else{
		info["path"] = location.root + "/" + request.uri.substr(tmpUri.size(), request.uri.size());
		info["path"] =  info["path"].substr(0, info["path"].find_first_of('?'));
	}
	if (!autoindex && (stat(info["path"].c_str(), &buf)) == 0){
		if (location.auto_index && S_ISDIR(buf.st_mode)){
			autoindex = 1;
		}
	}
	if (!acceptedMethod(request.method, location.method))
		return methodError(info, 405);
	if (CGIcapacity(uri.path, location)){
		CGI cgi(location.cgi_path, info["path"], request, clientfd, server, tmpUri, uri);
		body = methodCGI(cgi, location.cgi_path, uri);
	}
	if (request.method == "GET")
		methodGet(info, body, location.max_size, request.uri);
	else if (request.method == "DELETE")
		methodDelete(info);
	else if (request.method == "POST")
		methodPOST(info, request, location.max_size, body);
}

int Reponse::acceptedMethod(std::string requestMethod, std::vector<std::string> locationsMethod){
	for (size_t i = 0 ; i < locationsMethod.size(); ++i){
    	if (requestMethod == locationsMethod[i])
			return 1;
	}
	return 0;
}

void Reponse::methodGet(std::map<std::string, std::string> info, std::string body, std::string max_size, std::string uri){ 
	int bodysize;

	info["Content-Type"] = getMIMEType(info["path"]);
	if (autoindex == 1 || body.size() == 0) {
		try {
			if (autoindex == 0)
				body = readFile(info["path"]);
			else {
				body = directory_contents(info["path"].c_str(), uri);
				info["Content-Type"] = "text/html";
			}
		}
		catch (const std::exception &e){
			methodError(info, 404);
		}
		header += "Content-Type: ";
		header += info["Content-Type"];
		header += "\nContent-Length: ";
		char buffer[1000];
		sprintf(buffer, "%lu", body.size());
		header += buffer;
		header += "\n\n";
		header += body;
		bodysize = body.size();
	}
	else
		bodysize = readBodyCGI(body).size();
	if (max_size.size() && bodysize > atoi(max_size.c_str()))
		methodError(info, 413);
	//printResponse();
}

void Reponse::methodPOST(std::map<std::string, std::string> info, Request request, std::string max_size, std::string body){
	struct stat buf;

	if (max_size.size() && (int)request.body.size() > atoi(max_size.c_str())) {
		methodError(info, 413);
		return ;
	}
	if (body.size() != 0)
		request.body = readBodyCGI(body);
	if (max_size.size() && (int)request.body.size() > atoi(max_size.c_str())){
		methodError(info, 413);
		return ;
	}
	if (request.headers["Content-Type"].substr(0, 19) == "multipart/form-data")
		multiPartUpload(request, info);
	else if ((stat(info["path"].c_str(), &buf)) == 0){
		if (S_ISREG(buf.st_mode)){
			int fd = open(info["path"].c_str(), O_WRONLY | O_TRUNC, 0644);
			write(fd, request.body.c_str(), request.body.size());
			close(fd);
			header += "Content-Type: ";
			header += info["Content-Type"];
			header += "\nContent-Length: 0\n\n";
		}
		else 
			methodError(info, 500);
	}
	else {
		int fd = open(info["path"].c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
		write(fd, request.body.c_str(), request.body.size());
		close(fd);
		header = "HTTP/1.1 201 Created\n";
		header += "Content-Type: ";
		header += info["Content-Type"];
		header += "\nContent-Length: 0\n\n";
	}
}

void Reponse::methodDelete(std::map<std::string, std::string> info) {
	struct stat buf;

	if ((stat(info["path"].c_str(), &buf)) == 0 && S_ISREG(buf.st_mode)) {
		unlink(info["path"].c_str());
		info["Content-Type"] = getMIMEType(info["path"]);
		header += "Content-Type: ";
		header += info["Content-Type"];
		header += "\nContent-Length: 0\n\n";
	}
	else
		methodError(info, 404);
}

std::string Reponse::methodCGI(CGI cgi, std::string path, URI uri) {
	pid_t	pid;
	char	buffer[65536] = {0};
	int		realin = dup(STDIN_FILENO);
	int		realout = dup(STDOUT_FILENO);
	FILE *filefdin = tmpfile();
	FILE *filefdout = tmpfile();
	long	fd[2] = {fileno(filefdin), fileno(filefdout)};
	std::string cgibody;
	std::string body;
	
	char ** argv = doArgv(path, uri);
	if (request.body.size()){
		cgi.body = request.body;
	}
	else
		cgi.cgi_body(argv[1]);
	write(fd[0], cgi.body.c_str(), cgi.body.size());
	lseek(fd[0], 0, SEEK_SET);
	pid = fork();
	if (pid == 0) {
		dup2(fd[0], STDIN_FILENO);
		dup2(fd[1], STDOUT_FILENO);
		execve(argv[0], argv, cgi.headerCGI(cgi.body, argv));
		exit(0);
	}
	else {
		waitpid(-1, NULL, 0);
		lseek(fd[1], 0, SEEK_SET);
		while ((read(fd[1], buffer, 65536 - 1))) {
			body += buffer;
			memset(buffer, 0, 65536);
		}
		dup2(realin, STDIN_FILENO);
		dup2(realout, STDOUT_FILENO);
	}
	close(realin);
	close(realout);
	fclose(filefdin);
	fclose(filefdout);
	close(fd[0]);
	close(fd[1]);
	for (size_t i = 0; argv[i]; i++)
		free(argv[i]);
	free(argv);
	return body;
}

std::string Reponse::readBodyCGI(std::string body){
	size_t i = 0;
	for(; i < body.size();) {
		if (body.substr(i, 14) == "Content-Type: " || body.substr(i, 14) == "Content-type: ") {
			int j = 0;
			while (body[i + j] && (body[i + j] != ';' && body[i + j] != '\n'))
				j++;
			header += body.substr(i, j);
			while (body[i] != '\r')
				i++;
			while (body[i] == '\r' || body[i] == '\n' )
				i++;
			break ;
		}
		if (body.substr(i, 8) == "Status: ") {
			while (body[i] != '\n')
				i++;
			i++;
		}
		else
			i++;
	}
	header += "\nContent-Length: ";
	char buffer[1000];
	sprintf(buffer, "%lu", body.substr(i, body.size() - i).size());
	header += buffer;
	header += "\n\n";
	header += body.substr(i, body.size() - i);
	return body.substr(i, body.size() - i);
}

std::string Reponse::bodyError(std::string oldBody, int code) {
	size_t start_pos = 0;
	std::string value = "$1";
	char buffer[1000];
	sprintf(buffer, "%d", code);
	std::string tmp = buffer;

	while((start_pos = oldBody.find(value, start_pos)) != std::string::npos) {
		oldBody.replace(start_pos, value.length(), tmp);
		start_pos += tmp.length();
	}
	start_pos = 0;
	value = "$2";
	tmp = getMessage(code);
	while((start_pos = oldBody.find(value, start_pos)) != std::string::npos) {
		oldBody.replace(start_pos, value.length(), tmp);
		start_pos += tmp.length();
	}
	return (oldBody);
}

void Reponse::methodError(std::map<std::string, std::string> info, int code) {
	std::string body;

	char buffer[1000];
	sprintf(buffer, "%d", code);
	header = "HTTP/1.1 ";
	header += buffer;
	header += " " + getMessage(code);
	header += "Content-Type: ";
	header += info["Content-Type"];
	header += "\nContent-Length: ";
	body = bodyError(readFile("./www/error.html"), code);
	sprintf(buffer, "%lu", body.size());
	header += buffer;
	header += "\n\n";
	header += body;
}

std::string Reponse::getMessage(size_t code) {
	std::map<std::size_t, std::string> message;

	message[100] = "Continue";
	message[101] = "Switching Protocols";
	message[200] = "OK";
	message[201] = "Created";
	message[202] = "Accepted";
	message[203] = "Non-Authoritative Information";
	message[204] = "No Content";
	message[205] = "Reset Content";
	message[206] = "Partial Content";
	message[300] = "Multiple Choices";
	message[301] = "Moved Permanently";
	message[302] = "Found";
	message[303] = "See Other";
	message[304] = "Not Modified";
	message[305] = "Use Proxy";
	message[307] = "Temporary Redirect";
	message[308] = "Permanent Redirect";
	message[400] = "Bad Request";
	message[401] = "Unauthorized";
	message[402] = "Payment Required";
	message[403] = "Forbidden";
	message[404] = "Not Found";
	message[405] = "Method Not Allowed";
	message[406] = "Not Acceptable";
	message[407] = "Proxy Authentication Required";
	message[408] = "Request Timeout";
	message[409] = "Conflict";
	message[410] = "Gone";
	message[411] = "Length Required";
	message[412] = "Precondition Failed";
	message[413] = "Payload Too Large";
	message[414] = "URI Too Long";
	message[415] = "Unsupported Media Type";
	message[416] = "Range Not Satisfiable";
	message[417] = "Expectation Failed";
	message[426] = "Upgrade Required";
	message[500] = "Internal Server Error";
	message[501] = "Not Implemented";
	message[502] = "Bad Gateway";
	message[503] = "Service Unavailable";
	message[504] = "Gateway Timeout";
	message[505] = "HTTP Version Not Supported";
	return message[code];
}

std::string Reponse::readFile(std::string file) {
	char buffer[256 + 1] = {0};
	int fd;
	int res;
	std::string result;

	if((fd = open(file.c_str(), O_RDONLY)) < -1){
		throw std::out_of_range("The file does not exists.");
	}
	while ((res = read(fd, buffer, 256)) > 0)
		for (size_t j = 0; j < (size_t)res; ++j)
			result += buffer[j];
	close(fd);
	if (res < 0)
		throw std::out_of_range("Error while reading.");
	return (result);
}

bool Reponse::CGIcapacity(std::string path, Location location) {
	std::string tmpCGI;

	if (location.cgi_path.size())
		for (size_t i = path.size() - 1; i > 0 ; i--)
			if(path[i] == '.') {
				tmpCGI = path.substr(i, path.size() - 1);
				return (tmpCGI == location.cgi ? true : false);
			}
	return false;
}

void Reponse::printResponse(){
	std::cout << "\033[32m" << "Response--->" << std::endl;
	std::cout << header << std::endl;
	std::cout << "------------" << "\033[0m" << std::endl;
}

std::string		Reponse::directory_contents(const char *directory_path, std::string tmpUri) {
	DIR				*dh;
	struct dirent	*contents;
	std::string		pathString(directory_path);
	std::string		finalResult;
	struct stat buf;

	tmpUri =  tmpUri.substr(0, tmpUri.find_first_of('?'));
	dh = opendir(directory_path);
	if (tmpUri != "/")
		tmpUri += "/";
	if (!dh){
		return (NULL);
	}
	finalResult += "<head>\n	<style>\n		@import url('https://fonts.googleapis.com/css2?family=Quicksand&display=swap');\n";
	finalResult += "		html {\n			font-family: 'Quicksand';\n		}\n		body {\n			text-align:left;\n";
	finalResult += "			margin-top: 2%;\n		}\n		h1, h2 {\n			font-weight: 400;\n			margin: 0;\n		}\n";
	finalResult += "		h1 > span {\n			font-weight: 900;\n		}\n		a {\n			padding-left: 10;\n		}\n";
	finalResult += "		h1 {\n			margin-bottom: -30;\n		}\n	</style>\n";
	finalResult += "	<meta charset=\"UTF-8\">\n";
	finalResult += "	<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
	finalResult += "	<title>WebServ - Auto--Index</title>\n";
	finalResult += "</head>\n<body>\n";
	finalResult += "	<h1>	Auto-Index</h1> <h1>_______________<br /><br /></h1>\n";
	finalResult += "	<h2>Index of " + tmpUri + "</h2>\n";
	while ((contents = readdir(dh)) != NULL) {
		std::string name = contents->d_name;
		std::string line;
		if (name == "..")
			line = "	<a href=\"." + tmpUri + name + "\">" + "Parent Directory/" + "</a><br />\n";
		else if (name == ".")
			continue ;
		else {
			if ((stat((std::string(directory_path) + "/" + name).c_str(), &buf)) == 0 && S_ISDIR(buf.st_mode))
				line = "	<a href=\"." + tmpUri + name + "\">" + name + "/</a><br />\n";
			else
				line = "	<a href=\"." + tmpUri + name + "\">" + name + "</a><br />\n";
		}
		finalResult += line;
	}
	closedir(dh);
	finalResult += "</body>\n</html>\n";
	return (finalResult);
}

void Reponse::getRedirection(Location location){
	if (location.redirection.first == 301 || location.redirection.first == 302 || location.redirection.first == 303 ||
		location.redirection.first == 307 || location.redirection.first == 308){
		char buffer[1000];
		sprintf(buffer, "%d", location.redirection.first);
		header += "HTTP/1.1 ";
		header += buffer;
		header += " " + getMessage(location.redirection.first) + "\n";
		header += "Location: " + location.redirection.second + "\n";
		header += "Content-Type: text/html; charset=UTF-8\n\n";
		header += "<HTML>\n<HEAD>\n	<meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n";
		header += "	<TITLE>Moved</TITLE>\n</HEAD>\n<BODY>\n";
		sprintf(buffer, "%d", location.redirection.first);
		header += "	<H1>";
		header += buffer;
		header += " Moved</H1>\n";
		header += "	You're going elsewhere\n";
		header += "	<A HREF=\"" + location.redirection.second + "\">here</A>.\n";
		header += "</BODY>\n</HTML>\n";
	}
	else if (location.redirection.second.size() == 0){
		std::map<std::string, std::string> info;
		info["Content-Type"] = "text/html";
		methodError(info, location.redirection.first);
	}
	else {
		char buffer[1000];
		sprintf(buffer, "%d", location.redirection.first);
		header += "HTTP/1.1 ";
		header += buffer;
		header += " " + getMessage(location.redirection.first) + "\n";
		header += "Content-Type: text/html; charset=UTF-8\n";
		sprintf(buffer, "%lu", location.redirection.second.size());
		header += "Content-Length: ";
		header += buffer;
		header += "\n\n";
		header += location.redirection.second;
	}
	//printResponse();
}

std::vector<std::string>	splitDeux(std::string str, std::string delimiter)
{
	std::vector<std::string>	vec;
	size_t						pos = 0;
	std::string					token;

	while ((pos = str.find(delimiter)) != std::string::npos)
	{
		token = str.substr(0, pos);
		vec.push_back(token);
		str.erase(0, pos + delimiter.length());
	}
	if (str.size() > 0)
		vec.push_back(str);
	return (vec);
}

void Reponse::multiPartUpload(Request request, std::map<std::string, std::string> info){
	size_t i = request.headers["Content-Type"].find("boundary=") + 9;
	std::string nameboundary = request.headers["Content-Type"].substr(i, request.headers["Content-Type"].size() - i);
	std::vector<std::string> body = splitDeux(request.body,"\n");
	for (i = 0; i < request.body.size();){
		if (body[i].find(nameboundary) != std::string::npos){
			if (body[i][body[i].find(nameboundary) + nameboundary.size() + 1] == '-')
				break;
			i++;
			size_t j = body[i].find("filename=");
			if (j == std::string::npos)
				while (i < body.size() && body[i].find(nameboundary) == std::string::npos)
					i++;
			else {
				std::string filename = body[i].substr(j + 10, body[i].find_last_of("\"") - (j + 10));
				if (filename.size() == 0)
					while (i < body.size() && body[i].find(nameboundary) == std::string::npos)
						i++;
				else {
					std::string path = info["path"] + "/upload/" + filename;
					while(body[i].size() != 1) {
						if (body[i].find("Content-Type: image/jpeg") != std::string::npos)
							return methodError(info, 415);
						i++;
					}
					i++;
					struct stat buf;
					int fd;
					if ((stat(path.c_str(), &buf)) == 0){
						if (S_ISREG(buf.st_mode))
							fd = open(path.c_str(), O_WRONLY | O_TRUNC, S_IRWXU);
						else
							return methodError(info, 415);
					}
					else
						fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT, S_IRWXU);
					while (i < request.body.size() && body[i].find(nameboundary) == std::string::npos){
						write(fd, body[i].c_str(), body[i].size());
						write(fd, "\n", 1);
						i++;
					}
					close(fd);
				}
			}

		}
		else 
			break ;
	}
	header = "HTTP/1.1 201 Created\n";
	header += "Content-Type: ";
	header += info["Content-Type"];
	header += "\nContent-Length: 0\n\n";
}