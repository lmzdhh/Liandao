# -*- coding:utf-8 -*-
import os
import datetime


# 本程序用于每天将收到的md数据导出journal, 展开平日车博欧克0，并压缩后，放入特定的目录
if __name__ == "__main__":
    now = datetime.datetime.now()
    last_day_time = now + datetime.timedelta(days=-1)

    yesterday_str = last_day_time.strftime('%Y%m%d')
    print("yesterday_str", yesterday_str)
    yesterday_morning_str = last_day_time.strftime('%Y%m%d') + "-00:00:00"

    print("yesterday_morning_str", yesterday_morning_str)
    today_morning_str = now.strftime('%Y%m%d') + "-00:00:00"
    print("today_morning_str", today_morning_str)

    # 今天的临时文件存放目录
    temp_folder = "/tmp/coin_kungfu/"+ yesterday_str

    commands_lines = "#!/bin/sh\n"
    # 清空目录重建
    commands_lines += "rm -rf %s\n" % (temp_folder)
    commands_lines += "mkdir -p %s\n" % (temp_folder)
    # 导出文件
    dump_scripts = [
    "yjj dump -n MD_BINANCE -s %s -e %s -m 106 -o %s/binance_pricebook20_tmp.csv",
    "yjj dump -n MD_BINANCE -s %s -e %s -m 105 -o %s/binance_trade.csv",

    "yjj dump -n MD_COINMEX -s %s -e %s -m 106 -o %s/coinmex_pricebook20_tmp.csv",
    "yjj dump -n MD_COINMEX -s %s -e %s -m 105 -o %s/coinmex_trade.csv",

    "yjj dump -n MD_BITFINEX -s %s -e %s -m 106 -o %s/bitfinex_pricebook20_tmp.csv",
    "yjj dump -n MD_BITFINEX -s %s -e %s -m 105 -o %s/bitfinex_trade.csv"
    ]
    for dump_template in dump_scripts:
        commands_lines += (dump_template % (yesterday_morning_str, today_morning_str, temp_folder)) + "\n"

    # price_book20_dump_csv_expand.py 展开文件
    commands_lines += ("python /root/liandao/yijinjing/tools/price_book20_dump_csv_expand.py -f %s/binance_pricebook20_tmp.csv -o %s/binance_pricebook20.csv" % (temp_folder, temp_folder)) + "\n"
    commands_lines += ("python /root/liandao/yijinjing/tools/price_book20_dump_csv_expand.py -f %s/coinmex_pricebook20_tmp.csv -o %s/coinmex_pricebook20.csv" % (temp_folder, temp_folder)) + "\n"
    commands_lines += ("python /root/liandao/yijinjing/tools/price_book20_dump_csv_expand.py -f %s/bitfinex_pricebook20_tmp.csv -o %s/bitfinex_pricebook20.csv" % (temp_folder, temp_folder)) + "\n"

    # gzip 原地压缩文件
    gzip_rm_tmp_scripts = [
    "gzip %s/binance_pricebook20.csv",
    "rm -rf %s/binance_pricebook20_tmp.csv",
    "gzip %s/binance_trade.csv",

    "gzip %s/coinmex_pricebook20.csv",
    "rm -rf %s/coinmex_pricebook20_tmp.csv",
    "gzip %s/coinmex_trade.csv",

    "gzip %s/bitfinex_pricebook20.csv",
    "rm -rf %s/bitfinex_pricebook20_tmp.csv",
    "gzip %s/bitfinex_trade.csv"
    ]
    for gzip_script in gzip_rm_tmp_scripts:
        commands_lines += (gzip_script % (temp_folder)) + "\n"

    # 移动到docker共享目录
    target_folder = "/share/coin_kungfu/"
    commands_lines += "rm -rf %s\n" % (target_folder)
    commands_lines += "mkdir -p %s\n" % (target_folder)

    commands_lines += ("mv %s %s" % (temp_folder, target_folder)) + "\n"

    with open(os.path.join("/tmp/export_kungfu_csv.sh"), 'w') as shell_file:
        shell_file.write(commands_lines)
        shell_file.flush()
        shell_file.close()
    os.chmod("/tmp/export_kungfu_csv.sh",  0o755)

    # print(commands_lines)
    print("shell file create successful:", "/tmp/export_kungfu_csv.sh")

