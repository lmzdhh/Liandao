/*****************************************************************************
 * Copyright [2017] [taurus.ai]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *****************************************************************************/

/**
 * MDEngineIndodax: Indodax's market data engine adapter.
 * @Author cjiang (changhao.jiang@taurus.ai)
 * @since   April, 2017
 */

#include "MDEngineIndodax.h"
#include "TypeConvert.hpp"
#include "Timer.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"

USING_WC_NAMESPACE

MDEngineIndodax::MDEngineIndodax(): IMDEngine(SOURCE_CTP), api(nullptr), connected(false), logged_in(false), reqId(0)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Indodax");
}

void MDEngineIndodax::load(const json& j_config)
{
    /*
    broker_id = j_config[WC_CONFIG_KEY_BROKER_ID].get<string>();
    user_id = j_config[WC_CONFIG_KEY_USER_ID].get<string>();
    password = j_config[WC_CONFIG_KEY_PASSWORD].get<string>();
    front_uri = j_config[WC_CONFIG_KEY_FRONT_URI].get<string>();
    */
   
    broker_id = "Indodax";
    user_id = "testIndodax";
    password = "test";
    front_uri = "";
    symbols.push_back("btc_idr");
}

void MDEngineIndodax::connect(long timeout_nsec)
{
    if(api == nullptr){
        api = new libIndodax();
        if (!api)
        {
            throw std::runtime_error("CTP_MD failed to create api");
        }
    }
    connected = true;

    // if (api == nullptr)
    // {
    //     api = CThostFtdcMdApi::CreateFtdcMdApi();
    //     if (!api)
    //     {
    //         throw std::runtime_error("CTP_MD failed to create api");
    //     }
    //     api->RegisterSpi(this);
    // }
    // if (!connected)
    // {
    //     api->RegisterFront((char*)front_uri.c_str());
    //     api->Init();
    //     long start_time = yijinjing::getNanoTime();
    //     while (!connected && yijinjing::getNanoTime() - start_time < timeout_nsec)
    //     {}
    // }
    
    //api = new int;
}

void MDEngineIndodax::login(long timeout_nsec)
{
    // if (!logged_in)
    // {
    //     CThostFtdcReqUserLoginField req = {};
    //     strcpy(req.BrokerID, broker_id.c_str());
    //     strcpy(req.UserID, user_id.c_str());
    //     strcpy(req.Password, password.c_str());
    //     if (api->ReqUserLogin(&req, reqId++))
    //     {
    //         KF_LOG_ERROR(logger, "[request] login failed!" << " (Bid)" << req.BrokerID
    //                                                        << " (Uid)" << req.UserID);
    //     }
    //     long start_time = yijinjing::getNanoTime();
    //     while (!logged_in && yijinjing::getNanoTime() - start_time < timeout_nsec)
    //     {}
    // }
    logged_in = true;
}

void MDEngineIndodax::logout()
{
    // 
    connected = false;
    logged_in = false;
}

void MDEngineIndodax::release_api()
{
    if (api != nullptr)
    {
        //api->Release();
        delete api;
        api = nullptr;
    }
}

void MDEngineIndodax::getMarketAndTradeData(const vector<string>& instruments, const vector<string>& markets)
{
    // int nCount = instruments.size();
    // char* insts[nCount];
    // for (int i = 0; i < nCount; i++)
    //     insts[i] = (char*)instruments[i].c_str();
    // api->SubscribeMarketData(insts, nCount);
    Document d;
    string res;
    for(int i = 0;, i < symbols.size(); i++){
        // get market data
        res = api->getDepthC(symbols.get(i));
        d.Parse(res.c_str());
        // get trade data
        res = api->getTradeC(symbols.get(i));
        d.Parse(res.c_str());
    }
}

/*
 * SPI functions
 */
void MDEngineIndodax::OnFrontConnected()
{
    KF_LOG_INFO(logger, "[OnFrontConnected]");
    connected = true;
}

void MDEngineIndodax::OnFrontDisconnected(int nReason)
{
    KF_LOG_INFO(logger, "[OnFrontDisconnected] reason=" << nReason);
    connected = false;
    logged_in = false;
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

void MDEngineIndodax::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (pRspInfo != nullptr && pRspInfo->ErrorID != 0)
    {
        KF_LOG_ERROR(logger, "[OnRspUserLogin]" << " (errID)" << pRspInfo->ErrorID
                                                << " (errMsg)" << GBK2UTF8(pRspInfo->ErrorMsg));
    }
    else
    {
        KF_LOG_INFO(logger, "[OnRspUserLogin]" << " (Bid)" << pRspUserLogin->BrokerID
                                               << " (Uid)" << pRspUserLogin->UserID
                                               << " (SName)" << pRspUserLogin->SystemName);
        logged_in = true;
    }
}

void MDEngineIndodax::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (pRspInfo != nullptr && pRspInfo->ErrorID != 0)
    {
        KF_LOG_ERROR(logger, "[OnRspUserLogout]" << " (errID)" << pRspInfo->ErrorID
                                                 << " (errMsg)" << GBK2UTF8(pRspInfo->ErrorMsg));
    }
    else
    {
        KF_LOG_INFO(logger, "[OnRspUserLogout]" << " (Bid)" << pUserLogout->BrokerID
                                                << " (Uid)" << pUserLogout->UserID);
        logged_in = false;
    }
}

void MDEngineIndodax::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (pRspInfo != nullptr && pRspInfo->ErrorID != 0)
    {
        KF_LOG_ERROR(logger, "[OnRspSubMarketData]" << " (errID)" << pRspInfo->ErrorID
                                                    << " (errMsg)" << GBK2UTF8(pRspInfo->ErrorMsg)
                                                    << " (Tid)" << ((pSpecificInstrument != nullptr) ?
                                                                    pSpecificInstrument->InstrumentID : "null"));
    }
}

void MDEngineIndodax::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
    auto data = parseFrom(*pDepthMarketData);
    on_market_data(&data);
    // if need to write raw data...
    // raw_writer->write_frame(pDepthMarketData, sizeof(CThostFtdcDepthMarketDataField),
    //                         source_id, MSG_TYPE_LF_MD_CTP, 1/*islast*/, -1/*invalidRid*/);
}

BOOST_PYTHON_MODULE(libctpmd)
{
    using namespace boost::python;
    class_<MDEngineIndodax, boost::shared_ptr<MDEngineIndodax> >("Engine")
    .def(init<>())
    .def("init", &MDEngineIndodax::initialize)
    .def("start", &MDEngineIndodax::start)
    .def("stop", &MDEngineIndodax::stop)
    .def("logout", &MDEngineIndodax::logout)
    .def("wait_for_stop", &MDEngineIndodax::wait_for_stop);
}