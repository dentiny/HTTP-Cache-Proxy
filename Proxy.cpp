#include "ProxyException.hpp"
#include "Parser.hpp"
#include "Logger.hpp"
#include "Request.hpp"
#include "Response.hpp"
#include "LRUCache.hpp"
#include <thread>
#include <string>
#include <vector>
#include <climits>
#include <netdb.h>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <iostream>
#include <exception>
#include <algorithm>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define BACKLOG 100
#define CACHE_SIZE 500
#define BUFFER_SIZE 65536

class Proxy
{
private:
	Parser parser; // has-a relationship
	Logger logger; // has-a relationship
	LRUCache cache; // has-a relationship
	const char * listen_port = "5555"; // listern port
	int status; // global status to mark success or not
	int socket_fd;

	// construct a proxy, listen on port 5555
	// called in the constructor, exception cannot happen here
	// if an error happens, release all allocated resource and exit the process
	void constructServer()
    {
        // initialize host information
        struct addrinfo host_info;
    	struct addrinfo * host_info_list;
        memset(&host_info, 0, sizeof(host_info));
        host_info.ai_family = AF_UNSPEC;
        host_info.ai_socktype = SOCK_STREAM;
        host_info.ai_flags = AI_PASSIVE;

        // get address information
        status = getaddrinfo(NULL, listen_port, &host_info, &host_info_list);
        if(status != 0)
        {
            std::cerr << "Construct server getaddrinfo error" << std::endl;
            exit(EXIT_FAILURE);
        }

        // create a socket file descriptor
        socket_fd = socket(host_info_list->ai_family,
                        host_info_list->ai_socktype,
                        host_info_list->ai_protocol);
        if(socket_fd == -1) 
        {
        	freeaddrinfo(host_info_list);
            std::cerr << "Construct server create socket error" << std::endl;
            exit(EXIT_FAILURE);
        }

        // set socket option
        int yes = 1;
        status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if(status == -1)
        {
        	freeaddrinfo(host_info_list);
        	close(socket_fd);
            std::cerr << "Construct server set socket option error" << std::endl;
            exit(EXIT_FAILURE);
        }

        // bind
        status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
        if(status == -1) 
        {
        	freeaddrinfo(host_info_list);
        	close(socket_fd);
            std::cerr << "Construct server cannot bind socket" << std::endl;
            exit(EXIT_FAILURE);
        }

        // listen
        status = listen(socket_fd, BACKLOG);
        if(status == -1)
        {
        	freeaddrinfo(host_info_list);
        	close(socket_fd);
            std::cerr << "Construct server listen error" << std::endl;
            exit(EXIT_FAILURE);
        }

        // free allocated host_info_list
        freeaddrinfo(host_info_list);
    }

    // blocking status, wait until a client request arrives
    // return client_fd if success, return -1 if error
    int acceptConnection(std::string & client_ip)
    {
    	// keep waiting until a client request
        struct sockaddr_storage socket_addr;
        socklen_t socket_addr_len = sizeof(socket_addr);
        int accept_fd = accept(socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len); // accept() implements blocking accept
        if(accept_fd == -1) 
        {
            return -1;
        }

        // get the client ip
        struct sockaddr_in * temp = (struct sockaddr_in *)&socket_addr;
  		client_ip = inet_ntoa(temp->sin_addr);

        return accept_fd;
    }

	// accept HTTP request
	Request acceptRequest(int fd)
	{
		// receive port number of the current player
		std::vector<char> buffer(BUFFER_SIZE, '\0');
	    int len = recv(fd, &buffer.data()[0], BUFFER_SIZE, 0);
	    if(len == -1)
	    {
	        throw ProxyException("In acceptRequest(), server receive request error");
	    }
	    buffer[len] = '\0';

	    // get current time for request
	    time_t cur = time(NULL);
	    tm * tm = localtime(&cur);
	    char * dt = asctime(tm);
	    std::string cur_time(dt);

	    // extract HTTP action and url from request
	    Request request(cur_time, buffer, len);
	    parser.parseRequest(request);
	    return request;
	}

	// helper function for checkCaching(), find the index of first '\r\n'
	int findFirstLine(const std::vector<char> & content)
	{
		int i = 0;
		int N = content.size();
		for(; i + 1 <= N - 1; ++i)
		{
			if(content[i] == '\r' && content[i + 1] == '\n')
			{
				break;
			}
		}
		return i;
	}

