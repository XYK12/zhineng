/* ==========================================================================
 *  2048 —— C 语言初学者版（真彩色界面）
 *
 *  运行方式（Windows）：
 *      Visual Studio 里直接按 F5（工程文件 2048.sln 已经配好）
 *      或命令行：gcc 2048.c -o 2048.exe -Wall
 *
 *  操作：w/a/s/d 或方向键移动，q 退出，r 重开
 *
 *  代码结构（建议按这个顺序读）：
 *      1. 颜色工具      第 1 节  —— struct Rgb / 混色 / 输出 ANSI 转义序列
 *      2. 终端初始化    第 2 节  —— 打开"虚拟终端"开关，否则转义序列会被当普通字符打印
 *      3. 棋盘与游戏逻辑 第 4~6 节 —— 和界面完全无关，可以单独看
 *      4. 界面绘制      第 7 节  —— 每个格子画成"高光 / 底色+数字 / 阴影"三条横带
 *      5. 主循环        第 8 节  —— 画一帧 -> 读键 -> 移动 -> 补数字
 * ========================================================================== */

#if defined(_MSC_VER)
#  define _CRT_SECURE_NO_WARNINGS   /* 让 MSVC 不要对 sprintf/strcpy 报"不安全"警告 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#  include <conio.h>      /* _getch()：读一个键，不回显、不用回车 */
#  include <windows.h>    /* 控制台代码页 / 虚拟终端开关 */
#else
#  include <termios.h>
#  include <unistd.h>
#endif

#define N          4        /* 棋盘边长。想玩 3x3 或 5x5，改这一个数字即可 */
#define WIN_VALUE  2048     /* 拼出这个数就算赢 */
#define CELL_W     6        /* 每个格子占几个字符宽 */

#define ST_PLAY    0        /* 三种状态，交给绘制函数决定底部提示 */
#define ST_WIN     1
#define ST_OVER    2

/* ==========================================================================
 *  一、颜色
 *
 *  终端里的颜色靠"ANSI 转义序列"实现：一段以 ESC（\x1b）开头的字符，
 *  终端收到后不会显示它，而是当成指令执行。格式：
 *
 *      设置前景色(文字)：  \x1b[38;2;R;G;Bm
 *      设置背景色(底色)：  \x1b[48;2;R;G;Bm
 *      恢复默认：          \x1b[0m
 *      加粗 / 取消加粗：   \x1b[1m  /  \x1b[22m
 *
 *  其中的 2 表示"后面跟三个 0~255 的分量"，也就是真彩色（24 位色）。
 * ========================================================================== */

struct Rgb { int r, g, b; };

/* 造一个颜色。C 不支持直接用花括号赋值给已声明的结构体变量，
   所以写个小函数代劳 */
static struct Rgb mk(int r, int g, int b)
{
    struct Rgb c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}

/* 把 a 和 b 按比例混合：pct = 0 得到 a，pct = 100 得到 b。
   用来从"底色"派生出"高光"和"阴影"，这样每种数字只需要写一个基础色 */
static struct Rgb mix(struct Rgb a, struct Rgb b, int pct)
{
    struct Rgb c;
    c.r = a.r + (b.r - a.r) * pct / 100;
    c.g = a.g + (b.g - a.g) * pct / 100;
    c.b = a.b + (b.b - a.b) * pct / 100;
    return c;
}

static int g_color = 1;     /* 1 = 终端支持真彩色；0 = 退化成纯文本界面 */

static void set_bg(struct Rgb c) { printf("\x1b[48;2;%d;%d;%dm", c.r, c.g, c.b); }
static void set_fg(struct Rgb c) { printf("\x1b[38;2;%d;%d;%dm", c.r, c.g, c.b); }
static void attr_reset(void)     { printf("\x1b[0m"); }

/* ---------- 游戏配色：取自原版 2048 的那套暖色 ---------- */

static struct Rgb c_board(void) { return mk(0xBB, 0xAD, 0xA0); }  /* 棋盘底板 */
static struct Rgb c_empty(void) { return mk(0xCD, 0xC1, 0xB4); }  /* 空格子   */
static struct Rgb c_dark (void) { return mk(0x77, 0x6E, 0x65); }  /* 深棕字   */
static struct Rgb c_light(void) { return mk(0xF9, 0xF6, 0xF2); }  /* 米白字   */

