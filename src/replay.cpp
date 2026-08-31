#include "chaoslab/replay.h"

#include "chaoslab/text.h"

namespace chaoslab {

const char* replay_mode_name(ReplayMode m) noexcept {
  switch (m) {
    case ReplayMode::PLAN_REPLAY: return "plan_replay";
    case ReplayMode::EVIDENCE_REPLAY: return "evidence_replay";
    case ReplayMode::COMPARE: return "compare";
  }
  return "unknown";
}

std::string ReplayReport::summary() const {
  std::string s = std::string(replay_mode_name(mode)) + ":";
  s += std::string(" plan=") + (same_plan ? "same" : "DIFF");
  s += std::string(" injections=") + (same_injections ? "same" : "DIFF");
  s += std::string(" assertions=") + (same_assertions ? "same" : "DIFF");
  s += std::string(" terminal=") + (same_terminal_state ? "same" : "DIFF");
  s += std::string(" digest=") + (same_state_digest ? "same" : "DIFF");
  if (!differences.empty()) s += " (" + std::to_string(differences.size()) + " differences)";
  return s;
}

ReplayReport compare_runs(const std::vector<EvidenceRecord>& a,
                          const std::vector<EvidenceRecord>& b) {
  ReplayReport r;
  r.mode = ReplayMode::COMPARE;

  // Compare structural identity and contents.
  if (a.size() != b.size()) {
    r.same_plan = false;
    r.differences.push_back("evidence record count differs: " + std::to_string(a.size()) +
                            " vs " + std::to_string(b.size()));
  }

  std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i].id.value() != b[i].id.value() || a[i].kind != b[i].kind ||
        a[i].phase != b[i].phase || a[i].to_text() != b[i].to_text()) {
      r.same_plan = false;
      r.differences.push_back("record " + std::to_string(i) + " differs");
    }
  }

  // Count injections and assertion outcomes by kind/status.
  auto count_kind = [&](const std::vector<EvidenceRecord>& v, EvidenceKind k) {
    std::size_t c = 0;
    for (auto& ev : v) if (ev.kind == k) ++c;
    return c;
  };
  r.same_injections = count_kind(a, EvidenceKind::INJECTION) == count_kind(b, EvidenceKind::INJECTION);
  if (!r.same_injections) r.differences.push_back("injection counts differ");
  r.same_assertions = count_kind(a, EvidenceKind::ASSERTION) == count_kind(b, EvidenceKind::ASSERTION);
  if (!r.same_assertions) r.differences.push_back("assertion counts differ");

  // Terminal state: compare FINAL_RESULT fields.
  std::string term_a, term_b;
  for (auto& ev : a) if (ev.kind == EvidenceKind::FINAL_RESULT) term_a = ev.field_or("phase", "");
  for (auto& ev : b) if (ev.kind == EvidenceKind::FINAL_RESULT) term_b = ev.field_or("phase", "");
  if (term_a != term_b) {
    r.same_terminal_state = false;
    r.differences.push_back("terminal phase differs: " + term_a + " vs " + term_b);
  }

  // State digest across the two streams.
  EvidenceRecorder ra, rb;
  for (auto& ev : a) ra.record_raw(ev);
  for (auto& ev : b) rb.record_raw(ev);
  if (ra.digest() != rb.digest()) {
    r.same_state_digest = false;
    r.differences.push_back("state digest differs");
  }
  return r;
}

ReplayReport replay_evidence(const std::vector<EvidenceRecord>& records,
                             const Digest256& expected_digest) {
  ReplayReport r;
  r.mode = ReplayMode::EVIDENCE_REPLAY;
  EvidenceRecorder rec;
  for (auto& ev : records) rec.record_raw(ev);
  Digest256 actual = rec.digest();
  r.same_state_digest = (actual == expected_digest);
  if (!r.same_state_digest) {
    r.differences.push_back("recomputed digest differs: " + actual.hex() +
                            " vs expected " + expected_digest.hex());
  }
  return r;
}

ReplayReport replay_plan(const ChaosCampaign& campaign,
                         const std::vector<std::string>& events,
                         const std::vector<std::string>& expected_injections) {
  ReplayReport r;
  r.mode = ReplayMode::PLAN_REPLAY;
  FaultScheduler sched(campaign.seed);
  sched.plan(campaign);
  auto& plan = sched.schedule();

  std::vector<std::string> fired;
  if (events.empty()) {
    for (auto& si : plan) fired.push_back(si.id.to_string());
  } else {
    for (auto& e : events) {
      auto ids = sched.observe_event(e);
      for (auto& id : ids) fired.push_back(id.to_string());
    }
  }

  if (fired.size() != expected_injections.size()) {
    r.same_injections = false;
    r.differences.push_back("injection count differs: " + std::to_string(fired.size()) +
                            " vs " + std::to_string(expected_injections.size()));
  } else {
    for (std::size_t i = 0; i < fired.size(); ++i) {
      if (fired[i] != expected_injections[i]) {
        r.same_injections = false;
        r.differences.push_back("injection " + std::to_string(i) + " differs");
      }
    }
  }
  return r;
}

} // namespace chaoslab
