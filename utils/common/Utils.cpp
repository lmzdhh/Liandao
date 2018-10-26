//
// Created by wang on 10/22/18.
//
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <iostream>
#include <sstream>
#include "Utils.h"
#include "../../yijinjing/log/KfLog.h"
namespace  biossgzip = boost::iostreams::gzip;
namespace  biostream= boost::iostreams;
using namespace kungfu::yijinjing;
KfLogPtr   g_logger;
namespace  kungfu
{
     std::string LDUtils::gzip_compress(const std::string& src)
     {
         try
         {
             std::string ret;
             biostream::filtering_ostream fos;
             fos.push(biostream::gzip_compressor(biostream::gzip_params(biossgzip::best_compression)));
             fos.push(boost::iostreams::back_inserter(ret));
             fos << src;
             boost::iostreams::close(fos);
             return std::move(ret);//ret is char array , not string
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
             std::string ret;
             biostream::filtering_ostream fos;
             fos.push(biostream::gzip_decompressor());
             fos.push(biostream::back_inserter(ret));
             fos << src;
             fos << std::flush;
             return std::move(ret);
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