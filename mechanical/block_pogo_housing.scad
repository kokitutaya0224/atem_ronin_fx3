// ============================================================
// RSAポート用 ポゴピン"ブロック"収納ハウジング
// ============================================================
//
// 対象部品: 貫通型ポゴピンブロック（表裏に4本ずつ、見た目のチップは
// 計8個だが表裏は電気的に同じ線でつながっており独立回路は4系統）。
//
// 実測値（2026-09-01ユーザー計測）:
//   - ブロック全長（ピンが並ぶ方向, X） = 10mm
//   - ブロック厚み（ピンが突き出る方向, Z）= 5mm
//   - ポゴピン全長                       = 10mm
//     → 前後に (10-5)/2 = 2.5mm ずつ突出する計算
//
// 【未確認・要注意】
//   - 幅（Y方向、行に対して垂直の奥行き）は未計測。block_width_y は
//     プレースホルダーなので実物と合わなければ調整すること
//   - このブロックは独立4系統しかなく、CAN方式で必要な5系統
//     （VCC/CANH/CANL/AD_COM/GND）には1本足りない。AD_COM用に
//     バラの個別ポゴピンを追加するか、要件を見直す必要がある
//     （本ファイルは「ブロックを収める箱」だけを提供する）
//   - ピンのばね圧縮ストロークが2.5mmより短い場合、実機に密着させた
//     ときに底突きする可能性がある。組み立て後に必ず確認すること
//
// 【固定方法】
//   ブロックの前面が内側の段差（肩）に突き当たって止まる構造。
//   前面窓は奥のポケットよりY方向だけ一回り小さくしてあり
//   （X方向はピン位置が不明なため窓を狭めない＝全長ぶん開けておく）、
//   ここでピンの頭だけが外に出る。ブロック本体はその奥の
//   ポケット（block_len×block_width_y×block_thickness、
//   pocket_clearance分の余裕）に収まり、肩に当たった位置で止まるので
//   押し込む深さで迷わない。仮固定後、配線室側からエポキシを
//   流し込んで固定する。
//
// ============================================================

// ---------- PARAMETERS ----------

block_len        = 10;   // 実測: ピン並び方向の全長 [mm]
block_thickness  = 5;    // 実測: ピン突出方向の厚み [mm]
block_width_y    = 4;    // ★未実測(推定値): 奥行き方向の幅 [mm]。実物に合わせて調整
pocket_clearance = 0.3;  // ブロックとポケットの片側クリアランス [mm]

shoulder_depth     = 1.0; // 前面の肩（窓）の厚み [mm]
pin_window_margin_y = 0.5; // 前面窓をY方向だけ狭める片側マージン[mm]（肩の幅になる）
                            // X方向は窓を狭めない（ピンの正確な位置が未確認のため塞がない）

// 上下のM4固定穴（RSAポート実測前のプレースホルダー。rsa_pogo_housing.scadと同じ値）
mount_hole_dia      = 4.3;
mount_hole_spacing  = 30.0;
mount_hole_offset_x = 0;

// ハウジング寸法
housing_w = 24;
housing_h = 40;
housing_wall_t = 3;
wire_cavity_depth = 10;

// ---------- 内部計算 ----------
plate_t = shoulder_depth + block_thickness; // 前面プレート全体の厚み（肩+ブロック厚み分を確保）

pocket_len_x = block_len + 2*pocket_clearance;
pocket_len_y = block_width_y + 2*pocket_clearance;
window_x = pocket_len_x;                         // X方向は窓を狭めない
window_y = pocket_len_y - 2*pin_window_margin_y;  // Y方向だけ肩をつくる

// 参考値（プレビュー確認用）: 前面から見たピン突出量
pin_protrusion = (10 - block_thickness) / 2; // ポゴピン全長10mm前提
front_face_tip_stickout = pin_protrusion - shoulder_depth; // ハウジング前面(z=0)からの突き出し量
echo(str("参考: ハウジング前面からのピン突き出し量 = ", front_face_tip_stickout, " mm"));

module mount_holes() {
    for (dy = [-mount_hole_spacing/2, mount_hole_spacing/2])
        translate([mount_hole_offset_x, dy, 0])
        children();
}

// ブロック用ポケット（前面窓＋段差＋本体収納部）
module block_pocket() {
    union() {
        // 前面窓（肩から前面まで貫通。ピンの頭がここを通る）
        translate([-window_x/2, -window_y/2, -1])
            cube([window_x, window_y, shoulder_depth + 1]);
        // ブロック収納ポケット（肩の奥。配線室側へつながる深さまで）
        translate([-pocket_len_x/2, -pocket_len_y/2, shoulder_depth])
            cube([pocket_len_x, pocket_len_y, plate_t - shoulder_depth + 1]);
    }
}

// 【修正: 背面は塞がない】以前の版は配線室の外壁を作った際に背面まで
// 塞いでしまい、ブロックの差し込み口もジャンパー線の取り出し口も
// 存在しない完全密閉の箱になっていた（組み立て不可能なミス）。
// 側壁だけのトンネル状にして前後とも開放し、後ろからブロックを挿入
// →ハンダ付け→配線の引き出しができるようにする。防塵等が必要なら
// 動作確認後に別途フタを設計して被せる想定（今回は機能優先）。
module full_housing() {
    difference() {
        union() {
            // 前面プレート（肩+ブロック厚み分の厚み）
            translate([-housing_w/2, -housing_h/2, 0])
                cube([housing_w, housing_h, plate_t]);
            // 側壁のみのトンネル（背面は開放＝フタなし）
            translate([-housing_w/2, -housing_h/2, plate_t])
                cube([housing_w, housing_h, wire_cavity_depth]);
        }

        // トンネルの内側を前後まるごと貫通させる（壁厚housing_wall_tだけ残す）
        translate([
            -housing_w/2 + housing_wall_t,
            -housing_h/2 + housing_wall_t,
            plate_t - 1
        ])
            cube([
                housing_w - 2*housing_wall_t,
                housing_h - 2*housing_wall_t,
                wire_cavity_depth + 2
            ]);

        // ブロック用ポケット＋前面窓
        block_pocket();

        // M4固定穴
        mount_holes()
            translate([0, 0, -1])
            cylinder(d = mount_hole_dia, h = plate_t + wire_cavity_depth + 2, $fn = 32);
    }
}

full_housing();
