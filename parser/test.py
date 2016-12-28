#!/usr/bin/env python

import argparse
import hashlib
import glob
import json
import os
import sys
from subprocess import STDOUT, check_output as qx
from chess_db import Parser

DB = {'GM_games'               : {'games':  20, 'moves':  1519, 'fixed':   0},
      'ambiguous'              : {'games':   4, 'moves':   194, 'fixed':   2},
      'bali02'                 : {'games':  16, 'moves':  1485, 'fixed':   0},
      'chessdoctor'            : {'games':  55, 'moves':  4648, 'fixed': 249},
      'd00_chess_informant'    : {'games': 103, 'moves':  7019, 'fixed':   0},
      'electronic_campfire'    : {'games':   6, 'moves':   286, 'fixed':   2},
      'europe_echecs'          : {'games': 307, 'moves': 21024, 'fixed':  15},
      'exeter_lessons_from_tal': {'games':   8, 'moves':   628, 'fixed':   0},
      'famous_games'           : {'games': 500, 'moves': 28796, 'fixed':  60},
      'great_masters'          : {'games':  13, 'moves':   872, 'fixed':  34},
      'hartwig'                : {'games':  29, 'moves':  2203, 'fixed':   2},
      'hayes'                  : {'games':   9, 'moves':   637, 'fixed':   0},
      'human_computer'         : {'games':  29, 'moves':  2290, 'fixed':   0},
      'immortal_games'         : {'games':  36, 'moves':  2026, 'fixed':   8},
      'kramnik'                : {'games':  40, 'moves':  3293, 'fixed':   1},
      'middleg'                : {'games': 481, 'moves': 31560, 'fixed':  19},
      'moscow64'               : {'games':  42, 'moves':  3760, 'fixed':   0},
      'newyork1924'            : {'games': 110, 'moves': 10517, 'fixed':  27},
      'perle'                  : {'games': 194, 'moves': 14922, 'fixed':  11},
      'polgar'                 : {'games':  40, 'moves':  3348, 'fixed':   0},
      'pon_korch'              : {'games':   8, 'moves':  1003, 'fixed':   0},
      'romero'                 : {'games':   4, 'moves':   363, 'fixed':   0},
      'russian_chess'          : {'games':  10, 'moves':   725, 'fixed':   8},
      'scarborough_2001'       : {'games': 385, 'moves': 30137, 'fixed':  15},
      'scca'                   : {'games':  75, 'moves':  5114, 'fixed':   6},
      'schiller'               : {'games':   8, 'moves':   681, 'fixed':   0},
      'semicomm'               : {'games':  46, 'moves':  3004, 'fixed':   1}}


FIND_TEST = {
    'romero.bin' : {
        "input": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                  'output':
                       {
                           "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                           "key": 5060803636482931868,
                           "moves": [
                              {
                                   "move": "e2e4", "weight": 49151, "games": 3, "wins": 1, "losses": 2, "draws": 0, "pgn offsets": [0, 5176, 3656]
                              },
                              {
                                   "move": "d2d4", "weight": 16383, "games": 1, "wins": 1, "losses": 0, "draws": 0, "pgn offsets": [1688]
                              }
                           ]
                       }

    },
    'hayes.bin' : {
        "input": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "input_2": {"type": "skiplimit", "cmd": "limit 2 skip 1 rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        'output':
            {"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "key": 5060803636482931868, "moves": [
                {"draws": 0, "games": 5, "losses": 4, "move": "e2e4", "pgn offsets": [0, 2808, 5464, 8304, 16480],
                 "weight": 36408, "wins": 1},
                {"draws": 0, "games": 4, "losses": 3, "move": "d2d4", "pgn offsets": [14368, 17736, 1512, 11576],
                 "weight": 29126, "wins": 1}]
             }
    },

}

def signature(matches):
    d = str(matches)
    sha = hashlib.sha1(d).hexdigest()
    return sha[:7]

def check_result(output, fname, stats):
    fname = os.path.splitext(fname)[0]
    games = output.split('Games: ')
    moves = output.split('Moves: ')
    fixed = output.split('Incorrect moves: ')
    if len(games) != 2 or len(moves) != 2 or len(fixed) != 2:
        return 'ERROR\n' + output
    elif fname not in DB:
        return 'done'
    else:
        games = int(games[1].split('\n')[0])
        moves = int(moves[1].split('\n')[0])
        fixed = int(fixed[1].split('\n')[0])
        stats['games'] += games
        stats['moves'] += moves
        stats['fixed'] += fixed
        ok1 = DB[fname]['games'] == games
        ok2 = DB[fname]['moves'] == moves
        ok3 = DB[fname]['fixed'] == fixed
        return 'OK' if ok1 and ok2 and ok3 else 'FAIL'


def run_file(file, stats):
    fname = os.path.basename(file)
    sys.stdout.write('Processing ' + fname + '...')
    sys.stdout.flush()
    output = qx(["./parser", 'book', file, 'full'], stderr=STDOUT)
    result = check_result(output, fname, stats)
    print(result)


def run_find_test(fname, expected_output):
    sys.stdout.write('Processing ' + fname + ' for find test')
    output = qx(["./parser", 'find', fname, expected_output['input']], stderr=STDOUT)

    json_output = json.loads(output)
    for m in json_output["moves"]:
        m["pgn offsets"].sort()

    sorted_output = json.dumps(json_output, sort_keys=True)

    expected_output_dict = expected_output[u'output']
    for m in expected_output_dict["moves"]:
        m["pgn offsets"].sort()

    expected_result = json.dumps(expected_output_dict, sort_keys=True)

    assert (sorted_output == expected_result)

    if 'input_2' in expected_output and 'type' in expected_output['input_2'] \
            and expected_output['input_2']['type'] == 'skiplimit':
        output_2 = qx(["./parser", 'find', fname, expected_output['input_2']['cmd']], stderr=STDOUT)
        json_output_2 = json.loads(output_2)
        json_output = json.loads(output)

        for i, m in enumerate(json_output["moves"]):
            assert(set(json_output_2["moves"][i]["pgn offsets"]) < set(m["pgn offsets"]))

    sys.stdout.write('...OK\n')
    sys.stdout.flush()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Run test on pgn files')
    parser.add_argument('--dir', default='../pgn/')
    args = parser.parse_args()

    if not os.path.isdir(args.dir):
        print("Directory {} does not exsist".format(args.dir))
        sys.exit(0)

    stats = {'games': 0, 'moves': 0, 'fixed': 0}
    files = sorted(glob.glob(args.dir + '/*.pgn'))
    for fn in files:
        run_file(fn, stats)

    for k, v in FIND_TEST.items():
        run_find_test(args.dir+k, v)

    print("\ngames {}, moves {}, fixed {}\n"
          .format(stats['games'], stats['moves'], stats['fixed']))
