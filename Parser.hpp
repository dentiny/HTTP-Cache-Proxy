#ifndef PARSER_HPP__
#define PARSER_HPP__

#include "Request.hpp"
#include "Response.hpp"
#include <ctime>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <unordered_map>

class Parser
{
private:
	void pushClear(std::string & temp, std::string & data)
	{
		if(!temp.empty())
		{
			data = temp;
			temp.clear();
		}
	}

	// tell the difference between http and https: http request starts with http://
	// http reuqest has no trailing port number, while https has trailing port 443
	void extractAddrPort(std::string & url, std::string & hostname, std::string & port)
	{
		// eg: http://people.duke.edu/~bmr23/ece568/
		// url: http://people.duke.edu/~bmr23/ece568/
		// hostname: people.duke.edu
		// port: 80
		if(url.find("http://") == 0)
		{
			// port number
			port = "80";

			// hostname
			int idx = 0;
			int N = url.length();
			int slash = 0;
			for(; idx < N; ++idx)
			{
				slash += url[idx] == '/';
				if(slash == 3) // "http://" has two slash
				{
					break;
				}
			}
			hostname = url.substr(7, idx - 7);
		}

		// eg: github.com:443
		// url: github.com
		// hostname: github.com
		// port: 443
		else
		{
			int idx = url.find_last_of(':');
			hostname = url.substr(0, idx);
			url = url.substr(0, idx);
			port = "443";
		}
	}

	// extract the first line of the response, including status code
	void extractFirstLine(Response & response)
	{
		// first line
		const std::string header = response.header;
		for(char c : header)
		{
			if(c == '\r')
			{
				break;
			}
			else
			{
				response.first_line += c;
			}
		}

		// status code
		// eg: first line: HTTP/1.1 200 OK
		const std::string & first_line = response.first_line;
		int idx1 = first_line.find_first_of(' ');
		int idx2 = first_line.find_last_of(' ');
		response.status_code = std::stoi(first_line.substr(idx1, idx2 - idx1)); // [idx1, idx2)
	}

	// helper function for extractKV()
	// extract every key-pair value
	void extractKV(const std::string & content, std::unordered_map<std::string, std::string> & kv)
	{
		std::string key = "";
		std::string val = "";
		std::string temp;
		for(char c : content)
		{
			if(c == ':' || c == ' ')
			{
				pushClear(temp, key);
			}
		}
		pushClear(temp, val);
		kv.insert(std::make_pair(key, val));
	}

	// extract all k-v pairs of the response and store them into the map
	void extractKV(Response & response)
	{
		// extract k-v pair header
		const std::string & header = response.header;
		std::unordered_map<std::string, std::string> & kv = response.kv; 
		unsigned idx1 = header.find_first_of("\r\n"); // the first line status and kv-pair is seperated by \r\n
		unsigned idx2 = header.find_first_of("\r\n\r\n"); // header and content is seperated by \r\n\r\n
		std::string kv_header = header.substr(idx1 + 2, idx2 - idx1 - 2); // extract the real header in k-v pair

		// extract k-v pair to the unordered_map
		int N = kv_header.length();
		std::string key;
		std::string val;
		std::string temp;
		for(int idx = 0; idx < N; ++idx)
		{
			char c = kv_header[idx];

			// \r\n the seperator of lines
			if(c == '\r' && idx + 1 <= N - 1 && kv_header[idx + 1] == '\n')
			{
				pushClear(temp, val);
				kv.insert(std::make_pair(key, val));
				key.clear();
				val.clear();
				++idx;
			}

			// : the seperator of key-value pair
			else if(c == ':' && idx + 1 <= N - 1 && kv_header[idx + 1] == ' ')
			{
				pushClear(temp, key);
				++idx;
			}

			// normal characters
			else
			{
				temp += c;
			}
		}
		pushClear(temp, val);
		kv.insert(std::make_pair(key, val));
	}

	// extract key attributes from the response k-v pair
	// including no-store, no-cache, max-age, e-tag
	void extractAttri(Response & response)
	{
		std::unordered_map<std::string, std::string> & kv = response.kv; 

		// no-store and no-cache attribute
		if(kv.find("Cache-Control") != kv.end())
		{
			std::string cache_control = kv["Cache-Control"];
			if(cache_control.find("no-store") != -1)
			{
				response.no_store = true;
			}
			if(cache_control.find("no-cache") != -1)
			{
				response.no_cache = true;
			}
		}

		// e-tag
		if(kv.find("ETag") != kv.end())
		{
			response.etag = kv["ETag"];
		}
	}