	// helper function for checkCaching()
	// insert new k-v pair into the requested content, and do resending
	std::vector<char> insertSectionToContent(const std::vector<char> & content, const std::string & section)
	{
		int idx = findFirstLine(content); // idx is the index of the '\r' in the first line of response
		std::vector<char> content_to_send(content.begin(), content.begin() + idx);
		for(char c : section) content_to_send.push_back(c);
		content_to_send.insert(content_to_send.end(), content.begin() + idx, content.end());
		return content_to_send;
	}

	// helper function for checkCaching(), check whether the return code is legal
	// status code 304 for legal, 200 for illegal
	// return value true for legal, false for illegal
	bool checkStatusCode(const std::string & header)
	{
		int idx = header.find("\r\n");
		int i = 0;
		for(; i + 2 <= idx; ++i)
		{
			if(header[i] == '3' && header[i + 1] == '0' && header[i + 2] == '4')
			{
				return true;
			}
		}
		return false;
	}

	// resend and validate
	// if receive status code 304, directly return content stored in the cache
	// if receive status code 200, receive all the bytes sent by the server, send it to the client, and stored in cache
	void resendCheckStatus(int client_id, int client_fd, int server_fd, const std::vector<char> & content_to_send, const std::string & url)
	{
		// re-send the inserted message to the server
		int sent_length = 0;
		int N = content_to_send.size();
		while(sent_length < N)
		{
			int len = send(server_fd, &content_to_send.data()[0] + sent_length, N - sent_length, 0);
			if(len == -1)
			{
				throw ProxyException("Send with If-None-Match/If-Modified-Since error");
			}
			sent_length += len;
		}

		// receive header from server and decide by the status code
		std::vector<char> buffer(BUFFER_SIZE, '\0'); // buffer to store header temporarily
		int len = recv(server_fd, &buffer.data()[0], BUFFER_SIZE, 0);
		if(len == -1)
		{
			throw ProxyException("Receive with If-None-Match/If-Modified-Since error");
		}
		buffer[len] = '\0';

		// check header status
		const std::string header(buffer.begin(), buffer.begin() + len);
		bool status_304 = checkStatusCode(header);

		// if get true(status code 304), directly return cached content
		if(status_304)
		{
			// write first line of response to log
			std::string first_line = parser.extractFirstLine(header);
			std::string log_content = std::to_string(client_id) + ": Received " + first_line + " from " + url;
			logger.log(log_content);
			log_content = std::to_string(client_id) + ": Responding " + first_line;
			logger.log(log_content);

			// respond to the client with client
			respondCached(client_fd, url);
			return;
		}

		// if get false(status code 200), receive all the sent, and send to the client
		std::vector<std::vector<char>> segment;
		segment.push_back(std::vector<char>(buffer.begin(), buffer.begin() + len));

		// respond header to client, send every character in the buffer to client
		// true for send success, false for send error
		bool respondClient_suc = respondClient(client_fd, buffer, len);
		if(!respondClient_suc) 
		{
			throw ProxyException("Respond header to client error");
		}

		// receive content from server and send it to client segment by segment
		try
		{
			std::string httpAction = "GET"; // for resend, the http action has to be "GET"
			getResponse(client_id, client_fd, server_fd, url, header, segment, len, httpAction);	
		}
		catch(std::exception & e)
		{
			throw e;
		}
	}

