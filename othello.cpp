#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <vector>

// Include Cilk headers
#include <cilk/cilk.h>
#include <cilk/reducer.h>
#include <cilk/cilk_api.h>  // for controlling # of workers if desired

// ---------------------------------------------------------------------
// Basic definitions
// ---------------------------------------------------------------------
#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1 - (c))

typedef unsigned long long ull;

// Board storing black and white disks via two 64-bit masks
typedef struct {
    ull disks[2];
} Board;

// A move is row,col on the 8x8 board
typedef struct {
    int row;
    int col;
} Move;

Move offsets[] = {
  { 0,  1}, { 0, -1},
  {-1,  0}, { 1,  0},
  {-1, -1}, {-1,  1},
  { 1,  1}, { 1, -1}
};
int noffsets = sizeof(offsets) / sizeof(Move);

char diskcolor[] = {'.', 'X', 'O', 'I'};

// Macros for mapping (row,col) to bit positions
#define BOARD_BIT_INDEX(row, col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row, col)       (0x1ULL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m)      BOARD_BIT((m).row, (m).col)

// Check if move is within 1..8 for row,col
#define IS_MOVE_OFF_BOARD(m) ((m).row < 1 || (m).row > 8 || (m).col < 1 || (m).col > 8)

// Masks for rows, columns
#define ROW8 ( \
  BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) | \
  BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8) )

#define COL8 ( \
  BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) | \
  BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8) )

#define COL1 (COL8 << 7)
#define MOVE_OFFSET_TO_BIT_OFFSET(m) ((m).row * 8 + (m).col)

// Starting board (standard Reversi setup)
Board start = {
    (BOARD_BIT(4,5) | BOARD_BIT(5,4)), // X (black)
    (BOARD_BIT(4,4) | BOARD_BIT(5,5))  // O (white)
};

// ---------------------------------------------------------------------
// Printing the board
// ---------------------------------------------------------------------
void PrintDisk(int x_black, int o_white) {
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
    PrintBoardRows(b.disks[0], b.disks[1], 8);
}

// ---------------------------------------------------------------------
// Place or flip disks
// ---------------------------------------------------------------------
void PlaceOrFlip(Move m, Board *b, int color) {
    ull bit = MOVE_TO_BOARD_BIT(m);
    b->disks[color] |= bit;
    b->disks[OTHERCOLOR(color)] &= ~bit;
}

/*
    TryFlips: recursively check if continuing in a direction
    can flip any opponent disks. Returns 0 if no flips, or
    (1 + #flips) if we eventually find a color disk.
*/
int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove) {
    Move next;
    next.row = m.row + offset.row;
    next.col = m.col + offset.col;

    if (!IS_MOVE_OFF_BOARD(next)) {
        ull nextbit = MOVE_TO_BOARD_BIT(next);
        if (nextbit & b->disks[OTHERCOLOR(color)]) {
            int nflips = TryFlips(next, offset, b, color, verbose, domove);
            if (nflips > 0) {
                if (verbose) {
                    printf("flipping disk at %d,%d\n", next.row, next.col);
                }
                if (domove) {
                    PlaceOrFlip(next, b, color);
                }
                return nflips + 1;
            }
        }
        else if (nextbit & b->disks[color]) {
            return 1;
        }
    }
    return 0;
}

// FlipDisks: tries flipping along all offsets, returns total # flipped
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
// Generating legal moves
// ---------------------------------------------------------------------
Board NeighborMoves(Board b, int color) {
    Board neighbors = {0ULL, 0ULL};
    for (int i = 0; i < noffsets; i++) {
        ull colmask = 0ULL;
        if (offsets[i].col != 0) {
            colmask = (offsets[i].col > 0) ? COL1 : COL8;
        }
        int offset = MOVE_OFFSET_TO_BIT_OFFSET(offsets[i]);
        if (offset > 0) {
            neighbors.disks[color] |= 
              (b.disks[OTHERCOLOR(color)] >> offset) & ~colmask;
        } else {
            neighbors.disks[color] |= 
              (b.disks[OTHERCOLOR(color)] << (-offset)) & ~colmask;
        }
    }
    // exclude squares already occupied
    neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
    return neighbors;
}

