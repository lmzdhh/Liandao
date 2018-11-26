// auto generated by struct_info_parser.py, please DO NOT edit!!!

#ifndef LONGFIST_UTILS_H
#define LONGFIST_UTILS_H

#include "LFDataStruct.h"

#include <string>

inline std::string getExchangeName(short exchange_id)
{
	switch(exchange_id)
	{
		case 2: // 深圳证券交易所
			return "SZE";
		case 11: // 中国金融期货交易所
			return "CFFEX";
		case 1: // 上海证券交易所
			return "SSE";
		case 13: // 大连商品交易所
			return "DCE";
		case 12: // 上海期货交易所
			return "SHFE";
		case 14: // 郑州商品交易所
			return "CZCE";
	}
}

inline int getExchangeId(std::string exchange_name)
{
	if (exchange_name.compare("SZE") == 0 || exchange_name.compare("sze") == 0) //深圳证券交易所
		return 2;
	if (exchange_name.compare("CFFEX") == 0 || exchange_name.compare("cffex") == 0) //中国金融期货交易所
		return 11;
	if (exchange_name.compare("SSE") == 0 || exchange_name.compare("sse") == 0) //上海证券交易所
		return 1;
	if (exchange_name.compare("DCE") == 0 || exchange_name.compare("dce") == 0) //大连商品交易所
		return 13;
	if (exchange_name.compare("SHFE") == 0 || exchange_name.compare("shfe") == 0) //上海期货交易所
		return 12;
	if (exchange_name.compare("CZCE") == 0 || exchange_name.compare("czce") == 0) //郑州商品交易所
		return 14;
	return -1;
}

typedef std::pair<std::string, std::string> JournalPair;

inline JournalPair getMdJournalPair(short source)
{
	switch(source)
	{
		case 1:
			return {"/shared/kungfu/journal/MD/CTP/", "MD_CTP"};
		case 15:
			return {"/shared/kungfu/journal/MD/XTP/", "MD_XTP"};
		case 16:
			return {"/shared/kungfu/journal/MD/BINANCE/", "MD_BINANCE"};
		case 19:
			return {"/shared/kungfu/journal/MD/COINMEX/", "MD_COINMEX"};
		case 20:
			return {"/shared/kungfu/journal/MD/MOCK/", "MD_MOCK"};
		case 21:
			return {"/shared/kungfu/journal/MD/BITMAX/", "MD_BITMAX"};
		case 22:
			return {"/shared/kungfu/journal/MD/BITFINEX/", "MD_BITFINEX"};
		case 23:
			return {"/shared/kungfu/journal/MD/BITMEX/", "MD_BITMEX"};
		case 24:
			return {"/shared/kungfu/journal/MD/HITBTC/", "MD_HITBTC"};
		case 25:
			return {"/shared/kungfu/journal/MD/OCEANEX/", "MD_OCEANEX"};
		case 26:
			return {"/shared/kungfu/journal/MD/HUOBI/", "MD_HUOBI"};
		case 28:
			return{ "/shared/kungfu/journal/MD/PROBIT/", "MD_PROBIT" };
		default:
			return {"", ""};
	}
}

inline JournalPair getMdRawJournalPair(short source)
{
	switch(source)
	{
		case 1:
			return {"/shared/kungfu/journal/MD_RAW/CTP/", "MDRAW_CTP"};
		case 15:
			return {"/shared/kungfu/journal/MD_RAW/XTP/", "MDRAW_XTP"};
		case 19:
			return {"/shared/kungfu/journal/MD_RAW/COINMEX/", "MDRAW_COINMEX"};
		case 20:
			return {"/shared/kungfu/journal/MD_RAW/MOCK/", "MDRAW_MOCK"};
		case 21:
			return {"/shared/kungfu/journal/MD_RAW/BITMAX/", "MDRAW_BITMAX"};
		case 22:
			return {"/shared/kungfu/journal/MD_RAW/BITFINEX/", "MDRAW_BITFINEX"};
        case 23:
            return {"/shared/kungfu/journal/MD_RAW/BITMEX/", "MDRAW_BITMEX"};
		case 24:
			return {"/shared/kungfu/journal/MD_RAW/HITBTC/", "MDRAW_HITBTC"};
		case 25:
			return {"/shared/kungfu/journal/MD_RAW/OCEANEX/", "MDRAW_OCEANEX"};
        case 26:
            return {"/shared/kungfu/journal/MD_RAW/HUOBI/", "MDRAW_HUOBI"};
		case 28:
			return{ "/shared/kungfu/journal/MD_RAW/PROBIT/", "MDRAW_PROBIT" };
		default:
			return {"", ""};
	}
}

