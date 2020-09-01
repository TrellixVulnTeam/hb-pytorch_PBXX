#
#   line_trace.py
#
#   Common header file for line trace generator
#   Note: Do not use directly; use compress_trace.py and print_trace.py as described below
#
#   @author: Krithik Ranjan (kr397@cornell.edu) 08/19/20
#
#   Usage: 
#
#   Step 1: Compress the .csv trace file with compress_trace.py
#       python compress_trace.py --trace {vanilla_operaton_trace.csv}
#                                --startpc {optional} {Starting PC (of kernel) for the beginning of line trace}
#                                --endpc {optional} {Ending PC (of kernel) for the ending of line trace}
#                                --fastnfake {optional} {Flag to generate line trace of 4x4 Bladerunner}
#   Note: if startpc and endpc are not specified, line trace is generated for the entire trace file
#   Example:
#       python compress_trace.py --trace vanilla_operation_trace.py
#                                --startpc 1cd2c
#                                --endpc 1cd80
#
#   Step 1b (optional): Compress the disassembly of PyTorch to use while printing trace in 'full' mode
#       python compress_asm.py --asm {path to disassembly file}
#   Note: this step needs to be performed before printing the trace in 'full' mode (step 2)
#   Example: 
#       python compress_asm.py --asm /work/global/kr397/hb-pytorch/torch/riscv/kernel.dis   
#
#   Step 2: Prints the line trace between two PCs specified while running compress_trace.py
#   
#   For every tile, the instructions can be printed in either 'lo', 'mid', 'hi', 'pc' or 'full' 
#   - lo: single character code for stalls (listed below), '@' for instruction
#   - mid: three character code for stalls and instructions (listed below)
#   - hi: first fifteen characters of all stalls and instructions
#   - pc: lowest two bytes of the PC of every instruction, ##ff for stalls
#   - full: lowest two bytes of the PC + first 15 characters of actual instruction from asm, 
#           ##<complete stall code> for stalls
#       
#       python print_trace.py --mode {optional} {print mode for all tiles; 'lo', 'mid', 'hi'}
#                             --lo {optional} {range of tiles to print in 'lo' mode}
#                             --mid {optional} {range of tiles to print in 'mid' mode}
#                             --hi {optional} {range of tiles to print in 'hi' mode}
#                             --pc {optional} {range of tiles to print in 'pc' mode}
#                             --full {optional} {range of tiles to print in 'full' mode}
#   
#   Note: at least one of mode, lo, mid, hi must be provided to print the line trace
#
#   Example:
#       python print_trace.py --mode lo
#           (prints the line trace of all tiles in lo mode)
#       python print_trace.py --mode lo
#                             --mid 4 9
#           (prints the line trace of tiles [4-9) in mid, and all else in lo)
#       python print_trace.py --lo 0 3
#                             --mid 5 9
#                             --hi 10 11
#           (prints the line trace of tiles [0-3) in lo, [5-9) in mid, [10-11) in hi)
#
#   Note: compress_trace.py generates a file trace.obj in the active directory; ensure that
#   print_trace.py is run from the same directory so that it can access trace.obj
#   Note: compress_asm.py generates a file kernel.dic in the active directory; ensure that 
#   print_trace.py in 'full' mode is run from the same directory


import csv
import argparse

#import colorama
#from colorama import Fore, Back, Style
#colorama.init(autoreset=True)