/* 数字 -> 格子底色 */
static struct Rgb tile_bg(int v)
{
    switch (v) {
    case 2:    return mk(0xEE, 0xE4, 0xDA);
    case 4:    return mk(0xED, 0xE0, 0xC8);
    case 8:    return mk(0xF2, 0xB1, 0x79);
    case 16:   return mk(0xF5, 0x95, 0x63);
    case 32:   return mk(0xF6, 0x7C, 0x5F);
    case 64:   return mk(0xF6, 0x5E, 0x3B);
    case 128:  return mk(0xED, 0xCF, 0x72);
    case 256:  return mk(0xED, 0xCC, 0x61);
    case 512:  return mk(0xED, 0xC8, 0x50);
    case 1024: return mk(0xED, 0xC5, 0x3F);
    case 2048: return mk(0xED, 0xC2, 0x2E);
    case 4096: return mk(0x3C, 0x3A, 0x32);
    default:   return mk(0x2E, 0x2C, 0x26);   /* 8192 以上 */
    }
}

/* 数字 -> 文字颜色。浅色格子用深棕字，深色格子用米白字，否则看不清 */
static struct Rgb tile_fg(int v)
{
    return (v <= 4) ? c_dark() : c_light();
}

/* ==========================================================================
 *  二、终端初始化
 * ========================================================================== */

/* Windows 控制台默认把转义序列当普通字符打印，必须先打开这个开关 */
static void init_console(void)
{
#ifdef _WIN32
    HANDLE h;
    DWORD  mode;

    SetConsoleOutputCP(65001);          /* 65001 = UTF-8，中文才不会乱码 */

    h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(h, &mode) ||
        !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        g_color = 0;                    /* 老系统 / 输出被重定向：降级成纯文本 */
    }
#endif
}

/* 光标回到左上角。用它代替逐帧清屏，画面才不会闪 */
static void screen_home(void)
{
    printf("\x1b[H");
}

/* ==========================================================================
 *  三、读键盘
 * ========================================================================== */

static int read_key(void)
{
#ifdef _WIN32
    int c = _getch();
    if (c == 0 || c == 224) {           /* 方向键是两次 _getch：先 0/224，再方向码 */
        switch (_getch()) {
        case 72: return 'w';            /* 上 */
        case 80: return 's';            /* 下 */
        case 75: return 'a';            /* 左 */
        case 77: return 'd';            /* 右 */
        }
        return 0;
    }
    return tolower(c);
#else
    struct termios old_t, new_t;
    int c;

    tcgetattr(STDIN_FILENO, &old_t);
    new_t = old_t;
    new_t.c_lflag &= ~(ICANON | ECHO);  /* 关掉行缓冲和回显，实现"按一下就生效" */
    tcsetattr(STDIN_FILENO, TCSANOW, &new_t);

    c = getchar();
    if (c == 27) {                      /* 方向键是 ESC [ A/B/C/D 三个字节 */
        getchar();                      /* 吃掉 '[' */
        c = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
        switch (c) {
        case 'A': return 'w';
        case 'B': return 's';
        case 'C': return 'd';
        case 'D': return 'a';
        }
        return 0;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    return tolower(c);
#endif
}

/* ==========================================================================
 *  四、棋盘状态
 * ========================================================================== */

static void board_clear(int b[N][N])
{
    int r, c;
    for (r = 0; r < N; r++)
        for (c = 0; c < N; c++)
            b[r][c] = 0;
}

/* 在随机一个空格子里放一个 2（90%）或 4（10%） */
static void spawn_tile(int b[N][N])
{
    int rows[N * N], cols[N * N];
    int count = 0, r, c, k;

    /* 先把所有空位收集起来，这样每个空位被选中的概率才相等 */
    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            if (b[r][c] == 0) {
                rows[count] = r;
                cols[count] = c;
                count++;
            }
        }
    }
    if (count == 0) return;

    k = rand() % count;
    b[rows[k]][cols[k]] = (rand() % 10 == 0) ? 4 : 2;
}

