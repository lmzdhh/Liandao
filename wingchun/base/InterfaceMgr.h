#ifndef INTERFACE_MANAGER_H
#define INTERFACE_MANAGER_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

struct HostInterface
{
	bool enable;
	std::string host;
	int64_t beginTime;
};

class InterfaceMgr   {
public:
  	InterfaceMgr() {m_timeout = 0;}
  	void init(const std::string& interfaces);
	void init(const std::string& interfaces, int timeout);

	void disable(const std::string& interface);
	std::string getActiveInterface();
	void print();
	
private: 
	std::vector<std::string> split(const std::string& src, const std::string& delimiter);
	inline int64_t getTimestamp();
	void initArray(const std::string &Interface);
	
private:
	std::vector<HostInterface> m_vector;
	int m_timeout;
};


#endif


