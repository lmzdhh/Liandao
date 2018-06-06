#ifndef MDEngine_INDODAX_H
#define MDEngine_INDODAX_H

#include "IMDEngine.h"
#include "longfist/LFConstants.h"
#include "ThostFtdcMdApi.h"
#include "libIndodax.h"
#include "rapidjson/document.h"
#include <vector>
WC_NAMESPACE_START

class MDEngineIndodax: public IMDEngine, public CThostFtdcMdSpi
{
public:
    /** load internal information from config json */
    virtual void load(const json& j_config);
    virtual void connect(long timeout_nsec);
    virtual void login(long timeout_nsec);
    virtual void logout();
    virtual void release_api();
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets);
    virtual bool is_connected() const { return connected; };
    virtual bool is_logged_in() const { return logged_in; };
    virtual string name() const { return "MDEngineCTP"; };

public:
    MDEngineIndodax();

private:
    /** ctp api */
    libIndodax* api;
    /** internal information */
    string broker_id;
    string user_id;
    string password;
    string front_uri;
    // internal flags
    bool connected;
    bool logged_in;
    int  reqId;

    //
    vector<string> symbols;
public:
    // SPI
    ///当客户端与交易后台建立起通信连接时（还未登录前），该方法被调用。
    virtual void OnFrontConnected();

    ///当客户端与交易后台通信连接断开时，该方法被调用。当发生这个情况后，API会自动重新连接，客户端可不做处理。
    ///@param nReason 错误原因
    ///        0x1001 网络读失败
    ///        0x1002 网络写失败
    ///        0x2001 接收心跳超时
    ///        0x2002 发送心跳失败
    ///        0x2003 收到错误报文
    virtual void OnFrontDisconnected(int nReason);

    ///登录请求响应
    virtual void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast);

    ///登出请求响应
    virtual void OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast);

    ///订阅行情应答
    virtual void OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast);

    ///深度行情通知
    virtual void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData);
};

DECLARE_PTR(MDEngineIndodax);

WC_NAMESPACE_END

#endif