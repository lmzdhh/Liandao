#include "InterfaceMgr.h"
#include <iostream>
#include <chrono>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

InterfaceMgr::InterfaceMgr()
{
	m_timeout = 0;
	m_index = 0;
	m_mutex = new std::mutex();
}

InterfaceMgr::~InterfaceMgr()
{
	m_timeout = 0;
	if(m_mutex != nullptr) delete m_mutex;
}

void InterfaceMgr::init(const std::string& interfaces)
{
	m_timeout = 0;
	this->initArray(interfaces);
}

void InterfaceMgr::init(const std::string& interfaces, int timeout)
{
	m_timeout = timeout;
	this->initArray(interfaces);
}

inline int64_t InterfaceMgr::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

std::vector<std::string> InterfaceMgr::split(const std::string& src, const std::string& delimiter)
{
	std::vector<std::string> ret;
	try {
		boost::split(ret, src, boost::is_any_of(delimiter));
	} catch (const std::exception& e) {
		std::cout<<"split exception,{error:"<<e.what()<<"}"<<std::endl;
	}
	return std::move(ret);
}

void InterfaceMgr::initArray(const std::string& interfaces)
{
	std::vector<std::string> array = this->split(interfaces, "-");
	for(size_t i =0; i < array.size(); i++) {
		HostInterface item;
		item.enable = true;
		item.beginTime = this->getTimestamp();
		item.host = array[i];
		m_vector.push_back(item);
	}
	
	return;
}

void InterfaceMgr::disable(const std::string& interface)
{
	std::lock_guard<std::mutex> guard_mutex(*m_mutex);
	
	for(size_t i = 0; i < m_vector.size(); i++) {
		if (m_vector[i].host == interface) {
			m_vector[i].enable = false;
			m_vector[i].beginTime = this->getTimestamp();
			break;
		}
	}

	return;
}

void InterfaceMgr::print()
{
	for(size_t i = 0; i < m_vector.size(); i++) {
		std::cout <<"name:" << m_vector[i].host << " beginTime:" 
		<< m_vector[i].beginTime << " enable: " <<m_vector[i].enable << std::endl;
	}

	return;
}

std::string InterfaceMgr::getActiveInterface()
{	
	size_t number, size;
	std::string hostName;
	HostInterface item;

	std::lock_guard<std::mutex> guard_mutex(*m_mutex);
	
	//1. vector is empty
	size = m_vector.size();
	if (size == 0) return hostName;

	if (m_index > 1000000) m_index = 0;
	number = (m_index++) % size;

	//2. not set timeout
	int64_t currentTime = this->getTimestamp();
	if (m_timeout <= 0) {
		hostName = m_vector[number].host;
		return hostName;
	}

	//3. set timeout
	for(size_t i = 0; i < size; i++) {
		item = m_vector[number];
		if (item.enable) {
			hostName = item.host;
			break;
		}

		//item timeout
		if (currentTime - item.beginTime > m_timeout) {
			m_vector[number].enable = true;
			m_vector[number].beginTime = currentTime;
			hostName = item.host;
			
			break;
		}

		if (m_index > 1000000) m_index = 0;
		number = (m_index++) % size;
	}

	return hostName;
}


