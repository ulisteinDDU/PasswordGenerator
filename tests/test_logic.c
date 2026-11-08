/*
 * test_logic.c — 生成逻辑单元测试（控制台）
 * 编译：gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt
 */
#include <stdio.h>
#include <string.h>

#define CIPHER_TESTING            /* 排除 password_generator.c 的 UI 区 */
#include "../src/password_generator.c"

static int g_fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, #cond); g_fails++; } \
} while (0)

/* pwd 前 len 个字符中属于 set 的个数 */
static int count_from_set(const char *pwd, int len, const char *set, int setLen)
{
    int cnt = 0;
    for (int i = 0; i < len; i++)
        for (int j = 0; j < setLen; j++)
            if (pwd[i] == set[j]) { cnt++; break; }
    return cnt;
}

static int chr_in(const char *s, char c) { return strchr(s, c) != NULL; }

int main(void)
{
    CharsetInfo ci;
    char pwd[MAX_LEN + 1];

    /* --- build_charset：默认三集合 --- */
    build_charset(TRUE, TRUE, TRUE, FALSE, FALSE, &ci);
    CHECK(ci.setCount == 3);
    CHECK(ci.poolLen == 10 + 26 + 26);
    CHECK(strlen(ci.pool) == (size_t)ci.poolLen);
    CHECK(ci.setLens[0] == 10 && ci.setLens[1] == 26 && ci.setLens[2] == 26);

    /* --- build_charset：全勾选 + 排除易混淆 --- */
    build_charset(TRUE, TRUE, TRUE, TRUE, TRUE, &ci);
    CHECK(ci.setCount == 4);
    CHECK(ci.setLens[0] == 8);              /* 数字剔 0/1 剩 8 */
    CHECK(ci.setLens[1] == 24 && ci.setLens[2] == 24);
    CHECK(!chr_in(ci.pool, '0') && !chr_in(ci.pool, 'O')
          && !chr_in(ci.pool, 'o') && !chr_in(ci.pool, '1')
          && !chr_in(ci.pool, 'l') && !chr_in(ci.pool, 'I'));
    CHECK(ci.poolLen == 8 + 24 + 24 + 26);

    /* --- build_charset：全不勾 --- */
    build_charset(FALSE, FALSE, FALSE, FALSE, FALSE, &ci);
    CHECK(ci.setCount == 0 && ci.poolLen == 0);

    /* --- generate_password：长度、合法性、每类保底（200 次随机抽样） --- */
    build_charset(TRUE, TRUE, TRUE, TRUE, FALSE, &ci);
    for (int trial = 0; trial < 200; trial++) {
        int len = 4 + (trial % 253);        /* 4..256 全范围 */
        CHECK(generate_password(&ci, len, pwd, sizeof pwd));
        CHECK((int)strlen(pwd) == len);
        for (int i = 0; i < len; i++)
            if (!chr_in(ci.pool, pwd[i])) { CHECK(0 && "非法字符"); break; }
        for (int s = 0; s < ci.setCount; s++)
            if (count_from_set(pwd, len, ci.sets[s], ci.setLens[s]) < 1) {
                CHECK(0 && "某勾选集合缺字符"); break;
            }
    }

    /* --- generate_password：长度 < 集合数 → 拒绝 --- */
    build_charset(TRUE, TRUE, TRUE, TRUE, TRUE, &ci);
    CHECK(!generate_password(&ci, 3, pwd, sizeof pwd));

    /* --- generate_password：长度 == 集合数 → 每类恰好 1 个 --- */
    CHECK(generate_password(&ci, 4, pwd, sizeof pwd));
    for (int s = 0; s < 4; s++)
        CHECK(count_from_set(pwd, 4, ci.sets[s], ci.setLens[s]) == 1);

    /* --- generate_password：缓冲区不足 → 拒绝 --- */
    char small[8];
    CHECK(!generate_password(&ci, 10, small, sizeof small));

    /* --- estimate_entropy_bits --- */
    double e = estimate_entropy_bits(10, 8);          /* 8*log2(10)≈26.5754 */
    CHECK(e > 26.57 && e < 26.58);
    CHECK(estimate_entropy_bits(1, 100) == 0.0);
    CHECK(estimate_entropy_bits(0, 8) == 0.0);

    /* --- write_passwords_file：批量生成并写文件 --- */
    {
        const wchar_t *bpath = L"test_batch_output.txt";
        _wremove(bpath);

        build_charset(TRUE, TRUE, TRUE, TRUE, FALSE, &ci);
        CHECK(write_passwords_file(bpath, 25, &ci, 20));

        FILE *f = _wfopen(bpath, L"rb");
        CHECK(f != NULL);
        if (f) {
            static char buf[16384];
            size_t got = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[got] = '\0';

            int lines = 0;                       /* CRLF 行数 = 数量 */
            for (size_t i = 0; i < got; i++)
                if (buf[i] == '\n') lines++;
            CHECK(lines == 25);
            CHECK(got == 25 * (20 + 2));         /* 每行 20 字符 + \r\n */

            char line[512];                      /* 抽验前 3 行 */
            int li = 0, checked = 0;
            for (size_t i = 0; i <= got && checked < 3; i++) {
                if (i == got || buf[i] == '\n') {
                    if (li >= 2) {
                        line[li] = '\0';
                        if (line[li - 1] == '\r') line[li - 1] = '\0';
                        int llen = (int)strlen(line);
                        CHECK(llen == 20);
                        for (int k = 0; k < llen; k++)
                            if (!chr_in(ci.pool, line[k])) { CHECK(0 && "批量行非法字符"); break; }
                        for (int s = 0; s < ci.setCount; s++)
                            if (count_from_set(line, llen, ci.sets[s], ci.setLens[s]) < 1) {
                                CHECK(0 && "批量行缺类"); break;
                            }
                        checked++;
                    }
                    li = 0;
                } else {
                    line[li++] = buf[i];
                }
            }
            CHECK(checked == 3);
        }
        _wremove(bpath);

        /* 参数拒绝路径 */
        CHECK(!write_passwords_file(L"test_batch_bad.txt", 0, &ci, 16));
        CHECK(!write_passwords_file(L"test_batch_bad.txt", 5, NULL, 16));
        CHECK(!write_passwords_file(NULL, 5, &ci, 16));
        CHECK(!write_passwords_file(L"test_batch_bad.txt", 5, &ci, 3));   /* 长度<集合数 */
        CHECK(!write_passwords_file(L"Z:\\no_such_dir_9x\\p.txt", 5, &ci, 16)); /* 无效路径 */
    }

    if (g_fails == 0) printf("ALL TESTS PASSED\n");
    return g_fails ? 1 : 0;
}
