#include "chaoslab/evidence.h"

#include "chaoslab/text.h"

#include <array>

namespace chaoslab {

namespace {
constexpr std::array<const char*, static_cast<std::size_t>(EvidenceKind::Last)> kKindNames = {
  "campaign_definition","process_launch","process_exit","stdout_stderr","protocol_event",
  "injection","assertion","state_snapshot","authority_envelope","persistence_digest",
  "recovery_event","cleanup","final_result","machine_info","target_version","seed","misc"
};

std::string json_escape(std::string_view v) {
  static const char* hexd = "0123456789abcdef";
  std::string out;
  out.reserve(v.size() + 2);
  for (char c : v) {
    unsigned char u = static_cast<unsigned char>(c);
    if (c == '"')       { out.push_back('\\'); out.push_back('"'); }
    else if (c == '\\') { out.push_back('\\'); out.push_back('\\'); }
    else if (u < 0x20)  {
      out.push_back('\\'); out.push_back('u'); out.push_back('0'); out.push_back('0');
      out.push_back(hexd[(u >> 4) & 15]); out.push_back(hexd[u & 15]);
    } else out.push_back(c);
  }
  return out;
}
} // namespace

std::string esc(std::string_view v) {
  std::string o; o.reserve(v.size());
  for (char c : v) {
    if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r"; else if (c == '|') o += "\\|"; else o += c;
  }
  return o;
}

std::string unesc(std::string_view v) {
  std::string o; o.reserve(v.size());
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      char n = v[i + 1];
      if (n == 'n') { o += '\n'; ++i; }
      else if (n == 'r') { o += '\r'; ++i; }
      else if (n == '|') { o += '|'; ++i; }
      else { o += v[i]; }
    } else o += v[i];
  }
  return o;
}

const char* evidence_kind_name(EvidenceKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < kKindNames.size() ? kKindNames[i] : "unknown";
}

void EvidenceRecord::set(std::string key, std::string value) { fields[std::move(key)] = std::move(value); }

std::string EvidenceRecord::field_or(std::string_view key, std::string fallback) const {
  auto it = fields.find(std::string(key));
  return it == fields.end() ? std::move(fallback) : it->second;
}

std::string EvidenceRecord::to_text() const {
  std::string out;
  out += to_hex(static_cast<std::uint64_t>(seq));
  out.push_back('|');
  out += evidence_kind_name(kind);
  out.push_back('|');
  out += phase;
  out.push_back('|');
  bool first = true;
  for (auto& [k, v] : fields) {
    if (!first) out.push_back(',');
    first = false;
    out += esc(k); out.push_back('='); out += esc(v);
  }
  out.push_back('|');
  out += esc(payload);
  return out;
}

std::vector<EvidenceRecord> EvidenceRecorder::snapshot() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return records_;
}
bool EvidenceRecorder::empty() const noexcept { std::lock_guard<std::mutex> lk(mtx_); return records_.empty(); }
std::size_t EvidenceRecorder::size() const noexcept { std::lock_guard<std::mutex> lk(mtx_); return records_.size(); }
std::int64_t EvidenceRecorder::next_seq() const noexcept { std::lock_guard<std::mutex> lk(mtx_); return next_seq_; }

EvidenceId EvidenceRecorder::record(EvidenceKind kind, std::string phase,
                                    std::map<std::string, std::string> fields,
                                    std::string payload) {
  std::lock_guard<std::mutex> lk(mtx_);
  EvidenceRecord ev;
  ev.id = EvidenceId(static_cast<std::uint64_t>(next_seq_ + 1));
  ev.kind = kind;
  ev.seq = next_seq_++;
  ev.phase = std::move(phase);
  ev.fields = std::move(fields);
  ev.payload = std::move(payload);
  records_.push_back(std::move(ev));
  return records_.back().id;
}

