#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import requests 
import os 

# Load environment variables from the .env file
rpiapiurl = 'http://localhost:9097'  # Get the Raspberry Pi API URL from the environment variables

def rpiemotion(ctgry, vlen):
    try:
        print(f"Calling rpiemotion with ctgry: {ctgry}, vlen: {vlen}")  # Debugging line
        headers = {'accept': 'application/json',
                   'content-type': 'application/x-www-form-urlencoded',}
        params = {'ctgry': "les", 'vlen': int(vlen),}
        url = rpiapiurl + "/rpi/rpi_api"
        print(f"Making POST request to {url} with params {params}")  # Debugging line
        r = requests.post(url=url, params=params, headers=headers, timeout=60)
        result = r
        print(f"Response from rpi: {result}")  # Debugging line
        return result
    except Exception as e:
        print(f"Error in rpiemotion: {e}")  # Debugging line
        return {"message": 'rpi connection is not available, ' + str(e)}

if __name__ == '__main__':
    pass
