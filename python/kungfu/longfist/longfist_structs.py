#!/usr/bin/env python
# -*- coding: utf-8 -*-
# auto generated by struct_info_parser.py, please DO NOT edit!!!

from ctypes import *

import longfist_constants as lf

from longfist_structs_sniffer import *

class LFMarketDataField(Structure):
    _fields_ = [
        ("TradingDay", c_char * 13),	# 交易日 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ("ExchangeInstID", c_char * 64),	# 合约在交易所的代码 
        ("LastPrice", c_double),	# 最新价 
        ("PreSettlementPrice", c_double),	# 上次结算价 
        ("PreClosePrice", c_double),	# 昨收盘 
        ("PreOpenInterest", c_double),	# 昨持仓量 
        ("OpenPrice", c_double),	# 今开盘 
        ("HighestPrice", c_double),	# 最高价 
        ("LowestPrice", c_double),	# 最低价 
        ("Volume", c_int),	# 数量 
        ("Turnover", c_double),	# 成交金额 
        ("OpenInterest", c_double),	# 持仓量 
        ("ClosePrice", c_double),	# 今收盘 
        ("SettlementPrice", c_double),	# 本次结算价 
        ("UpperLimitPrice", c_double),	# 涨停板价 
        ("LowerLimitPrice", c_double),	# 跌停板价 
        ("PreDelta", c_double),	# 昨虚实度 
        ("CurrDelta", c_double),	# 今虚实度 
        ("UpdateTime", c_char * 13),	# 最后修改时间 
        ("UpdateMillisec", c_uint64),	# 最后修改毫秒 
        ("BidPrice1", c_int64),	# 申买价一 
        ("BidVolume1", c_uint64),	# 申买量一 
        ("AskPrice1", c_int64),	# 申卖价一 
        ("AskVolume1", c_uint64),	# 申卖量一 
        ("BidPrice2", c_int64),	# 申买价二 
        ("BidVolume2", c_uint64),	# 申买量二 
        ("AskPrice2", c_int64),	# 申卖价二 
        ("AskVolume2", c_uint64),	# 申卖量二 
        ("BidPrice3", c_int64),	# 申买价三 
        ("BidVolume3", c_uint64),	# 申买量三 
        ("AskPrice3", c_int64),	# 申卖价三 
        ("AskVolume3", c_uint64),	# 申卖量三 
        ("BidPrice4", c_int64),	# 申买价四 
        ("BidVolume4", c_uint64),	# 申买量四 
        ("AskPrice4", c_int64),	# 申卖价四 
        ("AskVolume4", c_uint64),	# 申卖量四 
        ("BidPrice5", c_int64),	# 申买价五 
        ("BidVolume5", c_uint64),	# 申买量五 
        ("AskPrice5", c_int64),	# 申卖价五 
        ("AskVolume5", c_uint64),	# 申卖量五 
        ]

class LFPriceLevelField(Structure):
   _fields_ = [("price", c_int64), ("volume", c_uint64)]
   def __repr__(self):
      return "{}@{}".format(self.volume, self.price)

class LFPriceLevel20Field(Structure):
   _fields_ = [("levels", LFPriceLevelField * 20)]
   def __repr__(self):
      ret = ""
      for l in self.levels:
        ret += "{};".format(str(l))

      return ret

class LFPriceBook20Field(Structure):
   _fields_ = [
        ("InstrumentID", c_char * 31),	 
        ("ExchangeID", c_char * 9),	 
        ("UpdateMicroSecond", c_uint64),
        ("BidLevelCount", c_int),
        ("AskLevelCount", c_int),
        ("BidLevels", LFPriceLevel20Field),	
        ("AskLevels", LFPriceLevel20Field),	
        ]
 

