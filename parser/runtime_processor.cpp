#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <stdexcept>
#include <cstdint>
#include <set>
#include <optional>

// -------------------------------------------------------
// 型ID（Python側と共有）
// -------------------------------------------------------
enum class ValueType : uint8_t {
    STR      = 0,
    SSTR     = 1,
    ID       = 2,
    NUM      = 3,
    BOOL     = 4,
    SET      = 5,
    JSON     = 6,
    REGEX    = 7,
    FSTRING  = 8,
    INJECTED = 9
};

// クラスター種別
enum class CluType : uint8_t {
    NORMAL = 0,
    META   = 1,
    WMETA  = 2,
    HEADER = 3
};

// クラスターmodifier
enum class CluMod : uint8_t {
    NONE     = 0,
    PRIME    = 1,
    REJECTED = 2
};

// ペア種別
enum class PairType : uint8_t {
    ONEWAY = 0,
    TWOWAY = 1
};

// -------------------------------------------------------
// 値の型
// -------------------------------------------------------
struct InjectedData {
    std::string extension;
    std::string type_name;
    std::string value;
};

// Valueが持てるデータ
using ValueData = std::variant<
    std::string,                // STR, SSTR, ID, JSON, REGEX, FSTRING
    double,                     // NUM
    bool,                       // BOOL
    std::set<std::string>,      // SET
    InjectedData                // INJECTED
>;

struct Value {
    ValueType type;
    ValueData data;

    // 便利アクセサ
    const std::string& as_str()  const { return std::get<std::string>(data); }
    double             as_num()  const { return std::get<double>(data); }
    bool               as_bool() const { return std::get<bool>(data); }
    const std::set<std::string>& as_set() const { return std::get<std::set<std::string>>(data); }
    const InjectedData& as_injected() const { return std::get<InjectedData>(data); }
};

// -------------------------------------------------------
// datum（1つのキーバリューペア）
// -------------------------------------------------------
struct Datum {
    bool      is_protected;  // ! フラグ
    bool      is_sideway;    // / フラグ
    PairType  pair_type;     // oneway / twoway
    Value     lhs;           // キー
    std::vector<Value> rhs;  // 値（onewayは1個、twowayは複数可）
};

// -------------------------------------------------------
// diff（クラスター内のブランチ）
// -------------------------------------------------------
struct Diff {
    std::string          name;
    std::vector<Datum>   data;
    // キー→datumの高速アクセス用インデックス（起動時に構築）
    std::unordered_map<std::string, size_t> index;
};

// -------------------------------------------------------
// 継承エントリ
// -------------------------------------------------------
struct InheritEntry {
    std::string name;    // 親クラスター名
    bool        prime;   // * フラグ（このcluster内でprime扱い）
    bool        reserve; // ? フラグ（予備）
};

// -------------------------------------------------------
// クラスター（1つのメモリ空間）
// -------------------------------------------------------
struct Cluster {
    CluType    type;
    CluMod     modifier;
    std::string name;

    std::vector<InheritEntry>  inheritance;   // 継承リスト（順番が優先度）
    std::vector<Datum>         data;          // ローカルデータ
    std::vector<Diff>          diffs;         // diffブランチ

    // キー→datumの高速アクセス用インデックス（起動時に構築）
    std::unordered_map<std::string, size_t> index;

    // アクセス結果キャッシュ（遅延評価）
    mutable std::unordered_map<std::string, std::optional<Value>> cache;
};

// -------------------------------------------------------
// バイナリ読み込みヘルパー
// -------------------------------------------------------
class BinaryReader {
    std::ifstream f;
public:
    BinaryReader(const std::string& path) : f(path, std::ios::binary) {
        if (!f) throw std::runtime_error("cannot open: " + path);
    }

    // uint8を読む
    uint8_t r_u8() {
        uint8_t v;
        f.read(reinterpret_cast<char*>(&v), 1);
        return v;
    }

    // uint32を読む（リトルエンディアン）
    uint32_t r_u32() {
        uint32_t v;
        f.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }

    // uint64を読む
    uint64_t r_u64() {
        uint64_t v;
        f.read(reinterpret_cast<char*>(&v), 8);
        return v;
    }

    // double(64bit)を読む
    double r_f64() {
        double v;
        f.read(reinterpret_cast<char*>(&v), 8);
        return v;
    }

    // 文字列を読む: uint32(長さ) + UTF-8bytes
    std::string r_str() {
        uint32_t len = r_u32();
        std::string s(len, '\0');
        f.read(s.data(), len);
        return s;
    }

    // 指定オフセットにジャンプ
    void seek(uint64_t offset) {
        f.seekg(static_cast<std::streamoff>(offset));
    }
};

