import os, sys, subprocess
import datetime


if __name__ == "__main__":
	
    if len(sys.argv) < 4:
	    print("usage: {} exchange date output_dir (no_delete)\n".format(sys.argv[0]))
	    exit(1)
	
    exchange_str = sys.argv[1]
    md_exchange_str = "MD_{}".format(exchange_str.upper())
   	
    date = int(sys.argv[2])
    year = date / 10000
    month = (date - year * 10000) / 100
    day = date - year * 10000 - month * 100
	
    last_day_time = datetime.datetime(year, month, day) 
    next_day_time = last_day_time + datetime.timedelta(days=1)

    last_date_str = last_day_time.strftime('%Y%m%d')
    next_date_str = next_day_time.strftime('%Y%m%d')

    target_folder = sys.argv[3] + "/" + last_date_str + "/" + exchange_str

    temp_folder = "/tmp/coin_kungfu/" + last_date_str + "/" + exchange_str

    commands_lines = "#!/bin/sh\n"
    commands_lines += "rm -rf %s\n" % (temp_folder)
    commands_lines += "mkdir -p %s\n" % (temp_folder)

    start_time = 0;

    while True:
        if start_time >= 24:
            break
		
        next_start_time = start_time + 1	
        yesterday_morning_str = last_date_str + "-{0:02d}:00:00".format(start_time)
        today_morning_str = (last_date_str + "-{0:02d}:00:00".format(next_start_time)) if next_start_time < 24 else next_date_str + "-00:00:00"
        start_time = next_start_time	

        pricebook_csv_file_name = "{}_pricebook20_{}.csv".format(md_exchange_str, yesterday_morning_str)
        pricebook_csv_temp_file_name = "{}_pricebook20_tmp_{}.csv".format(md_exchange_str, yesterday_morning_str)
        trade_csv_file_name = "{}_trade_{}.csv".format(md_exchange_str, yesterday_morning_str)

        dump_scripts = [
              "yjj dump -c yjj_dump_{} -n {} -s %s -e %s -m 106 -o %s/{}".format(exchange_str, md_exchange_str, pricebook_csv_temp_file_name),
              "yjj dump -c yjj_dump_{} -n {} -s %s -e %s -m 105 -o %s/{}".format(exchange_str, md_exchange_str, trade_csv_file_name),
        ]
        
        for dump_template in dump_scripts:
             commands_lines += (dump_template % (yesterday_morning_str, today_morning_str, temp_folder)) + "\n"

        commands_lines += ("python /root/liandao/yijinjing/tools/price_book20_dump_csv_expand.py -f {0}/{1} -o {0}/{2}".format(temp_folder, pricebook_csv_temp_file_name, pricebook_csv_file_name)) + "\n"

        gzip_rm_tmp_scripts = [
            "gzip %s/{}".format(pricebook_csv_file_name),
            "rm -rf %s/{}".format(pricebook_csv_temp_file_name),
            "gzip %s/{}".format(trade_csv_file_name),
        ]
        
        for gzip_script in gzip_rm_tmp_scripts:
            commands_lines += (gzip_script % (temp_folder)) + "\n"

    commands_lines += "rm -rf %s/*\n" % (target_folder)
    commands_lines += "mkdir -p %s\n" % (target_folder)
    commands_lines += ("mv %s/* %s/" % (temp_folder, target_folder)) + "\n"
	
    if not (len(sys.argv) == 5 and sys.argv[4] == "no_delete"):
        last_date_str2 = last_day_time.strftime('%Y-%m-%d')
        next_date_str2 = next_day_time.strftime('%Y-%m-%d')
        commands_lines += ("for f in `find /shared/kungfu/journal/MD/{}/* -newermt \"{} 00:00:00\" ! -newermt \"{} 00:00:00\"`; do rm -rf $f; done".format(exchange_str.upper(), last_date_str2, next_date_str2))
	
    script_name = "/tmp/export_kungfu_csv_{}.sh".format(exchange_str)
    with open(os.path.join(script_name), 'w') as shell_file:
        shell_file.write(commands_lines)
        shell_file.flush()
        shell_file.close()
    os.chmod(script_name,  0o755)

    print("shell file create successful: {}, running it now...".format(script_name))

    proc = subprocess.Popen("bash {}".format(script_name), shell=True)
    try:
        proc.wait() 
    except:
        proc.terminate()
