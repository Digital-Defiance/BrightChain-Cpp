#include "brightchain/v2/codec.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>

namespace brightchain::v2 {

namespace {

constexpr std::uint64_t kMaxSafeInteger = 9007199254740991ULL;
const std::array<std::uint8_t, 4> kMagic = {0x42, 0x44, 0x42, 0x32};
const std::string kZeroWriter(32, '0');

bool isLowerHex(const std::string& s, std::size_t length) {
  if (s.size() != length) return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

/// Strict UTF-8 validation (no overlongs, no surrogates, max U+10FFFF).
bool validUtf8(const std::string& s) {
  const auto* p = reinterpret_cast<const std::uint8_t*>(s.data());
  std::size_t i = 0, n = s.size();
  while (i < n) {
    std::uint8_t c = p[i];
    if (c < 0x80) { i++; continue; }
    std::size_t len;
    std::uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
    else return false;
    if (i + len > n) return false;
    for (std::size_t k = 1; k < len; k++) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
    if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
    i += len;
  }
  return true;
}

class Writer {
 public:
  void bytes(const std::uint8_t* data, std::size_t n) {
    if (buf_.size() + n > kMaxMetadataBytes) throw CodecError("Metadata block exceeds 4096 bytes");
    buf_.insert(buf_.end(), data, data + n);
  }
  void bytes(const Bytes& b) { bytes(b.data(), b.size()); }
  void u8(std::uint8_t v) { bytes(&v, 1); }
  void u16(std::uint16_t v) { std::uint8_t b[2] = {std::uint8_t(v >> 8), std::uint8_t(v)}; bytes(b, 2); }
  void u64(std::uint64_t v) {
    std::uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = std::uint8_t(v >> (56 - 8 * i));
    bytes(b, 8);
  }
  void id(const std::string& hex) {
    if (!isLowerHex(hex, 32)) throw CodecError("An ID must be 32 lowercase hexadecimal digits");
    bytes(fromHex(hex));
  }
  void digest(const Digest& d) {
    if (!isLowerHex(d, 128)) throw CodecError("A digest must be 128 lowercase hexadecimal digits");
    bytes(fromHex(d));
  }
  void ref(const std::optional<Digest>& d) {
    u8(d ? 1 : 0);
    if (d) digest(*d);
  }
  void version(const Version& v) {
    if (v.counter == 0 || v.writerId == kZeroWriter)
      throw CodecError("A version requires a positive counter and a nonzero writer ID");
    u64(v.counter);
    id(v.writerId);
  }
  Bytes finish() { return std::move(buf_); }

 private:
  Bytes buf_;
};

class Reader {
 public:
  explicit Reader(const Bytes& b) : buf_(b) {}
  const std::uint8_t* take(std::size_t n) {
    if (off_ + n > buf_.size()) throw CodecError("Truncated metadata block");
    const auto* p = buf_.data() + off_;
    off_ += n;
    return p;
  }
  std::uint8_t u8() { return *take(1); }
  std::uint16_t u16() { const auto* p = take(2); return std::uint16_t((p[0] << 8) | p[1]); }
  std::uint64_t u64() {
    const auto* p = take(8);
    std::uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
  }
  std::string id() { const auto* p = take(16); return toHex(Bytes(p, p + 16)); }
  Digest digest() { const auto* p = take(kDigestBytes); return toHex(Bytes(p, p + kDigestBytes)); }
  std::optional<Digest> ref() {
    auto tag = u8();
    if (tag == 0) return std::nullopt;
    if (tag == 1) return digest();
    throw CodecError("Invalid reference or record-state tag");
  }
  Version version() {
    Version v;
    v.counter = u64();
    v.writerId = id();
    if (v.counter == 0 || v.writerId == kZeroWriter)
      throw CodecError("A version requires a positive counter and a nonzero writer ID");
    return v;
  }
  std::string key() {
    auto len = u16();
    if (len < 1 || len > kMaxKeyBytes) throw CodecError("A key must occupy 1-1024 UTF-8 bytes");
    const auto* p = take(len);
    return std::string(reinterpret_cast<const char*>(p), len);
  }
  void finish() const {
    if (off_ != buf_.size()) throw CodecError("Trailing bytes after metadata block");
  }

 private:
  const Bytes& buf_;
  std::size_t off_ = 0;
};

void validateBranch(const std::vector<BranchChild>& children) {
  if (children.empty() || children.size() > 16) throw CodecError("A branch must contain 1-16 children");
  unsigned __int128 total = 0;
  int previous = -1;
  for (const auto& c : children) {
    if (c.nibble <= previous || c.nibble > 15) throw CodecError("Invalid or duplicate branch nibble");
    previous = c.nibble;
    if (c.recordBytes == 0) throw CodecError("Branch child record bytes must be positive");
    total += c.recordBytes;
    if (total > UINT64_MAX) throw CodecError("Branch record byte total exceeds the unsigned 64-bit range");
  }
  if (total <= kMaxMetadataBytes - kLeafHeaderBytes)
    throw CodecError("A branch must collapse when its records fit in a leaf");
}

// ---- JSON canonicalization -------------------------------------------------

/// ECMAScript Number::toString for a finite double.
std::string esNumber(double d) {
  if (d == 0) return "0";
  std::string out;
  if (d < 0) { out = "-"; d = -d; }
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);
  std::string sci(buf, res.ptr);  // d[.ddd]e[+-]XX
  auto epos = sci.find('e');
  std::string mant = sci.substr(0, epos);
  int exp10 = std::stoi(sci.substr(epos + 1));
  std::string digits;
  for (char c : mant) if (c != '.') digits.push_back(c);
  int k = int(digits.size());
  int n = exp10 + 1;
  if (k <= n && n <= 21) {
    out += digits + std::string(n - k, '0');
  } else if (0 < n && n <= 21) {
    out += digits.substr(0, n) + "." + digits.substr(n);
  } else if (-6 < n && n <= 0) {
    out += "0." + std::string(-n, '0') + digits;
  } else {
    int e = n - 1;
    std::string es = (e < 0 ? "-" : "+") + std::to_string(std::abs(e));
    out += digits.substr(0, 1);
    if (k > 1) out += "." + digits.substr(1);
    out += "e" + es;
  }
  return out;
}

void appendString(std::string& out, const std::string& s) {
  if (!validUtf8(s)) throw CodecError("Document strings must be well-formed Unicode");
  out.push_back('"');
  static const char* hex = "0123456789abcdef";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
        else out.push_back(char(c));
    }
  }
  out.push_back('"');
}

