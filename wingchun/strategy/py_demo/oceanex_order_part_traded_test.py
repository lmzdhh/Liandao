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
    context.add_md(source=SOURCE.OCEANEX)
    context.ticker = 'eth_btc'
    context.exchange_id = EXCHANGE.SHFE
    context.buy_price = -1
    context.sell_price = -1
    context.order_rid = -1
    context.cancel_id = -1
    context.add_td(source=SOURCE.OCEANEX)
    context.subscribe(tickers=[context.ticker], source=SOURCE.OCEANEX)
    context.handle_tick = True

def on_pos(context, pos_handler, request_id, source, rcv_time):
    print("on_pos,", pos_handler, request_id, source, rcv_time)
    if request_id == -1:
        if pos_handler is None:
            print '-- got no pos in initial, so req pos --'
            context.req_pos(source=SOURCE.OCEANEX)
            context.pos_set = False
            return
        else:
            print '-- got pos in initial --'
            context.print_pos(pos_handler)
    else:
        print '-- got pos requested --'
        context.print_pos(pos_handler)
        print(pos_handler)
        if not context.pos_set:
            context.data_wrapper.set_pos(pos_handler, source)
        #context.stop()


def on_tick(context, market_data, source, rcv_time):
    #print('market_data',market_data)
    #print('instrument:',market_data.InstrumentID)
    #print('ticker:',context.ticker)
    #
    if market_data.InstrumentID == context.ticker and context.handle_tick:
        print("context.insert_limit_order ")
        context.order_rid = context.insert_limit_order(source=SOURCE.OCEANEX,
                                                       ticker=context.ticker,
                                                       price=market_data.AskPrice1,
                                                       exchange_id=context.exchange_id,
                                                       volume=market_data.AskVolume1,
                                                       direction=DIRECTION.Buy,
                                                       offset=OFFSET.Open)
        print ("price:",market_data.AskPrice1,"vol:",market_data.AskVolume1)
	context.handle_tick=False
        print("context.order_rid:", context.order_rid)

def on_rtn_order(context, rtn_order, order_id, source, rcv_time):
    print('----on rtn order----',rtn_order, ' order_id ', order_id,'status',rtn_order.OrderStatus)
    #if order_id == context.order_rid and context.cancel_id < 0 and rtn_order.OrderStatus != 'a':
    #    print('send cancel order, but is no time, cancel will be fail')
    #    context.cancel_id = context.cancel_order(source=source, order_id=order_id)
    #    print 'cancel (order_id)', order_id, ' (request_id)', context.cancel_id
    if order_id == context.order_rid and rtn_order.OrderStatus == '1':
        print 'Part Traded!'
    #if order_id == context.order_rid and rtn_order.OrderStatus == '5':
    #    print 'cancel successfully!'
    #    context.stop()
    if order_id == context.order_rid and rtn_order.OrderStatus == '0':
        print 'All Traded!'
        context.stop()
def on_price_book_update(context, pricebook_data, source, rcv_time):
    print('----on price book update----',pricebook_data)
    if(pricebook_data.InstrumentID == context.ticker):
        print("context.insert_limit_order ")
        context.order_rid = context.insert_limit_order(source=SOURCE.OCEANEX,
                                                       ticker=context.ticker,
                                                       price=pricebook_data.AskLevels[0].price,
                                                       exchange_id=context.exchange_id,
                                                       volume=pricebook_data.AskLevels[0].volume * 2,
                                                       direction=DIRECTION.Buy,
                                                       offset=OFFSET.Open)
        print("context.order_rid:", context.order_rid)

def on_error(context, error_id, error_msg, order_id, source, rcv_time):
    print 'on_error:', error_id, error_msg, order_id, source, rcv_time

def on_rtn_trade(context, rtn_trade, order_id, source, rcv_time):
    print '----on rtn trade----'
    context.print_pos(context.get_pos(source=SOURCE.OCEANEX))
    context.req_rid = context.req_pos(source=SOURCE.OCEANEX)
