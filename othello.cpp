#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <cilk/cilk.h>
#include <cilk/reducer.h>
#include <cilk/reducer_min_max.h>
#include <assert.h>

#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1 - (c))

#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1ULL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT(m.row, m.col)

#define ROW8 \
  (BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) | \
   BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8))

#define COL8 \
  (BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) | \
   BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8))

#define COL1 (COL8 << 7)

#define IS_MOVE_OFF_BOARD(m) (m.row < 1 || m.row > 8 || m.col < 1 || m.col > 8)
#define MOVE_OFFSET_TO_BIT_OFFSET(m) (m.row * 8 + m.col)

typedef unsigned long long ull;

typedef struct {
    ull disks[2];
} Board;

typedef struct {
    int row;
    int col;
} Move;

Move offsets[] = {
  {0,1},   {0,-1}, 
  {-1,0},  {1,0},  
  {-1,-1}, {-1,1}, 
  {1,1},   {1,-1}
};
int noffsets = sizeof(offsets)/sizeof(Move);

// for printing
char diskcolor[] = { '.', 'X', 'O', 'I' };

// === The initial 8x8 board with 4 starting disks in the center
Board start = {
    (BOARD_BIT(4,5) | BOARD_BIT(5,4)), // X_BLACK
    (BOARD_BIT(4,4) | BOARD_BIT(5,5))  // O_WHITE
};

// ---------------------------------------------------------------------
// Utility / printing
// ---------------------------------------------------------------------

void PrintDisk(int x_black, int o_white)
{
    printf(" %c", diskcolor[x_black + (o_white << 1)]);
}

void PrintBoardRow(int x_black, int o_white, int disks)
{
    if (disks > 1) {
        PrintBoardRow(x_black >> 1, o_white >> 1, disks - 1);
    }
    PrintDisk(x_black & BIT, o_white & BIT);
}

void PrintBoardRows(ull x_black, ull o_white, int rowsleft)
{
    if (rowsleft > 1) {
        PrintBoardRows(x_black >> 8, o_white >> 8, rowsleft - 1);
    }
    printf("%d", rowsleft);
    PrintBoardRow((int)(x_black & ROW8), (int)(o_white & ROW8), 8);
    printf("\n");
}

void PrintBoard(Board b)
{
    printf("  1 2 3 4 5 6 7 8\n");
    PrintBoardRows(b.disks[0], b.disks[1], 8);
}

// ---------------------------------------------------------------------
// Flip logic
// ---------------------------------------------------------------------

void PlaceOrFlip(Move m, Board *b, int color)
{
    ull bit = MOVE_TO_BOARD_BIT(m);
    b->disks[color] |= bit;
    b->disks[OTHERCOLOR(color)] &= ~bit;
}

/*
  TryFlips: check if continuing along a direction can flip
  any opponent disks. Return 0 if no flips were made, or
  1 + numberOfFlips if successful (the 1 is a sentinel marker).
*/
int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove)
{
    Move next;
    next.row = m.row + offset.row;
    next.col = m.col + offset.col;

    if (!IS_MOVE_OFF_BOARD(next)) {
        ull nextbit = MOVE_TO_BOARD_BIT(next);
        // if next is an opponent disk, keep going in same direction
        if (nextbit & b->disks[OTHERCOLOR(color)]) {
            int nflips = TryFlips(next, offset, b, color, verbose, domove);
            if (nflips) {
                if (verbose)
                    printf("flipping disk at %d,%d\n", next.row, next.col);
                if (domove) PlaceOrFlip(next, b, color);
                return nflips + 1;
            }
        }
        // if next is my disk, success
        else if (nextbit & b->disks[color]) {
            return 1;
        }
    }
    return 0;
}

int FlipDisks(Move m, Board *b, int color, int verbose, int domove)
{
    int i;
    int nflips = 0;

    for (i = 0; i < noffsets; i++) {
        int flipresult = TryFlips(m, offsets[i], b, color, verbose, domove);
        if (flipresult > 0) {
            // the returned flipresult is 1 + numberOfDisksFlipped
            nflips += flipresult - 1;
        }
    }
    return nflips;
}

// ---------------------------------------------------------------------
// Move enumeration
// ---------------------------------------------------------------------

