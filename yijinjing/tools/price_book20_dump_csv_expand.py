#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import pandas as pd
#import matplotlib as mpl
import numpy as np
#from matplotlib import pyplot as plt
#from numba import *

#mpl.style.use('seaborn-whitegrid')
pd.set_option('display.width', 21250)
pd.set_option('display.max_columns', 21250)
#mpl.rcParams['font.sans-serif'] = ['SimHei']
#mpl.rcParams['axes.unicode_minus'] = False

columns_def = [
    'InstrumentID(c31)',
    'ExchangeID(c9)',
    'UpdateMicroSecond(i)',
    'BidLevelCount(i)',
    'AskLevelCount(i)',
    'BidLevels([])',
    'AskLevels([])',
    'h_extra_nano(l)',
    'h_nano(l)',
    'h_msg_type(i)',
    'h_request_id(i)',
    'h_source(i)',
    'h_is_last(i)',
    'h_error_id(i)',
    'j_name(s)'
]

columns_type = {
    'InstrumentID(c31)': np.str,
    'ExchangeID(c9)': np.str,
    'UpdateMicroSecond(i)': np.int64,
    'BidLevelCount(i)': np.int64,
    'AskLevelCount(i)': np.int64,
    'BidLevels([])': np.str,
    'AskLevels([])': np.str,
    'h_extra_nano(l)': np.int64,
    'h_nano(l)': np.int64,
    'h_msg_type(i)': np.int64,
    'h_request_id(i)': np.int64,
    'h_source(i)': np.int64,
    'h_is_last(i)': np.int64,
    'h_error_id(i)': np.int64,
    'j_name(s)': np.str
}


def expand_level20_price_volume(df, collapseFieldName, newFieldName):
    columns_bids = {}
    for i in range(0, 20, 1):
        columns_bids[i] = newFieldName + "_" + str(i)
    # print(columns_bids)
    df = pd.merge(df, df[collapseFieldName].str.split(';', n=-1, expand=True).rename(columns=columns_bids), left_index=True, right_index=True, how='left')
    # 会额外多出一个20的列，需要删掉
    df.drop([20], axis=1, inplace=True)
    for i in range(0, 20, 1):
        columns_bid_price_volumes = {0: newFieldName + "_price_%s" % (str(i)), 1: newFieldName + "_volume_%s" % (str(i))}
        # print(columns_bid_price_volumes)
        df = pd.merge(df, df[newFieldName + "_%s" % (str(i))].str.split('@', n=-1, expand=True).rename(columns=columns_bid_price_volumes), left_index=True, right_index=True, how='left')
        df.drop([newFieldName + "_%s" % (str(i))], axis=1, inplace=True)
        # print(df.head(3))

    return df

# InstrumentID(c31),ExchangeID(c9),UpdateMicroSecond(i),BidLevelCount(i),AskLevelCount(i),BidLevels([]),AskLevels([]),h_extra_nano(l),h_nano(l),h_msg_type(i),h_request_id(i),h_source(i),h_is_last(i),h_error_id(i),j_name(s)
# btc_usdt,coinmex,0,20,19,3515028@723171420000;17774592@719542150000;18189039@718244230000;7504079@712800000000;5380039@710820000000;6858752@707850000000;5898097@707721460000;7141438@704880000000;4602951@704682000000;6934215@702900000000;6623379@700920000000;5017398@698445000000;6675185@695970000000;5949903@693198000000;3307804@693099000000;6571574@693000000000;3307804@689832000000;4240310@688050000000;5069204@683199000000;125899999@50000000;,3835555@813050000000;5725892@808000000000;2981854@807596000000;3530662@799011000000;7616229@789820000000;6823507@787800000000;7250357@783154000000;170000@780000000000;6274699@778710000000;5299042@777700000000;4140448@776488000000;3713597@774468000000;5603935@769620000000;4140448@762550000000;6762528@757500000000;5969806@756490000000;17525255@752450000000;16915469@744850510000;6457635@737835500000;
# 0@0;,0,1532171672294003521,106,-1,19,1,0,MD_COINMEX
# mvp_btc,coinmex,0,2,6,50000000000@100;60100000000@1;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;,18100000000@760000000;200000000@100000000;12800000000@100000;18800000000@15228;3062570000@200;18800000000@180;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;0@0;,0,1532171672300020300,106,-1,19,1,0,MD_COINMEX
def expandCsv(inputCsvFileName, outputCsvFileName):
    print("inputCsvFileName:", inputCsvFileName, ", outputCsvFileName:", outputCsvFileName)
    df_days_instrument_one_by_one = pd.read_csv(inputCsvFileName, header=0,
                                                names=columns_def,
                                                dtype=columns_type,
                                                error_bad_lines=False,
                                                na_filter=True, verbose=True, skip_blank_lines=True,
                                                engine='c',
                                                warn_bad_lines=True, chunksize=1000000, iterator=True)
    df = pd.concat(df_days_instrument_one_by_one, ignore_index=True)
    # print(df)
    if len(df) < 1:
        print("seems no data in the file:", outputCsvFileName)
        pd.DataFrame().to_csv(outputCsvFileName, index=None, header=True, mode='w', encoding="utf-8", chunksize=1000000)
        return

    df = expand_level20_price_volume(df, 'AskLevels([])', 'asks')
    df = expand_level20_price_volume(df, 'BidLevels([])', 'bids')

    df.drop(["BidLevels([])", "AskLevels([])"], axis=1, inplace=True)
    # print(df.head(3))
    df.to_csv(outputCsvFileName, index=None, header = True, mode ='w',encoding="utf-8",chunksize = 1000000 )
    print("outputCsvFile create successfully:", outputCsvFileName)

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--file', help='input csv file name', type=str, default='')
    parser.add_argument('-o', '--output', help='output csv file name', type=str, default='')
    args = parser.parse_args()
    ready_to_expand = True
    if len(args.file) == 0:
        print('file (-f) need to be specified')
        ready_to_expand = False
    if len(args.output) == 0:
        print('output (-o) need to be specified')
        ready_to_expand = False
    if os.path.exists(args.output):
        print('error: output file exist, please use another output file name.')
        ready_to_expand = False

    if ready_to_expand:
        print('expand: ','file ', args.file,'to file ', args.output)
        expandCsv(args.file, args.output)
    else:
        print("usage example:")
        print("python price_book20_dump_csv_expand.py -f D:\\beavoinvest\\td_md_csv\\MD_COINMEX_106_20180722.csv -o  D:\\beavoinvest\\td_md_csv\\MD_COINMEX_106_20180722_expand3.csv")