class LFL2MarketDataField(Structure):
    _fields_ = [
        ("TradingDay", c_char * 9),	# 交易日 
        ("TimeStamp", c_char * 9),	# 时间戳 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("PreClosePrice", c_double),	# 昨收盘价 
        ("OpenPrice", c_double),	# 今开盘价 
        ("ClosePrice", c_double),	# 收盘价 
        ("IOPV", c_double),	# 净值估值 
        ("YieldToMaturity", c_double),	# 到期收益率 
        ("AuctionPrice", c_double),	# 动态参考价格 
        ("TradingPhase", c_char),	# 交易阶段 char
        ("OpenRestriction", c_char),	# 开仓限制 char
        ("HighPrice", c_double),	# 最高价 
        ("LowPrice", c_double),	# 最低价 
        ("LastPrice", c_double),	# 最新价 
        ("TradeCount", c_double),	# 成交笔数 
        ("TotalTradeVolume", c_double),	# 成交总量 
        ("TotalTradeValue", c_double),	# 成交总金额 
        ("OpenInterest", c_double),	# 持仓量 
        ("TotalBidVolume", c_double),	# 委托买入总量 
        ("WeightedAvgBidPrice", c_double),	# 加权平均委买价 
        ("AltWeightedAvgBidPrice", c_double),	# 债券加权平均委买价 
        ("TotalOfferVolume", c_double),	# 委托卖出总量 
        ("WeightedAvgOfferPrice", c_double),	# 加权平均委卖价 
        ("AltWeightedAvgOfferPrice", c_double),	# 债券加权平均委卖价格 
        ("BidPriceLevel", c_int),	# 买价深度 
        ("OfferPriceLevel", c_int),	# 卖价深度 
        ("BidPrice1", c_double),	# 申买价一 
        ("BidVolume1", c_double),	# 申买量一 
        ("BidCount1", c_int),	# 实际买总委托笔数一 
        ("BidPrice2", c_double),	# 申买价二 
        ("BidVolume2", c_double),	# 申买量二 
        ("BidCount2", c_int),	# 实际买总委托笔数二 
        ("BidPrice3", c_double),	# 申买价三 
        ("BidVolume3", c_double),	# 申买量三 
        ("BidCount3", c_int),	# 实际买总委托笔数三 
        ("BidPrice4", c_double),	# 申买价四 
        ("BidVolume4", c_double),	# 申买量四 
        ("BidCount4", c_int),	# 实际买总委托笔数四 
        ("BidPrice5", c_double),	# 申买价五 
        ("BidVolume5", c_double),	# 申买量五 
        ("BidCount5", c_int),	# 实际买总委托笔数五 
        ("BidPrice6", c_double),	# 申买价六 
        ("BidVolume6", c_double),	# 申买量六 
        ("BidCount6", c_int),	# 实际买总委托笔数六 
        ("BidPrice7", c_double),	# 申买价七 
        ("BidVolume7", c_double),	# 申买量七 
        ("BidCount7", c_int),	# 实际买总委托笔数七 
        ("BidPrice8", c_double),	# 申买价八 
        ("BidVolume8", c_double),	# 申买量八 
        ("BidCount8", c_int),	# 实际买总委托笔数八 
        ("BidPrice9", c_double),	# 申买价九 
        ("BidVolume9", c_double),	# 申买量九 
        ("BidCount9", c_int),	# 实际买总委托笔数九 
        ("BidPriceA", c_double),	# 申买价十 
        ("BidVolumeA", c_double),	# 申买量十 
        ("BidCountA", c_int),	# 实际买总委托笔数十 
        ("OfferPrice1", c_double),	# 申卖价一 
        ("OfferVolume1", c_double),	# 申卖量一 
        ("OfferCount1", c_int),	# 实际卖总委托笔数一 
        ("OfferPrice2", c_double),	# 申卖价二 
        ("OfferVolume2", c_double),	# 申卖量二 
        ("OfferCount2", c_int),	# 实际卖总委托笔数二 
        ("OfferPrice3", c_double),	# 申卖价三 
        ("OfferVolume3", c_double),	# 申卖量三 
        ("OfferCount3", c_int),	# 实际卖总委托笔数三 
        ("OfferPrice4", c_double),	# 申卖价四 
        ("OfferVolume4", c_double),	# 申卖量四 
        ("OfferCount4", c_int),	# 实际卖总委托笔数四 
        ("OfferPrice5", c_double),	# 申卖价五 
        ("OfferVolume5", c_double),	# 申卖量五 
        ("OfferCount5", c_int),	# 实际卖总委托笔数五 
        ("OfferPrice6", c_double),	# 申卖价六 
        ("OfferVolume6", c_double),	# 申卖量六 
        ("OfferCount6", c_int),	# 实际卖总委托笔数六 
        ("OfferPrice7", c_double),	# 申卖价七 
        ("OfferVolume7", c_double),	# 申卖量七 
        ("OfferCount7", c_int),	# 实际卖总委托笔数七 
        ("OfferPrice8", c_double),	# 申卖价八 
        ("OfferVolume8", c_double),	# 申卖量八 
        ("OfferCount8", c_int),	# 实际卖总委托笔数八 
        ("OfferPrice9", c_double),	# 申卖价九 
        ("OfferVolume9", c_double),	# 申卖量九 
        ("OfferCount9", c_int),	# 实际卖总委托笔数九 
        ("OfferPriceA", c_double),	# 申卖价十 
        ("OfferVolumeA", c_double),	# 申卖量十 
        ("OfferCountA", c_int),	# 实际卖总委托笔数十 
        ("InstrumentStatus", c_char * 7),	# 合约状态 
        ("PreIOPV", c_double),	# 昨净值估值 
        ("PERatio1", c_double),	# 市盈率一 
        ("PERatio2", c_double),	# 市盈率二 
        ("UpperLimitPrice", c_double),	# 涨停价 
        ("LowerLimitPrice", c_double),	# 跌停价 
        ("WarrantPremiumRatio", c_double),	# 权证溢价率 
        ("TotalWarrantExecQty", c_double),	# 权证执行总数量 
        ("PriceDiff1", c_double),	# 升跌一 
        ("PriceDiff2", c_double),	# 升跌二 
        ("ETFBuyNumber", c_double),	# ETF申购笔数 
        ("ETFBuyAmount", c_double),	# ETF申购数量 
        ("ETFBuyMoney", c_double),	# ETF申购金额 
        ("ETFSellNumber", c_double),	# ETF赎回笔数 
        ("ETFSellAmount", c_double),	# ETF赎回数量 
        ("ETFSellMoney", c_double),	# ETF赎回金额 
        ("WithdrawBuyNumber", c_double),	# 买入撤单笔数 
        ("WithdrawBuyAmount", c_double),	# 买入撤单数量 
        ("WithdrawBuyMoney", c_double),	# 买入撤单金额 
        ("TotalBidNumber", c_double),	# 买入总笔数 
        ("BidTradeMaxDuration", c_double),	# 买入委托成交最大等待时间 
        ("NumBidOrders", c_double),	# 买方委托价位数 
        ("WithdrawSellNumber", c_double),	# 卖出撤单笔数 
        ("WithdrawSellAmount", c_double),	# 卖出撤单数量 
        ("WithdrawSellMoney", c_double),	# 卖出撤单金额 
        ("TotalOfferNumber", c_double),	# 卖出总笔数 
        ("OfferTradeMaxDuration", c_double),	# 卖出委托成交最大等待时间 
        ("NumOfferOrders", c_double),	# 卖方委托价位数 
        ]

