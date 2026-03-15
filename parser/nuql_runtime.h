#pragma once
#include "nuql_types.h"
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// -------------------------------------------------------
// BinaryReader: .nuqlbファイルの逐次読み込みクラス
// fstreamをラップして各型の読み込みメソッドを提供する。
// -------------------------------------------------------
class BinaryReader {
  std::ifstream f; // バイナリ読み込み用ファイルストリーム
public:
  // コンストラクタ: ファイルを開く。失敗したら例外を投げる。
  explicit BinaryReader(const std::string &path);

  uint8_t r_u8();      // 1バイト読む
  uint32_t r_u32();    // 4バイト（リトルエンディアン）読む
  uint64_t r_u64();    // 8バイト（リトルエンディアン）読む
  double r_f64();      // 64bit浮動小数点読む
  std::string r_str(); // uint32(長さ) + UTF-8バイト列を読む

  // 指定オフセットにファイルポインタをジャンプさせる
  void seek(uint64_t offset);
};

// -------------------------------------------------------
// NUQLContext: .nuqlbファイルを読み込み、値へのアクセスを提供するクラス
// 各クラスターを独立したメモリ空間とみなし、
// 継承を他のメモリ空間へのアクセス権の貸し出しとして扱う。
// -------------------------------------------------------
class NUQLContext {
  uint8_t version; // NUQLバージョン番号
  uint8_t cc_mode; // 0=ncc（大文字小文字区別なし）/ 1=cc（区別あり）

  std::vector<Cluster> clusters; // 全クラスターのリスト

  // クラスター名 → clustersリスト内のインデックス（O(1)アクセス用）
  std::unordered_map<std::string, size_t> clu_index;

  // meta / wmeta クラスターのインデックスリスト
  // アクセス時に最優先で参照するため別管理する
  std::vector<size_t> meta_indices;
  std::vector<size_t> wmeta_indices;

  // --- 内部検索メソッド ---

  // [変更] is_reverse引数を追加
  // is_reverse=false: lhsをキーに正方向検索
  // is_reverse=true:  rhsをキーに逆方向検索（twowayのみ有効）
  std::optional<Value> find_in_cluster(const Cluster &clu,
                                       const std::string &key,
                                       bool is_reverse) const;

  // [変更] is_reverse引数を追加（meta/wmetaへの方向指定を伝搬する）
  std::optional<Value> find_in_meta(const std::string &key,
                                    bool is_reverse) const;

  // 継承を辿ってキーを探す（遅延評価 + キャッシュ）
  // 優先順位: meta/wmeta > prime inheritance > 通常inheritance > local
  // [変更] is_reverse引数を追加（継承先への方向指定を伝搬する）
  std::optional<Value> find_with_inheritance(const Cluster &clu,
                                             const std::string &key,
                                             bool is_reverse) const;

public:
  // .nuqlbファイルを読み込んでコンテキストを構築する
  void load(const std::string &path);

  // [変更] is_reverseを追加
  // is_reverse=false: lhs → rhs の正方向参照（デフォルト）
  // is_reverse=true:  rhs → lhs の逆方向参照（twowayのみ有効）
  Value get(const std::string &clu_name, const std::string &key,
            bool is_reverse = false) const;

  // [変更] is_reverseを追加（diffブランチ内でも双方向参照を可能にする）
  Value get_diff(const std::string &clu_name, const std::string &diff_name,
                 const std::string &key, bool is_reverse = false) const;

  // クラスターが存在するか確認する
  bool has_cluster(const std::string &clu_name) const;
};