	// when receiving request from client, first check caching
	// true means caching function handles responding
	// false means main function handles responding
	bool checkCaching(int client_id, int client_fd, int server_fd, Request & request)
	{
		// (1) url not exist in the cache
		const std::string url = request.url;
		if(!cache.existsUrl(url))
		{
			std::string log_content = std::to_string(client_id) + ": not in cache";
			logger.log(log_content);
			return false;
		}
		Response response = cache.get(url);

		// (2) check expiration time
		// note: expiration time = response time + max-age 
		// if there's no no-cache mark in the response, and the response hasn't been expired, directly fetch it from cache
		time_t cur_time = time(NULL);
		if(cur_time <= response.expiration_time && !response.no_cache)
		{
			std::string log_content = std::to_string(client_id) + ": in cache, valid";
			logger.log(log_content);
			respondCached(client_fd, url);
			return true;
		}

		// (3) check e-tag
		// if these's etag, request If-None-Match tag to the web server
		// format: insert "If-None-Match: etag value" into the content 
		if(!response.etag.empty())
		{
			// write to log
			std::string log_content = std::to_string(client_id) + ": in cache, requires validation";
			logger.log(log_content);

			// create If-None-Match section
			std::string if_none_match = "\r\nIf-None-Match: " + response.etag;

			// insert the section into the request
			std::vector<char> content_to_send = insertSectionToContent(request.content, if_none_match);

			// resend and check the status code
			try
			{
				resendCheckStatus(client_id, client_fd, server_fd, content_to_send, url);
			}
			catch(std::exception & e)
			{
				throw e;
			}

			// has resolved re-validation, updated cache and resending
			return true;
		}

		// (4) check last-modified
		// if there's no etag, but there is last-modified
		else if(response.last_modified != 0)
		{
			// write to log
			std::string log_content = std::to_string(client_id) + ": in cache, requires validation";
			logger.log(log_content);

			// create If-Modified-Since section
  			std::string if_modified_since = "\r\nIf-Modified-Since: " + response.kv["Last-Modified"];

  			// insert the section into the request
  			std::vector<char> content_to_send = insertSectionToContent(request.content, if_modified_since);

  			// resend and check the status code
			try
			{
				resendCheckStatus(client_id, client_fd, server_fd, content_to_send, url);
			}
			catch(std::exception & e)
			{
				throw e;
			}

			// has resolved re-validation, updated cache and resending
			return true;
		}

		// convert expiration time to string
		tm * tm = localtime(&response.expiration_time);
		char * dt = asctime(tm);
		std::string expiration(dt);

		// write to log
		std::string log_content = std::to_string(client_id) + ": in cache, but expired at "  + expiration;
		logger.log(log_content);

		return false;
	}

