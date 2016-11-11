#!/usr/bin/env python

import argparse
import glob
import os
import sys
from subprocess import STDOUT, check_output as qx

Tests = { 'GM_games'                : {'games':  20, 'moves':  1519},
          'ambiguous'               : {'games':   2, 'moves':    76},
          'bali02'                  : {'games':  16, 'moves':  1485},
          'd00_chess_informant'     : {'games': 103, 'moves':  7019},
          'electronic_campfire'     : {'games':   6, 'moves':   286},
          'europe_echecs'           : {'games': 307, 'moves': 21024},
          'exeter_lessons_from_tal' : {'games':   8, 'moves':   628},
          'famous_games'            : {'games': 500, 'moves': 28796},
          'great_masters'           : {'games':  13, 'moves':   872},
          'hartwig'                 : {'games':  29, 'moves':  2203},
          'hayes'                   : {'games':   9, 'moves':   637},
          'human_computer'          : {'games':  29, 'moves':  2290},
          'immortal_games'          : {'games':  36, 'moves':  2026},
          'kramnik'                 : {'games':  40, 'moves':  3293},
          'middleg'                 : {'games': 481, 'moves': 31560},
          'moscow64'                : {'games':  42, 'moves':  3760},
          'newyork1924'             : {'games': 110, 'moves': 10517},
          'perle'                   : {'games': 194, 'moves': 14922},
          'polgar'                  : {'games':  40, 'moves':  3348},
          'pon_korch'               : {'games':   8, 'moves':  1003},
          'russian_chess'           : {'games':  10, 'moves':   725},
          'romero'                  : {'games':   4, 'moves':   363},
          'scarborough_2001'        : {'games': 385, 'moves': 30137},
          'scca'                    : {'games':  75, 'moves':  5114},
          'schiller'                : {'games':   8, 'moves':   681},
          'semicomm'                : {'games':  46, 'moves':  3004},
        }


def check_result(output, fname, stats):
    fname = os.path.splitext(fname)[0]
    games = output.split('Games: ')
    moves = output.split('Moves: ')
    if len(games) != 2 or len(moves) != 2:
        return 'ERROR\n' + output
    elif fname not in Tests:
        return 'done'
    else:
        games = int(games[1].split('\n')[0])
        moves = int(moves[1].split('\n')[0])
        stats['games'] += games
        stats['moves'] += moves
        ok1 = Tests[fname]['games'] == games
        ok2 = Tests[fname]['moves'] == moves
        return 'OK' if ok1 and ok2 else 'FAIL'


def run_file(file, stats):
    fname = os.path.basename(file)
    sys.stdout.write('Processing ' + fname + '...')
    sys.stdout.flush()
    output = qx(["./parser", file], stderr=STDOUT)
    result = check_result(output, fname, stats)
    print(result)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Run test on pgn files')
    parser.add_argument('--dir', default='../pgn/')
    args = parser.parse_args()

    if not os.path.isdir(args.dir):
        print("Directory {} does not exsist".format(args.dir))
        sys.exit(0)

    stats = {'moves': 0, 'games': 0}
    files = sorted(glob.glob(args.dir + '/*.pgn'))
    for fn in files:
        run_file(fn, stats)

    print("\ngames {}, moves {}\n".format(stats['games'], stats['moves']))
