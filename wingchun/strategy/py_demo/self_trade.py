'''
Copyright [2017] [taurus.ai]

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
'''

'''
test limit order and order cancelling for new wingchun strategy system.
you may run this program by:
wingchun strategy -n my_test -p binance_order_cancel_test.py
'''

import functools, random

global_order_id_ticker_map = {}
global_pair_order_id = {}
global_cancel_order_id = {}

def print_log(context, text):
    print text
    context.log_info(text)

def get_source_from_exchange(exch_name):
    if exch_name == "oceanex":
        return SOURCE.OCEANEX
    elif exch_name == "probit":
        return SOURCE.PROBIT
    else:
        return None

def timer_callback(symbol, context):
    total_period = context.white_list[symbol][0]
    next_period = context.white_list[symbol][1]
    if next_period in (0, -1):
        next_trade_period = random.randint(1, total_period)
        context.white_list[symbol][1] =  total_period - next_trade_period
        context.insert_func_after_c(next_trade_period, functools.partial(timer_callback, symbol))
    else:
        
        if context.white_list[symbol][5] == 1 and context.order_status[symbol][2] == 0 and context.order_status[symbol][3] == 0:
            trade_price = context.white_list[symbol][2]
            spread = int(context.white_list[symbol][4] / 3)
            trade_price += random.randint(-1 * spread, spread)
            trade_price = int(trade_price * context.price_skew[symbol])

            trade_volume = context.white_list[symbol][3]
            qty_denom = context.qty_denom[symbol]
            new_qty_denom = random.randint(int(qty_denom/2), qty_denom)
            trade_volume = int(trade_volume / new_qty_denom)
     
            log_text = "trying to self trade, symbol {} trade price {} trade volume {} mid price {}".format(symbol, trade_price, trade_volume, context.white_list[symbol][2])
            print_log(context, log_text)
            buy_order_rid = context.insert_limit_order(source=context.exch_src,
                                                         ticker=symbol,
                                                         price=trade_price,
                                                         exchange_id=context.exch_id,
                                                         volume=trade_volume,
                                                         direction=DIRECTION.Buy,
                                                         offset=OFFSET.Open)
		
            sell_order_rid = context.insert_limit_order(source=context.exch_src,
                                                         ticker=symbol,
                                                         price=trade_price,
                                                         exchange_id=context.exch_id,
                                                         volume=trade_volume,
                                                         direction=DIRECTION.Sell,
                                                         offset=OFFSET.Open)

            global_order_id_ticker_map[buy_order_rid] = symbol
            global_order_id_ticker_map[sell_order_rid] = symbol

            global_pair_order_id[buy_order_rid] = sell_order_rid
            global_pair_order_id[sell_order_rid] = buy_order_rid

            context.order_status[symbol][0] = buy_order_rid
            context.order_status[symbol][1] = sell_order_rid
            context.order_status[symbol][2] = 2
            context.order_status[symbol][3] = 2
            log_text = "self trade orders: {} {}".format(symbol, context.order_status[symbol])
            print_log(context, log_text)
        else:
            print_log(context, "can not do self trade now because the previous trade is still pending or invalid market data...")
		
        context.white_list[symbol][1] = -1
        context.insert_func_after_c(next_period, functools.partial(timer_callback, symbol))

def initialize(context):
    context.white_list = {}
    context.order_status = {}
    context.qty_denom = {}
    context.price_skew = {}
    import os
    if "config" in os.environ:
        import json
        with open(os.environ["config"]) as f:
            data = json.load(f)
            
            invalid_exch = False 
            if "exchange" in data:
                exch_src = get_source_from_exchange(data["exchange"])
                if not exch_src:
                    invalid_exch = True
                else:
                    context.exch_src = exch_src
                    context.exch_id = str(data["exchange"])
            else:
                invalid_exch = True
            
            if invalid_exch:            
                raise Exception("invalid exchange in config file")

            if "tickers" in data:
                for ticker, ticker_conf in data["tickers"].items():
                     context.qty_denom[str(ticker)] = int(ticker_conf["qty_denominator"])
                     context.price_skew[str(ticker)] = float(ticker_conf["price_skew_pct"])
                     period = int(ticker_conf["period"])
                     if period > 0:
                         context.white_list[str(ticker)] = [period, -1, -1, -1, -1, 0]

    context.add_md(source=context.exch_src)
    context.add_td(source=context.exch_src)
    context.subscribe(tickers=map(lambda x : x[0], context.white_list.items()), source=context.exch_src)

    print context.white_list
    random.seed()
    for t, c in context.white_list.items():
        next_trade_period = random.randint(1, c[0])
        c[1] = c[0] - next_trade_period
        context.order_status[t] = [-1, -1, 0, 0]
        context.insert_func_after_c(next_trade_period, functools.partial(timer_callback, t))

