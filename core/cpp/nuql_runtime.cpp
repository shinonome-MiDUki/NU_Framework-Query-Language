#include "nuql_runtime.h"
#include "nuql_types.h"
#include <fstream>
#include <stdexcept>

// -------------------------------------------------------
// BinaryReader: .nuqlbファイルの逐次読み込みクラス
// fstreamをラップして各型の読み込みメソッドを提供する。
// -------------------------------------------------------

BinaryReader::BinaryReader(const std::string &path)
    : f(path, std::ios::binary) {
  // ファイルが開けなかった場合は即座に例外を投げる
  if (!f)
    throw std::runtime_error("cannot open: " + path);
}

uint8_t BinaryReader::r_u8() {
  // 1バイト読んで uint8_t として返す
  uint8_t v;
  f.read(reinterpret_cast<char *>(&v), 1);
  return v;
}

uint32_t BinaryReader::r_u32() {
  // 4バイトをリトルエンディアンで読んで uint32_t として返す
  uint32_t v;
  f.read(reinterpret_cast<char *>(&v), 4);
  return v;
}

uint64_t BinaryReader::r_u64() {
  // 8バイトをリトルエンディアンで読んで uint64_t として返す
  uint64_t v;
  f.read(reinterpret_cast<char *>(&v), 8);
  return v;
}

double BinaryReader::r_f64() {
  // 8バイトをIEEE754倍精度浮動小数点として読む
  double v;
  f.read(reinterpret_cast<char *>(&v), 8);
  return v;
}

std::string BinaryReader::r_str() {
  // uint32（バイト長）を読んでから、その長さ分のUTF-8バイトを読む
  uint32_t len = r_u32();
  std::string s(len, '\0');
  f.read(s.data(), len);
  return s;
}

void BinaryReader::seek(uint64_t offset) {
  // 指定したオフセットにファイルポインタをジャンプさせる
  // オフセットテーブルを使ってクラスターの先頭に直接移動するために使う
  f.seekg(static_cast<std::streamoff>(offset));
}

// -------------------------------------------------------
// read_value: バイナリから Value を1つ読む
// 先頭1バイトの型IDで分岐し、型に応じたデータを読む。
// Python側の write_value() と対応している。
// -------------------------------------------------------
static Value read_value(BinaryReader &r) {
  Value v;
  v.type = static_cast<ValueType>(r.r_u8()); // 型IDを読む

  switch (v.type) {
  // 文字列系は全て同じフォーマット（uint32長 + UTF-8バイト列）
  case ValueType::STR:
  case ValueType::SSTR:
  case ValueType::ID:
  case ValueType::JSON:
  case ValueType::REGEX:
  case ValueType::FSTRING:
    v.data = r.r_str();
    break;

  case ValueType::NUM:
    // 数値は64bit浮動小数点
    v.data = r.r_f64();
    break;

  case ValueType::BOOL:
    // 真偽値は1バイト（0=false, 1=true）
    v.data = static_cast<bool>(r.r_u8());
    break;

  case ValueType::SET: {
    // セットは要素数（uint32）の後に各文字列が続く
    uint32_t count = r.r_u32();
    std::set<std::string> s;
    for (uint32_t i = 0; i < count; i++)
      s.insert(r.r_str());
    v.data = std::move(s);
    break;
  }

  case ValueType::INJECTED: {
    // 注入型はextension・type_name・valueの3文字列が順番に続く
    InjectedData inj;
    inj.extension = r.r_str();
    inj.type_name = r.r_str();
    inj.value = r.r_str();
    v.data = std::move(inj);
    break;
  }
  }
  return v;
}