inline JournalPair getTdJournalPair(short source)
{
	switch(source)
	{
		case 1:
			return {"/shared/kungfu/journal/TD/CTP/", "TD_CTP"};
		case 15:
			return {"/shared/kungfu/journal/TD/XTP/", "TD_XTP"};
		case 16:
			return {"/shared/kungfu/journal/TD/BINANCE/", "TD_BINANCE"};
		case 19:
			return {"/shared/kungfu/journal/TD/COINMEX/", "TD_COINMEX"};
		case 20:
			return {"/shared/kungfu/journal/TD/MOCK/", "TD_MOCK"};
		case 21:
			return {"/shared/kungfu/journal/TD/BITMAX/", "TD_BITMAX"};
		case 22:
			return {"/shared/kungfu/journal/TD/BITFINEX/", "TD_BITFINEX"};
        case 23:
            return {"/shared/kungfu/journal/TD/BITMEX/", "TD_BITMEX"};
		case 24:
			return {"/shared/kungfu/journal/TD/HITBTC/", "TD_HITBTC"};
		case 25:
			return {"/shared/kungfu/journal/TD/OCEANEX/", "TD_OCEANEX"};
		case 26:
			return {"/shared/kungfu/journal/TD/HUOBI/", "TD_HUOBI"};
		case 28:
		return {"/shared/kungfu/journal/TD/PROBIT/", "TD_PROBIT"};
		default:
			return {"", ""};
	}
}

inline JournalPair getTdSendJournalPair(short source)
{
	switch(source)
	{
		case 1:
			return {"/shared/kungfu/journal/TD_SEND/CTP/", "TD_SEND_CTP"};
		case 15:
			return {"/shared/kungfu/journal/TD_SEND/XTP/", "TD_SEND_XTP"};
		case 16:
            return {"/shared/kungfu/journal/TD_SEND/BINANCE/", "TD_SEND_BINANCE"};
		case 19:
			return {"/shared/kungfu/journal/TD_SEND/COINMEX/", "TD_SEND_COINMEX"};
		case 20:
			return {"/shared/kungfu/journal/TD_SEND/MOCK/", "TD_SEND_MOCK"};
		case 21:
			return {"/shared/kungfu/journal/TD_SEND/BITMAX/", "TD_SEND_BITMAX"};
		case 22:
			return {"/shared/kungfu/journal/TD_SEND/BITFINEX/", "TD_SEND_BITFINEX"};
        case 23:
            return {"/shared/kungfu/journal/TD_SEND/BITMEX/", "TD_SEND_BITMEX"};
		case 24:
			return {"/shared/kungfu/journal/TD_SEND/HITBTC/", "TD_SEND_HITBTC"};
		case 25:
			return {"/shared/kungfu/journal/TD_SEND/OCEANEX/", "TD_SEND_OCEANEX"};
        case 26:
            return {"/shared/kungfu/journal/TD_SEND/HUOBI/", "TD_SEND_HUOBI"};
        case 28:
		return {"/shared/kungfu/journal/TD_SEND/PROBIT/", "TD_SEND_PROBIT"};
		default:
			return {"", ""};
	}
}

inline JournalPair getTdRawJournalPair(short source)
{
	switch(source)
	{
		case 1:
			return {"/shared/kungfu/journal/TD_RAW/CTP/", "TD_RAW_CTP"};
		case 15:
			return {"/shared/kungfu/journal/TD_RAW/XTP/", "TD_RAW_XTP"};
		case 16:
			return {"/shared/kungfu/journal/TD_RAW/BINANCE/", "TD_RAW_BINANCE"};
		case 19:
			return {"/shared/kungfu/journal/TD_RAW/COINMEX/", "TD_RAW_COINMEX"};
		case 20:
			return {"/shared/kungfu/journal/TD_RAW/MOCK/", "TD_RAW_MOCK"};
		case 21:
			return {"/shared/kungfu/journal/TD_RAW/BITMAX/", "TD_RAW_BITMAX"};
		case 22:
			return {"/shared/kungfu/journal/TD_RAW/BITFINEX/", "TD_RAW_BITFINEX"};
        case 23:
            return {"/shared/kungfu/journal/TD_RAW/BITMEX/", "TD_RAW_BITMEX"};
		case 24:
			return {"/shared/kungfu/journal/TD_RAW/HITBTC/", "TD_RAW_HITBTC"};
		case 25:
			return {"/shared/kungfu/journal/TD_RAW/OCEANEX/", "TD_RAW_OCEANEX"};
        case 26:
            return {"/shared/kungfu/journal/TD_RAW/HUOBI/", "TD_RAW_HUOBI"};
        case 28:
		return {"/shared/kungfu/journal/TD_RAW/PROBIT/", "TD_RAW_PROBIT"};
		default:
			return {"", ""};
	}
}

