// ============================================================
// 既存STL（3d-print-ronin-can-connector.stl）の外殻はそのまま流用し、
// 中央のポケットだけを実物ブロックのサイズに合わせて作り直す
// ============================================================
//
// 【元STLの実測結果（2026-09-01、trimeshで解析）】
//   外殻: 19.80mm(X) × 28.85mm(Y) × 8.00mm(Z)、四隅に丸みあり
//   取付穴: 直径4.30mm ×2、中心間隔19.85mm(Y方向)、X中心位置 x=-4.01
//   既存ポケット（2段、いずれも中心はおよそ x=-3.98, y=4.0）:
//     前面側(z=3.6が外面): 14.78×10.17mm の浅い凹み、深さ約3.95mm
//     背面側(z=-4.4が外面): 11.06×6.06mm の細い貫通穴、深さ約4.05mm
//
// 【問題】この既存ポケットは実物のポゴピンブロック（10mm×約4-5mm×
// 厚み5mm）よりずっと大きく、そこに挿すとガバガバでぐらつく
// （2026-09-01ユーザー報告「真ん中の穴にポゴピン刺してぐらつく」）。
//
// 【対応】外殻・取付穴はそのまま（元STLをimportして流用）、中央の
// 既存ポケットだけを一度solidで埋め戻し、実測ブロックサイズに合わせた
// 新しい段差付きポケット（前面は肩で止め、背面は配線室として開放）を
// 掘り直す。考え方は block_pogo_housing.scad と同じ。
//
// ============================================================

// ---------- PARAMETERS ----------

// 実物ブロックの実測値（2026-09-01ユーザー計測）
block_len        = 10;   // ピン並び方向の全長 [mm]
block_thickness  = 5;    // ピン突出方向の厚み [mm]
block_width_y    = 4;    // ★未実測(推定値)。実物に合わせて調整
pocket_clearance = 0.3;  // ブロックとポケットの片側クリアランス [mm]

shoulder_depth      = 1.0; // 前面の肩（窓）の厚み [mm]
pin_window_margin_y = 0.5; // 前面窓をY方向だけ狭める片側マージン[mm]
                            // X方向は窓を狭めない（ピンの正確な位置が未確認のため）

// ---------- 元STLの実測値（変更しない） ----------
front_z = 3.6;    // 元STLの前面(外面、Zの最大値)
back_z  = -4.4;   // 元STLの背面(外面、Zの最小値)
pocket_cx = -3.98; // 既存ポケットの中心X
pocket_cy = 4.0;   // 既存ポケットの中心Y

// 既存ポケット（埋め戻し用、実測bboxに0.3mmずつパディングして隙間を残さない）
old_wide_x = 14.78 + 0.6;  old_wide_y = 10.17 + 0.6;  old_wide_z0 = -0.35; // 前面側
old_narrow_x = 11.06 + 0.6; old_narrow_y = 6.06 + 0.6;                    // 背面側

// ---------- 内部計算 ----------
pocket_len_x = block_len + 2*pocket_clearance;
pocket_len_y = block_width_y + 2*pocket_clearance;
window_x = pocket_len_x;                          // X方向は窓を狭めない
window_y = pocket_len_y - 2*pin_window_margin_y;   // Y方向だけ肩をつくる
shoulder_z = front_z - shoulder_depth;

echo(str("参考: ハウジング前面からのピン突き出し量 = ", (10-block_thickness)/2 - shoulder_depth, " mm"));

// 元STL本体
module original_shell() {
    import("/Users/kokit/Downloads/3d-print-ronin-can-connector.stl");
}

// 既存の穴を埋め戻すフィラー（2段分をまとめて埋める）
module old_pocket_filler() {
    // 前面側の浅い凹みぶん
    translate([pocket_cx - old_wide_x/2, pocket_cy - old_wide_y/2, old_wide_z0])
        cube([old_wide_x, old_wide_y, (front_z - old_wide_z0) + 0.5]);
    // 背面側の細い貫通穴ぶん
    translate([pocket_cx - old_narrow_x/2, pocket_cy - old_narrow_y/2, back_z - 0.5])
        cube([old_narrow_x, old_narrow_y, (old_wide_z0 - back_z) + 0.5 + 0.5]);
}

// 実物ブロックに合わせた新ポケット（前面窓＋肩＋背面まで開放の本体ポケット）
module new_block_pocket() {
    // 前面窓（肩から前面まで貫通）
    translate([pocket_cx - window_x/2, pocket_cy - window_y/2, shoulder_z])
        cube([window_x, window_y, (front_z - shoulder_z) + 1]);
    // 本体ポケット（肩から背面まで開放＝配線・ハンダ・エポキシ注入用にフタなし）
    translate([pocket_cx - pocket_len_x/2, pocket_cy - pocket_len_y/2, back_z - 1])
        cube([pocket_len_x, pocket_len_y, (shoulder_z - back_z) + 1]);
}

module retrofit_housing() {
    difference() {
        union() {
            original_shell();
            old_pocket_filler();
        }
        new_block_pocket();
    }
}

retrofit_housing();
