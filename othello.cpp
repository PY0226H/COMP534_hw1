#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <vector>

// Include Cilk headers
#include <cilk/cilk.h>
#include <cilk/reducer.h>

// ---------------------------------------------------------------------
// Basic definitions
// ---------------------------------------------------------------------
#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1 - (c))

// Board is stored as two 64-bit masks for X and O
typedef unsigned long long ull;

typedef struct {
    ull disks[2]; 
} Board;

// A move is a row, col on the 8x8 board
typedef struct {
    int row;
    int col;
} Move;

// Offsets for flipping
Move offsets[] = {
  { 0,  1}, { 0, -1}, 
  {-1,  0}, { 1,  0}, 
  {-1, -1}, {-1,  1}, 
  { 1,  1}, { 1, -1}
};
int noffsets = sizeof(offsets) / sizeof(Move);

// For printing
char diskcolor[] = {'.', 'X', 'O', 'I'};

// Macros to map row,col -> bit positions
#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1ULL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT((m).row, (m).col)

// For row/column boundary checks
#define IS_MOVE_OFF_BOARD(m) ((m).row < 1 || (m).row > 8 || (m).col < 1 || (m).col > 8)

// Masks for each row or column
#define ROW8 ( \
  BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) | \
  BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8))

#define COL8 ( \
  BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) | \
  BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8))

#define COL1 (COL8 << 7)

#define MOVE_OFFSET_TO_BIT_OFFSET(m) ((m).row * 8 + (m).col)


// ---------------------------------------------------------------------
// Starting board
// X at (4,5) & (5,4); O at (4,4) & (5,5).
// The row=4, col=5 means the bit is set in the "X" mask, etc.
// ---------------------------------------------------------------------
Board start = {
    (BOARD_BIT(4,5) | BOARD_BIT(5,4)),  // X_BLACK
    (BOARD_BIT(4,4) | BOARD_BIT(5,5))   // O_WHITE
};

// ---------------------------------------------------------------------
// Printing the board
// ---------------------------------------------------------------------
void PrintDisk(int x_black, int o_white) {
    // x_black or o_white is 0/1, so this picks from diskcolor[] 
    printf(" %c", diskcolor[x_black + (o_white << 1)]);
}

void PrintBoardRow(int x_black, int o_white, int disks) {
    if (disks > 1) {
        PrintBoardRow(x_black >> 1, o_white >> 1, disks - 1);
    }
    PrintDisk(x_black & BIT, o_white & BIT);
}

void PrintBoardRows(ull x_black, ull o_white, int rowsleft) {
    if (rowsleft > 1) {
        PrintBoardRows(x_black >> 8, o_white >> 8, rowsleft - 1);
    }
    printf("%d", rowsleft);
    PrintBoardRow((int)(x_black & ROW8), (int)(o_white & ROW8), 8);
    printf("\n");
}

void PrintBoard(Board b) {
    printf("  1 2 3 4 5 6 7 8\n");
    PrintBoardRows(b.disks[X_BLACK], b.disks[O_WHITE], 8);
}

// ---------------------------------------------------------------------
// Functions to place or flip disks on the board
// ---------------------------------------------------------------------
void PlaceOrFlip(Move m, Board *b, int color) {
    ull bit = MOVE_TO_BOARD_BIT(m);
    b->disks[color] |= bit;
    b->disks[OTHERCOLOR(color)] &= ~bit;
}

/*
    TryFlips: recursively check if continuing in a direction
    can flip any opponent disks. If so, flip them.

    Returns 0 if no flips occur, or (1 + flips) if it eventually
    hits a disk of the same color after flipping some opponent disks.
*/
int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove) {
    Move next;
    next.row = m.row + offset.row;
    next.col = m.col + offset.col;

    if (!IS_MOVE_OFF_BOARD(next)) {
        ull nextbit = MOVE_TO_BOARD_BIT(next);
        // If next is an opponent disk, continue
        if (nextbit & b->disks[OTHERCOLOR(color)]) {
            int nflips = TryFlips(next, offset, b, color, verbose, domove);
            if (nflips > 0) {
                // flip the disk at 'next'
                if (verbose) {
                    printf("flipping disk at %d,%d\n", next.row, next.col);
                }
                if (domove) {
                    PlaceOrFlip(next, b, color);
                }
                return nflips + 1;
            }
        } 
        // If next is my disk => success
        else if (nextbit & b->disks[color]) {
            return 1;
        }
    }
    return 0;
}