// -------------------------------------------------------
// Valueを1つ読む
// -------------------------------------------------------
static Value read_value(BinaryReader& r) {
    Value v;
    v.type = static_cast<ValueType>(r.r_u8());

    switch (v.type) {
        case ValueType::STR:
        case ValueType::SSTR:
        case ValueType::ID:
        case ValueType::JSON:
        case ValueType::REGEX:
        case ValueType::FSTRING:
            v.data = r.r_str();
            break;

        case ValueType::NUM:
            v.data = r.r_f64();
            break;

        case ValueType::BOOL:
            v.data = static_cast<bool>(r.r_u8());
            break;

        case ValueType::SET: {
            uint32_t count = r.r_u32();
            std::set<std::string> s;
            for (uint32_t i = 0; i < count; i++)
                s.insert(r.r_str());
            v.data = std::move(s);
            break;
        }

        case ValueType::INJECTED: {
            InjectedData inj;
            inj.extension = r.r_str();
            inj.type_name = r.r_str();
            inj.value     = r.r_str();
            v.data = std::move(inj);
            break;
        }
    }
    return v;
}

// -------------------------------------------------------
// Datumを1つ読む
// -------------------------------------------------------
static Datum read_datum(BinaryReader& r) {
    Datum d;
    uint8_t flags  = r.r_u8();
    d.is_protected = (flags & 0x01) != 0;  // bit0 = protected
    d.is_sideway   = (flags & 0x02) != 0;  // bit1 = sideway
    d.pair_type    = static_cast<PairType>(r.r_u8());
    d.lhs          = read_value(r);
    uint32_t rhs_count = r.r_u32();
    for (uint32_t i = 0; i < rhs_count; i++)
        d.rhs.push_back(read_value(r));
    return d;
}

// -------------------------------------------------------
// Clusterを1つ読む
// -------------------------------------------------------
static Cluster read_cluster(BinaryReader& r) {
    Cluster clu;
    clu.type     = static_cast<CluType>(r.r_u8());
    clu.modifier = static_cast<CluMod>(r.r_u8());
    clu.name     = r.r_str();

    // 継承リスト
    uint32_t inh_count = r.r_u32();
    for (uint32_t i = 0; i < inh_count; i++) {
        InheritEntry e;
        uint8_t flags = r.r_u8();
        e.prime   = (flags & 0x01) != 0;
        e.reserve = (flags & 0x02) != 0;
        e.name    = r.r_str();
        clu.inheritance.push_back(std::move(e));
    }

    // datumリスト
    uint32_t datum_count = r.r_u32();
    for (uint32_t i = 0; i < datum_count; i++) {
        Datum d = read_datum(r);
        // キーのインデックスを構築（lhsのas_str()をキーとする）
        size_t idx = clu.data.size();
        clu.index[d.lhs.as_str()] = idx;
        clu.data.push_back(std::move(d));
    }

    // diffリスト
    uint32_t diff_count = r.r_u32();
    for (uint32_t i = 0; i < diff_count; i++) {
        Diff diff;
        diff.name = r.r_str();
        uint32_t dcnt = r.r_u32();
        for (uint32_t j = 0; j < dcnt; j++) {
            Datum d = read_datum(r);
            size_t idx = diff.data.size();
            diff.index[d.lhs.as_str()] = idx;
            diff.data.push_back(std::move(d));
        }
        clu.diffs.push_back(std::move(diff));
    }

    return clu;
}

// -------------------------------------------------------
// NUQLコンテキスト（メインのアクセスクラス）
// -------------------------------------------------------
class NUQLContext {
    uint8_t  version;
    uint8_t  cc_mode;

    // 全クラスターの保持
    std::vector<Cluster> clusters;

    // 名前→クラスターインデックスの高速アクセス
    std::unordered_map<std::string, size_t> clu_index;

    // meta/wmetaクラスターのインデックスリスト
    std::vector<size_t> meta_indices;
    std::vector<size_t> wmeta_indices;

public:
    // .nuqlbファイルを読み込んでコンテキストを構築する
    void load(const std::string& path) {
        BinaryReader r(path);

        // マジックナンバー確認
        char magic[4];
        // fstreamから直接読むためBinaryReaderを少し拡張
        // ここでは簡易的にr_u32で読んでチェック
        uint32_t magic_val = r.r_u32();
        if (magic_val != 0x4C51554E) { // "NUQL"のリトルエンディアン
            throw std::runtime_error("invalid .nuqlb file");
        }

        version = r.r_u8();
        cc_mode = r.r_u8();
        uint32_t clu_count = r.r_u32();

        // オフセットテーブルを読む
        std::vector<uint64_t> offsets(clu_count);
        for (uint32_t i = 0; i < clu_count; i++)
            offsets[i] = r.r_u64();

        // 各クラスターを読み込む
        clusters.reserve(clu_count);
        for (uint32_t i = 0; i < clu_count; i++) {
            r.seek(offsets[i]);
            Cluster clu = read_cluster(r);

            // meta/wmetaインデックスを記録
            if (clu.type == CluType::META)
                meta_indices.push_back(clusters.size());
            else if (clu.type == CluType::WMETA)
                wmeta_indices.push_back(clusters.size());

            // 名前インデックスに登録（meta/wmetaは名前なし）
            if (!clu.name.empty())
                clu_index[clu.name] = clusters.size();

            clusters.push_back(std::move(clu));
        }
    }