Board NeighborMoves(Board b, int color)
{
    Board neighbors = {0ULL, 0ULL};
    for (int i = 0; i < noffsets; i++) {
        // handle edges
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
    neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
    return neighbors;
}

/*
  EnumerateLegalMoves: build a Board structure that has bits set
  for all legal move positions for 'color'. Returns number of moves.
*/
int EnumerateLegalMoves(Board b, int color, Board *legal_moves)
{
    static Board no_legal_moves = {0ULL, 0ULL};
    Board neighbors = NeighborMoves(b, color);
    ull my_neighbor_moves = neighbors.disks[color];
    *legal_moves = no_legal_moves;

    int num_moves = 0;
    for (int row = 8; row >= 1; row--) {
        ull thisrow = (my_neighbor_moves & ROW8);
        for (int col = 8; thisrow && col >= 1; col--) {
            if (thisrow & COL8) {
                Move m = {row, col};
                // check if flipping is possible
                if (FlipDisks(m, &b, color, 0, 0) > 0) {
                    legal_moves->disks[color] |= BOARD_BIT(row, col);
                    num_moves++;
                }
            }
            thisrow >>= 1;
        }
        my_neighbor_moves >>= 8;
    }
    return num_moves;
}

// ---------------------------------------------------------------------
// Count and Evaluate
// ---------------------------------------------------------------------

int CountBitsOnBoard(Board *b, int color)
{
    ull bits = b->disks[color];
    int ndisks = 0;
    while (bits) {
        bits &= (bits - 1);  // clear the least significant set bit
        ndisks++;
    }
    return ndisks;
}

/*
   EvaluateBoard(b, color):
   A simple evaluation function that returns (# of color disks)
   minus (# of opponent disks).
*/
int EvaluateBoard(Board *b, int color)
{
    int myDisks = CountBitsOnBoard(b, color);
    int oppDisks = CountBitsOnBoard(b, OTHERCOLOR(color));
    return (myDisks - oppDisks);
}

/*
   Check if game is over:
   We say game is over if:
   - Neither player has a legal move.
   - Or the board is full (no empty squares)  -- can also be checked.

   For simplicity, we'll do a quick check: 
   If both players have zero legal moves, we treat it as game over.
*/
int IsGameOver(Board b)
{
    Board legalX, legalO;
    int xMoves = EnumerateLegalMoves(b, X_BLACK, &legalX);
    int oMoves = EnumerateLegalMoves(b, O_WHITE, &legalO);
    if (xMoves == 0 && oMoves == 0) {
        return 1; // no moves left for either side
    }
    return 0;
}

// ---------------------------------------------------------------------
// Negamax with parallel search
// ---------------------------------------------------------------------

// We'll define a small struct to hold a (score, move) pair:
typedef struct {
    int score;
    Move bestMove;
} MoveEval;

// We want to reduce over the best score (maximize).
// If there's a tie in score, we choose deterministically 
// for reproducibility (e.g., pick the smaller row, or column, etc.)
// For now, we'll just pick the first best for tie.
struct BestMoveReducer {
    // The view for each strand:
    MoveEval val;

    // Constructors:
    BestMoveReducer() {
        val.score = INT_MIN;
        val.bestMove.row = -1;
        val.bestMove.col = -1;
    }
    BestMoveReducer(const MoveEval &init) {
        val = init;
    }

    // reduce function merges two partial results:
    void merge(const BestMoveReducer &other) {
        if (other.val.score > val.score) {
            val = other.val;
        } else if (other.val.score == val.score) {
            // tie-break if you want deterministic ordering
            // For example, pick the move with smallest row, etc.
            // Not strictly required if you just want 
            // some deterministic method
            if (other.val.bestMove.row < val.bestMove.row) {
                val = other.val;
            } else if ((other.val.bestMove.row == val.bestMove.row) &&
                       (other.val.bestMove.col < val.bestMove.col)) {
                val = other.val;
            }
        }
    }
};

/*
   The Cilk reducer that wraps our struct.
   We'll use the "com::merge" approach. 
*/
typedef cilk::reducer<BestMoveReducer> best_move_reducer_t;

/*
   NEGAMAX FUNCTION:
   Returns the best score from the perspective of 'color'.
   Also sets *chosenMove to the move that yields that best score
   at the current depth.

   Pseudocode:
     if depth == 0 or game over => return EvaluateBoard
     generate all moves
     for each move => 
        apply the move, recursively call negamax on the resulting board 
        score = -negamax(newBoard, OTHERCOLOR(color), depth-1)
        keep track of max over all moves
     return maxScore
*/
int Negamax(Board b, int color, int depth, Move *chosenMove)
{
    // base case
    if (depth == 0 || IsGameOver(b)) {
        return EvaluateBoard(&b, color);
    }

    // find all legal moves for 'color'
    Board legalMoves;
    int numMoves = EnumerateLegalMoves(b, color, &legalMoves);

    // if no legal moves, we can skip the turn 
    // => either the next player can move or the game ends.
    if (numMoves == 0) {
        // check if next player also has no moves => game ends
        if (IsGameOver(b)) {
            return EvaluateBoard(&b, color);
        }
        // no move for 'color'; skip turn
        // effectively: score = -Negamax(b, OTHERCOLOR(color), depth)
        //   (but do NOT decrement depth, because we haven't used a ply)
        return -Negamax(b, OTHERCOLOR(color), depth, chosenMove);
    }

    // We'll do a parallel loop over each move.
    best_move_reducer_t bestMoveReducer(MoveEval{INT_MIN, {-1,-1}});

    // We'll iterate over all bits in legalMoves.disks[color].
    // For row=1..8, col=1..8, if that bit is set => legal move
    // Because we store bits from row=8..1, we can do a double loop:
    for (int row = 8; row >= 1; row--) {
        ull rowmask = (legalMoves.disks[color] & ROW8);
        for (int col = 8; col >= 1; col--) {
            if (rowmask & COL8) {
                // We found a legal move (row,col).
                // We'll spawn a parallel sub-computation:
                cilk_spawn[&] {
                    Board newBoard = b; // copy board for safe parallel updates
                    Move m = {row, col};

                    // actually do the flips on newBoard
                    FlipDisks(m, &newBoard, color, 0, 1);
                    PlaceOrFlip(m, &newBoard, color);

                    // recursion: negamax from opponent's perspective
                    int subScore = -Negamax(newBoard, OTHERCOLOR(color), depth - 1, NULL);

                    // accumulate in the reducer
                    MoveEval localEval;
                    localEval.score   = subScore;
                    localEval.bestMove = m;
                    bestMoveReducer->merge(BestMoveReducer(localEval));
                };
            }
            rowmask >>= 1;
        }
        legalMoves.disks[color] >>= 8; // move to next row
    }
    cilk_sync; // ensure all spawned tasks complete

    // retrieve final best (score, move)
    MoveEval finalEval = bestMoveReducer->val;
    if (chosenMove) {
        *chosenMove = finalEval.bestMove;
    }
    return finalEval.score;
}

// ---------------------------------------------------------------------
// Turn-taking: Human or Computer
// ---------------------------------------------------------------------

void PrintFlips(Move m, Board *b, int color)
{
    int nflips = FlipDisks(m, b, color, 1, 1);
    PlaceOrFlip(m, b, color);
    printf("You flipped %d disks\n", nflips);
    PrintBoard(*b);
}

// For a human turn, we read from stdin until we get a legal move
int HumanTurn(Board *b, int color)
{
    Board legal_moves;
    int num_moves = EnumerateLegalMoves(*b, color, &legal_moves);
    if (num_moves == 0) {
        return 0; // no moves
    }
    // read a valid move
    for (;;) {
        printf("Enter %c's move as 'row,col': ", diskcolor[color+1]);
        Move m;
        scanf("%d,%d",&m.row,&m.col);

        // check if on board
        if (IS_MOVE_OFF_BOARD(m)) {
            printf("Illegal move: row,col not on board\n");
            PrintBoard(*b);
            continue;
        }
        ull movebit = MOVE_TO_BOARD_BIT(m);
        // check if empty
        if (movebit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
            printf("Illegal move: position occupied\n");
            PrintBoard(*b);
            continue;
        }
        // check if flip is possible
        Board temp = *b; // test
        if (FlipDisks(m, &temp, color, 0, 0) == 0) {
            printf("Illegal move: no disks flipped\n");
            PrintBoard(*b);
            continue;
        }
        // okay, do the move
        PrintFlips(m, b, color);
        break;
    }
    return 1;
}

// For a computer turn, do a parallel search
int ComputerTurn(Board *b, int color, int depth)
{
    Board legal_moves;
    int num_moves = EnumerateLegalMoves(*b, color, &legal_moves);
    if (num_moves == 0) {
        // no legal moves
        return 0;
    }
    // use negamax to pick the best move
    Move bestM;
    int bestScore = Negamax(*b, color, depth, &bestM);
    printf("\nComputer (%c) chooses move: %d,%d with predicted score=%d\n",
           diskcolor[color+1], bestM.row, bestM.col, bestScore);

    // Now apply the move to the real board
    int nflips = FlipDisks(bestM, b, color, 1, 1);
    PlaceOrFlip(bestM, b, color);
    printf("Flipped %d disks\n", nflips);
    PrintBoard(*b);

    return 1;
}

// ---------------------------------------------------------------------
// End-of-game
// ---------------------------------------------------------------------
void EndGame(Board b)
{
    int o_score = CountBitsOnBoard(&b, O_WHITE);
    int x_score = CountBitsOnBoard(&b, X_BLACK);
    printf("Game over.\n");
    if (o_score == x_score) {
        printf("Tie game. Each player has %d disks\n", o_score);
    } else {
        printf("X has %d disks. O has %d disks. %c wins.\n",
               x_score, o_score, (x_score > o_score ? 'X' : 'O'));
    }
}

// ---------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------

int main(int argc, const char *argv[])
{
    Board gameboard = start;
    PrintBoard(gameboard);

    // === NEW: read from user whether X or O is human/computer
    char p1_type, p2_type;
    int p1_depth = 0, p2_depth = 0;

    printf("Is player X (1) human or computer? (h/c): ");
    scanf(" %c", &p1_type);  // e.g. 'h' or 'c'
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

    int color = X_BLACK; // start with X
    int move_possible = 1;

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
            // skip turn if no move
            printf("%c cannot move, skipping turn.\n", diskcolor[color+1]);
        }
        // alternate color
        color = OTHERCOLOR(color);
    }

    EndGame(gameboard);
    return 0;
}