// -------------------------------------------------------
// read_datum: バイナリから Datum を1つ読む
// Python側の write_datum() と対応している。
// -------------------------------------------------------
static Datum read_datum(BinaryReader &r) {
  Datum d;

  // modifierフラグ（1バイト、bit0=protected, bit1=sideway）
  uint8_t flags = r.r_u8();
  d.is_protected = (flags & 0x01) != 0;
  d.is_sideway = (flags & 0x02) != 0;

  // ペア種別（oneway / twoway）
  d.pair_type = static_cast<PairType>(r.r_u8());

  // lhs（キー）
  d.lhs = read_value(r);

  // rhs（値リスト）: onewayは1個、twowayは複数
  uint32_t rhs_count = r.r_u32();
  for (uint32_t i = 0; i < rhs_count; i++)
    d.rhs.push_back(read_value(r));

  return d;
}

// -------------------------------------------------------
// read_cluster: バイナリから Cluster を1つ読む
// Python側の write_cluster() と対応している。
// -------------------------------------------------------
static Cluster read_cluster(BinaryReader &r) {
  Cluster clu;

  // クラスター種別・modifier・名前
  clu.type = static_cast<CluType>(r.r_u8());
  clu.modifier =
      r.r_u8();         // ビットフラグ（MOD_PRIME | MOD_REJECTED の組み合わせ）
  clu.name = r.r_str(); // meta/wmetaは空文字列

  // 継承リスト
  uint32_t inh_count = r.r_u32();
  for (uint32_t i = 0; i < inh_count; i++) {
    InheritEntry e;
    uint8_t flags = r.r_u8();
    e.prime = (flags & 0x01) != 0;   // bit0: local prime
    e.reserve = (flags & 0x02) != 0; // bit1: reserve
    e.name = r.r_str();
    clu.inheritance.push_back(std::move(e));
  }

  // datumリストを読み、同時にキー→インデックスのマップを構築する
  uint32_t datum_count = r.r_u32();
  for (uint32_t i = 0; i < datum_count; i++) {
    Datum d = read_datum(r);
    // lhsのキー文字列をインデックスに登録してO(1)アクセスを可能にする
    clu.index[d.lhs.as_str()] = clu.data.size();
    // [追加]
    // twowayの場合はrhsもreverse_indexに登録して逆方向O(1)アクセスを可能にする
    if (d.pair_type == PairType::TWOWAY) {
      for (const auto &rhs : d.rhs)
        clu.reverse_index[rhs.as_str()] = clu.data.size();
    }
    clu.data.push_back(std::move(d));
  }

  // diffリストを読む
  uint32_t diff_count = r.r_u32();
  for (uint32_t i = 0; i < diff_count; i++) {
    Diff diff;
    diff.name = r.r_str(); // diff名（例: animation_case1）

    // このdiff内のdatumリストを読む
    uint32_t dcnt = r.r_u32();
    for (uint32_t j = 0; j < dcnt; j++) {
      Datum d = read_datum(r);
      // diff内もキー→インデックスのマップを構築する
      diff.index[d.lhs.as_str()] = diff.data.size();
      // [追加] diff内のtwowayもreverse_indexに登録する
      if (d.pair_type == PairType::TWOWAY) {
        for (const auto &rhs : d.rhs)
          diff.reverse_index[rhs.as_str()] = diff.data.size();
      }
      diff.data.push_back(std::move(d));
    }
    clu.diffs.push_back(std::move(diff));
  }

  return clu;
}

// -------------------------------------------------------
// NUQLContext の実装
// -------------------------------------------------------

void NUQLContext::load(const std::string &path) {
  BinaryReader r(path);

  // マジックナンバー確認（'NUQL' = 0x4C51554E リトルエンディアン）
  uint32_t magic_val = r.r_u32();
  if (magic_val != 0x4C51554E)
    throw std::runtime_error("invalid .nuqlb file");

  version = r.r_u8(); // バージョン番号
  cc_mode = r.r_u8(); // 0=ncc, 1=cc
  uint32_t clu_count = r.r_u32();

  // オフセットテーブルを読む
  // 各クラスターの先頭バイト位置が格納されている
  std::vector<uint64_t> offsets(clu_count);
  for (uint32_t i = 0; i < clu_count; i++)
    offsets[i] = r.r_u64();

  // 各クラスターをオフセットに直接ジャンプして読み込む
  clusters.reserve(clu_count);
  for (uint32_t i = 0; i < clu_count; i++) {
    r.seek(offsets[i]);
    Cluster clu = read_cluster(r);

    // meta / wmeta のインデックスを記録する
    // アクセス時に最優先で参照するために別リストで管理する
    if (clu.type == CluType::META)
      meta_indices.push_back(clusters.size());
    else if (clu.type == CluType::WMETA)
      wmeta_indices.push_back(clusters.size());

    // 名前→クラスターインデックスのマップに登録する
    // meta/wmetaは名前なし（空文字列）なので登録しない
    if (!clu.name.empty())
      clu_index[clu.name] = clusters.size();

    clusters.push_back(std::move(clu));
  }
}

