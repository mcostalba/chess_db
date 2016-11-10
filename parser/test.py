#!/usr/bin/env python

import argparse
import glob
import os
import sys
from subprocess import STDOUT, check_output as qx

Tests = { 'GM_games'              : { 'games': 20, 'moves':1519},
          'ambiguous'             : { 'games':  1, 'moves':  59},
          'bali02'                : { 'games': 16, 'moves':1485},
          'd00_chess_informant'   : { 'games':103, 'moves':7019},
          'electronic_campfire'   : { 'games':  6, 'moves': 286},
}

def check_result(output, fname):
    fname = os.path.splitext(fname)[0]
    games = output.split('Games: ')
    moves = output.split('Moves: ')
    if len(games) != 2 or len(moves) != 2:
        return 'ERROR\n' + output
    elif fname not in Tests:
        return 'done'
    else:
        games = games[1].split('\n')[0]
        moves = moves[1].split('\n')[0]
        ok1 = Tests[fname]['games'] == int(games)
        ok2 = Tests[fname]['moves'] == int(moves)
        return 'OK' if ok1 and ok2 else 'FAIL'

def run_file(file):
    fname = os.path.basename(file)
    sys.stdout.write('Processing ' + fname + '...')
    sys.stdout.flush()
    output = qx(["./parser", file], stderr=STDOUT)
    result = check_result(output, fname)
    print(result)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Run test on pgn files')
    parser.add_argument('--dir', default='../pgn/')
    args = parser.parse_args()

    if not os.path.isdir(args.dir):
        print("Directory {} does not exsist".format(args.dir))
        sys.exit(0)

    files = sorted(glob.glob(args.dir + '/*.pgn'))
    for fn in files:
        run_file(fn)
