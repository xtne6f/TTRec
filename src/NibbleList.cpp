#include <Windows.h>

// EpgTimerのソースを参考に作成

#if 1 // static
static LPCTSTR g_nibble2_FF[] = {
    TEXT("")
};

static LPCTSTR g_nibble2_00[] = {
    TEXT("すべて"),                 // 0x00FF
    TEXT("定時・総合"),             // 0x0000
    TEXT("天気"),                   // 0x0001
    TEXT("特集・ドキュメント"),     // 0x0002
    TEXT("政治・国会"),
    TEXT("経済・市況"),
    TEXT("海外・国際"),
    TEXT("解説"),
    TEXT("討論・会談"),
    TEXT("報道特番"),
    TEXT("ローカル・地域"),
    TEXT("交通"),
    TEXT("その他")                  // 0x000F
};

static LPCTSTR g_nibble2_01[] = {
    TEXT("すべて"),
    TEXT("スポーツニュース"),
    TEXT("野球"),
    TEXT("サッカー"),
    TEXT("ゴルフ"),
    TEXT("その他の球技"),
    TEXT("相撲・格闘技"),
    TEXT("オリンピック・国際大会"),
    TEXT("マラソン・陸上・水泳"),
    TEXT("マリン・ウィンタースポーツ"),
    TEXT("競馬・公営競技"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_02[] = {
    TEXT("すべて"),
    TEXT("芸能・ワイドショー"),
    TEXT("ファッション"),
    TEXT("暮らし・住まい"),
    TEXT("健康・医療"),
    TEXT("ショッピング・通販"),
    TEXT("グルメ・料理"),
    TEXT("イベント"),
    TEXT("番組紹介・お知らせ"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_03[] = {
    TEXT("すべて"),
    TEXT("国内ドラマ"),
    TEXT("海外ドラマ"),
    TEXT("時代劇"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_04[] = {
    TEXT("すべて"),
    TEXT("国内ロック・ポップス"),
    TEXT("海外ロック・ポップス"),
    TEXT("クラシック・オペラ"),
    TEXT("ジャズ・フュージョン"),
    TEXT("歌謡曲・演歌"),
    TEXT("ライブ・コンサート"),
    TEXT("ランキング・リクエスト"),
    TEXT("カラオケ・のど自慢"),
    TEXT("民謡・邦楽"),
    TEXT("童謡・キッズ"),
    TEXT("民族音楽・ワールドミュージック"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_05[] = {
    TEXT("すべて"),
    TEXT("クイズ"),
    TEXT("ゲーム"),
    TEXT("トークバラエティ"),
    TEXT("お笑い・コメディ"),
    TEXT("音楽バラエティ"),
    TEXT("旅バラエティ"),
    TEXT("料理バラエティ"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_06[] = {
    TEXT("すべて"),
    TEXT("洋画"),
    TEXT("邦画"),
    TEXT("アニメ"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_07[] = {
    TEXT("すべて"),
    TEXT("国内アニメ"),
    TEXT("海外アニメ"),
    TEXT("特撮"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_08[] = {
    TEXT("すべて"),
    TEXT("社会・時事"),
    TEXT("歴史・紀行"),
    TEXT("自然・動物・環境"),
    TEXT("宇宙・科学・医学"),
    TEXT("カルチャー・伝統文化"),
    TEXT("文学・文芸"),
    TEXT("スポーツ"),
    TEXT("ドキュメンタリー全般"),
    TEXT("インタビュー・討論"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_09[] = {
    TEXT("すべて"),
    TEXT("現代劇・新劇"),
    TEXT("ミュージカル"),
    TEXT("ダンス・バレエ"),
    TEXT("落語・演芸"),
    TEXT("歌舞伎・古典"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_0A[] = {
    TEXT("すべて"),
    TEXT("旅・釣り・アウトドア"),
    TEXT("園芸・ペット・手芸"),
    TEXT("音楽・美術・工芸"),
    TEXT("囲碁・将棋"),
    TEXT("麻雀・パチンコ"),
    TEXT("車・オートバイ"),
    TEXT("コンピュータ・ＴＶゲーム"),
    TEXT("会話・語学"),
    TEXT("幼児・小学生"),
    TEXT("中学生・高校生"),
    TEXT("大学生・受験"),
    TEXT("生涯教育・資格"),
    TEXT("教育問題"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_0B[] = {
    TEXT("すべて"),
    TEXT("高齢者"),
    TEXT("障害者"),
    TEXT("社会福祉"),
    TEXT("ボランティア"),
    TEXT("手話"),
    TEXT("文字（字幕）"),
    TEXT("音声解説"),
    TEXT("その他")
};

static LPCTSTR g_nibble2_0F[] = {
    TEXT("")
};
#endif

LPCTSTR g_nibble1List[14] = {
    TEXT("すべて"),                 // 0xFFFF
    TEXT("ニュース／報道"),         // 0x00XX
    TEXT("スポーツ"),               // 0x01XX
    TEXT("情報／ワイドショー"),     // 0x02XX
    TEXT("ドラマ"),                 // 0x03XX
    TEXT("音楽"),                   // 0x04XX
    TEXT("バラエティ"),             // 0x05XX
    TEXT("映画"),                   // 0x06XX
    TEXT("アニメ／特撮"),           // 0x07XX
    TEXT("ドキュメンタリー／教養"), // 0x08XX
    TEXT("劇場／公演"),             // 0x09XX
    TEXT("趣味／教育"),             // 0x0AXX
    TEXT("福祉"),                   // 0x0BXX
    TEXT("その他")                  // 0x0FFF
};

LPCTSTR *g_nibble2List[14] = {
    g_nibble2_FF,
    g_nibble2_00,
    g_nibble2_01,
    g_nibble2_02,
    g_nibble2_03,
    g_nibble2_04,
    g_nibble2_05,
    g_nibble2_06,
    g_nibble2_07,
    g_nibble2_08,
    g_nibble2_09,
    g_nibble2_0A,
    g_nibble2_0B,
    g_nibble2_0F
};

int g_nibble2ListSize[14] = {
    sizeof(g_nibble2_FF) / sizeof(g_nibble2_FF[0]),
    sizeof(g_nibble2_00) / sizeof(g_nibble2_00[0]),
    sizeof(g_nibble2_01) / sizeof(g_nibble2_01[0]),
    sizeof(g_nibble2_02) / sizeof(g_nibble2_02[0]),
    sizeof(g_nibble2_03) / sizeof(g_nibble2_03[0]),
    sizeof(g_nibble2_04) / sizeof(g_nibble2_04[0]),
    sizeof(g_nibble2_05) / sizeof(g_nibble2_05[0]),
    sizeof(g_nibble2_06) / sizeof(g_nibble2_06[0]),
    sizeof(g_nibble2_07) / sizeof(g_nibble2_07[0]),
    sizeof(g_nibble2_08) / sizeof(g_nibble2_08[0]),
    sizeof(g_nibble2_09) / sizeof(g_nibble2_09[0]),
    sizeof(g_nibble2_0A) / sizeof(g_nibble2_0A[0]),
    sizeof(g_nibble2_0B) / sizeof(g_nibble2_0B[0]),
    sizeof(g_nibble2_0F) / sizeof(g_nibble2_0F[0])
};