class Trace:

    _TILE_X_DIM = 16
    _TILE_Y_DIM = 8

    # List of types of stalls incurred by the core
    # Stall type : [lo code, mid code]
    _STALLS_LIST   = {"stall_depend_dram_load" : ['D', '#DL'],
                      "stall_depend_group_load" : ['O', '#GR'] ,
                      "stall_depend_global_load" : ['G', '#GL'],
                      "stall_depend_local_load" : ['L', '#LL'],

                      "stall_depend_idiv" : ['H', '#ID'],
                      "stall_depend_fdiv" : ['Q', '#FD'],
                      "stall_depend_imul" : ['X', '#IM'],

                      "stall_amo_aq" : ['M', '#AQ'],
                      "stall_amo_rl" : ['K', '#RL'],

                      "stall_bypass" : ['Y', '#BP'],
                      "stall_lr_aq" : ['A', '#LR'],
                      "stall_fence" : ['F', '#FE'],
                      "stall_remote_req" : ['R', '#RR'],
                      "stall_remote_credit" : ['T', '#RC'],

                      "stall_fdiv_busy" : ['V', '#FB'],
                      "stall_idiv_busy" : ['U', '#IB'],

                      "stall_fcsr" : ['S', '#FC'],
                      "stall_remote_ld" : ['E', '#LD'],

                      "stall_remote_flw_wb" : ['W', '#FL'],

                      "bubble_branch_miss" : ['B', '#BM'],
                      "bubble_jalr_miss" : ['J', '#JM'],

                      "stall_ifetch_wait" : ['F', '#IF'],
                      "bubble_icache_miss" : ['I', '#IC'],
                      "icache_miss" : ['C', '#IM']}


    # List of types of integer instructions executed by the core
    _INSTRS_LIST    = {"local_ld" : 'lld',
                       "local_st": 'lst',
                       "remote_ld_dram": 'ldr',
                       "remote_ld_global": 'lgl',
                       "remote_ld_group": 'lgr',
                       "remote_st_dram": 'sdr',
                       "remote_st_global": 'sgl',
                       "remote_st_group": 'sgr',
                       "local_flw": 'flw',
                       "local_fsw": 'fsw',
                       "remote_flw_dram": 'fdr',
                       "remote_flw_global": 'fgl',
                       "remote_flw_group": 'fgr',
                       "remote_fsw_dram": 'fsd',
                       "remote_fsw_global": 'fsg',
                       "remote_fsw_group": 'fsr',
                       # icache_miss is no longer treated as an instruction
                       # but treated the same as stall_ifetch_wait
                       # "icache_miss",
                       "lr": 'lr ',
                       "lr_aq": 'lra',
                       "amoswap": 'ams',
                       "amoor": 'amo',
                       "beq": 'beq',
                       "bne": 'bne',
                       "blt": 'blt',
                       "bge": 'bge',
                       "bltu": 'blu',
                       "bgeu": 'bgu',
                       "jal": 'jal',
                       "jalr": 'jar',
                       "beq_miss": 'eqm',
                       "bne_miss": 'nem',
                       "blt_miss": 'ltm',
                       "bge_miss": 'gem',
                       "bltu_miss": 'lum',
                       "bgeu_miss": 'gum',
                       "jalr_miss": 'jam',
                       "sll": 'sll',
                       "slli": 'sli',
                       "srl": 'srl',
                       "srli": 'sri',
                       "sra": 'sra',
                       "srai": 'sai',
                       "add": 'add',
                       "addi": 'adi',
                       "sub": 'sub',
                       "lui": 'lui',
                       "auipc": 'apc',
                       "xor": 'xor',
                       "xori": 'xri',
                       "or": 'or ',
                       "ori": 'ori',
                       "and": 'and',
                       "andi": 'ani',
                       "slt": 'slt',
                       "slti": 'sli',
                       "sltu": 'slu',
                       "sltiu": 'siu',
                       "div": 'div',
                       "divu": 'diu',
                       "rem": 'rem',
                       "remu": 'reu',
                       "mul": 'mul',
                       "fence": 'fen',
                       "csrrw": 'crw',
                       "csrrs": 'crs',
                       "csrrc": 'crc',
                       "csrrwi": 'cwi',
                       "csrrsi": 'csi',
                       "csrrci": 'cci',
                       "unknown": 'unk'}


    # List of types of floating point instructions executed by the core
    _FP_INSTRS_LIST = {"fadd": 'fad',
                       "fsub": 'fsu',
                       "fmul": 'fmu',
                       "fsgnj": 'fgj',
                       "fsgnjn": 'fjn',
                       "fsgnjx": 'fjx',
                       "fmin": 'fmi',
                       "fmax": 'fma',
                       "fcvt_s_w": 'fcw',
                       "fcvt_s_wu": 'fcu',
                       "fmv_w_x": 'fwx',
                       "fmadd": 'fma',
                       "fmsub": 'fms',
                       "fnmsub": 'fns',
                       "fnmadd": 'fna',
                       "feq": 'feq',
                       "flt": 'flt',
                       "fle": 'fle',
                       "fcvt_w_s": 'fcs',
                       "fcvt_wu_s": 'fus',
                       "fclass": 'fcl',
                       "fmv_x_w": 'fxw',
                       "fdiv": 'fdi',
                       "fsqrt": 'fsq'}

    def __init__(self, trace_file, start_pc, end_pc, ff):
        if ff:
            self._TILE_Y_DIM = 4
            self._TILE_X_DIM = 4
        self.start_cycle = -1
        self.end_cycle = -1

        # Parse vanilla trace file to generate traces
        self.traces = self.__parse_traces(trace_file, start_pc, end_pc)


    
    def __parse_traces(self, trace_file, start_pc, end_pc):
        traces = {}
        
        for tile in range(self._TILE_X_DIM * self._TILE_Y_DIM):
            tile_x = tile % self._TILE_X_DIM
            tile_y = tile // self._TILE_X_DIM
            traces[(tile_x, tile_y)] = {"inrange": False, "mode": 'na', "instr": {}}

        with open(trace_file) as csv_trace:
            reader = csv.DictReader(csv_trace, delimiter=',')

            for row in reader:
                tile_x = int(row["x"])
                tile_y = int(row["y"])

                if row["pc"] == start_pc or start_pc == 'xx':
                    if not traces[(tile_x, tile_y)]["inrange"]:
                        traces[(tile_x, tile_y)]["inrange"] = True
                    if self.start_cycle == -1:
                        self.start_cycle = int(row["cycle"])
                
                if traces[(tile_x, tile_y)]["inrange"]:
                    traces[(tile_x, tile_y)]["instr"][int(row["cycle"])] = [row["pc"], row["operation"]]
                
                if row["pc"] == end_pc:
                    traces[(tile_x, tile_y)]["inrange"] = False
                    self.end_cycle = int(row["cycle"])
                
                if end_pc == 'xx':
                    self.end_cycle= int(row["cycle"])
        
        print("Start cycle: " + str(self.start_cycle) + " end cycle: " + str(self.end_cycle))
        
        return traces            

    def set_mode(self, mode, start, end, all_tiles = False):
        if all_tiles:
            start = 0
            end = self._TILE_X_DIM * self._TILE_Y_DIM

        for tile in range(start, end):
            tile_x = tile % self._TILE_X_DIM
            tile_y = tile // self._TILE_X_DIM
            self.traces[(tile_x, tile_y)]["mode"] = mode

    def print_trace(self, asm = None):
        print("Start cycle: " + str(self.start_cycle) + " End cycle: " + str(self.end_cycle))

        print('Tiles', end='\t')
        for tile in self.traces:
            if self.traces[tile]["mode"]  == 'lo':
                if tile[0] >= 10:
                    print(hex(tile[0]).lstrip('0x'), end='')
                else:
                    print(tile[0], end='')
            elif self.traces[tile]["mode"]  == 'mid':
                if tile[0] >= 10:
                    print(hex(tile[0]).lstrip('0x').ljust(3), end=' ')
                else:
                    print(str(tile[0]).ljust(3), end=' ')
            elif self.traces[tile]["mode"]  == 'hi':
                if tile[0] >= 10:
                    print(hex(tile[0]).lstrip('0x').ljust(15), end=' ')
                else:
                    print(str(tile[0]).ljust(15), end=' ')
            elif self.traces[tile]["mode"] == 'pc':
                if tile[0] >= 10:
                    print(hex(tile[0]).lstrip('0x').ljust(4), end=' ')
                else:
                    print(str(tile[0]).ljust(4), end=' ')
            elif self.traces[tile]["mode"]  == 'full':
                if tile[0] >= 10:
                    print(hex(tile[0]).lstrip('0x').ljust(20), end=' ')
                else:
                    print(str(tile[0]).ljust(20), end=' ')

        print("\nCycles", end='\t')
        for tile in self.traces:
            if self.traces[tile]["mode"] == 'lo':
                print(str(tile[1]), end='')
            elif self.traces[tile]["mode"] == 'mid':
                print(str(tile[1]).ljust(3), end=' ')
            elif self.traces[tile]["mode"] == 'hi':
                print(str(tile[1]).ljust(15), end=' ')
            elif self.traces[tile]["mode"] == 'pc':
                print(str(tile[1]).ljust(4), end=' ')
            elif self.traces[tile]["mode"] == 'full':
                print(str(tile[1]).ljust(20), end=' ')

        print()

        for cycle in range(self.start_cycle, self.end_cycle+1):
            print(cycle, end='\t')

            for tile in self.traces:
                self.__print_op(self.traces[tile], cycle, asm)
            print()
        
                
    def __print_op(self, tile_trace, cycle, asm = None):
        if tile_trace["mode"] == 'lo':
            if cycle in tile_trace["instr"]:
                op = tile_trace["instr"][cycle][1]
                if op in self._INSTRS_LIST or op in self._FP_INSTRS_LIST:
                    #print('\033[30;106m' + '@' + '\033[0m', end='')
                    print('@', end='')
                    #print(Fore.BLACK + Back.CYAN + '@', end='')
                elif op in self._STALLS_LIST:
                    print(self._STALLS_LIST[op][0], end='')
                    #print('\033[101m' + self._STALLS_LIST[op][0] + '\033[0m', end='')
                else:
                    print('0', end='')
            else :
                print(' ', end='')
        elif tile_trace["mode"] == 'mid':
            if cycle in tile_trace["instr"]:
                op = tile_trace["instr"][cycle][1]
                if op in self._INSTRS_LIST:
                    #print(Fore.BLACK + Back.CYAN + self._INSTRS_LIST[op], end=' ')
                    print(self._INSTRS_LIST[op], end=' ')
                elif op in self._FP_INSTRS_LIST:
                    #print(Fore.BLACK + Back.CYAN + self._FP_INSTRS_LIST[op], end=' ')
                    print(self._FP_INSTRS_LIST[op], end=' ')
                elif op in self._STALLS_LIST:
                    print(self._STALLS_LIST[op][1], end=' ')
                else:
                    print('000', end=' ')
            else:
                print('   ', end=' ')
        elif tile_trace["mode"] == 'hi':
            if cycle in tile_trace["instr"]:
                op_len = 15
                op = tile_trace["instr"][cycle][1]
                if len(op) < op_len:
                    print(op.ljust(op_len), end=' ')
                else:
                    print(op[:op_len], end=' ')
            else:
                print('               ', end=' ')
        elif tile_trace["mode"] == 'pc':
            if cycle in tile_trace["instr"]:
                pc = tile_trace["instr"][cycle][0]
                pc_len = 4
                if len(pc) < pc_len:
                    pc = pc.ljust(pc_len)
                else:
                    pc = pc[(-1)*pc_len:]

                if pc == 'fffc':
                    print('##FF', end=' ')
                else:
                    print(pc, end=' ')
                    #print(Fore.BLACK + Back.CYAN + pc, end=' ')
            else :
                print('    ', end='')
        elif tile_trace["mode"] == 'full':
            if cycle in tile_trace["instr"]:
                pc = tile_trace["instr"][cycle][0]
                op = tile_trace["instr"][cycle][1]
                op_len = 20
                pc_len = 4

                if pc == 'fffffffc':
                    if op in self._STALLS_LIST:
                        ind = op.index('_')
                        op = '##' + op[ind+1:]
                    else:
                        op = 'fffc ' + op
                else:
                    instr = asm[pc]
                    op = pc[(-1)*pc_len:]
                    for st in instr:
                        op = op + ' ' + st
                
                if len(op) < op_len:
                    print(op.ljust(op_len), end=' ')
                    #if op[:2] == '##':
                    #    print(op.ljust(op_len), end=' ')
                    #else:
                    #    print(Fore.BLACK + Back.CYAN + op.ljust(op_len), end=' ')
                else:
                    print(op[:op_len], end=' ')
                    #if op[:2] == '##':
                    #    print(op[:op_len], end=' ')
                    #else:
                    #    print(Fore.BLACK + Back.CYAN + op[:op_len], end=' ')
            else:
                print('                    ', end='')                

            