// FlipDisks: tries flipping along all 8 directions; returns how many were flipped
int FlipDisks(Move m, Board *b, int color, int verbose, int domove) {
    int total_flips = 0;
    for (int i = 0; i < noffsets; i++) {
        int f = TryFlips(m, offsets[i], b, color, verbose, domove);
        if (f > 0) {
            total_flips += (f - 1); 
        }
    }
    return total_flips;
}

// ---------------------------------------------------------------------
// Generating / enumerating legal moves
// ---------------------------------------------------------------------
Board NeighborMoves(Board b, int color) {
    Board neighbors = {0ULL, 0ULL};
    for (int i = 0; i < noffsets; i++) {
        // handle edges
        ull colmask = 0ULL;
        if (offsets[i].col != 0) {
            colmask = (offsets[i].col > 0) ? COL1 : COL8;
        }
        int offset = MOVE_OFFSET_TO_BIT_OFFSET(offsets[i]);

        // shift bitboard of the opponent's disks
        if (offset > 0) {
            neighbors.disks[color] |= 
              (b.disks[OTHERCOLOR(color)] >> offset) & ~colmask;
        } else {
            neighbors.disks[color] |= 
              (b.disks[OTHERCOLOR(color)] << (-offset)) & ~colmask;
        }
    }
    // exclude already-occupied squares
    neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
    return neighbors;
}

/*
    EnumerateLegalMoves: returns a board with bits set
    for all legal moves for `color`, and also returns
    how many such moves exist.
*/
int EnumerateLegalMoves(Board b, int color, Board *legal_moves) {
    static Board no_legal = {0ULL, 0ULL};
    *legal_moves = no_legal;

    // potential squares adjacent to an opponent's disk
    Board candidates = NeighborMoves(b, color);
    ull candidateBits = candidates.disks[color];

    int num_moves = 0;

    for (int row = 8; row >= 1; row--) {
        // check the bits in the current row
        ull thisRow = candidateBits & ROW8;
        for (int col = 8; thisRow && (col >= 1); col--) {
            if (thisRow & COL8) {
                Move m = {row, col};
                // Check flipping in a temporary copy
                Board temp = b;
                if (FlipDisks(m, &temp, color, 0, 0) > 0) {
                    legal_moves->disks[color] |= BOARD_BIT(row,col);
                    num_moves++;
                }
            }
            thisRow >>= 1;
        }
        candidateBits >>= 8;
    }
    return num_moves;
}

// ---------------------------------------------------------------------
// Counting / evaluating the board
// ---------------------------------------------------------------------
int CountBitsOnBoard(const Board *b, int color) {
    ull bits = b->disks[color];
    int count = 0;
    while (bits) {
        bits &= (bits - 1);  // clear LSB set
        count++;
    }
    return count;
}

/*
    A simple evaluation: (# of color's disks) - (# of opponent's disks).
*/
int EvaluateBoard(const Board *b, int color) {
    int myCount  = CountBitsOnBoard(b, color);
    int oppCount = CountBitsOnBoard(b, OTHERCOLOR(color));
    return (myCount - oppCount);
}

// Return true if both players have no moves (game over)
int IsGameOver(Board b) {
    Board movesX, movesO;
    int xMoves = EnumerateLegalMoves(b, X_BLACK, &movesX);
    int oMoves = EnumerateLegalMoves(b, O_WHITE, &movesO);
    if (xMoves == 0 && oMoves == 0) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 1) Data structures for a custom reducer
// ---------------------------------------------------------------------

// We'll store (score, row, col) in each strand's "view"
struct MoveEval {
    int score;
    int row;
    int col;
};