def on_price_book(context, price_book, source, rcv_time):
    if price_book.InstrumentID in context.white_list:
        if price_book.BidLevelCount == 0 or price_book.AskLevelCount == 0 or price_book.AskLevels.levels[0].price <= price_book.BidLevels.levels[0].price:
            context.white_list[price_book.InstrumentID][5] = 0
            print_log(context, 'invalid book, can not do self trade...')
            return

        global mid_price
        global trade_volume

        mid_price = (price_book.BidLevels.levels[0].price + price_book.AskLevels.levels[0].price) / 2
        trade_volume = 0
        for i in range(2):
			trade_volume += price_book.BidLevels.levels[i].volume
			trade_volume += price_book.AskLevels.levels[i].volume
        #mid_price = int(mid_price * context.price_skew[price_book.InstrumentID])
        #trade_volume /= context.qty_denom[price_book.InstrumentID]
        
        #log_text = "symbol {}, ExchangeID {}, mid_price {}, trade_volume {}".format(price_book.InstrumentID, price_book.ExchangeID, mid_price, trade_volume)
        #print_log(context, log_text)
         
        context.white_list[price_book.InstrumentID][2] = mid_price
        context.white_list[price_book.InstrumentID][3] = trade_volume 
        context.white_list[price_book.InstrumentID][4] = price_book.AskLevels.levels[0].price - price_book.BidLevels.levels[0].price 
        context.white_list[price_book.InstrumentID][5] = 1
        

def on_pos(context, pos_handler, request_id, source, rcv_time):
    print("on_pos,", pos_handler, request_id, source, rcv_time)
    if request_id == -1:
        if pos_handler is None:
            print '-- got no pos in initial, so req pos --'
            context.req_pos(context.exch_src)
            context.pos_set = False
            return
        else:
            print '-- got pos in initial --'
            context.print_pos(pos_handler)
            context.data_wrapper.set_pos(pos_handler, source)
            context.pos_set = True
    else:
        print '-- got pos requested --'
        context.print_pos(pos_handler)
        print(pos_handler)
        if not context.pos_set:
            context.data_wrapper.set_pos(pos_handler, source)
            context.pos_set = True

def on_rtn_order(context, rtn_order, order_id, source, rcv_time):
    log_text = "on_rtn_order {} {} {}".format(order_id, rtn_order.InstrumentID, rtn_order.OrderStatus)
    print_log(context, log_text)
    if rtn_order.OrderStatus == 'b':
        if context.order_status[rtn_order.InstrumentID][3] > 0:
            context.order_status[rtn_order.InstrumentID][3] -= 1

        if context.order_status[rtn_order.InstrumentID][3] == 0: 
            if context.order_status[rtn_order.InstrumentID][0] in global_order_id_ticker_map:
                log_text = "going to cancel order {}".format(context.order_status[rtn_order.InstrumentID][0])
                print_log(context, log_text)
                context.cancel_order(source=source, order_id=context.order_status[rtn_order.InstrumentID][0])
            if context.order_status[rtn_order.InstrumentID][1] in global_order_id_ticker_map:
                log_text = "going to cancel order", context.order_status[rtn_order.InstrumentID][1]
                print_log(context, log_text)
                context.cancel_order(source=source, order_id=context.order_status[rtn_order.InstrumentID][1])
        else:
           global_cancel_order_id[global_pair_order_id[order_id]] = order_id 
    elif rtn_order.OrderStatus in ('0', '5') and context.order_status[rtn_order.InstrumentID][2] > 0 and order_id in global_order_id_ticker_map:
        context.order_status[rtn_order.InstrumentID][2] -= 1
        another_order_id = global_order_id_ticker_map[order_id]
        if another_order_id in global_cancel_order_id:
            del global_cancel_order_id[global_pair_order_id[order_id]]
        del global_order_id_ticker_map[order_id]

def on_error(context, error_id, error_msg, order_id, source, rcv_time):
    log_text = "on_error {} {} {}".format(error_id, error_msg, order_id)
    print_log(context, log_text)
    if order_id in global_order_id_ticker_map:
        ticker = global_order_id_ticker_map[order_id]
        if context.order_status[ticker][2] > 0:
            context.order_status[ticker][2] -= 1
            del global_order_id_ticker_map[order_id]
        
        if context.order_status[ticker][3] > 0:
            context.order_status[ticker][3] -= 1 

        if order_id in global_cancel_order_id:
            log_text = "going to cancel order {}".format(global_cancel_order_id[order_id])
            print_log(context, log_text)
            context.cancel_order(source=source, order_id=global_cancel_order_id[order_id])
            del global_pair_order_id[order_id]

def on_rtn_trade(context, rtn_trade, order_id, source, rcv_time):
    log_text = "on rtn trade {} {} {} {}".format(rtn_trade.InstrumentID, order_id, rtn_trade.Price, rtn_trade.Volume)
    print_log(context, log_text)
    #context.print_pos(context.get_pos(source=context.exch_src))
    #context.req_rid = context.req_pos(source=context.exch_src)
