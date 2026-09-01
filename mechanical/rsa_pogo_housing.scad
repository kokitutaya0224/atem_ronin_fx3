// ============================================================
// RS4 Pro RSA/NATOポート用 ポゴピン変換ハウジング（パラメトリック）
// ============================================================
//
// 【前提】DJI R SDK文書 Figure 39（v2.5, p.19）の図は旧世代(RS2)の
// 「突起ピン型」コネクタを描いており、RS4 Proの実物（平面パッド×6を
// 2×3グリッド配置＋上下M4ボルト穴でアクセサリ固定）とは形状が異なる。
// 既存STL（3d-print-ronin-can-connector.stl）はRS2向け8コンタクト
// 設計のため、そのままではRS4 Proに物理的に合わない可能性が高い。
//
// このファイルは実測値を下の PARAMETERS セクションに埋めるだけで
// RS4 Pro実機に合わせたハウジングを再生成できるようにしたテンプレート。
// 採寸手順は同ディレクトリの README.md を参照。
//
// 【使い方】
//   1. TEST_MODE = true でまず「穴位置確認用の平板」だけ印刷し、
//      実機に当てて6パッド全部にちゃんと重なるか確認する
//      （ここで数値を追い込む。ポゴピンはまだ実装しない）
//   2. 位置がずれなければ TEST_MODE = false にしてポゴピン用ボアと
//      配線室付きのフルモデルを生成→印刷→ポゴピン圧入/はんだ
//
// ============================================================

// ---------- PARAMETERS（★実測して埋める） ----------

// 動作モード: true=穴位置確認用の薄板のみ / false=フルモデル
TEST_MODE = true;

// パッドグリッド: 2列(row) × 3行(col) 想定。列間隔・行間隔をそれぞれ実測
pad_pitch_col   = 5.0;   // ★実測: 横方向(列)のパッド中心間距離 [mm]
pad_pitch_row   = 5.0;   // ★実測: 縦方向(行)のパッド中心間距離 [mm]
pad_diameter    = 2.0;   // ★実測: パッド自体の直径（接触面） [mm]
num_cols        = 3;     // グリッド列数（写真通りなら3のはず）
num_rows        = 2;     // グリッド行数（写真通りなら2のはず）

// 上下のM4固定穴（アクセサリを本体にボルト留めする穴）
mount_hole_dia     = 4.3;  // M4クリアランス穴 [mm]（M4ネジ径4.0+遊び）
mount_hole_spacing = 30.0; // ★実測: 上穴-下穴の中心間距離 [mm]
mount_hole_offset_x = 0;   // ★実測: パッドグリッド中心から見た穴の左右オフセット [mm]

// パッド面からの窪み量（実物はパッドが窪みの中にある、との観察あり）
pad_recess_depth = 0.5;  // ★実測: パッド面がハウジング取り付け面から何mm奥か [mm]

// ハウジング寸法
housing_w = 24;      // 全体の幅
housing_h = 40;      // 全体の高さ（M4穴を含む全長より少し大きく）
housing_wall_t = 3;  // 側壁厚
plate_t = 2.5;        // 取り付け面プレートの厚み（TEST_MODEはこれだけの板になる）

// ポゴピン（Preci-Dip 811-S1系を想定。発注前にデータシートで要再確認）
pogo_bore_dia   = 1.55; // ポゴピン圧入用ボア径 [mm]（811-S1バレル外径+余裕。要データシート確認）
pogo_bore_depth = 6.0;  // ボア深さ（ピン全長に合わせる）[mm]
pogo_body_len   = 8.5;  // ポゴピン全長の目安（配線室の深さ計算に使用）

// 配線室（背面にケーブルを引き出す空間）
wire_cavity_depth = 10;

// AD_COMプルダウン抵抗（10〜100kΩ）をコネクタ側に内蔵するためのポケット
// 「ESP32側配線し忘れ」を構造的に防ぐ狙い（2026-08-31の知見: 未実装が
// 疑われた実績あり）
resistor_pocket = true;
resistor_pocket_w = 6;
resistor_pocket_h = 3;
resistor_pocket_d = 3;

// ---------- 内部計算用 ----------
grid_w = (num_cols - 1) * pad_pitch_col;
grid_h = (num_rows - 1) * pad_pitch_row;

module pad_positions() {
    for (c = [0 : num_cols - 1])
        for (r = [0 : num_rows - 1])
            translate([
                -grid_w/2 + c * pad_pitch_col,
                -grid_h/2 + r * pad_pitch_row,
                0
            ])
            children();
}

module mount_holes() {
    for (dy = [-mount_hole_spacing/2, mount_hole_spacing/2])
        translate([mount_hole_offset_x, dy, 0])
        children();
}

// ---------- TEST_MODE: 穴位置確認用の薄板 ----------
module test_plate() {
    difference() {
        // ベースプレート（グリッド+M4穴が全部乗るサイズ）
        translate([-housing_w/2, -housing_h/2, 0])
            cube([housing_w, housing_h, plate_t]);

        // パッド位置を貫通穴で可視化（実機に重ねて位置確認する）
        pad_positions()
            translate([0, 0, -1])
            cylinder(d = pad_diameter, h = plate_t + 2, $fn = 32);

        // M4固定穴
        mount_holes()
            translate([0, 0, -1])
            cylinder(d = mount_hole_dia, h = plate_t + 2, $fn = 32);
    }
}

// ---------- フルモデル: ポゴピン圧入ハウジング ----------
module full_housing() {
    difference() {
        union() {
            // 取り付け面プレート
            translate([-housing_w/2, -housing_h/2, 0])
                cube([housing_w, housing_h, plate_t]);
            // 背面の配線室ボックス（外壁）
            translate([-housing_w/2, -housing_h/2, plate_t])
                cube([housing_w, housing_h, wire_cavity_depth + housing_wall_t]);
        }

        // 配線室の内側をくり抜く（壁厚housing_wall_tを残す）
        translate([
            -housing_w/2 + housing_wall_t,
            -housing_h/2 + housing_wall_t,
            plate_t
        ])
            cube([
                housing_w - 2*housing_wall_t,
                housing_h - 2*housing_wall_t,
                wire_cavity_depth + 1
            ]);

        // ポゴピン圧入ボア（プレートを貫通し、配線室側へ抜ける）
        pad_positions()
            translate([0, 0, -1])
            cylinder(d = pogo_bore_dia, h = plate_t + pogo_bore_depth, $fn = 24);

        // M4固定穴
        mount_holes()
            translate([0, 0, -1])
            cylinder(d = mount_hole_dia, h = plate_t + wire_cavity_depth + housing_wall_t + 2, $fn = 32);

        // AD_COMプルダウン抵抗ポケット（背面、配線室内の隅）
        if (resistor_pocket) {
            translate([
                housing_w/2 - housing_wall_t - resistor_pocket_w - 1,
                -housing_h/2 + housing_wall_t + 1,
                plate_t
            ])
                cube([resistor_pocket_w, resistor_pocket_h, resistor_pocket_d + 1]);
        }
    }
}

// ---------- 出力 ----------
if (TEST_MODE) {
    test_plate();
} else {
    full_housing();
}

// パッド番号の参考表示（印刷はされない。プレビュー確認用）
// Figure 39のピン配置（1=VCC 2=CANL 3=SBUS_RX 4=CANH 5=AD_COM 6=GND）を
// どのグリッド位置に割り当てるかは実機のテスター導通確認で決まるまで未確定。
// 決まったら pad_positions() のループ内でパッドごとにラベルコメントを追記すること。
