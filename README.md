# chess_db

This repo helps index PGN files to polyglot books with the standard moves and weights, but also has Win/Loss/Draw Stats and game_index information.

1. To build, go to parser and execute "make build ARCH=x86-64 (or whatever your architecture is)"
2. sudo make install to make a system binary

To run:

1. Execute `parser book <pgn file> full` 

The full text is optional but building it allows generation of Win/Loss/Draw stats along with game_id information.

To query against the booK:

1. `parser find <book file ending in .bin> fen`
or
2. `parser find <book file ending in .bin> max_game_offsets <max number of game ids to output> fen`

Example:

`parser find ../pgn/hayes.bin max_game_offsets 2 rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1`

Output will be:

~~~
{
    "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "key": 5060803636482931868,
    "moves": [
       {
            "move": "e2e4", "weight": 36408, "games": 5, "wins": 1, "losses": 4, "draws": 0, "pgn offsets": [11576, 17728]
       },
       {
            "move": "d2d4", "weight": 29126, "games": 4, "wins": 1, "losses": 3, "draws": 0, "pgn offsets": [14368, 20176]
       }
    ]
}
~~~