class LFL2IndexField(Structure):
    _fields_ = [
        ("TradingDay", c_char * 9),	# 交易日 
        ("TimeStamp", c_char * 9),	# 行情时间（秒） 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ("InstrumentID", c_char * 31),	# 指数代码 
        ("PreCloseIndex", c_double),	# 前收盘指数 
        ("OpenIndex", c_double),	# 今开盘指数 
        ("CloseIndex", c_double),	# 今日收盘指数 
        ("HighIndex", c_double),	# 最高指数 
        ("LowIndex", c_double),	# 最低指数 
        ("LastIndex", c_double),	# 最新指数 
        ("TurnOver", c_double),	# 参与计算相应指数的成交金额（元） 
        ("TotalVolume", c_double),	# 参与计算相应指数的交易数量（手） 
        ]

class LFL2OrderField(Structure):
    _fields_ = [
        ("OrderTime", c_char * 9),	# 委托时间（秒） 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("Price", c_double),	# 委托价格 
        ("Volume", c_double),	# 委托数量 
        ("OrderKind", c_char * 2),	# 报单类型 
        ]

class LFL2TradeField(Structure):
    _fields_ = [
        ("TradeTime", c_char * 9),	# 成交时间（秒） 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("Price", c_int64),	# 成交价格 
        ("Volume", c_uint64),	# 成交数量 
        ("OrderKind", c_char * 2),	# 报单类型 
        ("OrderBSFlag", c_char * 2),	# 内外盘标志 
        ]

class LFBarMarketDataField(Structure):
    _fields_ = [
        ("TradingDay", c_char * 9),	# 交易日 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("UpperLimitPrice", c_int64),	# 涨停板价 
        ("LowerLimitPrice", c_int64),	# 跌停板价 
        ("StartUpdateTime", c_char * 13),	# 首tick修改时间 
        ("StartUpdateMillisec", c_int),	# 首tick最后修改毫秒 
        ("EndUpdateTime", c_char * 13),	# 尾tick最后修改时间 
        ("EndUpdateMillisec", c_int),	# 尾tick最后修改毫秒 
        ("PeriodMillisec", c_int),	
        ("Open", c_int64),	# 开 
        ("Close", c_int64),	# 收 
        ("Low", c_int64),	# 低 
        ("High", c_int64),	# 高 
        ("Volume", c_uint64),	# 区间交易量 
        ("StartVolume", c_uint64),	# 初始总交易量 
        ]

class LFQryPositionField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ]

class LFRspPositionField(Structure):
    _fields_ = [
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("YdPosition", c_uint64),	# 上日持仓
        ("Position", c_uint64),	# 总持仓
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("PositionCost", c_int64),	# 持仓成本
        ("HedgeFlag", c_char),	# 投机套保标志 LfHedgeFlagType
        ("PosiDirection", c_char),	# 持仓多空方向 LfPosiDirectionType
        ]

