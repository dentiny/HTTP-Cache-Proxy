#ifndef REQUEST_HPP__
#define REQUEST_HPP__

#include <ctime>
#include <string>
#include <vector>

class Request
{
public:
	std::string request_time;
	std::string first_line; // used in logging
	std::string httpAction;
	std::string url;
	std::string hostname;
	std::string port; // 80 for HTTP, 443 for HTTPS
	std::vector<char> content; // the complete http request from client

	Request() {}

	Request(const std::string & cur_time, std::vector<char> buffer, int len) :
		request_time { cur_time },
		content { std::vector<char>(buffer.begin(), buffer.begin() + len) }
		{}

	Request & operator=(const Request & rhs)
	{
		if(this != &rhs)
		{
			request_time = rhs.request_time;
			first_line = rhs.first_line;
			httpAction = rhs.httpAction;
			url = rhs.url;
			hostname = rhs.hostname;
			port = rhs.port;
			content = rhs.content;
		}
		return *this;
	}
};

#endif