// -------------------------------------------------------
// find_in_cluster: 1つのクラスターのローカルデータからキーを探す
// インデックスを使ってO(1)で検索する。
// [変更] is_reverse引数を追加
//   false: lhsをキーに正方向検索 → rhsの先頭を返す
//   true:  rhsをキーに逆方向検索 → lhsを返す（twowayのみ有効）
// -------------------------------------------------------
std::optional<Value> NUQLContext::find_in_cluster(const Cluster &clu,
                                                  const std::string &key,
                                                  bool is_reverse) const {
  if (!is_reverse) {
    // 正方向: lhsのインデックスからdatumを引き、rhsの先頭を返す
    auto it = clu.index.find(key);
    if (it == clu.index.end())
      return std::nullopt; // キーなし
    const Datum &d = clu.data[it->second];
    if (d.rhs.empty())
      return std::nullopt; // 値なし
    // onewayは1個、twowayは先頭を返す（L-Parse未実装）
    return d.rhs[0];
  } else {
    // [追加] 逆方向: reverse_indexからdatumを引き、lhsを返す
    // twowayのみ逆引き可能。onewayにはreverse_indexエントリが存在しない。
    auto it = clu.reverse_index.find(key);
    if (it == clu.reverse_index.end())
      return std::nullopt; // キーなし
    const Datum &d = clu.data[it->second];
    return d.lhs; // 逆方向なのでlhsを返す
  }
}

// -------------------------------------------------------
// find_in_meta: meta / wmeta クラスター全体からキーを探す
// metaを先に探し、なければwmetaを順番に探す。
// [変更] is_reverseを受け取ってfind_in_clusterに伝搬する
// -------------------------------------------------------
std::optional<Value> NUQLContext::find_in_meta(const std::string &key,
                                               bool is_reverse,
                                               bool include_wmeta) const {
  // metaは最優先（1ファイルに1つのみ）
  for (size_t idx : meta_indices) {
    auto v = find_in_cluster(clusters[idx], key, is_reverse);
    if (v)
      return v;
  }
  // wmetaはmetaの次（複数存在可、rejectedは既にロード時に除外済み想定）
  if (include_wmeta) {
    for (size_t idx : wmeta_indices) {
      auto v = find_in_cluster(clusters[idx], key, is_reverse);
      if (v)
        return v;
    }
  }
  return std::nullopt;
}

