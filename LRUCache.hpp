#ifndef LRU_CACHE_HPP__
#define LRU_CACHE_HPP__

#include "ProxyException.hpp"
#include "Response.hpp"
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

std::mutex mtx1; // first level mutex
std::mutex mtx2; // second level mutex

class LRUCache 
{
private:
    int sz;
    int capacity;
    std::list<std::string> cache; // for LRU order
    std::unordered_map<std::string, Response> kv; // key for url, value for response
    
public:
    LRUCache(int _capacity) :
        sz { 0 },
        capacity { _capacity }
        {}

    bool existsUrl(const std::string & url)
    {
        std::unique_lock<std::mutex> lck(mtx2);
        return kv.find(url) != kv.end();
    }

    void remove(const std::string & url)
    {
        std::unique_lock<std::mutex> lck(mtx1);
        kv.erase(url);
        cache.remove(url);
    }

    Response get(const std::string & url) 
    {
        std::unique_lock<std::mutex> lck(mtx1);

        // url doesn't exist in cache
        if(!existsUrl(url))
        {
            throw ProxyException("Url doesn't exist in cache");
        }

        Response ret = kv[url];
        cache.remove(url);
        cache.push_back(url);
        return ret;
    }
    
    void put(const std::string & url, const Response & response) 
    {
        std::unique_lock<std::mutex> lck(mtx1);
        if(existsUrl(url))
        {
            cache.remove(url);
            kv[url] = response;
        }
        else
        {
            if(sz == capacity)
            {
                const std::string & url = cache.front(); cache.pop_front();
                kv.erase(url);
            }
            else
            {
                ++sz;
            }
            kv.insert(std::make_pair(url, response));
        }
        cache.push_back(url);
    }
};

#endif