/* ==========================================================================
 *  控制台版俄罗斯方块  ——  C 语言入门实战项目
 *  编译环境：Visual Studio（MSVC）+ Windows
 *
 *  这个项目会用到下面这些 C 语言知识点，建议边读边对照：
 *    · 二维数组      -> 棋盘 board[20][10]
 *    · 结构体        -> 把所有游戏状态打包成一个 Game
 *    · 枚举 / 常量   -> 7 种方块、4 个旋转态、每种方块的颜色
 *    · 函数拆分      -> 移动 / 旋转 / 消行 / 绘制 各自独立成函数
 *    · 随机数        -> srand + rand 决定下一个方块是什么
 *    · 非阻塞键盘输入 -> _kbhit + _getch 读取方向键
 *
 *  运行后：← → 移动，↑ 旋转，↓ 加速下落，空格 直落，P 暂停，R 重开，Esc 退出
 * ========================================================================== */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <conio.h>
#include <windows.h>

/* ============================ 1. 常量定义 ============================ */

#define BOARD_W    10      /* 棋盘宽度（列数） */
#define BOARD_H    20      /* 棋盘高度（行数） */
#define CELL       4       /* 方块矩阵的最大边长（I 型是 4x4） */
#define TYPE_COUNT 7       /* 7 种方块 */
#define ROT_COUNT  4       /* 每种方块 4 个旋转状态 */

/* 默认文字颜色：红+绿+蓝 = 灰白色 */
#define DEF_COLOR (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

/* 7 种方块的名字（枚举其实就是给整数起个好记的名字） */
enum { T_I = 0, T_O, T_T, T_J, T_L, T_S, T_Z };

/* 每种方块在控制台里显示的颜色 */
static const int TYPE_COLOR[TYPE_COUNT] = {
    FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY, /* I  亮青 */
    FOREGROUND_BLUE | FOREGROUND_INTENSITY,                    /* J  亮蓝 */
    FOREGROUND_RED  | FOREGROUND_GREEN,                        /* L  橙色 */
    FOREGROUND_RED  | FOREGROUND_GREEN | FOREGROUND_INTENSITY, /* O  亮黄 */
    FOREGROUND_GREEN | FOREGROUND_INTENSITY,                   /* S  亮绿 */
    FOREGROUND_RED  | FOREGROUND_BLUE | FOREGROUND_INTENSITY,  /* T  亮紫 */
    FOREGROUND_RED  | FOREGROUND_INTENSITY                     /* Z  亮红 */
};


/* ============================ 2. 数据结构 ============================ */

/* 一个方块的“形状”：用一个小方阵描述，1 表示这一格有方块，0 表示没有。
   例如 T 型方块的初始形态是：
       0 1 0
       1 1 1
       0 0 0                                                    */
typedef struct {
    int cells[CELL][CELL];
    int size;              /* 实际使用的边长：I=4，O=2，其余=3 */
} Shape;

/* 全局形状表：7 种方块 × 4 个旋转态，程序启动时一次性算好 */
static Shape g_shapes[TYPE_COUNT][ROT_COUNT];

