#!/usr/bin/python3

import sys
sys.path.append('../testdata')
import config

import requests
import argparse
import json
from datetime import datetime


def instruct_daemon(method, params):
    payload = json.dumps({"method": method, "params": params}, skipkeys=False)
    # print(payload)
    headers = {'content-type': "application/json"}
    try:
        response = requests.request("POST", "http://"+config.listen_ip+":"+config.listen_port+"/json_rpc", data=payload, headers=headers)
        return json.loads(response.text)
    except requests.exceptions.RequestException as e:
        print(e)
    except:
        print('No response from daemon, check daemon is running on this machine')


params = {}
answer = instruct_daemon('bls_merkle_request', params)

# print(json.dumps(answer['result']['block_header']['timestamp'], indent=4, sort_keys=True))

print(answer)