	// calculate the expiration time
	// expirationTime = responseTime + freshnessLifetime - currentAge
	void calcExpiration(Response & response)
	{
		std::unordered_map<std::string, std::string> & kv = response.kv;
		const time_t & response_time = response.cur_time;

		// get fressness time
		int fressness_time = 0;
		if(kv.find("Cache-Control") != kv.end())
		{
			const std::string & cache_control = kv["Cache-Control"];
			int idx = cache_control.find("max-age");
			if(idx != -1)
			{
				// eg: max-age=1234567
				std::string max_age_s = cache_control.substr(idx + 8);
				fressness_time = std::stoi(max_age_s);
			}
		}

		response.expiration_time = response.cur_time + fressness_time;
	}

	// extract last modified time
	void extractLastModified(Response & response)
	{
		std::unordered_map<std::string, std::string> & kv = response.kv;
		if(kv.find("Last-Modified") != kv.end())
		{
			const char * time = kv["Last-Modified"].c_str();
			struct tm tm;
			memset(&tm, 0, sizeof(struct tm));
			strptime(time, "%a, %d %b %Y %H:%M:%S %z", &tm);
			response.last_modified = mktime(&tm);
		}
	}

public:
	// extract from user request
	// (1) httpAction
	// (2) hostname
	// (3) port
	// (4) content
	// eg: GET http://people.duke.edu/~bmr23/ece568/ HTTP/1.1
	// eg: CONNECT www.google.com:443
	// Host: people.duke.edu
	void parseRequest(Request & request)
	{
		bool extractAddrPort_suc = false;
		std::string temp;
		for(char c : request.content)
		{
			// extract the first line of the request
			if(c == '\r') 
			{
				break;
			}
			request.first_line += c;

			// extract http action, hostname, port
			if(!extractAddrPort_suc && c == ' ')
			{
				// get HTTP action and url from request
				if(request.httpAction.empty())
				{
					pushClear(temp, request.httpAction);
				}
				else
				{
					pushClear(temp, request.url);
					extractAddrPort(request.url, request.hostname, request.port);
					extractAddrPort_suc = true;
				}
			}
			else
			{
				temp += c;
			}
		}
	}

	// parse all k-v pair in the response
	// response header format eg:
	// HTTP/1.1 200 OK
	// Cache-Control: no-cache, no-store
	// Pragma: no-cache
	// Trailer: X-HttpWatch-Sample
	// Transfer-Encoding: chunked
	// Content-Type: image/jpeg; charset=utf-8
	// Expires: -1
	// Date: Mon, 24 Feb 2020 00:32:34 GMT
	void parseResponse(Response & response)
	{
		extractFirstLine(response);
		extractKV(response);
		extractAttri(response);	
		calcExpiration(response);
		extractLastModified(response);
	}

	// extract content length from response from server
	int extractContentLength(const std::string & header)
	{
		// start extract content length
		int res = 0;
		int idx = header.find("Content-Length");
		int N = header.length();
		bool digit_found = false;
		for(int i = idx; i < N; ++i)
		{
			char c = header[i];
			if(digit_found && isdigit(c))
			{
				res = res * 10 + (c - '0');
			}
			else if(digit_found && !isdigit(c))
			{
				break;
			}
			else if(!digit_found && isdigit(c))
			{
				digit_found = true;
				res = c - '0';
			}
		}
		return res;
	}

	// check the validility of E-Tag
	// true for valid etag, false for invalid etag
	bool checkEtagValidiklity(const std::vector<char> & buffer, Response & response)
	{
		const std::string etag1 = response.etag;
		if(etag1.empty()) return false;
		const std::string header = std::string(buffer.begin(), buffer.end());
		int idx1 = header.find("ETag");
		if(idx1 == -1) return false;
		int idx2 = header.find(": ", idx1);
		int idx3 = header.find("\r\n", idx2);
		const std::string etag2(header.begin() + idx2 + 2, header.begin() + idx3);
		return etag1 == etag2;
	}

	// get first line from header, used in return 304 status code
	std::string extractFirstLine(const std::string & header)
	{
		int idx = header.find_first_of('\r');
		return header.substr(0, idx);
	}
};

#endif