/* ==========================================================================
 *  五、核心算法：把一条线（长度 N）向"左端"滑动并合并
 *
 *  输入形如：[2, 0, 2, 4]      输出：[4, 4, 0, 0]，得分 +4
 *  输入形如：[2, 2, 2, 2]      输出：[4, 4, 0, 0]，得分 +8
 *
 *  关键规则：每个数字在一次移动中只能参与一次合并
 *  （所以 [2,2,2,2] 不会变成 [8]，而是 [4,4]）
 *
 *  实现分三步：① 压缩（非零往前挤） ② 合并（相邻相等就翻倍）
 *              ③ 补齐 0 并写回，同时告诉调用者"到底有没有变化"
 * ========================================================================== */
static int slide_line(int line[N], int *gain)
{
    int tmp[N], n = 0;
    int merged[N], m = 0;
    int i, changed = 0;

    /* 第 1 步：压缩。把 0 全挤掉，非零数字依次前移 */
    for (i = 0; i < N; i++)
        if (line[i] != 0)
            tmp[n++] = line[i];

    /* 第 2 步：合并。从左往右扫，相邻两个相等就合成一个双倍数 */
    for (i = 0; i < n; i++) {
        if (i + 1 < n && tmp[i] == tmp[i + 1]) {
            merged[m++] = tmp[i] * 2;
            *gain += tmp[i] * 2;            /* 累加分数 */
            i++;                            /* 跳过被吃掉的那一个 */
        } else {
            merged[m++] = tmp[i];
        }
    }

    /* 第 3 步：不足 N 个的补 0，写回原数组，并判断是否发生变化 */
    for (i = 0; i < N; i++) {
        int v = (i < m) ? merged[i] : 0;
        if (v != line[i]) changed = 1;
        line[i] = v;
    }
    return changed;                         /* 1 = 这条线动了；0 = 没动 */
}

/* ==========================================================================
 *  六、四方向移动与局面判断
 *
 *  dir: 0=上  1=右  2=下  3=左
 *
 *  诀窍：不管往哪边推，都先用 line_pos() 把对应格子按"从移动方向起点开始"
 *        的顺序读进 line[]，交给 slide_line() 处理，再按同样顺序写回。
 *        这样只需要写一遍合并逻辑。
 * ========================================================================== */
static void line_pos(int dir, int i, int j, int *r, int *c)
{
    switch (dir) {
    case 0:  *r = j;             *c = i;             break;  /* 上：列 i，自上而下 */
    case 1:  *r = i;             *c = N - 1 - j;     break;  /* 右：行 i，自右而左 */
    case 2:  *r = N - 1 - j;     *c = i;             break;  /* 下：列 i，自下而上 */
    default: *r = i;             *c = j;             break;  /* 左：行 i，自左而右 */
    }
}

static int move_board(int b[N][N], int dir, int *gain)
{
    int i, j, r, c, moved = 0;
    int line[N];

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {       /* 取出这一条线 */
            line_pos(dir, i, j, &r, &c);
            line[j] = b[r][c];
        }

        if (slide_line(line, gain))
            moved = 1;

        for (j = 0; j < N; j++) {       /* 写回棋盘 */
            line_pos(dir, i, j, &r, &c);
            b[r][c] = line[j];
        }
    }
    return moved;
}

/* 还能不能走？还有空格，或者存在相邻相同的数字，就还能走 */
static int can_move(int b[N][N])
{
    int r, c;
    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            if (b[r][c] == 0) return 1;
            if (c + 1 < N && b[r][c] == b[r][c + 1]) return 1;
            if (r + 1 < N && b[r][c] == b[r + 1][c]) return 1;
        }
    }
    return 0;
}

static int has_won(int b[N][N])
{
    int r, c;
    for (r = 0; r < N; r++)
        for (c = 0; c < N; c++)
            if (b[r][c] >= WIN_VALUE) return 1;
    return 0;
}