struct BestMoveView {
    MoveEval val; // best partial result

    // Constructor
    BestMoveView() {
        val.score = INT_MIN;
        val.row   = -1;
        val.col   = -1;
    }

    // Optional constructor w/ initial MoveEval
    BestMoveView(const MoveEval &initVal) {
        val = initVal;
    }

    // Merge another view into this one
    void merge(const BestMoveView &rhs) {
        if (rhs.val.score > val.score) {
            val = rhs.val;
        }
        else if (rhs.val.score == val.score) {
            // tie-break (for determinism)
            if (rhs.val.row < val.row) {
                val = rhs.val;
            }
            else if (rhs.val.row == val.row && rhs.val.col < val.col) {
                val = rhs.val;
            }
        }
    }
};

// The monoid must define view_type, reduce(), identity()
struct BestMoveMonoid {
    typedef BestMoveView view_type; // required
    static void reduce(view_type* left, view_type* right) {
        left->merge(*right);
    }
    static void identity(view_type* v) {
        v->val.score = INT_MIN;
        v->val.row   = -1;
        v->val.col   = -1;
    }
};

// This is the actual reducer type
typedef cilk::reducer<BestMoveMonoid> best_move_reducer_t;


// ---------------------------------------------------------------------
// 2) Parallel negamax search
// ---------------------------------------------------------------------

// Forward declaration
int Negamax(Board b, int color, int depth, Move *chosenMove);

// EvaluateSingleMove: a helper function to spawn
void EvaluateSingleMove(Board b, int color, Move m, int depth,
                        best_move_reducer_t &bestR)
{
    // 1. Apply the move on a local copy
    FlipDisks(m, &b, color, 0, 1);
    PlaceOrFlip(m, &b, color);

    // 2. Recurse with negamax from the opponent's perspective
    int subScore = -Negamax(b, OTHERCOLOR(color), depth - 1, NULL);

    // 3. Merge partial result into the reducer
    BestMoveView localView;
    localView.val.score = subScore;
    localView.val.row   = m.row;
    localView.val.col   = m.col;

    bestR->merge(localView); // merges local result into the global
}


/*
   Negamax(b, color, depth): returns best score at the current node,
   sets chosenMove if non-null. Depth-limited, skipping moves if necessary.
*/
int Negamax(Board b, int color, int depth, Move *chosenMove) {
    // base case
    if (depth == 0 || IsGameOver(b)) {
        return EvaluateBoard(&b, color);
    }
    // gather all legal moves
    Board legalMoves;
    int numMoves = EnumerateLegalMoves(b, color, &legalMoves);
    if (numMoves == 0) {
        // If truly no moves for either side => game ends
        if (IsGameOver(b)) {
            return EvaluateBoard(&b, color);
        }
        // otherwise skip turn (no move used, so same depth)
        return -Negamax(b, OTHERCOLOR(color), depth, chosenMove);
    }

    // 1) Collect the actual moves in a vector
    std::vector<Move> possibleMoves;
    ull bits = legalMoves.disks[color];
    // Because bits are stored row=8..1, we do:
    for (int row = 8; row >= 1; row--) {
        ull thisRow = bits & ROW8;
        for (int col = 8; thisRow && col >= 1; col--) {
            if (thisRow & COL8) {
                Move mv = {row, col};
                possibleMoves.push_back(mv);
            }
            thisRow >>= 1;
        }
        bits >>= 8;
    }

    // 2) Create a reducer for the best move
    best_move_reducer_t bestReducer;

    // 3) Parallel spawn for each legal move
    for (auto &m : possibleMoves) {
        cilk_spawn EvaluateSingleMove(b, color, m, depth, bestReducer);
    }
    cilk_sync; // wait for all subcalls

    // 4) Extract final best (score, row, col)
    MoveEval finalEval = bestReducer->view().val;
    if (chosenMove) {
        chosenMove->row = finalEval.row;
        chosenMove->col = finalEval.col;
    }
    return finalEval.score;
}


