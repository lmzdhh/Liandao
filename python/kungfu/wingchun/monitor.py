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

import signal, json
from kungfu.longfist.longfist_utils import _byteify as _byteify

def register_exit_handler(monitor, name, tp):
    def exit_handler(*args):
        print 'stop monitor: {}_{}'.format(tp, name)
        monitor.stop()
    signal.signal(signal.SIGTERM, exit_handler)

class Monitor(object):

    kungfu_config = '/opt/kungfu/master/etc/kungfu/kungfu.json'

    def __init__(self, name, tp):
        lib_name = 'lib{}{}'.format(tp, name)
        lib = None
        try:
            lib = __import__(lib_name)
        except Exception as e:
            print 'Unexpected lib is imported', lib_name
	    print e
            exit(1)

        globals()[lib_name] = lib
        self.core_monitor = lib.Monitor()
        self.api_name = name
        self.tp = tp
        fin = open(self.kungfu_config)
        try:
            # we first parse account.json and get account info
            json_info = json.load(fin, object_hook=_byteify)[tp][name]
            # then simply dump to str and will init
            self.info = json.dumps(json_info)
        except Exception as e:
            print 'Cannot find related kungfu config: ', tp, name, ' in ', self.kungfu_config
            print e
            exit(1)

    def start(self):
        register_exit_handler(self.core_monitor, self.api_name, self.tp)
        if self.core_monitor.init(self.info) != True:
            print 'init failed,',self.api_name
            return
        if self.core_monitor.start() != True:
            print 'start failed,',self.api_name
            return
        self.core_monitor.wait_for_stop()
