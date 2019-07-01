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

def initialize(context):
    #context.add_md(source=SOURCE.PROBIT)
    context.ticker = 'eos_btc'
    context.exchange_id = EXCHANGE.SHFE
    context.buy_price = -1
    context.sell_price = -1
    context.order_rid = -1
    context.cancel_id = -1
    context.add_td(source=SOURCE.PROBIT)
    context.subscribe(tickers=[context.ticker], source=SOURCE.PROBIT)
    print('init ok')

def on_pos(context, pos_handler, request_id, source, rcv_time):
    print("on_pos,", pos_handler, request_id, source, rcv_time)
    if request_id == -1:
        if pos_handler is None:
            print '-- got no pos in initial, so req pos --'
            context.req_pos(source=SOURCE.PROBIT)
            context.pos_set = False
            return
        else:
            print '-- got pos in initial --'
            context.print_pos(pos_handler)
            #context.stop()
            print '----will test buy cancel----'
            context.buy_price = 100000000 #market_data.LowerLimitPrice
            context.sell_price = 999999999 #market_data.UpperLimitPrice
            if context.order_rid < 0:
                print("context.insert_limit_order 1.")
                context.order_rid = context.insert_market_order(source=SOURCE.PROBIT,
                                                               ticker=context.ticker,
                                                             #  price=context.buy_price,
                                                               exchange_id=context.exchange_id,
                                                               volume=100000000,
                                                               direction=DIRECTION.Buy,
                                                               offset=OFFSET.Open)
                print("context.order_rid:", context.order_rid)
                #print('will cancel it')
               # context.cancel_id = context.cancel_order(source=source, order_id=context.order_rid)
               # print 'cancel (order_id)', context.order_rid, ' (request_id)', context.cancel_id



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
        # context.buy_price = 001000000 #market_data.LowerLimitPrice
        context.sell_price = 99999999 #market_data.UpperLimitPrice
        # if context.order_rid < 0:
        #     print("context.insert_limit_order 1.")
        #     context.order_rid = context.insert_limit_order(source=SOURCE.PROBIT,
        #                                                  ticker=context.ticker,
        #                                                  price=context.buy_price,
        #                                                  exchange_id=context.exchange_id,
        #                                                  volume=100000000,
        #                                                  direction=DIRECTION.Buy,
        #                                                  offset=OFFSET.Open)
        #     print("context.order_rid:", context.order_rid)

def on_rtn_order(context, rtn_order, order_id, source, rcv_time):
    print('----on rtn order----',rtn_order, ' order_id ', order_id)
    if order_id == context.order_rid and context.cancel_id < 0 and rtn_order.OrderStatus != 'a':
        print('send cancel order, but is no time, cancel will be fail')
        #context.cancel_id = context.cancel_order(source=source, order_id=order_id)
        #print 'cancel (order_id)', order_id, ' (request_id)', context.cancel_id
    if order_id == context.order_rid and rtn_order.OrderStatus == '5':
        print 'cancel successfully!'
        context.stop()

def on_error(context, error_id, error_msg, order_id, source, rcv_time):
    print 'on_error:', error_id, error_msg, order_id, source, rcv_time

def on_rtn_trade(context, rtn_trade, order_id, source, rcv_time):
    print '----on rtn trade----'
    context.print_pos(context.get_pos(source=1))
    context.req_rid = context.req_pos(source=1)