std::u16string toUtf16(const std::string& s) {
  std::u16string r;
  const auto* p = reinterpret_cast<const std::uint8_t*>(s.data());
  for (std::size_t i = 0; i < s.size();) {
    std::uint32_t cp; std::size_t len;
    if (p[i] < 0x80) { cp = p[i]; len = 1; }
    else if (p[i] < 0xE0) { cp = p[i] & 0x1F; len = 2; }
    else if (p[i] < 0xF0) { cp = p[i] & 0x0F; len = 3; }
    else { cp = p[i] & 0x07; len = 4; }
    for (std::size_t k = 1; k < len; k++) cp = (cp << 6) | (p[i + k] & 0x3F);
    i += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      r.push_back(char16_t(0xD800 + (cp >> 10)));
      r.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
    } else r.push_back(char16_t(cp));
  }
  return r;
}

void canonical(std::string& out, const nlohmann::json& v, std::size_t depth) {
  if (depth > 512) throw CodecError("Document nesting is too deep");
  if (out.size() > kMaxDocumentBytes) throw CodecError("Document payload exceeds 1,048,576 bytes");
  using T = nlohmann::json::value_t;
  switch (v.type()) {
    case T::null: out += "null"; break;
    case T::boolean: out += v.get<bool>() ? "true" : "false"; break;
    case T::string: appendString(out, v.get_ref<const std::string&>()); break;
    case T::number_integer: {
      auto i = v.get<std::int64_t>();
      std::uint64_t mag = i < 0 ? std::uint64_t(-(i + 1)) + 1 : std::uint64_t(i);
      if (mag > kMaxSafeInteger) throw CodecError("Document number is outside the supported range");
      out += std::to_string(i);
      break;
    }
    case T::number_unsigned: {
      auto u = v.get<std::uint64_t>();
      if (u > kMaxSafeInteger) throw CodecError("Document number is outside the supported range");
      out += std::to_string(u);
      break;
    }
    case T::number_float: {
      double d = v.get<double>();
      if (!std::isfinite(d) || (d == std::floor(d) && std::fabs(d) > double(kMaxSafeInteger)))
        throw CodecError("Document number is outside the supported range");
      out += esNumber(d);
      break;
    }
    case T::array: {
      out.push_back('[');
      bool first = true;
      for (const auto& item : v) {
        if (!first) out.push_back(',');
        first = false;
        canonical(out, item, depth + 1);
      }
      out.push_back(']');
      break;
    }
    case T::object: {
      std::vector<std::pair<std::u16string, const std::string*>> keys;
      for (auto it = v.begin(); it != v.end(); ++it) keys.emplace_back(toUtf16(it.key()), &it.key());
      std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      out.push_back('{');
      bool first = true;
      for (const auto& [_, name] : keys) {
        if (!first) out.push_back(',');
        first = false;
        appendString(out, *name);
        out.push_back(':');
        canonical(out, v.at(*name), depth + 1);
      }
      out.push_back('}');
      break;
    }
    default:
      throw CodecError("A document may contain only JSON values");
  }
}

