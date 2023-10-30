#!/usr/bin/env python3

import argparse
from struct import *
import math

parser = argparse.ArgumentParser(description="generate uf2 file for davids flashing solution implemented in oct 4 2023")
parser.add_argument("input_path", help="The input path to the binary", type=str, default="./build/build_out/usbd_msc_ram_bl808_m0.bin")
parser.add_argument("output_path", help="The input path to the binary", type=str, default="./output.uf2")
args = parser.parse_args()



output = b''
input_contents = b''

current_offset = 0
current_block = 0

with open(args.input_path, mode='rb') as file:
    input_contents = file.read()

total_blocks = math.ceil(len(input_contents)/476)

input_remaining = input_contents
while (input_remaining):
        
        if (len(input_remaining) == 0):
            break;
        output += bytes.fromhex("0A3246559E5D515700002000") # first two headers
        output += pack("<IIII",current_offset,len(input_remaining[:476]),current_block,total_blocks)

        output += bytes.fromhex("64DA64DA") # board type

        output += input_remaining[:476]

        output += b'\x00'*(476-len(input_remaining[:476]))

        output += bytes.fromhex("0AB16F30")


        current_offset += len(input_remaining[:476])
        input_remaining = input_remaining[476:]
        current_block += 1

with open(args.output_path, "wb") as binary_file:
    binary_file.write(output)