#ifndef RESPONSE_HPP__
#define RESPONSE_HPP__

#include <ctime>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <unordered_map>

class Response
{
public:
	int status_code;
	std::string first_line; // used in first line
	std::string url; // receive url from request
	std::string header; // need to extract expiration related information from header
	std::vector<std::vector<char>> content; // all the content response from server
	std::unordered_map<std::string, std::string> kv; // k-v pair of the response header

	// several key attributes of the header
	bool no_store;
	bool no_cache;
	bool has_expiration;
	time_t cur_time;
	time_t expiration_time;
	time_t last_modified;
	std::string etag;

	// default constructor is needed to be the value of unordered_map in LRUCache
	Response() : 
		status_code { -1 },
		no_store { false },
		no_cache { false },
		has_expiration { false },
		cur_time { 0 },
		expiration_time { 0 },
		last_modified { 0 }
		{} 

	Response(const std::string _url, std::vector<std::vector<char>> buffer, const std::string h) : 
		status_code { -1 },
		url { _url },
		header { h },
		content { buffer },
		no_store { false }, // default can be stored in the cache
		no_cache { false }, // default can be cached in the cache(need to check e-tag)
		has_expiration { false }, // has expiration calculation machanism
		cur_time { time(NULL) },
		expiration_time { 0 },
		last_modified { 0 }
		{}

	// copy assignment is need to be the value of unordered_map in LRUCache
	Response & operator=(const Response & rhs)
	{
		if(this != &rhs)
		{
			status_code = rhs.status_code;
			first_line = rhs.first_line;
			url = rhs.url;
			header = rhs.header;
			content = rhs.content;
			kv = rhs.kv;
			no_store = rhs.no_store;
			no_cache = rhs.no_cache;
			has_expiration = rhs.has_expiration;
			cur_time = rhs.cur_time;
			expiration_time = rhs.expiration_time;
			last_modified = rhs.last_modified;
			etag = rhs.etag;
		}
		return *this;
	}
};

#endif