std::string validateDocumentKey(const nlohmann::json& id, const std::optional<std::string>& expected) {
  if (!id.is_string()) throw CodecError("A document must have a string _id");
  auto key = id.get<std::string>();
  encodeKey(key);
  if (expected && *expected != key) throw CodecError("Document _id does not match its primary key");
  return key;
}

}  // namespace

// ---- public ----------------------------------------------------------------

std::string toHex(const Bytes& bytes) {
  static const char* hex = "0123456789abcdef";
  std::string s;
  s.reserve(bytes.size() * 2);
  for (auto b : bytes) { s.push_back(hex[b >> 4]); s.push_back(hex[b & 15]); }
  return s;
}

Bytes fromHex(const std::string& hex) {
  Bytes out;
  out.reserve(hex.size() / 2);
  auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(std::uint8_t((nib(hex[i]) << 4) | nib(hex[i + 1])));
  return out;
}

int compareVersions(const Version& a, const Version& b) {
  if (a.counter != b.counter) return a.counter < b.counter ? -1 : 1;
  int c = a.writerId.compare(b.writerId);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

Digest digestPayload(const Bytes& payload) {
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, payload.data(), payload.size()) != 1 ||
      EVP_DigestFinal_ex(ctx, out, &len) != 1) {
    EVP_MD_CTX_free(ctx);
    throw CodecError("SHA3-512 failed");
  }
  EVP_MD_CTX_free(ctx);
  return toHex(Bytes(out, out + len));
}

Bytes encodeKey(const std::string& key) {
  if (!validUtf8(key)) throw CodecError("A key must be a well-formed Unicode string");
  if (key.empty() || key.size() > kMaxKeyBytes) throw CodecError("A key must occupy 1-1024 UTF-8 bytes");
  return Bytes(key.begin(), key.end());
}

