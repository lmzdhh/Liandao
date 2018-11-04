//
// Created by wang on 10/22/18.
//
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <iostream>
#include <sstream>
#include "Utils.h"
#include "../../yijinjing/log/KfLog.h"
#include "../zlib/include/zlib.h"
using namespace kungfu::yijinjing;
KfLogPtr   g_logger;
namespace  kungfu
{
     std::string LDUtils::gzip_compress(const std::string& src)
     {
         try
         {
             unsigned char dest[10240] = {0};
             unsigned long len = sizeof(dest);
             compress(dest, &len, (const unsigned char*)src.data(), src.size());
             return std::move(std::string((char*)dest, len));
         }
         catch (const std::exception& e)
         {
             KF_LOG_INFO(g_logger, "gzip compress exception,{error:"<<e.what()<<"}");
         }
         return std::string();
     }

     std::string LDUtils::gzip_decompress(const std::string& src)
     {
         try
         {
             unsigned char dest[10240] = {0};
             unsigned long len = sizeof(dest);
             uncompress(dest, &len, (const unsigned char*)src.data(), src.size());
             return std::move(std::string((char*)dest, len));
         }
         catch (const std::exception& e)
         {
             KF_LOG_INFO(g_logger, "gzip decompress exception,{error:"<<e.what()<<"}");
         }
         return std::string();
     }

    std::vector<std::string> LDUtils::split(const std::string& src, const std::string& pred)
    {
        std::vector<std::string> ret;
        try
        {
            boost::split(ret, src, boost::is_any_of(pred));
        }
        catch (const std::exception& e)
        {
            KF_LOG_INFO(g_logger, "split exception,{error:"<<e.what()<<"}");
        }
        return std::move(ret);
    }

 }