/*
    EnumerateLegalMoves: returns a board with bits set
    for all legal moves for `color`, and returns # of such moves.
*/
int EnumerateLegalMoves(Board b, int color, Board *legal_moves) {
    static Board no_legal = {0ULL, 0ULL};
    *legal_moves = no_legal;

    Board candidates = NeighborMoves(b, color);
    ull candidateBits = candidates.disks[color];

    int num_moves = 0;
    for (int row = 8; row >= 1; row--) {
        ull thisRow = candidateBits & ROW8;
        for (int col = 8; thisRow && (col >= 1); col--) {
            if (thisRow & COL8) {
                Move m = {row, col};
                // Check flipping in a temp copy
                Board temp = b;
                if (FlipDisks(m, &temp, color, 0, 0) > 0) {
                    legal_moves->disks[color] |= BOARD_BIT(row, col);
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
        bits &= (bits - 1);
        count++;
    }
    return count;
}

int EvaluateBoard(const Board *b, int color) {
    int myCount = CountBitsOnBoard(b, color);
    int oppCount = CountBitsOnBoard(b, OTHERCOLOR(color));
    return (myCount - oppCount);
}

// If both sides have no moves => game over
int IsGameOver(Board b) {
    Board movesX, movesO;
    int xMoves = EnumerateLegalMoves(b, X_BLACK, &movesX);
    int oMoves = EnumerateLegalMoves(b, O_WHITE, &movesO);
    return ((xMoves == 0) && (oMoves == 0));
}

// ---------------------------------------------------------------------
// 1) Data structures for a custom reducer
// ---------------------------------------------------------------------
struct MoveEval {
    int score;
    int row;
    int col;
};

// Each strand's partial result
struct BestMoveView {
    MoveEval val;  // best so far

    // Default constructor only
    BestMoveView() {
        val.score = INT_MIN;
        val.row   = -1;
        val.col   = -1;
    }

    // Merging logic
    void merge(const BestMoveView &rhs) {
        if (rhs.val.score > val.score) {
            val = rhs.val;
        }
        else if (rhs.val.score == val.score) {
            // tie-break for determinism
            if (rhs.val.row < val.row) {
                val = rhs.val;
            }
            else if (rhs.val.row == val.row && rhs.val.col < val.col) {
                val = rhs.val;
            }
        }
    }
};

// Inherit from monoid_base for older Intel compilers
struct BestMoveMonoid : public cilk::monoid_base<BestMoveView> {
    // Provide value_type and align_reducer
    typedef BestMoveView value_type;
    static const bool align_reducer = false;

    static void identity(value_type* p) {
        p->val.score = INT_MIN;
        p->val.row   = -1;
        p->val.col   = -1;
    }
    static void reduce(value_type* left, value_type* right) {
        left->merge(*right);
    }
};

// Our custom reducer type
typedef cilk::reducer<BestMoveMonoid> best_move_reducer_t;

// ---------------------------------------------------------------------
// 2) Negamax with a cilk_for approach
// ---------------------------------------------------------------------
int Negamax(Board b, int color, int depth, Move *chosenMove);

// EvaluateSingleMove: do a single child move => Negamax result
MoveEval EvaluateSingleMove(Board b, int color, Move m, int depth) {
    // Apply move locally
    FlipDisks(m, &b, color, 0, 1);
    PlaceOrFlip(m, &b, color);
    // Recurse: negamax from opponent's perspective
    int subScore = -Negamax(b, OTHERCOLOR(color), depth - 1, NULL);

    // Return partial result
    MoveEval local;
    local.score = subScore;
    local.row   = m.row;
    local.col   = m.col;
    return local;
}

int Negamax(Board b, int color, int depth, Move *chosenMove) {
    // Base case
    if (depth == 0 || IsGameOver(b)) {
        return EvaluateBoard(&b, color);
    }

    // gather legal moves
    Board legal;
    int numMoves = EnumerateLegalMoves(b, color, &legal);
    if (numMoves == 0) {
        if (IsGameOver(b)) {
            return EvaluateBoard(&b, color);
        }
        // skip turn
        return -Negamax(b, OTHERCOLOR(color), depth, chosenMove);
    }

    // Build a vector of moves
    std::vector<Move> possibleMoves;
    ull bits = legal.disks[color];
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

    // Create a reducer for best move
    best_move_reducer_t bestReducer;

    // Evaluate each move in parallel
    cilk_for (size_t i = 0; i < possibleMoves.size(); i++) {
        MoveEval partial = EvaluateSingleMove(b, color, possibleMoves[i], depth);

        // We can't do `merge({partial})` because older compilers
        // need a matching constructor. So we do this:
        BestMoveView temp;
        temp.val = partial;
        bestReducer->merge(temp);
    }

    // final result
    BestMoveView finalView = bestReducer.get_value();
    if (chosenMove) {
        chosenMove->row = finalView.val.row;
        chosenMove->col = finalView.val.col;
    }
    return finalView.val.score;
}

// ---------------------------------------------------------------------
// Human/Computer turn logic
// ---------------------------------------------------------------------
int HumanTurn(Board *b, int color) {
    Board legal;
    int numMoves = EnumerateLegalMoves(*b, color, &legal);
    if (numMoves == 0) {
        return 0;
    }
    for (;;) {
        printf("Enter %c's move as 'row,col': ", diskcolor[color+1]);
        Move m;
        scanf("%d,%d", &m.row, &m.col);

        if (IS_MOVE_OFF_BOARD(m)) {
            printf("Illegal move: row,col out of range.\n");
            PrintBoard(*b);
            continue;
        }
        ull bit = MOVE_TO_BOARD_BIT(m);
        if (bit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
            printf("Illegal move: position occupied.\n");
            PrintBoard(*b);
            continue;
        }
        // check flipping
        Board temp = *b;
        if (FlipDisks(m, &temp, color, 0, 0) == 0) {
            printf("Illegal move: no disks flipped.\n");
            PrintBoard(*b);
            continue;
        }
        // do the move
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
    if (numMoves == 0) {
        return 0;
    }
    Move bestM;
    int bestScore = Negamax(*b, color, depth, &bestM);

    printf("\nComputer (%c) chooses move %d,%d => predicted score = %d\n",
           diskcolor[color+1], bestM.row, bestM.col, bestScore);

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
        printf("Tie: each has %d disks.\n", xcount);
    } else {
        printf("X has %d, O has %d. %c wins.\n",
               xcount, ocount, (xcount > ocount ? 'X' : 'O'));
    }
}

// ---------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------
int main(int argc, char* argv[])
{
    Board gameboard = start;
    PrintBoard(gameboard);

    // prompt for each player's type
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
    while (!IsGameOver(gameboard)) {
        int didMove = 0;
        if (color == X_BLACK) {
            if (p1_type == 'h') {
                didMove = HumanTurn(&gameboard, color);
            } else {
                didMove = ComputerTurn(&gameboard, color, p1_depth);
            }
        } else {
            if (p2_type == 'h') {
                didMove = HumanTurn(&gameboard, color);
            } else {
                didMove = ComputerTurn(&gameboard, color, p2_depth);
            }
        }
        if (didMove == 0) {
            printf("%c cannot move, skipping turn.\n", diskcolor[color+1]);
        }
        color = OTHERCOLOR(color);
    }

    EndGame(gameboard);
    return 0;
}