	// try to connect to the server
	// server fd will be closed in the upper layer exception handling
	void connectServer(const Request & request, int & server_fd)
	{
		// initialize host info
		struct addrinfo host_info;
	    struct addrinfo * host_info_list;
	    memset(&host_info, 0, sizeof(host_info));
	    host_info.ai_family = AF_UNSPEC;
	    host_info.ai_socktype = SOCK_STREAM;

	    // get host information
	    const std::string & hostname = request.hostname;
	    const std::string & port = request.port;
	    status = getaddrinfo(hostname.c_str(), port.c_str(), &host_info, &host_info_list);
	    if(status != 0) 
	    {
	      	throw ProxyException("Connect server getaddrinfo error");
	    } 

	    // create socket
	    server_fd = socket(host_info_list->ai_family, 
	    					host_info_list->ai_socktype,
	                       	host_info_list->ai_protocol);
	    if(server_fd == -1) 
	    {
	    	freeaddrinfo(host_info_list);
	      	throw ProxyException("Connect server create socket error");
	    }

	    // connect a socket to a server
	    status = connect(server_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
	    if(status == -1) 
	    {
	    	freeaddrinfo(host_info_list);
	      	throw ProxyException("Connect socket to server error");
	    } 

	    // free address infor list
	    freeaddrinfo(host_info_list);
	}

	// send request to server(for GET/POST http request)
	void sendRequest(int server_fd, const Request & request)
	{
		int sent_length = 0;
		int request_length = request.content.size();
		const char * content = &request.content.data()[0];
		while(sent_length < request_length)
		{
			int len = send(server_fd, content + sent_length, request_length - sent_length, 0);
			if(len == -1)
		    {
		        throw ProxyException("Proxy send client request error");
		    }
		    sent_length += len;
		}
	}

	// get the header of response
	// since the return value of len is needed in the upper layer, throw error when sending fails
	int getResponseHeader(int server_fd, std::vector<char> & buffer)
	{
		int len = recv(server_fd, &buffer.data()[0], BUFFER_SIZE, 0);
	    if(len == -1)
	    {
	    	throw ProxyException("Receive header error");
	    }
	    buffer[len] = '\0';
	    return len;
	}

	// receive response from server(for GET/POST http request)
	// and send buffer to the client every time proxy receives the response of the server
	// this part of code takes charge of header part
	void getResponse(int client_id, int client_fd, int server_fd, const Request & request)
	{
	    // get header first, decide whether content-based or chunk-based
	    std::vector<char> buffer(BUFFER_SIZE, '\0');
	    int len = 0; // len is the length of received header length
	    try
	    {
	    	len = getResponseHeader(server_fd, buffer);
	    }
	    catch(std::exception & e)
	    {
	    	throw e;
	    }

	    // send header to the client
	    if(!respondClient(client_fd, buffer, len))
	    {
	    	throw ProxyException("Proxy send to client error");
	    }

	    // get url of the request, and pass it as the argument of getResponse() body part
	    const std::string url = request.url;
	    const std::string header(buffer.begin(), buffer.begin() + len + 1);
	    std::vector<std::vector<char>> segment; // store every segment of the response from the server
	    segment.push_back(std::vector<char>(buffer.begin(), buffer.begin() + len)); // push header into the segment vector
	    try
	    {
	    	getResponse(client_id, client_fd, server_fd, url, header, segment, len, request.httpAction);
	    }
	    catch(std::exception & e)
	    {
	    	throw e;
	    }
	}

	// receive response from server(for GET/POST http request)
	// and send buffer to the client every time proxy receives the response of the server
	// this part of code takes charge of content part
	void getResponse(int client_id,
					int client_fd, 
					int server_fd, 
					const std::string & url, 
					const std::string & header, 
					std::vector<std::vector<char>> & segment,
					int len,
					const std::string & httpAction)
	{
	    // content-based http response
	    // (1) extract content length from header
	    // (2) keep receiving until total received size exceeds content length(marks end)
	    std::vector<char> buffer(BUFFER_SIZE, '\0');
	    if(header.find("Content-Length") != -1)
	    {
	    	int received_length = len;
	    	int content_length = parser.extractContentLength(header);
	    	while(received_length < content_length)
	    	{
	    		// receive response from server
	    		len = recv(server_fd, &buffer.data()[0], BUFFER_SIZE - 1, 0);
		    	if(len == -1)
		    	{
		    		throw ProxyException("Proxy received from server error");
		    	}
		    	buffer[len] = '\0';
		    	segment.push_back(std::vector<char>(buffer.begin(), buffer.begin() + len));
		    	received_length += len;

		    	// send response to client
		    	bool respond_suc = respondClient(client_fd, buffer, len);
		    	if(!respond_suc)
		    	{
		    		throw ProxyException("Proxy respond to client error");
		    	}
	    	}
	    }

	    // chunk-based http response
	    // keep receiving till the last chunk of the response, which is marked as 0 at the first line of the chunk
	    else if(header.find("chunked") != -1)
	    {
	    	bool isLastBlock = false;
	    	while(!isLastBlock)
	    	{
	    		// receive message from server
	    		len = recv(server_fd, &buffer.data()[0], BUFFER_SIZE - 1, 0);
		    	if(len == -1)
		    	{
		    		throw ProxyException("Proxy received from server error");
		    	}
		    	buffer[len] = '\0';
		    	segment.push_back(std::vector<char>(buffer.begin(), buffer.begin() + len));

		    	// send response to the client
		    	bool respond_suc = respondClient(client_fd, buffer, len);
		    	if(!respond_suc)
		    	{
		    		throw ProxyException("Proxy respond to client error");
		    	}

		    	// the first character of the last block starts with 0
		    	if(buffer[0] == '0')
		    	{
		    		isLastBlock = true;
		    	}
	    	}	
	    }

	    // POST action
	    else
	    {
	    	// keep receiving until the received length is 0, which marks the end of transmission
	    	int received_length = len;
	    	while(true)
	    	{
	    		// receive from server
	    		len = recv(server_fd, &buffer.data()[0], BUFFER_SIZE - 1, 0);
		    	if(len == -1)
		    	{
		    		throw ProxyException("Proxy received from server error");
		    	}
		    	else if(len == 0)
		    	{
		    		break;
		    	}
		    	segment.push_back(std::vector<char>(buffer.begin(), buffer.begin() + len));

		    	// send response to the client
		    	bool respond_suc = respondClient(client_fd, buffer, len);
		    	if(!respond_suc)
		    	{
		    		throw ProxyException("Proxy respond to client error");
		    	}
	    	}
	    }
	    Response response(url, segment, header);
	    parser.parseResponse(response);

	    // write first line of response to log
	    std::string log_content = std::to_string(client_id) + ": Received " + response.first_line + " from " + response.url;
	    logger.log(log_content);
	    log_content = std::to_string(client_id) + ": Responding " + response.first_line;
	    logger.log(log_content);

	    // cache only works for GET http action, and apply on those with no "no-store" attribute
	    if(!response.no_store && httpAction == "GET")
	    {
	    	cache.put(url, response);
	    }

	    // if http action is GET and status code is 200, write it into log
	    if(httpAction == "GET" && response.status_code == 200)
	    {
	    	std::string log_content;
	    	if(response.no_store)
	    	{
	    		log_content = std::to_string(client_id) + ": not cachable bacause no-store in Cache-Control"; 
	    	}
	    	else if(!response.etag.empty() || response.last_modified != 0)
	    	{
	    		log_content = std::to_string(client_id) + ": cached, but requires re-validation"; 
	    	}
	    	else
	    	{
	    		// convert time_t to string
	    		tm * tm = localtime(&response.expiration_time);
	    		char * dt = asctime(tm);
	    		std::string expiration_time(dt);
	 			log_content = std::to_string(client_id) + ": cached, expired at " + expiration_time;
	    	}
	    	logger.log(log_content);
	    }
	}

	// send every character received to the client
	bool respondClient(int client_fd, const std::vector<char> & buffer, int received_length)
	{
		int sent_length = 0;
		while(sent_length < received_length)
		{
			int len = send(client_fd, &buffer.data()[0] + sent_length, received_length, 0);	
			if(len == -1)
			{
				return false;
			}
			sent_length += len;
		}
		return true;
	}

	// if caching is valid, directly respond with cached response
	// this function is called when 
	// (1) the stored response in the cache hasn't expired
	// (2) the reponse has expired, but has a etag, and pass the re-validation(get 304 status code)
	// (3) the reponse has expired and has no etag, doesn't exceed last-modified date, and pass the re-validation(get 304 status code)
	void respondCached(int client_fd, const std::string & url)
	{
		// send response to the client
		Response response = cache.get(url);
		for(const auto & seg : response.content)
		{
			int sent_length = 0;
			int length_to_be_sent = seg.size();
			while(sent_length < length_to_be_sent)
			{
				int len = send(client_fd, &seg.data()[0] + sent_length, length_to_be_sent - sent_length, 0);
				if(len == -1)
				{
					throw ProxyException("Send with cached response error");
				}
				sent_length += len;
			}
		}
	}

	// handle GET and POST request
	void handleGetPost(int client_id, int client_fd, int server_fd, const Request & request)
	{
		try
		{
			sendRequest(server_fd, request);
			getResponse(client_id, client_fd, server_fd, request);
		}
		catch(std::exception & e)
		{
			throw e;
		}
	}

	// handle CONNECT request
	void handleConnect(int client_id, int client_fd, int server_fd, const Request & request)
	{
		// send a 200 OK to client
		const char * okMsg = "200 OK";
		int len = send(client_fd, okMsg, strlen(okMsg) + 1, 0);
		if(len == -1)
		{
			throw ProxyException("Send 200 OK to client error");
		}

		// select and keep listening
		bool connectionEnd = false;
		fd_set rfds;
		int fds[] = { client_fd, server_fd };
		int max_fd = std::max(fds[0], fds[1]) + 1; // argument of select() is the maximum fd + 1
		std::vector<char> buffer(BUFFER_SIZE, '\0'); // buffer to store request or response
		while(true)
		{
			// check if break loop
			if(connectionEnd)
			{
				break;
			}

			// initialize the fd set
			FD_ZERO(&rfds);
			FD_SET(fds[0], &rfds);
			FD_SET(fds[1], &rfds);

			// listen to fds in a blocking way
			status = select(max_fd, &rfds, NULL, NULL, NULL); 
			if(status == -1)
			{
				throw ProxyException("Server or client select error");
			}

			// receive request or respond from the set fd
			// send to the other fd
			for(int i = 0; i < 2; ++i)
			{
				if(FD_ISSET(fds[i], &rfds))
				{
					// receive
					len = recv(fds[i], &buffer.data()[0], BUFFER_SIZE - 1, 0);
					if(len == -1)
					{
						throw ProxyException("Receive request or respond error");
					}

					// decide the end of CONNECT
					else if(len == 0)
					{
						connectionEnd = true;
						break;
					}
					buffer[len] = '\0';

					// send
					len = send(fds[1 - i], &buffer.data()[0], len, 0);
					if(len == -1)
					{
						throw ProxyException("Send to client or server error");
					}
				}
			}
		}
	}

	// handle request(actions after accept)
	// (1) connect server by client
	// (2) handle request accordingly
	void handleRequest(int client_id, int client_fd, const std::string & client_ip)
	{
		int server_fd = -1;
		Request request;

		try
		{
			// try to accept the request,
			// if an error occurs, throw the exception
			try
			{
				request = acceptRequest(client_fd);

				// record request to log
				std::string log_content = std::to_string(client_id) + ": " + request.httpAction + " from " + client_ip + " @ " + request.request_time;
				logger.log(log_content);
			}
			catch(std::exception & e)
			{
				throw e;
			}

			// try to connect server, and get the server fd
			// if an error occurs, throw the exception
			try
			{
				connectServer(request, server_fd);	
			}
			catch(std::exception & e)
			{
				throw e;
			}

			// handle specific http request
			try
			{
				std::string httpAction = request.httpAction;

				// record request to log, applied to GET, POST and CONNECT
				std::string log_content = std::to_string(client_id) + ": Requesting " + request.first_line + " from " + request.url;
				logger.log(log_content);

				if(httpAction == "CONNECT")
				{
					handleConnect(client_id, client_fd, server_fd, request);

					// write tunnel status to log
					std::string log_content = std::to_string(client_id) + ": Tunnel closed";
					logger.log(log_content); 
				}
				else if(httpAction == "GET")
				{
					// for GET http action, check caching first
					// if get return value true,
					// (1) either the cache is valid, has been sent by cache
					// (2) or the cache is invalid, needs to update the cache and resend it to client
					// anyway, the response has been sent to the client
					try
					{
						bool cacheValid = checkCaching(client_id, client_fd, server_fd, request);
						if(!cacheValid)
						{
							handleGetPost(client_id, client_fd, server_fd, request);
						}
					}
					catch(std::exception & e)
					{
						throw e;
					}
				}
				else if(httpAction == "POST")
				{
					std::cout << "begin post action" << std::endl;
					handleGetPost(client_id, client_fd, server_fd, request);
				}	
				else
				{
					throw ProxyException("Unknown HTTP request category");
				}
/*
				// DEBUG
				std::cout << "send and receive complete" << std::endl;
*/
				// release client and server fd
				close(server_fd);
				close(client_fd);
			}
			catch(std::exception & e)
			{
				throw e;
			}
		}

		// when an exception happens, write the exception into log and quit the thread
		catch(std::exception & e)
		{
			// write exception into log
			std::string errMsg(e.what());
			std::string log_content = std::to_string(client_id) + ": ERROR " + errMsg;
			logger.log(log_content);

			// close the allocated resource
			close(client_fd);
			if(server_fd != -1) close(server_fd);
			return;
		}
	}

public:
	Proxy() : 
		logger { "log.txt" },
		cache { LRUCache(CACHE_SIZE) }
	{
		// error shouldn't happen in the constructor
		// but if it does, release all the resouces allocated and exit the process
		constructServer();
	}

	~Proxy() noexcept
	{
		close(socket_fd);
	}

	void run()
	{
		unsigned client_id = 0; // id to mark different 

		// (1) wait until an request of sending arrives
		// (2) every time a client fd is caught, create a new thread to handle the request
		while(true)
		{
			std::string client_ip;
			int client_fd = acceptConnection(client_ip);
			if(client_fd == -1) // request error
			{
//				std::cerr << "Accept client error" << std::endl;
				continue;
			}
			std::thread thd(&Proxy::handleRequest, this, client_id, client_fd, client_ip);
			thd.detach();

			// assign every request/thread a unique id
			client_id = (client_id == INT_MAX) ? 0 : ++client_id;
		}
	}
};

int main(int argc, char ** argv)
{
	Proxy proxy;
	proxy.run();
	return EXIT_SUCCESS;
}
