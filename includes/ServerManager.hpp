#ifndef SERVERMANAGER_HPP
# define SERVERMANAGER_HPP

#include "Server.hpp"
#include "Location.hpp"
#include "Request.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include <iostream>
#include <list>

struct		clientInfo
{
	int						fd;
	socklen_t				addr_size;
	struct sockaddr_storage	addr;
	Server					server;
};


class 	ServerManager
{
public:

	ServerManager(void);
	~ServerManager(void);

	void	add_server(Server &server);
	void	start_servers(void);

private:

	std::vector<Server>			servers;
	std::vector<pollfd> 		pfds;
	std::list<clientInfo>		clients;

	void		add_to_pfds(int newfd);
	void		del_from_pfds(int i);
	int			is_server_socket(int fd);
	int			get_index_server(int fd);
	clientInfo	getClientByFd(int fd);
	void		handleNewConnexion(int fd);
	int 		create_server_socket(Server &server);
};

#endif