// -------------------------------------------------------
// find_with_inheritance: 継承を辿ってキーを探す（遅延評価 + キャッシュ）
// 優先順位: meta/wmeta > prime inheritance > 通常inheritance > local
// [変更] is_reverseを受け取り、方向ごとに別キャッシュを使う
// -------------------------------------------------------
std::optional<Value> NUQLContext::find_with_inheritance(const Cluster &clu,
                                                        const std::string &key,
                                                        bool is_reverse,
                                                        bool is_reserved) const {
  // [変更] is_reverseによってキャッシュを使い分ける
  // 正方向はcache、逆方向はreverse_cacheを参照する
  auto &active_cache = is_reverse ? clu.reverse_cache : clu.cache;

  // キャッシュに結果があればそれを返す（2回目以降はO(1)）
  auto cache_it = active_cache.find(key);
  if (cache_it != active_cache.end())
    return cache_it->second;

  std::optional<Value> result = std::nullopt;

  // 1. meta / wmeta（最優先）
  bool include_wmeta = clu.modifier == 0x00 || clu.modifier == 0x01;
  result = find_in_meta(key, is_reverse, include_wmeta);
  if (result) {
    active_cache[key] = result;
    return result;
  }

  // 2. prime inheritance（local primeフラグが立っている親を左から順に）
  for (const auto &entry : clu.inheritance) {
    bool is_prime_inheritance = false;
    if (entry.reserve){
      if (is_reserved){
        continue;
      }
    }
    auto it = clu_index.find(entry.name);
    if (it == clu_index.end())
      continue; // 親クラスターが見つからなければスキップ
    Cluster inherited_clu = clusters[it->second];
    is_prime_inheritance =
        inherited_clu.modifier == 0x01 || inherited_clu.modifier == 0x03 || entry.prime;
    if (is_prime_inheritance) {
      result = find_in_cluster(clusters[it->second], key, is_reverse);
      if (result) {
        active_cache[key] = result;
        return result;
      }
    }
  }

  // 3. 通常inheritance（左から順に）
  for (const auto &entry : clu.inheritance) {
    if (entry.prime)
      continue; // primeは上で処理済みなのでスキップ
    auto it = clu_index.find(entry.name);
    if (it == clu_index.end())
      continue;
    result = find_in_cluster(clusters[it->second], key, is_reverse);
    if (result) {
      active_cache[key] = result;
      return result;
    }
  }

  // 4. local（自クラスターのデータ）
  result = find_in_cluster(clu, key, is_reverse);
  active_cache[key] =
      result; // 結果をキャッシュ（nulloptも含めてキャッシュする）
  return result;
}

// -------------------------------------------------------
// get: クラスター名とキーで値を取得するパブリックAPI
// [変更] is_reverse引数を追加（デフォルトfalse=正方向）
// -------------------------------------------------------
Value NUQLContext::get(const std::string &clu_name, 
                       const std::string &key,
                       bool is_reverse,
                       bool is_reserved) const {
  // クラスターの存在確認
  auto it = clu_index.find(clu_name);
  if (it == clu_index.end())
    throw std::runtime_error("cluster not found: " + clu_name);

  // 継承を辿って値を探す
  auto result = find_with_inheritance(clusters[it->second], key, is_reverse, is_reserved);
  if (!result)
    throw std::runtime_error("key not found: " + key);
  return *result;
}

// -------------------------------------------------------
// get_diff: diffブランチ内のキーで値を取得するパブリックAPI
// [変更] is_reverse引数を追加（デフォルトfalse=正方向）
// -------------------------------------------------------
Value NUQLContext::get_diff(const std::string &clu_name,
                            const std::string &diff_name,
                            const std::string &key, bool is_reverse) const {
  // クラスターの存在確認
  auto it = clu_index.find(clu_name);
  if (it == clu_index.end())
    throw std::runtime_error("cluster not found: " + clu_name);

  const Cluster &clu = clusters[it->second];

  // diff名で検索し、その中からキーを探す
  for (const auto &diff : clu.diffs) {
    if (diff.name != diff_name)
      continue;
    if (!is_reverse) {
      // 正方向: lhsのインデックスからrhsを返す
      auto dit = diff.index.find(key);
      if (dit == diff.index.end())
        break; // このdiffにキーなし
      const Datum &d = diff.data[dit->second];
      if (!d.rhs.empty())
        return d.rhs[0];
    } else {
      // [追加] 逆方向: reverse_indexからlhsを返す
      auto dit = diff.reverse_index.find(key);
      if (dit == diff.reverse_index.end())
        break; // このdiffに逆引きキーなし
      const Datum &d = diff.data[dit->second];
      return d.lhs;
    }
  }
  throw std::runtime_error("key not found in diff: " + key);
}

// -------------------------------------------------------
// has_cluster: クラスターの存在確認パブリックAPI
// -------------------------------------------------------
bool NUQLContext::has_cluster(const std::string &clu_name) const {
  return clu_index.count(clu_name) > 0;
}