class LFInputOrderField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("UserID", c_char * 16),	# 用户代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("BusinessUnit", c_char * 21),	# 业务单元 
        ("ExchangeID", c_char * 9),	# 交易所代码 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("OrderRef", c_char * 21),	# 报单引用 
        ("LimitPrice", c_int64),	# 价格 
        ("Volume", c_uint64),	# 数量 
        ("MinVolume", c_uint64),	# 最小成交量 
        ("TimeCondition", c_char),	# 有效期类型 LfTimeConditionType
        ("VolumeCondition", c_char),	# 成交量类型 LfVolumeConditionType
        ("OrderPriceType", c_char),	# 报单价格条件 LfOrderPriceTypeType
        ("Direction", c_char),	# 买卖方向 LfDirectionType
        ("OffsetFlag", c_char),	# 开平标志 LfOffsetFlagType
        ("HedgeFlag", c_char),	# 投机套保标志 LfHedgeFlagType
        ("ForceCloseReason", c_char),	# 强平原因 LfForceCloseReasonType
        ("StopPrice", c_double),	# 止损价 
        ("IsAutoSuspend", c_int),	# 自动挂起标志 
        ("ContingentCondition", c_char),	# 触发条件 LfContingentConditionType
        ("MiscInfo", c_char * 30),	# 委托自定义标签 
        ("MassOrderSeqId", c_uint64),	
        ("MassOrderIndex", c_int),	
        ("MassOrderTotalNum", c_int),	
        ]

class LFRtnOrderField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("UserID", c_char * 16),	# 用户代码 
        ("ParticipantID", c_char * 11),	# 会员代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("BusinessUnit", c_char * 21),	# 业务单元 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("OrderRef", c_char * 21),	# 报单引用 
        ("ExchangeID", c_char * 11),	# 交易所代码 
        ("LimitPrice", c_int64),	# 价格 
        ("VolumeTraded", c_uint64),	# 今成交数量 
        ("VolumeTotal", c_uint64),	# 剩余数量 
        ("VolumeTotalOriginal", c_uint64),	# 数量 
        ("TimeCondition", c_char),	# 有效期类型 LfTimeConditionType
        ("VolumeCondition", c_char),	# 成交量类型 LfVolumeConditionType
        ("OrderPriceType", c_char),	# 报单价格条件 LfOrderPriceTypeType
        ("Direction", c_char),	# 买卖方向 LfDirectionType
        ("OffsetFlag", c_char),	# 开平标志 LfOffsetFlagType
        ("HedgeFlag", c_char),	# 投机套保标志 LfHedgeFlagType
        ("OrderStatus", c_char),	# 报单状态 LfOrderStatusType
        ("RequestID", c_int),	# 请求编号 
        ]

class LFRtnTradeField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("UserID", c_char * 16),	# 用户代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("BusinessUnit", c_char * 21),	# 业务单元 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("OrderRef", c_char * 21),	# 报单引用 
        ("ExchangeID", c_char * 11),	# 交易所代码 
        ("TradeID", c_char * 21),	# 成交编号 
        ("OrderSysID", c_char * 31),	# 报单编号 
        ("ParticipantID", c_char * 11),	# 会员代码 
        ("ClientID", c_char * 21),	# 客户代码 
        ("Price", c_int64),	# 价格 
        ("Volume", c_uint64),	# 数量 
        ("TradingDay", c_char * 13),	# 交易日 
        ("TradeTime", c_char * 13),	# 成交时间 
        ("Direction", c_char),	# 买卖方向 LfDirectionType
        ("OffsetFlag", c_char),	# 开平标志 LfOffsetFlagType
        ("HedgeFlag", c_char),	# 投机套保标志 LfHedgeFlagType
        ]

class LFOrderActionField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("InstrumentID", c_char * 31),	# 合约代码 
        ("ExchangeID", c_char * 11),	# 交易所代码 
        ("UserID", c_char * 16),	# 用户代码 
        ("OrderRef", c_char * 21),	# 报单引用 
        ("OrderSysID", c_char * 31),	# 报单编号 
        ("RequestID", c_int),	# 请求编号 
        ("ActionFlag", c_char),	# 报单操作标志 char
        ("LimitPrice", c_int64),	# 价格 
        ("VolumeChange", c_uint64),	# 数量变化 
        ("KfOrderID", c_int),	# Kf系统内订单ID 
        ("MassOrderSeqId", c_uint64),	
        ("MassOrderIndex", c_int),	
        ("MassOrderTotalNum", c_int),	
        ]

class LFQryAccountField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ]

