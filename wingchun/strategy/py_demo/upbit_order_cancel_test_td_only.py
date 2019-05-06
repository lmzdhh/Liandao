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
wingchun strategy -n my_test -p upbit_order_cancel_test_td_only.py
'''

def initialize(context):
    #context.add_md(source=SOURCE.COINMEX)
    context.ticker = 'krw_btc'
    context.exchange_id = EXCHANGE.SHFE
    context.buy_price = -1
    context.sell_price = -1
    context.order_rid = -1
    context.cancel_id = -1
    context.add_td(source=SOURCE.UPBIT)
    context.subscribe(tickers=[context.ticker], source=SOURCE.UPBIT)

def on_pos(context, pos_handler, request_id, source, rcv_time):
    print("on_pos,", pos_handler, request_id, source, rcv_time)
    if request_id == -1:
        if pos_handler is None:
            print '-- got no pos in initial, so req pos --'
            context.req_pos(source=SOURCE.UPBIT)
            context.pos_set = False
            return
        else:
            print '-- got pos in initial --'
            context.print_pos(pos_handler)
            #context.stop()
            print '----will test buy cancel----'
            context.buy_price = 690000000000000 #market_data.LowerLimitPrice
            context.sell_price = 3741000 #market_data.UpperLimitPrice
            if context.order_rid < 0:
                print("context.insert_limit_order 1.")
                context.order_rid = context.insert_limit_order(source=SOURCE.UPBIT,
                                                               ticker=context.ticker,
                                                               price=context.buy_price,
                                                               exchange_id=context.exchange_id,
                                                               volume=50000,
                                                               direction=DIRECTION.Sell,
                                                               offset=OFFSET.Open)
                print("context.order_rid:", context.order_rid)
                #print('will cancel it')
                #context.cancel_id = context.cancel_order(source=source, order_id=context.order_rid)
                #print 'cancel (order_id)', context.order_rid, ' (request_id)', context.cancel_id
                #quest5 fxw starts here
                print('will cancel it')
                context.cancel_id = context.cancel_order(source=source, order_id=context.order_rid)
                print 'cancel (order_id)', context.order_rid, ' (request_id)', context.cancel_id
                #quest5 fxw ends here

    else:
        print '-- got pos requested --'
        context.print_pos(pos_handler)
        print(pos_handler)
        if not context.pos_set:
            context.data_wrapper.set_pos(pos_handler, source)
        #context.stop()


def on_tick(context, market_data, source, rcv_time):
    print('market_data', market_data)
    if market_data.InstrumentID == context.ticker:
        # context.buy_price = 1 #market_data.LowerLimitPrice
        context.sell_price = 99999999 #market_data.UpperLimitPrice
        # if context.order_rid < 0:
        #     print("context.insert_limit_order 1.")
        #     context.order_rid = context.insert_limit_order(source=SOURCE.COINMEX,
        #                                                  ticker=context.ticker,
        #                                                  price=context.buy_price,
        #                                                  exchange_id=context.exchange_id,
        #                                                  volume=100000000,
        #                                                  direction=DIRECTION.Buy,
        #                                                  offset=OFFSET.Open)
        #     print("context.order_rid:", context.order_rid)

def on_rtn_order(context, rtn_order, order_id, source, rcv_time):
    print '----on rtn order----'
    print("orderid[",order_id,"] status[",rtn_order.OrderStatus,"]")

def on_error(context, error_id, error_msg, order_id, source, rcv_time):
    print 'on_error:', error_id, error_msg, order_id, source, rcv_time

def on_rtn_trade(context, rtn_trade, order_id, source, rcv_time):
    print '----on rtn trade----'
    print("price[",rtn_trade.Price,"] volume[",rtn_trade.Volume,"]") 