int compareKeys(const std::string& a, const std::string& b) {
  encodeKey(a);
  encodeKey(b);
  int c = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
  if (c != 0) return c < 0 ? -1 : 1;
  return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

std::size_t recordByteLength(const Record& r) {
  return kRecordHeaderBytes + encodeKey(r.key).size() + (r.target ? kDigestBytes : 0);
}

Bytes encodeMetadata(const Metadata& block) {
  Writer w;
  w.bytes(kMagic.data(), kMagic.size());
  if (const auto* d = std::get_if<DatabaseRoot>(&block)) {
    w.u8(0x01); w.id(d->databaseId); w.u64(d->clock); w.ref(d->catalog);
  } else if (const auto* c = std::get_if<CollectionRoot>(&block)) {
    w.u8(0x02); w.version(c->generation); w.ref(c->primary);
  } else if (const auto* b = std::get_if<Branch>(&block)) {
    w.u8(0x03);
    auto children = b->children;
    std::sort(children.begin(), children.end(), [](auto& x, auto& y) { return x.nibble < y.nibble; });
    validateBranch(children);
    std::uint16_t bitmap = 0;
    for (auto& ch : children) bitmap |= std::uint16_t(1u << ch.nibble);
    w.u16(bitmap);
    for (auto& ch : children) { w.u64(ch.recordBytes); w.digest(ch.digest); }
  } else {
    const auto& leaf = std::get<Leaf>(block);
    if (leaf.records.empty()) throw CodecError("A leaf must contain at least one record");
    if (kLeafHeaderBytes + leaf.records.size() * (kRecordHeaderBytes + 1) > kMaxMetadataBytes)
      throw CodecError("Metadata block exceeds 4096 bytes");
    w.u8(leaf.kind == LeafKind::Primary ? 0x04 : 0x05);
    auto records = leaf.records;
    std::sort(records.begin(), records.end(),
              [](const Record& x, const Record& y) { return compareKeys(x.key, y.key) < 0; });
    w.u16(std::uint16_t(records.size()));
    const std::string* prev = nullptr;
    for (const auto& r : records) {
      if (prev && compareKeys(*prev, r.key) == 0) throw CodecError("Duplicate leaf key");
      prev = &r.key;
      auto k = encodeKey(r.key);
      w.u16(std::uint16_t(k.size()));
      w.bytes(k);
      w.version(r.version);
      w.ref(r.target);
    }
  }
  return w.finish();
}

Metadata decodeMetadata(const Bytes& bytes, const std::optional<Digest>& expected) {
  if (bytes.size() > kMaxMetadataBytes) throw CodecError("Metadata block exceeds 4096 bytes");
  if (expected && digestPayload(bytes) != *expected) throw CodecError("Block digest mismatch");
  Reader r(bytes);
  const auto* magic = r.take(4);
  if (std::memcmp(magic, kMagic.data(), 4) != 0) throw CodecError("Invalid metadata block header");
  auto kind = r.u8();
  Metadata result;
  switch (kind) {
    case 0x01: {
      DatabaseRoot d;
      d.databaseId = r.id(); d.clock = r.u64(); d.catalog = r.ref();
      result = d;
      break;
    }
    case 0x02: {
      CollectionRoot c;
      c.generation = r.version(); c.primary = r.ref();
      result = c;
      break;
    }
    case 0x03: {
      auto bitmap = r.u16();
      Branch b;
      for (int n = 0; n < 16; n++) {
        if (bitmap & (1u << n)) {
          BranchChild ch;
          ch.nibble = n; ch.recordBytes = r.u64(); ch.digest = r.digest();
          b.children.push_back(ch);
        }
      }
      validateBranch(b.children);
      result = b;
      break;
    }
    case 0x04:
    case 0x05: {
      Leaf leaf;
      leaf.kind = kind == 0x04 ? LeafKind::Primary : LeafKind::Catalog;
      auto count = r.u16();
      if (count == 0) throw CodecError("A leaf must contain at least one record");
      for (int i = 0; i < count; i++) {
        Record rec;
        rec.key = r.key();
        if (!leaf.records.empty() && compareKeys(leaf.records.back().key, rec.key) >= 0)
          throw CodecError("Leaf keys must be unique and in ascending order");
        if (!validUtf8(rec.key)) throw CodecError("Malformed UTF-8");
        rec.version = r.version();
        rec.target = r.ref();
        leaf.records.push_back(std::move(rec));
      }
      result = std::move(leaf);
      break;
    }
    default:
      throw CodecError("Unknown metadata block kind");
  }
  r.finish();
  return result;
}

EncodedDocument encodeDocumentForWrite(const nlohmann::json& value, const std::string& generatedKey,
                                       const std::optional<std::string>& expectedKey) {
  if (!value.is_object()) throw CodecError("A document must be a JSON object");
  nlohmann::json doc = value;
  std::string key;
  auto it = doc.find("_id");
  if (it != doc.end()) {
    key = validateDocumentKey(*it, expectedKey);
  } else {
    // Validate the other values first, as the reference does, then default the key.
    std::string scratch;
    nlohmann::json copy = doc;
    copy.erase("_id");
    canonical(scratch, copy, 0);
    key = validateDocumentKey(expectedKey ? nlohmann::json(*expectedKey) : nlohmann::json(generatedKey),
                              std::nullopt);
    doc["_id"] = key;
  }
  std::string out;
  canonical(out, doc, 0);
  if (out.size() > kMaxDocumentBytes) throw CodecError("Document payload exceeds 1,048,576 bytes");
  return {key, Bytes(out.begin(), out.end())};
}

Bytes encodeDocument(const nlohmann::json& value, const std::optional<std::string>& expectedKey) {
  if (!value.is_object()) throw CodecError("A document must be a JSON object");
  if (!value.contains("_id")) throw CodecError("A document must have a string _id");
  return encodeDocumentForWrite(value, "", expectedKey).payload;
}

nlohmann::json decodeDocument(const Bytes& bytes, const std::optional<std::string>& expectedKey,
                              const std::optional<Digest>& expectedDigest) {
  if (bytes.size() > kMaxDocumentBytes) throw CodecError("Document payload exceeds 1,048,576 bytes");
  if (expectedDigest && digestPayload(bytes) != *expectedDigest) throw CodecError("Block digest mismatch");
  nlohmann::json value;
  try {
    value = nlohmann::json::parse(bytes.begin(), bytes.end());
  } catch (const nlohmann::json::exception&) {
    throw CodecError("Invalid document JSON");
  }
  if (encodeDocument(value, expectedKey) != bytes) throw CodecError("Document JSON is not canonical");
  return value;
}

}  // namespace brightchain::v2