class LFRspAccountField(Structure):
    _fields_ = [
        ("BrokerID", c_char * 11),	# 经纪公司代码 
        ("InvestorID", c_char * 19),	# 投资者代码 
        ("PreMortgage", c_double),	# 上次质押金额 
        ("PreCredit", c_double),	# 上次信用额度 
        ("PreDeposit", c_double),	# 上次存款额 
        ("preBalance", c_double),	# 上次结算准备金 
        ("PreMargin", c_double),	# 上次占用的保证金 
        ("Deposit", c_double),	# 入金金额 
        ("Withdraw", c_double),	# 出金金额 
        ("FrozenMargin", c_double),	# 冻结的保证金（报单未成交冻结的保证金） 
        ("FrozenCash", c_double),	# 冻结的资金（报单未成交冻结的总资金） 
        ("FrozenCommission", c_double),	# 冻结的手续费（报单未成交冻结的手续费） 
        ("CurrMargin", c_double),	# 当前保证金总额 
        ("CashIn", c_double),	# 资金差额 
        ("Commission", c_double),	# 手续费 
        ("CloseProfit", c_double),	# 平仓盈亏 
        ("PositionProfit", c_double),	# 持仓盈亏 
        ("Balance", c_double),	# 结算准备金 
        ("Available", c_double),	# 可用资金 
        ("WithdrawQuota", c_double),	# 可取资金 
        ("Reserve", c_double),	# 基本准备金 
        ("TradingDay", c_char * 9),	# 交易日 
        ("Credit", c_double),	# 信用额度 
        ("Mortgage", c_double),	# 质押金额 
        ("ExchangeMargin", c_double),	# 交易所保证金 
        ("DeliveryMargin", c_double),	# 投资者交割保证金 
        ("ExchangeDeliveryMargin", c_double),	# 交易所交割保证金 
        ("ReserveBalance", c_double),	# 保底期货结算准备金 
        ("Equity", c_double),	# 当日权益 
        ("MarketValue", c_double),	# 账户市值 
        ]