/* 7 种方块的“初始形态”，另外 3 个旋转态由代码自动旋转生成，不用手写 */
static const int BASE_SHAPE[TYPE_COUNT][CELL][CELL] = {
    { {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },  /* I  长条 */
    { {1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} },  /* O  方块 */
    { {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },  /* T  T型  */
    { {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },  /* J  J型  */
    { {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },  /* L  L型  */
    { {0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} },  /* S  S型  */
    { {1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} }   /* Z  Z型  */
};

static const int SHAPE_SIZE[TYPE_COUNT] = { 4, 2, 3, 3, 3, 3, 3 };

/* 整个游戏的状态，全部装进一个结构体，方便传给各个函数 */
typedef struct {
    int board[BOARD_H][BOARD_W];  /* 棋盘：0=空，1..7 表示已固定的方块种类+1 */
    int type, rot, x, y;          /* 当前正在下落的方块：种类、旋转态、位置 */
    int nextType, nextRot;        /* 下一个方块 */
    int score, lines, level;      /* 分数、消除行数、等级 */
    int over, paused;             /* 是否结束、是否暂停 */
} Game;


/* ======================== 3. 控制台小工具函数 ======================== */

static HANDLE g_hOut = NULL;   /* 控制台输出句柄 */

static void setColor(int attr)
{
    SetConsoleTextAttribute(g_hOut, (WORD)attr);
}

static void gotoxy(int x, int y)
{
    COORD pos;
    pos.X = (SHORT)x;
    pos.Y = (SHORT)y;
    SetConsoleCursorPosition(g_hOut, pos);
}

static void setupConsole(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    CONSOLE_CURSOR_INFO       cursor;
    COORD size;

    SetConsoleOutputCP(CP_UTF8);                       /* 让控制台能正确显示中文 */
    SetConsoleTitleA("俄罗斯方块 - 控制台版");           /* 注意要用带 A 的版本，避免 Unicode 字符集报错 */

    /* 把光标藏起来，画面就不会闪来闪去 */
    cursor.dwSize   = 1;
    cursor.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &cursor);

    /* 保证屏幕够大，免得画到一半被换行挤走 */
    GetConsoleScreenBufferInfo(g_hOut, &info);
    size.X = (SHORT)(info.dwSize.X < 80 ? 80 : info.dwSize.X);
    size.Y = (SHORT)(info.dwSize.Y < 30 ? 30 : info.dwSize.Y);
    SetConsoleScreenBufferSize(g_hOut, size);
}


/* ========================= 4. 方块的旋转 ========================= */

/* 把一个 n×n 方阵顺时针旋转 90°。
   规律：新矩阵 (r,c) 位置的值 = 原矩阵 (n-1-c, r) 位置的值。
   拿一张纸画一画 2×2 的矩阵就能验证。                          */
static void rotateCW(const int src[CELL][CELL], int n, int dst[CELL][CELL])
{
    int r, c;

    for (r = 0; r < CELL; r++)
        for (c = 0; c < CELL; c++)
            dst[r][c] = 0;

    for (r = 0; r < n; r++)
        for (c = 0; c < n; c++)
            dst[r][c] = src[n - 1 - c][r];
}

/* 程序启动时预计算 7×4 = 28 个形状，之后游戏里直接查表 */
static void initShapes(void)
{
    int t, k, r, c;

    for (t = 0; t < TYPE_COUNT; t++) {
        int n = SHAPE_SIZE[t];

        /* 第 0 个旋转态 = 手写的初始形态 */
        for (r = 0; r < CELL; r++)
            for (c = 0; c < CELL; c++)
                g_shapes[t][0].cells[r][c] = (r < n && c < n) ? BASE_SHAPE[t][r][c] : 0;
        g_shapes[t][0].size = n;

        /* 后 3 个旋转态：在上一个的基础上再转 90° */
        for (k = 1; k < ROT_COUNT; k++) {
            rotateCW(g_shapes[t][k - 1].cells, n, g_shapes[t][k].cells);
            g_shapes[t][k].size = n;
        }
    }
}

/* 找出某个形状最上面“有方块”的是第几行，用来让方块贴着顶部出生 */
static int shapeTopRow(int type, int rot)
{
    int r, c;
    for (r = 0; r < g_shapes[type][rot].size; r++)
        for (c = 0; c < g_shapes[type][rot].size; c++)
            if (g_shapes[type][rot].cells[r][c])
                return r;
    return 0;
}


/* ===================== 5. 碰撞检测与方块动作 ===================== */

/* 判断：type 这种方块，以 rot 旋转态放在 (px,py) 位置，会不会撞墙或撞到已有方块？
   这是整个游戏最核心的一个函数，移动、旋转、下落都要先问它。        */
static int canPlace(const Game *g, int type, int rot, int px, int py)
{
    const Shape *s = &g_shapes[type][rot];
    int r, c;

    for (r = 0; r < s->size; r++) {
        for (c = 0; c < s->size; c++) {
            int bx, by;

            if (!s->cells[r][c])       /* 这一格是空的，不用检查 */
                continue;

            bx = px + c;
            by = py + r;

            if (bx < 0 || bx >= BOARD_W)  return 0;   /* 撞左右墙 */
            if (by >= BOARD_H)            return 0;   /* 撞地板   */
            if (by < 0)                   continue;   /* 还在棋盘上方，允许 */
            if (g->board[by][bx] != 0)    return 0;   /* 撞到已有方块 */
        }
    }
    return 1;
}

/* 生成下一个方块：把 next 变成 current，再随机一个新的 next */
static void spawn(Game *g)
{
    g->type = g->nextType;
    g->rot  = g->nextRot;

    g->nextType = rand() % TYPE_COUNT;
    g->nextRot  = rand() % ROT_COUNT;

    g->x = (BOARD_W - g_shapes[g->type][g->rot].size) / 2;   /* 水平居中出生 */
    g->y = -shapeTopRow(g->type, g->rot);                    /* 贴着顶部出生 */

    /* 刚出生就放不下 —— 说明堆到顶了，游戏结束 */
    if (!canPlace(g, g->type, g->rot, g->x, g->y))
        g->over = 1;
}

/* 尝试平移：能走就走，走不动就原地不动 */
static int tryMove(Game *g, int dx, int dy)
{
    if (canPlace(g, g->type, g->rot, g->x + dx, g->y + dy)) {
        g->x += dx;
        g->y += dy;
        return 1;
    }
    return 0;
}

/* 尝试旋转。
   小技巧：贴着墙旋转时，直接转可能会“卡住”，所以依次尝试
   原地 / 左移1格 / 右移1格 / 左移2格 / 右移2格，哪个能放下就用哪个。
   这在俄罗斯方块里叫 Wall Kick（踢墙）。                            */
static int tryRotate(Game *g)
{
    static const int kick[5] = { 0, -1, 1, -2, 2 };
    int nextRot = (g->rot + 1) % ROT_COUNT;
    int i;

    for (i = 0; i < 5; i++) {
        if (canPlace(g, g->type, nextRot, g->x + kick[i], g->y)) {
            g->rot = nextRot;
            g->x  += kick[i];
            return 1;
        }
    }
    return 0;
}

/* 把当前方块“焊死”在棋盘上（写进 board 数组） */
static void lockPiece(Game *g)
{
    const Shape *s = &g_shapes[g->type][g->rot];
    int r, c;

    for (r = 0; r < s->size; r++) {
        for (c = 0; c < s->size; c++) {
            int bx, by;

            if (!s->cells[r][c])
                continue;

            bx = g->x + c;
            by = g->y + r;

            if (by < 0) {          /* 固定时还有方块露在棋盘外面 —— 顶到了 */
                g->over = 1;
                continue;
            }
            g->board[by][bx] = g->type + 1;   /* 存 1..7，0 留给空格 */
        }
    }
}

/* 扫描并消除所有填满的行，返回消除了几行 */
static int clearLines(Game *g)
{
    int r, c, rr;
    int cleared = 0;

    /* 从最底下往上扫。注意：消掉一行后，当前行会被上面的内容填进来，
       所以行号 r 不能自减，要再检查一遍同一行。                      */
    for (r = BOARD_H - 1; r >= 0; ) {
        int full = 1;

        for (c = 0; c < BOARD_W; c++) {
            if (g->board[r][c] == 0) {
                full = 0;
                break;
            }
        }

        if (!full) {
            r--;
            continue;
        }

        /* 把 r 行以上的所有行整体下移一格 */
        for (rr = r; rr > 0; rr--)
            for (c = 0; c < BOARD_W; c++)
                g->board[rr][c] = g->board[rr - 1][c];

        for (c = 0; c < BOARD_W; c++)
            g->board[0][c] = 0;

        cleared++;
    }
    return cleared;
}

/* 落定 + 消行 + 计分 + 出下一个方块，一条龙 */
static void lockAndNext(Game *g)
{
    /* 一次消得越多，得分越高：1行100 / 2行300 / 3行500 / 4行800 */
    static const int SCORE_TABLE[5] = { 0, 100, 300, 500, 800 };
    int cleared;

    lockPiece(g);

    cleared = clearLines(g);
    if (cleared > 0) {
        g->lines += cleared;
        g->score += SCORE_TABLE[cleared] * g->level;
        g->level  = g->lines / 10 + 1;      /* 每消 10 行升 1 级 */
    }

    spawn(g);
}

/* 空格键：一路到底。落得越快，额外加分越多（每格 2 分） */
static void hardDrop(Game *g)
{
    int dist = 0;

    while (canPlace(g, g->type, g->rot, g->x, g->y + 1)) {
        g->y++;
        dist++;
    }

    g->score += dist * 2;
    lockAndNext(g);
}

/* 自然下落一格；下不动了就落定 */
static void step(Game *g)
{
    if (!tryMove(g, 0, 1))
        lockAndNext(g);
}

/* 等级越高，下落越快（单位：毫秒） */
static int dropInterval(const Game *g)
{
    int t = 500 - (g->level - 1) * 45;
    return (t < 60) ? 60 : t;
}

/* 重开一局 */
static void resetGame(Game *g)
{
    int r, c;

    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++)
            g->board[r][c] = 0;

    g->score = 0;
    g->lines = 0;
    g->level = 1;
    g->over  = 0;
    g->paused = 0;

    g->nextType = rand() % TYPE_COUNT;
    g->nextRot  = rand() % ROT_COUNT;

    spawn(g);
}


/* ============================ 6. 画面绘制 ============================ */

/* 画一个格子：空格画两个空格，方块画一对彩色中括号 */
static void drawCell(int v)
{
    if (v == 0) {
        printf("  ");
    } else {
        setColor(TYPE_COLOR[v - 1]);
        printf("[]");
        setColor(DEF_COLOR);
    }
}

static void draw(const Game *g)
{
    int show[BOARD_H][BOARD_W];   /* 显示层：棋盘 + 正在下落的方块 */
    int r, c;
    const Shape *ns;
    int sx = BOARD_W * 2 + 4;     /* 侧边栏起始列，紧跟在棋盘右边 */

    /* 1) 先把棋盘原样拷到显示层 */
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++)
            show[r][c] = g->board[r][c];

    /* 2) 再把正在下落的方块叠加到显示层（这样不会弄脏真实棋盘数据） */
    if (!g->over) {
        const Shape *s = &g_shapes[g->type][g->rot];
        for (r = 0; r < s->size; r++) {
            for (c = 0; c < s->size; c++) {
                int bx, by;
                if (!s->cells[r][c])
                    continue;
                bx = g->x + c;
                by = g->y + r;
                if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W)
                    show[by][bx] = g->type + 1;
            }
        }
    }

    /* 3) 每次都把光标移回左上角重画，这样不会像 cls 那样闪屏 */
    gotoxy(0, 0);
    setColor(DEF_COLOR);
    printf("+--------------------+\n");
    for (r = 0; r < BOARD_H; r++) {
        printf("|");
        for (c = 0; c < BOARD_W; c++)
            drawCell(show[r][c]);
        printf("|\n");
    }
    printf("+--------------------+\n");

    /* 4) 右侧信息栏 */
    gotoxy(sx, 0);
    printf("俄罗斯方块");

    gotoxy(sx, 2);
    printf("下一个：");

    ns = &g_shapes[g->nextType][g->nextRot];
    gotoxy(sx, 3);
    printf("+--------+");
    for (r = 0; r < 4; r++) {
        gotoxy(sx, 4 + r);
        printf("|");
        for (c = 0; c < 4; c++) {
            if (r < ns->size && c < ns->size && ns->cells[r][c])
                drawCell(g->nextType + 1);
            else
                printf("  ");
        }
        printf("|");
    }
    gotoxy(sx, 8);
    printf("+--------+");

    gotoxy(sx, 10);
    printf("分数：%d", g->score);
    gotoxy(sx, 11);
    printf("消行：%d", g->lines);
    gotoxy(sx, 12);
    printf("等级：%d", g->level);

    gotoxy(sx, 14);
    printf("操作说明");
    gotoxy(sx, 15);
    printf("← →   左右移动");
    gotoxy(sx, 16);
    printf("↑     旋转");
    gotoxy(sx, 17);
    printf("↓     加速下落");
    gotoxy(sx, 18);
    printf("空格   直接落地");
    gotoxy(sx, 19);
    printf("P      暂停");
    gotoxy(sx, 20);
    printf("R      重新开始");
    gotoxy(sx, 21);
    printf("Esc    退出");

    /* 5) 暂停 / 结束提示 */
    if (g->paused) {
        setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        gotoxy(6, BOARD_H / 2);
        printf("   游戏已暂停   ");
        gotoxy(6, BOARD_H / 2 + 1);
        printf("  按 P 继续游戏  ");
        setColor(DEF_COLOR);
    }
}


