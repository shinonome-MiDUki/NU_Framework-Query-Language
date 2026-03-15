#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// -------------------------------------------------------
// 値の型ID（Python側のTYPE_*定数と共有）
// バイナリの先頭1バイトでどの型かを識別する。
// -------------------------------------------------------
enum class ValueType : uint8_t {
  STR = 0,     // シングルクォート文字列 'value'
  SSTR = 1,    // ダブルクォート文字列 "value"
  ID = 2,      // 識別子参照 #value
  NUM = 3,     // 数値 @value
  BOOL = 4,    // 真偽値 %true / %false
  SET = 5,     // セット s{...}
  JSON = 6,    // JSON j{...}
  REGEX = 7,   // 正規表現 r{...}
  FSTRING = 8, // フォーマット文字列 %...%
  INJECTED = 9 // 注入型 inj.extension TypeName value
};

// -------------------------------------------------------
// クラスター種別（Python側のCLU_*定数と共有）
// -------------------------------------------------------
enum class CluType : uint8_t {
  NORMAL = 0, // 通常クラスター clu name << ... >>
  META = 1,   // メタクラスター meta clu << ... >>
  WMETA = 2,  // 弱メタクラスター wmeta clu << ... >>
  HEADER = 3  // ヘッダークラスター & clu name << ... >>
};

// -------------------------------------------------------
// クラスターのmodifierフラグ（ビット演算で組み合わせ可能）
// Python側のMOD_*定数と共有。
// 例: prime かつ rejected → 0x01 | 0x02 = 0x03
// -------------------------------------------------------
constexpr uint8_t MOD_NONE = 0x00;     // 修飾子なし
constexpr uint8_t MOD_PRIME = 0x01;    // bit0: プライムクラスター *clu
constexpr uint8_t MOD_REJECTED = 0x02; // bit1: 拒否クラスター rejected clu

// -------------------------------------------------------
// ペアの種別（Python側のPAIR_*定数と共有）
// -------------------------------------------------------
enum class PairType : uint8_t {
  ONEWAY = 0, // 一方向参照 key = value
  TWOWAY = 1  // 双方向参照 key == value
};

// -------------------------------------------------------
// 注入型のデータ（inj.extension TypeName value）
// -------------------------------------------------------
struct InjectedData {
  std::string extension; // 拡張子（例: png）
  std::string type_name; // 型名（例: ImageType）
  std::string value;     // 値（例: img_001）
};

// -------------------------------------------------------
// Valueが持てるデータのvariant
// ValueTypeに対応する実際のC++型を列挙する。
// -------------------------------------------------------
using ValueData =
    std::variant<std::string,           // STR, SSTR, ID, JSON, REGEX, FSTRING
                 double,                // NUM
                 bool,                  // BOOL
                 std::set<std::string>, // SET
                 InjectedData           // INJECTED
                 >;

// -------------------------------------------------------
// 値（1つのデータ）
// type で種別を識別し、data に実際の値を持つ。
// -------------------------------------------------------
struct Value {
  ValueType type;
  ValueData data;

  // 型ごとのアクセサ（型が合わない場合は std::bad_variant_access を投げる）
  const std::string &as_str() const { return std::get<std::string>(data); }
  double as_num() const { return std::get<double>(data); }
  bool as_bool() const { return std::get<bool>(data); }
  const std::set<std::string> &as_set() const {
    return std::get<std::set<std::string>>(data);
  }
  const InjectedData &as_injected() const {
    return std::get<InjectedData>(data);
  }
};

// -------------------------------------------------------
// datum（1つのキーバリューペア）
// -------------------------------------------------------
struct Datum {
  bool is_protected;      // ! フラグ: 継承から上書きされない
  bool is_sideway;        // / フラグ: 読み取り専用
  PairType pair_type;     // oneway(=) / twoway(==)
  Value lhs;              // キー
  std::vector<Value> rhs; // 値（onewayは1個、twowayは複数可）
};

// -------------------------------------------------------
// diff（クラスター内のブランチ）
// :: diff name <~~ ... ~~> に対応する。
// -------------------------------------------------------
struct Diff {
  std::string name;        // diff名（例: animation_case1）
  std::vector<Datum> data; // このdiff内のdatumリスト

  // キー文字列 → data内のインデックス（起動時に構築、O(1)アクセス用）
  std::unordered_map<std::string, size_t> index;

  // [追加] rhs文字列 → data内のインデックス（twoway逆引き用、起動時に構築）
  std::unordered_map<std::string, size_t> reverse_index;
};

// -------------------------------------------------------
// 継承エントリ（1つの親クラスターへの参照）
// -------------------------------------------------------
struct InheritEntry {
  std::string name; // 親クラスター名
  bool prime;       // * フラグ: このcluster内でprime扱い
  bool reserve;     // ? フラグ: 予備継承
};

// -------------------------------------------------------
// クラスター（1つのメモリ空間）
// 各クラスターは独立したメモリ空間とみなす。
// 継承は他のメモリ空間へのアクセス権の貸し出しとして扱う。
// -------------------------------------------------------
struct Cluster {
  CluType type;     // クラスターの種別
  uint8_t modifier; // modifierフラグ（MOD_*のビット組み合わせ）
  std::string name; // クラスター名（meta/wmetaは空文字列）

  std::vector<InheritEntry> inheritance; // 継承リスト（順番が優先度）
  std::vector<Datum> data;               // ローカルデータ
  std::vector<Diff> diffs;               // diffブランチリスト

  // キー文字列 → data内のインデックス（起動時に構築、O(1)アクセス用）
  std::unordered_map<std::string, size_t> index;

  // rhs文字列 → data内のインデックス（twoway逆引き用、起動時に構築）
  std::unordered_map<std::string, size_t> reverse_index;

  // アクセス結果のキャッシュ（遅延評価、同じキーへの2回目以降はO(1)）
  // [追加] is_reverseごとにキャッシュを分ける
  // cache[false] = 正方向キャッシュ、cache[true] = 逆方向キャッシュ
  mutable std::unordered_map<std::string, std::optional<Value>> cache;
  mutable std::unordered_map<std::string, std::optional<Value>> reverse_cache;
};