DataFieldMap = {
	'LFL2MarketDataField': {
		'OfferVolumeA': 'd',
		'TotalOfferNumber': 'd',
		'WithdrawSellAmount': 'd',
		'BidCount3': 'i',
		'BidCount2': 'i',
		'BidCount1': 'i',
		'BidCount7': 'i',
		'BidCount6': 'i',
		'BidCount5': 'i',
		'BidCount4': 'i',
		'BidVolume7': 'd',
		'BidVolume6': 'd',
		'BidCount9': 'i',
		'BidCount8': 'i',
		'BidVolume3': 'd',
		'BidVolume2': 'd',
		'BidVolume1': 'd',
		'TradeCount': 'd',
		'BidPrice6': 'd',
		'PreIOPV': 'd',
		'TimeStamp': 'c9',
		'TradingDay': 'c9',
		'BidCountA': 'i',
		'OpenInterest': 'd',
		'BidVolumeA': 'd',
		'NumOfferOrders': 'd',
		'OfferVolume4': 'd',
		'OfferVolume5': 'd',
		'OfferVolume6': 'd',
		'OfferVolume7': 'd',
		'OfferVolume1': 'd',
		'OfferVolume2': 'd',
		'OfferVolume3': 'd',
		'OfferVolume8': 'd',
		'OfferVolume9': 'd',
		'ETFSellMoney': 'd',
		'TotalTradeVolume': 'd',
		'PriceDiff1': 'd',
		'PriceDiff2': 'd',
		'OfferPriceA': 'd',
		'BidPriceLevel': 'i',
		'TotalOfferVolume': 'd',
		'OfferPriceLevel': 'i',
		'InstrumentStatus': 'c7',
		'NumBidOrders': 'd',
		'ETFSellAmount': 'd',
		'WithdrawSellNumber': 'd',
		'AltWeightedAvgBidPrice': 'd',
		'WeightedAvgBidPrice': 'd',
		'OfferPrice8': 'd',
		'BidVolume9': 'd',
		'WithdrawBuyMoney': 'd',
		'OfferPrice4': 'd',
		'BidVolume8': 'd',
		'OfferPrice6': 'd',
		'OfferPrice7': 'd',
		'OfferPrice1': 'd',
		'OfferPrice2': 'd',
		'OfferPrice3': 'd',
		'WithdrawBuyAmount': 'd',
		'BidVolume5': 'd',
		'BidVolume4': 'd',
		'BidPrice9': 'd',
		'BidPrice8': 'd',
		'BidPrice5': 'd',
		'BidPrice4': 'd',
		'BidPrice7': 'd',
		'AltWeightedAvgOfferPrice': 'd',
		'BidPrice1': 'd',
		'TotalWarrantExecQty': 'd',
		'BidPrice3': 'd',
		'BidPrice2': 'd',
		'LowerLimitPrice': 'd',
		'OpenPrice': 'd',
		'WithdrawSellMoney': 'd',
		'OfferTradeMaxDuration': 'd',
		'OfferCount7': 'i',
		'WarrantPremiumRatio': 'd',
		'ExchangeID': 'c9',
		'ETFSellNumber': 'd',
		'AuctionPrice': 'd',
		'OfferPrice9': 'd',
		'YieldToMaturity': 'd',
		'OfferPrice5': 'd',
		'TradingPhase': 'c',
		'BidPriceA': 'd',
		'PERatio2': 'd',
		'TotalBidVolume': 'd',
		'PERatio1': 'd',
		'OfferCount8': 'i',
		'OfferCount9': 'i',
		'OfferCount6': 'i',
		'LowPrice': 'd',
		'OfferCount4': 'i',
		'OfferCount5': 'i',
		'OfferCount2': 'i',
		'OfferCount3': 'i',
		'TotalBidNumber': 'd',
		'OfferCount1': 'i',
		'WithdrawBuyNumber': 'd',
		'OpenRestriction': 'c',
		'BidTradeMaxDuration': 'd',
		'PreClosePrice': 'd',
		'UpperLimitPrice': 'd',
		'WeightedAvgOfferPrice': 'd',
		'InstrumentID': 'c31',
		'ClosePrice': 'd',
		'HighPrice': 'd',
		'TotalTradeValue': 'd',
		'IOPV': 'd',
		'LastPrice': 'd',
		'ETFBuyNumber': 'd',
		'ETFBuyMoney': 'd',
		'ETFBuyAmount': 'd',
		'OfferCountA': 'i',
	},
	'LFRtnTradeField': {
		'InstrumentID': 'c31',
		'ExchangeID': 'c11',
		'ParticipantID': 'c11',
		'TradeID': 'c21',
		'TradingDay': 'c13',
		'BusinessUnit': 'c21',
		'HedgeFlag': lf.LfHedgeFlagTypeMap,
		'Price': 'd',
		'UserID': 'c16',
		'Direction': lf.LfDirectionTypeMap,
		'ClientID': 'c21',
		'OrderRef': 'c21',
		'Volume': 'i',
		'InvestorID': 'c19',
		'BrokerID': 'c11',
		'OrderSysID': 'c31',
		'TradeTime': 'c13',
		'OffsetFlag': lf.LfOffsetFlagTypeMap,
	},
	'LFRspAccountField': {
		'Mortgage': 'd',
		'ExchangeDeliveryMargin': 'd',
		'FrozenMargin': 'd',
		'WithdrawQuota': 'd',
		'PositionProfit': 'd',
		'Commission': 'd',
		'Equity': 'd',
		'CashIn': 'd',
		'Available': 'd',
		'InvestorID': 'c19',
		'PreCredit': 'd',
		'PreMortgage': 'd',
		'ExchangeMargin': 'd',
		'PreMargin': 'd',
		'DeliveryMargin': 'd',
		'preBalance': 'd',
		'TradingDay': 'c9',
		'BrokerID': 'c11',
		'Deposit': 'd',
		'Withdraw': 'd',
		'Balance': 'd',
		'Reserve': 'd',
		'PreDeposit': 'd',
		'Credit': 'd',
		'MarketValue': 'd',
		'ReserveBalance': 'd',
		'CurrMargin': 'd',
		'FrozenCommission': 'd',
		'CloseProfit': 'd',
		'FrozenCash': 'd',
	},
	'LFL2IndexField': {
		'InstrumentID': 'c31',
		'ExchangeID': 'c9',
		'HighIndex': 'd',
		'TimeStamp': 'c9',
		'CloseIndex': 'd',
		'PreCloseIndex': 'd',
		'LastIndex': 'd',
		'TradingDay': 'c9',
		'OpenIndex': 'd',
		'TotalVolume': 'd',
		'LowIndex': 'd',
		'TurnOver': 'd',
	},
	'LFL2OrderField': {
		'InstrumentID': 'c31',
		'OrderTime': 'c9',
		'OrderKind': 'c2',
		'Price': 'd',
		'ExchangeID': 'c9',
		'Volume': 'd',
	},
	'LFQryPositionField': {
		'InstrumentID': 'c31',
		'InvestorID': 'c19',
		'ExchangeID': 'c9',
		'BrokerID': 'c11',
	},
	'LFInputOrderField': {
		'InstrumentID': 'c31',
		'ContingentCondition': lf.LfContingentConditionTypeMap,
		'ExchangeID': 'c9',
		'MinVolume': 'i',
		'OffsetFlag': lf.LfOffsetFlagTypeMap,
		'OrderPriceType': lf.LfOrderPriceTypeTypeMap,
		'BusinessUnit': 'c21',
		'HedgeFlag': lf.LfHedgeFlagTypeMap,
		'IsAutoSuspend': 'i',
		'ForceCloseReason': lf.LfForceCloseReasonTypeMap,
		'UserID': 'c16',
		'Direction': lf.LfDirectionTypeMap,
		'LimitPrice': 'd',
		'OrderRef': 'c21',
		'Volume': 'i',
		'InvestorID': 'c19',
		'VolumeCondition': lf.LfVolumeConditionTypeMap,
		'TimeCondition': lf.LfTimeConditionTypeMap,
		'BrokerID': 'c11',
		'MiscInfo': 'c30',
		'StopPrice': 'd',
        'MassOrderSeqId':'i64',
	    'MassOrderIndex':'i',
	    'MassOrderTotalNum':'i',
	},
	'LFRtnOrderField': {
		'InstrumentID': 'c31',
		'ExchangeID': 'c11',
		'ParticipantID': 'c11',
		'OrderPriceType': lf.LfOrderPriceTypeTypeMap,
		'BusinessUnit': 'c21',
		'HedgeFlag': lf.LfHedgeFlagTypeMap,
		'VolumeTotalOriginal': 'i',
		'RequestID': 'i',
		'UserID': 'c16',
		'Direction': lf.LfDirectionTypeMap,
		'LimitPrice': 'd',
		'OrderRef': 'c21',
		'InvestorID': 'c19',
		'VolumeCondition': lf.LfVolumeConditionTypeMap,
		'TimeCondition': lf.LfTimeConditionTypeMap,
		'BrokerID': 'c11',
		'OrderStatus': lf.LfOrderStatusTypeMap,
		'VolumeTraded': 'i',
		'VolumeTotal': 'i',
		'OffsetFlag': lf.LfOffsetFlagTypeMap,
	},
	'LFQryAccountField': {
		'InvestorID': 'c19',
		'BrokerID': 'c11',
	},
	'LFMarketDataField': {
		'HighestPrice': 'd',
		'BidPrice5': 'd',
		'BidPrice4': 'd',
		'BidPrice1': 'd',
		'BidPrice3': 'd',
		'BidPrice2': 'd',
		'LowerLimitPrice': 'd',
		'OpenPrice': 'd',
		'AskPrice5': 'd',
		'AskPrice4': 'd',
		'AskPrice3': 'd',
		'PreClosePrice': 'd',
		'AskPrice1': 'd',
		'PreSettlementPrice': 'd',
		'AskVolume1': 'i',
		'UpdateTime': 'c13',
		'UpdateMillisec': 'i',
		'BidVolume5': 'i',
		'BidVolume4': 'i',
		'BidVolume3': 'i',
		'BidVolume2': 'i',
		'PreOpenInterest': 'd',
		'AskPrice2': 'd',
		'Volume': 'i',
		'AskVolume3': 'i',
		'AskVolume2': 'i',
		'AskVolume5': 'i',
		'AskVolume4': 'i',
		'UpperLimitPrice': 'd',
		'BidVolume1': 'i',
		'InstrumentID': 'c31',
		'ClosePrice': 'd',
		'ExchangeID': 'c9',
		'TradingDay': 'c13',
		'PreDelta': 'd',
		'OpenInterest': 'd',
		'CurrDelta': 'd',
		'Turnover': 'd',
		'LastPrice': 'd',
		'SettlementPrice': 'd',
		'ExchangeInstID': 'c64',
		'LowestPrice': 'd',
	},
	'LFPriceBook20Field': {
		'InstrumentID' : 'c31',	 
        'ExchangeID' : 'c9',	 
        'UpdateMicroSecond' : 'i',
        'BidLevelCount' : 'i',
        'AskLevelCount' : 'i',
        'BidLevels' : [],	
        'AskLevels' : [],	
	},
	'LFRspPositionField': {
		'InstrumentID': 'c31',
		'PosiDirection': lf.LfPosiDirectionTypeMap,
		'HedgeFlag': lf.LfHedgeFlagTypeMap,
		'YdPosition': 'i',
		'InvestorID': 'c19',
		'PositionCost': 'd',
		'BrokerID': 'c11',
		'Position': 'i',
	},
	'LFBarMarketDataField': {
		'InstrumentID': 'c31',
		'Volume': 'd',
		'StartVolume': 'd',
		'EndUpdateMillisec': 'i',
		'PeriodMillisec': 'i',
		'High': 'd',
		'TradingDay': 'c9',
		'LowerLimitPrice': 'd',
		'Low': 'd',
		'UpperLimitPrice': 'd',
		'Close': 'd',
		'EndUpdateTime': 'c13',
		'StartUpdateTime': 'c13',
		'Open': 'd',
		'StartUpdateMillisec': 'i',
	},
	'LFL2TradeField': {
		'InstrumentID': 'c31',
		'ExchangeID': 'c9',
		'OrderKind': 'c2',
		'OrderBSFlag': 'c2',
		'Price': 'd',
		'Volume': 'd',
		'TradeTime': 'c9',
	},
	'LFOrderActionField': {
		'InstrumentID': 'c31',
		'ExchangeID': 'c11',
		'ActionFlag': 'c',
		'KfOrderID': 'i',
		'UserID': 'c16',
		'LimitPrice': 'd',
		'OrderRef': 'c21',
		'InvestorID': 'c19',
		'VolumeChange': 'i',
		'BrokerID': 'c11',
		'RequestID': 'i',
		'OrderSysID': 'c31',
        'MassOrderSeqId':'i64',
	    'MassOrderIndex':'i',
	    'MassOrderTotalNum':'i',
	},
}