/* ==========================================================================
 *  七、绘制
 *
 *  一个格子画成 3 行（代码里叫 band）：
 *
 *      band 0   顶部高光（底色混一点白）
 *      band 1   底色 + 居中的数字
 *      band 2   底部阴影（底色混一点黑）
 *
 *  上下两条横带只改颜色、不改宽度，整块格子就成了一块"上亮下暗"的立体卡片，
 *  投影方向统一朝下，看上去像被顶光打亮的实体方块。
 *
 *  （试过在上下两行左右各缩进一格来做圆角，但在 1 个字符的分辨率下，
 *    三行里有窄有宽，拼出来是"十字形"，反而更难看，所以放弃了。）
 * ========================================================================== */

static void put_spaces(int n)
{
    while (n-- > 0) putchar(' ');
}

/* 画一个格子的某一条横带 */
static void draw_cell(int v, int band)
{
    struct Rgb base, shaded;
    char num[16];
    int len, left, right;

    base = (v == 0) ? c_empty() : tile_bg(v);

    /* ---- 中间那条：写数字 ---- */
    if (band == 1) {
        set_bg(base);
        if (v == 0) {                   /* 空格子：什么都不写 */
            put_spaces(CELL_W);
            return;
        }
        sprintf(num, "%d", v);
        len   = (int)strlen(num);
        left  = (CELL_W - len) / 2;     /* 居中对齐 */
        right = CELL_W - len - left;

        set_fg(tile_fg(v));
        printf("\x1b[1m");              /* 加粗，数字更饱满 */
        put_spaces(left);
        fputs(num, stdout);
        put_spaces(right);
        printf("\x1b[22m");             /* 取消加粗 */
        return;
    }

    /* ---- 上/下两条：只铺颜色 ---- */
    shaded = (band == 0) ? mix(base, mk(255, 255, 255), 18)   /* 往白里提亮 */
                         : mix(base, mk(0, 0, 0), 16);        /* 往黑里压暗 */

    set_bg(shaded);
    put_spaces(CELL_W);
}

/* 画整块棋盘：四周和格子之间都留一格棋盘底色，格子就"浮"起来了 */
static void draw_board(int b[N][N])
{
    int r, c, band;
    int width = 1 + N * CELL_W + (N - 1) + 1;   /* 左留白 + 格子 + 间隙 + 右留白 */

    set_bg(c_board()); put_spaces(width); attr_reset(); putchar('\n');

    for (r = 0; r < N; r++) {
        for (band = 0; band < 3; band++) {
            set_bg(c_board()); putchar(' ');
            for (c = 0; c < N; c++) {
                draw_cell(b[r][c], band);
                if (c < N - 1) {                /* 格子之间的竖缝 */
                    set_bg(c_board());
                    putchar(' ');
                }
            }
            set_bg(c_board()); putchar(' ');
            attr_reset(); putchar('\n');
        }
        if (r < N - 1) {                        /* 格子行之间的横缝 */
            set_bg(c_board()); put_spaces(width); attr_reset(); putchar('\n');
        }
    }

    set_bg(c_board()); put_spaces(width); attr_reset(); putchar('\n');
}

/* 顶部：标题 + 分数面板 */
static void draw_header(int score, int best)
{
    set_fg(mk(0xE1, 0x8E, 0x2E));
    printf("\x1b[1m  2 0 4 8\x1b[22m");

    set_bg(c_board()); set_fg(c_dark());
    printf("    分数 %-6d", score);

    attr_reset();
    printf("   ");

    set_bg(c_board()); set_fg(c_dark());
    printf("  最高 %-6d", best);

    attr_reset(); putchar('\n'); putchar('\n');
}

/* 底部：操作提示 + 状态行（状态行永远打印整行，才能覆盖掉上一帧的残留） */
static void draw_footer(int state, int score)
{
    set_fg(mk(0x9A, 0x92, 0x88));
    printf("  w/a/s/d 或方向键 移动    r 重开    q 退出");
    attr_reset(); putchar('\n');

    if (state == ST_OVER) {
        set_fg(mk(0xD8, 0x5A, 0x30));
        printf("\x1b[1m  游戏结束，最终分数 %d —— 按任意键退出\x1b[22m", score);
    } else if (state == ST_WIN) {
        set_fg(mk(0x1D, 0x9E, 0x75));
        printf("  ★ 已达 2048，继续冲更高分");
    } else {
        putchar(' ');
    }
    attr_reset(); putchar('\n');
}

