#!/usr/bin/env python

import json
import os
import pexpect
import re
from pexpect.popen_spawn import PopenSpawn

PGN_HEADERS_REGEX = re.compile(r"\[([A-Za-z0-9_]+)\s+\"(.*)\"\]")
FIND_OUTPUT_REGEX = re.compile(r"}$")
BOOK_MAKE_DONE_REGEX = re.compile(r"Processing time \(ms\):[^$]+$")


class Parser:
    def __init__(self, engine=''):
        if not engine:
            engine = './parser'
        self.p = PopenSpawn(engine, encoding="utf-8")
        self.pgn = ''
        self.db = ''

    def open(self, pgn, full=True):
        '''Open a PGN file and create an index if not exsisting'''
        if not os.path.isfile(pgn):
            raise NameError("File {} does not exsist".format(pgn))
        self.pgn = pgn
        self.db = os.path.splitext(pgn)[0] + '.bin'
        if not os.path.isfile(self.db):
            self.db = self.make(full)

    def close(self):
        '''Terminate chess_db. Not really needed: engine will terminate as
           soon as pipe is closed, i.e. when we exit.'''
        self.p.sendline('quit')
        self.p.expect(pexpect.EOF)
        self.pgn = ''
        self.db = ''

    def make(self, full=True):
        '''Make an index out of a pgn file'''
        if not self.p or not self.pgn:
            raise NameError("Unknown DB, first open a PGN file")
        cmd = 'book ' + self.pgn
        if full:
            cmd += ' full'
        self.p.sendline(cmd)
        self.p.expect(BOOK_MAKE_DONE_REGEX)
        db = self.p.before.split('Book file: ')[1]
        db = db.split()[0]
        return db

    def find(self, fen, max_offsets=10):
        '''Find all games with positions equal to fen'''
        if not self.db or not self.p:
            raise NameError("Unknown DB, first open a PGN file")
        cmd = 'find ' + self.db + ' max_game_offsets ' + str(max_offsets) + ' '
        self.p.sendline(cmd + fen)
        self.p.expect(FIND_OUTPUT_REGEX)
        result = self.p.before + "}"
        self.p.before = ''
        return json.loads(result)

    def get_games(self, list):
        '''Retrieve the PGN games specified in the offsets list'''
        if not self.pgn:
            raise NameError("Unknown DB, first open a PGN file")
        pgn = []
        with open(self.pgn, "r") as f:
            for ofs in list:
                f.seek(ofs)
                game = ''
                for line in f:
                    if line.startswith('[Event "'):
                        if game:
                            break  # Second one, start of next game
                        else:
                            game = line  # First occurence
                    elif game:
                        game += line
                pgn.append(game.strip())
        return pgn

    def get_header(self, pgn):
        '''Return a dict with just header information out of a pgn game. The
           pgn tags are supposed to be consecutive'''
        header = {}
        for line in pgn.splitlines():
            line = line.strip()
            if line.startswith('[') and line.endswith(']'):
                tag_match = PGN_HEADERS_REGEX.match(line)
                if tag_match:
                    header[tag_match.group(1)] = tag_match.group(2)
                else:
                    break
        return header

    def get_game_headers(self, list):
        '''Return a list of headers out of a list of pgn games'''
        headers = []
        for pgn in list:
            h = self.get_header(pgn)
            headers.append(h)
        return headers