MsgType2LFStruct = {
    lf.MsgTypes.MD: LFMarketDataField,
    lf.MsgTypes.L2_MD: LFL2MarketDataField,
    lf.MsgTypes.PRICE_BOOK_20: LFPriceBook20Field,
    lf.MsgTypes.L2_INDEX: LFL2IndexField,
    lf.MsgTypes.L2_ORDER: LFL2OrderField,
    lf.MsgTypes.L2_TRADE: LFL2TradeField,
    lf.MsgTypes.BAR_MD: LFBarMarketDataField,
    lf.MsgTypes.QRY_POS: LFQryPositionField,
    lf.MsgTypes.RSP_POS: LFRspPositionField,
    lf.MsgTypes.ORDER: LFInputOrderField,
    lf.MsgTypes.RTN_ORDER: LFRtnOrderField,
    lf.MsgTypes.RTN_TRADE: LFRtnTradeField,
    lf.MsgTypes.ORDER_ACTION: LFOrderActionField,
    lf.MsgTypes.QRY_ACCOUNT: LFQryAccountField,
    lf.MsgTypes.RSP_ACCOUNT: LFRspAccountField,

    lf.MsgTypes.MSG_TYPE_LF_MD_BINANCE: LFMarketDataField,
    lf.MsgTypes.MSG_TYPE_LF_QRY_POS_BINANCE: LFQryPositionField,
    lf.MsgTypes.MSG_TYPE_LF_RSP_POS_BINANCE: LFRspPositionField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_BINANCE: LFInputOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_ORDER_BINANCE: LFRtnOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_TRADE_BINANCE: LFRtnTradeField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_ACTION_BINANCE: LFOrderActionField,

    lf.MsgTypes.MSG_TYPE_LF_MD_INDODAX: LFMarketDataField,
    # lf.MsgTypes.QRY_POS: LFQryPositionField,
    # lf.MsgTypes.RSP_POS: LFRspPositionField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_INDODAX: LFInputOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_ORDER_INDODAX: LFRtnOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_TRADE_INDODAX: LFRtnTradeField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_ACTION_INDODAX: LFOrderActionField,

    lf.MsgTypes.MSG_TYPE_LF_MD_OKEX: LFMarketDataField,
    # lf.MsgTypes.QRY_POS: LFQryPositionField,
    # lf.MsgTypes.RSP_POS: LFRspPositionField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_OKEX: LFInputOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_ORDER_OKEX: LFRtnOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_TRADE_OKEX: LFRtnTradeField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_ACTION_OKEX: LFOrderActionField,

    lf.MsgTypes.MSG_TYPE_LF_MD_COINMEX: LFMarketDataField,
    lf.MsgTypes.MSG_TYPE_LF_QRY_POS_COINMEX: LFQryPositionField,
    lf.MsgTypes.MSG_TYPE_LF_RSP_POS_COINMEX: LFRspPositionField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_COINMEX: LFInputOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_ORDER_COINMEX: LFRtnOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_TRADE_COINMEX: LFRtnTradeField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_ACTION_COINMEX: LFOrderActionField,

    lf.MsgTypes.MSG_TYPE_LF_MD_OCEANEX: LFMarketDataField,
    lf.MsgTypes.MSG_TYPE_LF_QRY_POS_OCEANEX: LFQryPositionField,
    lf.MsgTypes.MSG_TYPE_LF_RSP_POS_OCEANEX: LFRspPositionField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_OCEANEX: LFInputOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_ORDER_OCEANEX: LFRtnOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_TRADE_OCEANEX: LFRtnTradeField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_ACTION_OCEANEX: LFOrderActionField,

    lf.MsgTypes.MSG_TYPE_LF_QRY_POS_OCEANEX2: LFQryPositionField,
    lf.MsgTypes.MSG_TYPE_LF_RSP_POS_OCEANEX2: LFRspPositionField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_OCEANEX2: LFInputOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_ORDER_OCEANEX2: LFRtnOrderField,
    lf.MsgTypes.MSG_TYPE_LF_RTN_TRADE_OCEANEX2: LFRtnTradeField,
    lf.MsgTypes.MSG_TYPE_LF_ORDER_ACTION_OCEANEX2: LFOrderActionField


}

MsgType2LFStruct.update(SnifferMsgType2Struct)

LFStruct2MsgType = {
    LFMarketDataField: lf.MsgTypes.MD,
    LFPriceBook20Field: lf.MsgTypes.PRICE_BOOK_20,
    LFL2MarketDataField: lf.MsgTypes.L2_MD,
    LFL2IndexField: lf.MsgTypes.L2_INDEX,
    LFL2OrderField: lf.MsgTypes.L2_ORDER,
    LFL2TradeField: lf.MsgTypes.L2_TRADE,
    LFBarMarketDataField: lf.MsgTypes.BAR_MD,
    LFQryPositionField: lf.MsgTypes.QRY_POS,
    LFRspPositionField: lf.MsgTypes.RSP_POS,
    LFInputOrderField: lf.MsgTypes.ORDER,
    LFRtnOrderField: lf.MsgTypes.RTN_ORDER,
    LFRtnTradeField: lf.MsgTypes.RTN_TRADE,
    LFOrderActionField: lf.MsgTypes.ORDER_ACTION,
    LFQryAccountField: lf.MsgTypes.QRY_ACCOUNT,
    LFRspAccountField: lf.MsgTypes.RSP_ACCOUNT,
}