void EvidenceRecorder::record_raw(EvidenceRecord ev) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (ev.seq >= next_seq_) next_seq_ = ev.seq + 1;
  records_.push_back(std::move(ev));
}

std::string EvidenceRecorder::serialize_text() const {
  std::lock_guard<std::mutex> lk(mtx_);
  std::string out;
  for (auto& ev : records_) {
    out += to_hex(ev.id.value());
    out.push_back(':');
    out += ev.to_text();
    out.push_back('\n');
  }
  return out;
}

namespace {
void json_field(std::string& out, std::string_view name, std::string_view value) {
  out.push_back('"'); out += name; out.push_back('"');
  out.push_back(':');
  out.push_back('"'); out += json_escape(value); out.push_back('"');
}
void json_number(std::string& out, std::string_view name, std::uint64_t v) {
  out.push_back('"'); out += name; out.push_back('"');
  out.push_back(':');
  out += std::to_string(v);
}
} // namespace

std::string EvidenceRecorder::serialize_json() const {
  std::lock_guard<std::mutex> lk(mtx_);
  std::string out = "[";
  bool first = true;
  for (auto& ev : records_) {
    if (!first) out.push_back(',');
    first = false;
    out.push_back('{');
    json_number(out, "id", ev.id.value());
    out.push_back(',');
    json_number(out, "seq", static_cast<std::uint64_t>(ev.seq));
    out.push_back(',');
    json_field(out, "kind", evidence_kind_name(ev.kind));
    out.push_back(',');
    json_field(out, "phase", ev.phase);
    out.push_back(',');
    json_number(out, "timestamp_ns", ev.timestamp_ns);
    out.push_back(',');
    out.push_back('"'); out += "fields"; out.push_back('"'); out.push_back(':');
    out.push_back('{');
    bool f2 = true;
    for (auto& [k, v] : ev.fields) {
      if (!f2) out.push_back(',');
      f2 = false;
      json_field(out, k, v);
    }
    out.push_back('}');
    out.push_back(',');
    json_field(out, "payload", ev.payload);
    out.push_back('}');
  }
  out.push_back(']');
  return out;
}

Digest256 EvidenceRecorder::digest() const {
  std::lock_guard<std::mutex> lk(mtx_);
  Sha256 h;
  for (auto& ev : records_) {
    h.update(ev.to_text());
    h.update("\n");
  }
  return h.finish();
}

bool EvidenceRecorder::parse_text(std::string_view text, std::vector<EvidenceRecord>& out) {
  out.clear();
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t nl = text.find('\n', pos);
    std::string line = trim(text.substr(pos, nl == std::string_view::npos ? text.size() - pos : nl - pos));
    pos = (nl == std::string_view::npos) ? text.size() : nl + 1;
    if (line.empty()) continue;
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::uint64_t idv = 0;
    if (!from_hex(line.substr(0, colon), idv)) return false;
    std::string_view rest = std::string_view(line).substr(colon + 1);
    std::vector<std::string> parts = split(rest, '|');
    if (parts.size() != 5) return false;
    EvidenceRecord ev;
    ev.id = EvidenceId(idv);
    std::uint64_t seq = 0;
    if (!from_hex(parts[0], seq)) return false;
    ev.seq = static_cast<std::int64_t>(seq);
    ev.kind = EvidenceKind::MISC;
    for (std::size_t i = 0; i < kKindNames.size(); ++i) if (parts[1] == kKindNames[i]) { ev.kind = static_cast<EvidenceKind>(i); break; }
    ev.phase = unesc(parts[2]);
    if (!parts[3].empty()) {
      for (auto& item : split(parts[3], ',')) {
        std::size_t eq = item.find('=');
        if (eq == std::string::npos) return false;
        ev.fields[unesc(item.substr(0, eq))] = unesc(item.substr(eq + 1));
      }
    }
    ev.payload = unesc(parts[4]);
    out.push_back(std::move(ev));
  }
  return true;
}

} // namespace chaoslab