inline JournalPair getTdQJournalPair(short source)
{
	switch(source)
	{
		case 1:
			return {"/shared/kungfu/journal/TD_Q/CTP/", "TD_Q_CTP"};
		case 15:
			return {"/shared/kungfu/journal/TD_Q/XTP/", "TD_Q_XTP"};
		case 16:
			return {"/shared/kungfu/journal/TD_Q/BINANCE/", "TD_Q_BINANCE"};
		case 19:
			return {"/shared/kungfu/journal/TD_Q/COINMEX/", "TD_Q_COINMEX"};
		case 20:
			return {"/shared/kungfu/journal/TD_Q/MOCK/", "TD_Q_MOCK"};
		case 21:
			return {"/shared/kungfu/journal/TD_Q/BITMAX/", "TD_Q_BITMAX"};
		case 22:
			return {"/shared/kungfu/journal/TD_Q/BITFINEX/", "TD_Q_BITFINEX"};
        case 23:
            return {"/shared/kungfu/journal/TD_Q/BITMEX/", "TD_Q_BITMEX"};
		case 24:
			return {"/shared/kungfu/journal/TD_Q/HITBTC/", "TD_Q_HITBTC"};
		case 25:
			return {"/shared/kungfu/journal/TD_Q/OCEANEX/", "TD_Q_OCEANEX"};
        case 26:
            return {"/shared/kungfu/journal/TD_Q/HUOBI/", "TD_Q_HUOBI"};
        case 28:
		return {"/shared/kungfu/journal/TD_Q/PROBIT/", "TD_Q_PROBIT"};
		default:
			return {"", ""};
	}
}

