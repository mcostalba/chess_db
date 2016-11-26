/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  The code in this file is based on the opening book code in PolyGlot
  by Fabien Letouzey. PolyGlot is available under the GNU General
  Public License, and can be downloaded from http://wbec-ridderkerk.nl
*/

#include <algorithm>
#include <cassert>

#include "book.h"
#include "misc.h"

using namespace std;

PolyglotBook::~PolyglotBook() { if (is_open()) close(); }


/// operator>>() reads sizeof(T) chars from the file's binary byte stream and
/// converts them into a number of type T. A Polyglot book stores numbers in
/// big-endian format.

template<typename T> PolyglotBook& PolyglotBook::operator>>(T& n) {

  n = 0;
  for (size_t i = 0; i < sizeof(T); ++i)
      n = T((n << 8) + ifstream::get());

  return *this;
}

template<> PolyglotBook& PolyglotBook::operator>>(PolyEntry& e) {
  return *this >> e.key >> e.move >> e.weight >> e.learn;
}


/// open() tries to open a book file with the given name after closing any
/// existing one.

bool PolyglotBook::open(const char* fName) {

  if (is_open()) // Cannot close an already closed file
      close();

  ifstream::open(fName, ifstream::in | ifstream::binary);

  fileName = is_open() ? fName : "";
  ifstream::clear(); // Reset any error flag to allow a retry ifstream::open()
  return !fileName.empty();
}


/// probe() tries to find a book move for the given position. If no move is
/// found, it returns MOVE_NONE. If pickBest is true, then it always returns
/// the highest-rated move, otherwise it randomly chooses one based on the
/// move score.

size_t PolyglotBook::probe(Key key, const string& fName) {

  if (fileName != fName && !open(fName.c_str()))
      return 0;

  size_t ofs = find_first(key);
  close();
  return ofs;
}


/// find_first() takes a book key as input, and does a binary search through
/// the book file for the given key. Returns the index of the leftmost book
/// entry with the same key as the input.

size_t PolyglotBook::find_first(Key key) {

  seekg(0, ios::end); // Move pointer to end, so tellg() gets file's size

  size_t low = 0, mid, high = (size_t)tellg() / SizeOfPolyEntry - 1;
  PolyEntry e;

  assert(low <= high);

  while (low < high && good())
  {
      mid = (low + high) / 2;

      assert(mid >= low && mid < high);

      seekg(mid * SizeOfPolyEntry, ios_base::beg);
      *this >> e;

      if (key <= e.key)
          high = mid;
      else
          low = mid + 1;
  }

  assert(low == high);

  return low * SizeOfPolyEntry;
}
