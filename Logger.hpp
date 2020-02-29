#ifndef LOGGER_HPP__
#define LOGGER_HPP__

#include <mutex>
#include <string>
#include <fstream>

class Logger
{
private:
	std::mutex mtx;
	std::string path;
	std::ofstream out;

public:
	Logger(std::string _path) : 
		path { _path }
	{
		out.open(path, std::ofstream::out | std::ofstream::app);
		out.close();
	}

	void log(const std::string & content)
	{
		// protect against data race
		std::unique_lock<std::mutex> lck(mtx);

		// open the file in append mode if closed
		if(!out.is_open())
		{
			out.open(path, std::ofstream::out | std::ofstream::app);
		}

		out << content << std::endl;
		out.close();
	}
};

#endif