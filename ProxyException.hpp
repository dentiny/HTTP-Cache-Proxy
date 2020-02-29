#ifndef PROXY_EXCEPTION_HPP__
#define PROXY_EXCEPTION_HPP__

#include <string>
#include <exception>

class ProxyException : public std::exception
{
private:
	std::string message;

public:
	explicit ProxyException(std::string msg):
		message { msg }
		{}

public:
	virtual const char * what()
	{
		return message.c_str();
	}
};

#endif