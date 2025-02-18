#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <cilk/reducer_max.h>
#include <limits.h>

#define BIT 0x1
#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1-(c))
#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1LL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT(m.row, m.col)

typedef unsigned long long ull;

typedef struct { ull disks[2]; } Board;
typedef struct { int row; int col; } Move;

Board start = { 
    BOARD_BIT(4,5) | BOARD_BIT(5,4), /* X_BLACK */ 
    BOARD_BIT(4,4) | BOARD_BIT(5,5)  /* O_WHITE */
};

Move offsets[] = { {0,1}, {0,-1}, {-1,0}, {1,0}, {-1,-1}, {-1,1}, {1,1}, {1,-1} };
int noffsets = sizeof(offsets)/sizeof(Move);

void PrintBoard(Board b);
int EnumerateLegalMoves(Board b, int color, Board *legal_moves);
int FlipDisks(Move m, Board *b, int color, int verbose, int domove);
void PlaceOrFlip(Move m, Board *b, int color);
void HumanTurn(Board *b, int color);
void EndGame(Board b);

// Simple board evaluation function
int EvaluateBoard(Board *b, int color) {
    return __builtin_popcountll(b->disks[color]) - __builtin_popcountll(b->disks[OTHERCOLOR(color)]);
}

// Parallel Negamax function with alpha-beta pruning
int Negamax(Board b, int depth, int alpha, int beta, int color) {
    if (depth == 0) return EvaluateBoard(&b, color);
    
    Board legal_moves;
    if (EnumerateLegalMoves(b, color, &legal_moves) == 0) return EvaluateBoard(&b, color);
    
    cilk::reducer_max<int> best_score(INT_MIN);
    
    for (int row = 1; row <= 8; row++) {
        for (int col = 1; col <= 8; col++) {
            if (legal_moves.disks[color] & BOARD_BIT(row, col)) {
                Board new_board = b;
                Move m = {row, col};
                FlipDisks(m, &new_board, color, 0, 1);
                PlaceOrFlip(m, &new_board, color);
                
                int score;
                cilk_spawn score = -Negamax(new_board, depth - 1, -beta, -alpha, OTHERCOLOR(color));
                cilk_sync;
                
                best_score.calc_max(score);
                alpha = std::max(alpha, score);
                if (alpha >= beta) break; // Alpha-beta pruning
            }
        }
    }
    return best_score.get_value();
}

// Function to get best move using parallel search
Move GetBestMove(Board b, int depth, int color) {
    Board legal_moves;
    EnumerateLegalMoves(b, color, &legal_moves);
    
    Move best_move = {-1, -1};
    cilk::reducer_max<int> best_score(INT_MIN);
    
    for (int row = 1; row <= 8; row++) {
        for (int col = 1; col <= 8; col++) {
            if (legal_moves.disks[color] & BOARD_BIT(row, col)) {
                Board new_board = b;
                Move m = {row, col};
                FlipDisks(m, &new_board, color, 0, 1);
                PlaceOrFlip(m, &new_board, color);
                
                int score = -Negamax(new_board, depth - 1, INT_MIN, INT_MAX, OTHERCOLOR(color));
                if (score > best_score.get_value()) {
                    best_score.calc_max(score);
                    best_move = m;
                }
            }
        }
    }
    return best_move;
}

// Modified Game Loop with AI support
void PlayGame(int player1_ai, int depth1, int player2_ai, int depth2) {
    Board gameboard = start;
    int move_possible;
    PrintBoard(gameboard);
    
    do {
        if (player1_ai) {
            Move m = GetBestMove(gameboard, depth1, X_BLACK);
            FlipDisks(m, &gameboard, X_BLACK, 1, 1);
            PlaceOrFlip(m, &gameboard, X_BLACK);
        } else {
            HumanTurn(&gameboard, X_BLACK);
        }
        
        if (player2_ai) {
            Move m = GetBestMove(gameboard, depth2, O_WHITE);
            FlipDisks(m, &gameboard, O_WHITE, 1, 1);
            PlaceOrFlip(m, &gameboard, O_WHITE);
        } else {
            HumanTurn(&gameboard, O_WHITE);
        }
        
        move_possible = EnumerateLegalMoves(gameboard, X_BLACK, NULL) | EnumerateLegalMoves(gameboard, O_WHITE, NULL);
    } while(move_possible);
    
    EndGame(gameboard);
}

int main() {
    int player1_ai, depth1, player2_ai, depth2;
    printf("Enter 'h' or 'c' for Player 1: ");
    char p1; scanf(" %c", &p1);
    player1_ai = (p1 == 'c');
    if (player1_ai) {
        printf("Enter search depth for Player 1: ");
        scanf("%d", &depth1);
    }
    
    printf("Enter 'h' or 'c' for Player 2: ");
    char p2; scanf(" %c", &p2);
    player2_ai = (p2 == 'c');
    if (player2_ai) {
        printf("Enter search depth for Player 2: ");
        scanf("%d", &depth2);
    }
    
    PlayGame(player1_ai, depth1, player2_ai, depth2);
    return 0;
}