    // -------------------------------------------------------
    // キー検索のコア（1つのclusterのlocalを探す）
    // -------------------------------------------------------
private:
    std::optional<Value> find_in_cluster(const Cluster& clu, const std::string& key) const {
        auto it = clu.index.find(key);
        if (it == clu.index.end()) return std::nullopt;
        const Datum& d = clu.data[it->second];
        if (d.rhs.empty()) return std::nullopt;
        // onewayは1個、twowayは最初の値を返す（L-Parse未実装）
        return d.rhs[0];
    }

    // -------------------------------------------------------
    // メタ/wmetaを全て探す
    // -------------------------------------------------------
    std::optional<Value> find_in_meta(const std::string& key) const {
        // metaを先に探す
        for (size_t idx : meta_indices) {
            auto v = find_in_cluster(clusters[idx], key);
            if (v) return v;
        }
        // wmetaを探す（rejectedは既にロード時に除外済みの想定）
        for (size_t idx : wmeta_indices) {
            auto v = find_in_cluster(clusters[idx], key);
            if (v) return v;
        }
        return std::nullopt;
    }

    // -------------------------------------------------------
    // 継承を辿って探す（遅延評価 + キャッシュ）
    // -------------------------------------------------------
    std::optional<Value> find_with_inheritance(
        const Cluster& clu, const std::string& key) const
    {
        // キャッシュ確認
        auto cache_it = clu.cache.find(key);
        if (cache_it != clu.cache.end())
            return cache_it->second;

        std::optional<Value> result;

        // 1. meta / wmeta（最優先）
        result = find_in_meta(key);
        if (result) {
            clu.cache[key] = result;
            return result;
        }

        // 2. prime inheritance（local primeフラグが立っている親）
        for (const auto& entry : clu.inheritance) {
            if (!entry.prime) continue;
            auto it = clu_index.find(entry.name);
            if (it == clu_index.end()) continue;
            const Cluster& parent = clusters[it->second];
            // prime親はglobal primeかどうかに関わらずここで探す
            result = find_in_cluster(parent, key);
            if (result) {
                clu.cache[key] = result;
                return result;
            }
        }

        // 3. 通常 inheritance（左から順）
        for (const auto& entry : clu.inheritance) {
            if (entry.prime) continue;  // primeは上で処理済み
            auto it = clu_index.find(entry.name);
            if (it == clu_index.end()) continue;
            const Cluster& parent = clusters[it->second];
            result = find_in_cluster(parent, key);
            if (result) {
                clu.cache[key] = result;
                return result;
            }
        }

        // 4. local
        result = find_in_cluster(clu, key);
        clu.cache[key] = result;
        return result;
    }

public:
    // -------------------------------------------------------
    // パブリックAPI: クラスター名とキーで値を取得
    // -------------------------------------------------------
    Value get(const std::string& clu_name, const std::string& key) const {
        auto it = clu_index.find(clu_name);
        if (it == clu_index.end())
            throw std::runtime_error("cluster not found: " + clu_name);

        const Cluster& clu = clusters[it->second];
        auto result = find_with_inheritance(clu, key);
        if (!result)
            throw std::runtime_error("key not found: " + key);
        return *result;
    }

    // -------------------------------------------------------
    // パブリックAPI: diffブランチ内のキーで値を取得
    // -------------------------------------------------------
    Value get_diff(const std::string& clu_name,
                   const std::string& diff_name,
                   const std::string& key) const
    {
        auto it = clu_index.find(clu_name);
        if (it == clu_index.end())
            throw std::runtime_error("cluster not found: " + clu_name);

        const Cluster& clu = clusters[it->second];
        for (const auto& diff : clu.diffs) {
            if (diff.name != diff_name) continue;
            auto dit = diff.index.find(key);
            if (dit == diff.index.end()) break;
            const Datum& d = diff.data[dit->second];
            if (!d.rhs.empty()) return d.rhs[0];
        }
        throw std::runtime_error("key not found in diff: " + key);
    }

    // -------------------------------------------------------
    // パブリックAPI: クラスターが存在するか確認
    // -------------------------------------------------------
    bool has_cluster(const std::string& clu_name) const {
        return clu_index.count(clu_name) > 0;
    }
};