// ---------------------------------------------------------------------
// Turn logic: Human or Computer
// ---------------------------------------------------------------------
int HumanTurn(Board *b, int color) {
    Board legal;
    int numMoves = EnumerateLegalMoves(*b, color, &legal);
    if (numMoves == 0) return 0; // no moves

    for (;;) {
        printf("Enter %c's move as 'row,col': ", diskcolor[color + 1]);
        Move m;
        scanf("%d,%d", &m.row, &m.col);

        // check if on board
        if (IS_MOVE_OFF_BOARD(m)) {
            printf("Illegal move: row,col out of range\n");
            PrintBoard(*b);
            continue;
        }
        // check if empty
        ull bit = MOVE_TO_BOARD_BIT(m);
        if (bit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
            printf("Illegal move: position occupied\n");
            PrintBoard(*b);
            continue;
        }
        // check flipping
        Board temp = *b;
        if (FlipDisks(m, &temp, color, 0, 0) == 0) {
            printf("Illegal move: no disks flipped\n");
            PrintBoard(*b);
            continue;
        }
        // valid move => do it
        int flips = FlipDisks(m, b, color, 1, 1);
        PlaceOrFlip(m, b, color);
        printf("You flipped %d disks\n", flips);
        PrintBoard(*b);
        break;
    }
    return 1;
}

int ComputerTurn(Board *b, int color, int depth) {
    Board legal;
    int numMoves = EnumerateLegalMoves(*b, color, &legal);
    if (numMoves == 0) return 0; // no moves

    Move bestM;
    int bestScore = Negamax(*b, color, depth, &bestM);

    // Apply best move
    printf("\nComputer (%c) chooses %d,%d => predicted score = %d\n",
           diskcolor[color + 1], bestM.row, bestM.col, bestScore);

    int flips = FlipDisks(bestM, b, color, 1, 1);
    PlaceOrFlip(bestM, b, color);
    printf("Flipped %d disks.\n", flips);
    PrintBoard(*b);
    return 1;
}

// ---------------------------------------------------------------------
// End game
// ---------------------------------------------------------------------
void EndGame(Board b) {
    int xcount = CountBitsOnBoard(&b, X_BLACK);
    int ocount = CountBitsOnBoard(&b, O_WHITE);
    printf("Game over.\n");
    if (xcount == ocount) {
        printf("Tie: each has %d disks\n", xcount);
    } else {
        printf("X has %d, O has %d. %c wins.\n",
               xcount, ocount, (xcount > ocount ? 'X' : 'O'));
    }
}

// ---------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------
int main(int argc, char* argv[]) {
    Board gameboard = start;
    PrintBoard(gameboard);

    // Prompt for each player's type (human or computer)
    char p1_type, p2_type;
    int p1_depth = 0, p2_depth = 0;

    printf("Is player X (1) human or computer? (h/c): ");
    scanf(" %c", &p1_type);
    if (p1_type == 'c') {
        printf("Enter search depth for X (1..60): ");
        scanf("%d", &p1_depth);
    }

    printf("Is player O (2) human or computer? (h/c): ");
    scanf(" %c", &p2_type);
    if (p2_type == 'c') {
        printf("Enter search depth for O (1..60): ");
        scanf("%d", &p2_depth);
    }

    int color = X_BLACK; // X goes first

    // Keep playing while not game over
    while (!IsGameOver(gameboard)) {
        int didMove = 0;
        if (color == X_BLACK) {
            // X's turn
            if (p1_type == 'h') {
                didMove = HumanTurn(&gameboard, color);
            } else {
                didMove = ComputerTurn(&gameboard, color, p1_depth);
            }
        } else {
            // O's turn
            if (p2_type == 'h') {
                didMove = HumanTurn(&gameboard, color);
            } else {
                didMove = ComputerTurn(&gameboard, color, p2_depth);
            }
        }

        if (didMove == 0) {
            printf("%c cannot move, skipping turn.\n", diskcolor[color + 1]);
        }
        // Switch player
        color = OTHERCOLOR(color);
    }

    EndGame(gameboard);
    return 0;
}