/* ---------- 降级界面：终端不支持真彩色时使用 ---------- */

static void cell_text(int value, char out[CELL_W + 1])
{
    char num[16];
    int  len, left, right, i, p = 0;

    if (value == 0) {
        for (i = 0; i < CELL_W; i++) out[i] = ' ';
        out[CELL_W] = '\0';
        return;
    }
    sprintf(num, "%d", value);
    len   = (int)strlen(num);
    left  = (CELL_W - len) / 2;
    right = CELL_W - len - left;

    for (i = 0; i < left;  i++) out[p++] = ' ';
    for (i = 0; i < len;   i++) out[p++] = num[i];
    for (i = 0; i < right; i++) out[p++] = ' ';
    out[p] = '\0';
}

static void draw_plain(int b[N][N], int score, int best, int state)
{
    int r, c;
    char cell[CELL_W + 1];

#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
    printf("\n  2048          分数: %d    最高: %d\n\n", score, best);

    for (r = 0; r < N; r++) {
        printf("  +");
        for (c = 0; c < N; c++) { put_spaces(CELL_W); putchar('+'); }
        putchar('\n');
        printf("  |");
        for (c = 0; c < N; c++) { cell_text(b[r][c], cell); printf("%s|", cell); }
        putchar('\n');
    }
    printf("  +");
    for (c = 0; c < N; c++) { put_spaces(CELL_W); putchar('+'); }
    printf("\n\n  w/a/s/d 移动    r 重开    q 退出\n");

    if (state == ST_OVER)
        printf("\n  游戏结束，最终分数 %d\n", score);
    else if (state == ST_WIN)
        printf("\n  ★ 已达 2048\n");
}

/* 画一帧。整个界面行数固定，所以每帧只需把光标移到左上角重画 */
static void draw(int b[N][N], int score, int best, int state)
{
    static int first_frame = 1;

    if (!g_color) {
        draw_plain(b, score, best, state);
        return;
    }

    if (first_frame) {
        printf("\x1b[2J");      /* 第一帧清一次屏 */
        first_frame = 0;
    }
    screen_home();

    draw_header(score, best);
    draw_board(b);
    draw_footer(state, score);
}

/* ==========================================================================
 *  八、一局游戏
 * ========================================================================== */

static void new_game(int b[N][N], int *score, int *won)
{
    board_clear(b);
    spawn_tile(b);
    spawn_tile(b);
    *score = 0;
    *won   = 0;
}

int main(void)
{
    int board[N][N];
    int score, best = 0, won, gain, dir, key, state;

    init_console();
    srand((unsigned)time(NULL));

    if (g_color)
        printf("\x1b[?25l");    /* 隐藏光标，重画时不闪 */

    new_game(board, &score, &won);

    for (;;) {
        /* ---- 判定状态 ---- */
        if (has_won(board)) won = 1;
        if (!can_move(board))      state = ST_OVER;
        else if (won)              state = ST_WIN;
        else                       state = ST_PLAY;

        draw(board, score, best, state);

        if (state == ST_OVER) {    /* 死局：等一个键再退出 */
            read_key();
            break;
        }

        /* ---- 读键 ---- */
        key = read_key();

        if (key == 'q') break;
        if (key == 'r') { new_game(board, &score, &won); continue; }
        switch (key) {
        case 'w': dir = 0; break;
        case 'd': dir = 1; break;
        case 's': dir = 2; break;
        case 'a': dir = 3; break;
        default:  continue;        /* 其它按键直接忽略 */
        }

        /* ---- 移动，只有真的动了才生成新数字 ---- */
        gain = 0;
        if (move_board(board, dir, &gain)) {
            score += gain;
            if (score > best) best = score;
            spawn_tile(board);
        }
    }

    attr_reset();
    if (g_color) printf("\x1b[?25h");   /* 恢复光标 */
    return 0;
}