inline std::string getLfActionFlagType(char data)
{
	switch(data)
	{
		case '1':
			return "Suspend";
		case '0':
			return "Delete";
		case '3':
			return "Modify";
		case '2':
			return "Active";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfDirectionType(char data)
{
	switch(data)
	{
		case '1':
			return "Sell";
		case '0':
			return "Buy";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfYsOrderStateType(char data)
{
	switch(data)
	{
		case 'A':
			return "YsLeftDeleted";
		case 'C':
			return "YsDeleted";
		case 'B':
			return "YsFail";
		case 'E':
			return "YsDeletedForExpire";
		case 'D':
			return "YsSuppended";
		case 'G':
			return "YsApply";
		case 'F':
			return "YsEffect";
		case '1':
			return "YsAccept";
		case '0':
			return "YsSubmit";
		case '3':
			return "YsExctriggering";
		case '2':
			return "YsTriggering";
		case '5':
			return "YsPartFinished";
		case '4':
			return "YsQueued";
		case '7':
			return "YsCanceling";
		case '6':
			return "YsFinished";
		case '9':
			return "YsCanceled";
		case '8':
			return "YsModifying";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfYsPositionEffectType(char data)
{
	switch(data)
	{
		case 'C':
			return "YsClose";
		case 'T':
			return "YsCloseToday";
		case 'O':
			return "YsOpen";
		case 'N':
			return "YsNon";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfOrderPriceTypeType(char data)
{
	switch(data)
	{
		case '1':
			return "AnyPrice";
		case '3':
			return "BestPrice";
		case '2':
			return "LimitPrice";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfForceCloseReasonType(char data)
{
	switch(data)
	{
		case '1':
			return "LackDeposit";
		case '0':
			return "NotForceClose";
		case '3':
			return "MemberOverPositionLimit";
		case '2':
			return "ClientOverPositionLimit";
		case '5':
			return "Violation";
		case '4':
			return "NotMultiple";
		case '7':
			return "PersonDeliv";
		case '6':
			return "Other";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfOffsetFlagType(char data)
{
	switch(data)
	{
		case 'N':
			return "Non";
		case '1':
			return "Close";
		case '0':
			return "Open";
		case '3':
			return "CloseToday";
		case '2':
			return "ForceClose";
		case '5':
			return "ForceOff";
		case '4':
			return "CloseYesterday";
		case '6':
			return "LocalForceClose";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfContingentConditionType(char data)
{
	switch(data)
	{
		case 'A':
			return "AskPriceGreaterEqualStopPrice";
		case 'C':
			return "AskPriceLesserEqualStopPrice";
		case 'B':
			return "AskPriceLesserThanStopPrice";
		case 'E':
			return "BidPriceGreaterEqualStopPrice";
		case 'D':
			return "BidPriceGreaterThanStopPrice";
		case 'F':
			return "BidPriceLesserThanStopPrice";
		case 'H':
			return "BidPriceLesserEqualStopPrice";
		case '1':
			return "Immediately";
		case '3':
			return "TouchProfit";
		case '2':
			return "Touch";
		case '5':
			return "LastPriceGreaterThanStopPrice";
		case '4':
			return "ParkedOrder";
		case '7':
			return "LastPriceLesserThanStopPrice";
		case '6':
			return "LastPriceGreaterEqualStopPrice";
		case '9':
			return "AskPriceGreaterThanStopPrice";
		case '8':
			return "LastPriceLesserEqualStopPrice";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfVolumeConditionType(char data)
{
	switch(data)
	{
		case '1':
			return "AV";
		case '3':
			return "CV";
		case '2':
			return "MV";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfHedgeFlagType(char data)
{
	switch(data)
	{
		case '1':
			return "Speculation";
		case '9':
			return "AllValue";
		case '3':
			return "Hedge";
		case '2':
			return "Argitrage";
		case '4':
			return "MarketMaker";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfTimeConditionType(char data)
{
	switch(data)
	{
		case 'A':
			return "FAK";
		case 'O':
			return "FOK";
		case '1':
			return "IOC";
		case '3':
			return "GFD";
		case '2':
			return "GFS";
		case '5':
			return "GTC";
		case '4':
			return "GTD";
		case '6':
			return "GFA";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfYsSideTypeType(char data)
{
	switch(data)
	{
		case 'A':
			return "YsAll";
		case 'S':
			return "YsSell";
		case 'B':
			return "YsBuy";
		case 'N':
			return "YsNon";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfYsTimeConditionType(char data)
{
	switch(data)
	{
		case '1':
			return "YsGTC";
		case '0':
			return "YsGFD";
		case '3':
			return "YsFAK";
		case '2':
			return "YsGTD";
		case '4':
			return "YsFOK";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfPositionDateType(char data)
{
	switch(data)
	{
		case '1':
			return "Today";
		case '3':
			return "Both";
		case '2':
			return "History";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfYsHedgeFlagType(char data)
{
	switch(data)
	{
		case 'N':
			return "YsNon";
		case 'B':
			return "YsB";
		case 'T':
			return "YsT";
		case 'L':
			return "YsL";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfPosiDirectionType(char data)
{
	switch(data)
	{
		case '1':
			return "Net";
		case '3':
			return "Short";
		case '2':
			return "Long";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfOrderStatusType(char data)
{
	switch(data)
	{
		case 'a':
			return "Unknown";
		case 'c':
			return "Touched";
		case 'b':
			return "NotTouched";
		case 'd':
			return "Error";
		case 'i':
			return "OrderInserted";
		case 'j':
			return "OrderAccepted";
		case '1':
			return "PartTradedQueueing";
		case '0':
			return "AllTraded";
		case '3':
			return "NoTradeQueueing";
		case '2':
			return "PartTradedNotQueueing";
		case '5':
			return "Canceled";
		case '4':
			return "NoTradeNotQueueing";
		case '6':
			return "AcceptedNoReply";
		case 'k':
			return "PendingCancel";
		default:
			return "UnExpected!";
	};
}

inline std::string getLfYsOrderTypeType(char data)
{
	switch(data)
	{
		case '1':
			return "YsMarket";
		case '2':
			return "YsLimit";
		default:
			return "UnExpected!";
	};
}
#endif