/* ============================ 7. 键盘输入 ============================ */

static void handleInput(Game *g)
{
    int ch = _getch();

    /* 方向键比较特殊：会先发一个前缀码 0 或 224，再发真正的键码 */
    if (ch == 0 || ch == 224) {
        int ext = _getch();
        switch (ext) {
        case 75: tryMove(g, -1, 0); break;                    /* ← */
        case 77: tryMove(g,  1, 0); break;                    /* → */
        case 72: tryRotate(g);      break;                    /* ↑ */
        case 80:                                              /* ↓ */
            if (tryMove(g, 0, 1))
                g->score += 1;   /* 手动下移给 1 分奖励 */
            break;
        }
        return;
    }

    switch (ch) {
    case ' ':
        hardDrop(g);
        break;
    case 'p': case 'P':
        g->paused = !g->paused;
        break;
    case 'r': case 'R':
        resetGame(g);
        break;
    case 27:                     /* Esc */
        g->over = 1;
        break;
    }
}


/* ============================== 8. 主函数 ============================== */

int main(void)
{
    Game   g;
    clock_t lastTick;

    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    setupConsole();

    /* 随机数种子。只播一次种，之后 rand() 才会给出不同的序列 */
    srand((unsigned int)time(NULL));

    initShapes();
    resetGame(&g);

    system("cls");

    lastTick = clock();
    while (!g.over) {
        /* 有按键就立刻响应（_kbhit 不会卡住程序） */
        if (_kbhit())
            handleInput(&g);

        /* 到点了就自动下落一格 */
        if (!g.paused) {
            if (clock() - lastTick >= dropInterval(&g) * CLOCKS_PER_SEC / 1000) {
                lastTick = clock();
                step(&g);
            }
        } else {
            lastTick = clock();   /* 暂停时也要刷新计时，否则一恢复就狂掉 */
        }

        draw(&g);
        Sleep(16);                /* 大约 60 FPS，别把 CPU 跑满 */
    }

    /* 结束画面 */
    draw(&g);
    gotoxy(4, BOARD_H / 2);
    setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
    printf("  游戏结束！得分 %d  ", g.score);
    gotoxy(4, BOARD_H / 2 + 2);
    setColor(DEF_COLOR);
    printf("按任意键退出...");
    _getch();

    return 0;
}
