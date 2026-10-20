#include <qce/grammar/Morphology.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

namespace qce::grammar {

namespace {

constexpr char kMagic[8] = {'S', 'P', 'M', 'O', 'R', 'P', 'H', '2'};
constexpr uint32_t kInfoWidth = 3;  // bytes of an info number in a reading
// The flags byte of an arc: the last arc of its state; the target is the end
// (no address); the target follows this state's arcs (no address); and in
// the low 5 bits the number of a frequent label, or 0 when the label byte
// follows.
constexpr uint8_t kLastFlag = 0x80, kEndFlag = 0x40, kNextFlag = 0x20, kLabelMask = 0x1F;
constexpr size_t kFrequentLabels = 32;  // number 0 means none
constexpr uint32_t kNoState = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kEndState = kNoState - 1;
// The most bytes cut at the start of a form for its lemma ("naj", "nie").
constexpr size_t kMaxPrefixCut = 6;

// The lemma as made of the form: bytes cut at the start (p) and the end (k),
// each plus one (never 0, the separator), then the bytes added at the end;
// the cut at the start chosen so that the fewest bytes are added. Empty when
// a cut is too long for a byte.
std::string lemmaCode(std::string_view form, std::string_view lemma) {
    size_t bestPrefix = 0, bestCommon = 0, bestAdded = std::numeric_limits<size_t>::max();
    for (size_t p = 0; p <= std::min(kMaxPrefixCut, form.size()); ++p) {
        const std::string_view rest = form.substr(p);
        size_t common = 0;
        while (common < rest.size() && common < lemma.size() && rest[common] == lemma[common]) ++common;
        if (lemma.size() - common < bestAdded) {
            bestAdded = lemma.size() - common;
            bestPrefix = p;
            bestCommon = common;
        }
    }
    const size_t cutEnd = form.size() - bestPrefix - bestCommon;
    if (cutEnd + 1 > 255) return {};
    std::string code;
    code += char(bestPrefix + 1);
    code += char(cutEnd + 1);
    code += lemma.substr(bestCommon);
    return code;
}

// Builds a minimal acyclic automaton of byte strings added in sorted order,
// none a prefix of another (Daciuk, Mihov, Watson, Watson 2000). The states
// are numbered as registered; state 0 is the end (no arcs).
class AutomatonBuilder {
public:
    using Arcs = std::vector<std::pair<uint8_t, uint32_t>>;

    AutomatonBuilder() {
        m_states.emplace_back();
        m_register.emplace(std::string(), 0);
        m_path.emplace_back();
    }

    // False when the string is not after the previous one or is a prefix of it, or the reverse.
    bool add(std::string_view word) {
        size_t common = 0;
        while (common < word.size() && common < m_previous.size() && word[common] == m_previous[common]) ++common;
        if (!m_previous.empty() && (common == word.size() || common == m_previous.size() || word < m_previous))
            return false;
        minimize(common);
        for (size_t i = common; i < word.size(); ++i) {
            m_path.back().push_back({uint8_t(word[i]), kNoState});
            m_path.emplace_back();
        }
        m_previous.assign(word);
        return true;
    }

    // The root; the automaton is complete.
    uint32_t finish() {
        minimize(0);
        return registered(m_path.front());
    }

    const std::vector<Arcs>& states() const { return m_states; }

private:
    // Registers the states of the path after `depth`: each replaced by an
    // equal registered one, or registered itself.
    void minimize(size_t depth) {
        while (m_path.size() > depth + 1) {
            const uint32_t id = registered(m_path.back());
            m_path.pop_back();
            m_path.back().back().second = id;
        }
    }

    uint32_t registered(const Arcs& arcs) {
        std::string key;
        key.reserve(arcs.size() * 5);
        for (const auto& [label, target] : arcs) {
            key += char(label);
            key.append(reinterpret_cast<const char*>(&target), sizeof target);
        }
        const auto [it, added] = m_register.try_emplace(std::move(key), uint32_t(m_states.size()));
        if (added) m_states.push_back(arcs);
        return it->second;
    }

    std::vector<Arcs> m_states;
    std::unordered_map<std::string, uint32_t> m_register;
    std::vector<Arcs> m_path;  // the states of the previous string, not registered yet
    std::string m_previous;
};

void writeU32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out += char((value >> (8 * i)) & 0xFF);
}

// An address in 7-bit groups, the lowest first, the high bit "more follow".
void writeVarint(std::string& out, uint32_t value) {
    while (value >= 0x80) {
        out += char((value & 0x7F) | 0x80);
        value >>= 7;
    }
    out += char(value);
}

size_t varintSize(uint32_t value) {
    size_t size = 1;
    while (value >= 0x80) {
        value >>= 7;
        ++size;
    }
    return size;
}

uint32_t readU32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

} // namespace

uint32_t MorphologyBuilder::stringId(std::string_view text) {
    const auto [it, added] = m_stringIds.try_emplace(std::string(text), uint32_t(m_strings.size()));
    if (added) m_strings.emplace_back(text);
    return it->second;
}

bool MorphologyBuilder::add(std::string_view form, std::string_view lemma, std::string_view tag,
                            std::string_view label, std::string_view qualifiers) {
    const std::string code = lemmaCode(form, lemma);
    if (code.empty() || form.empty() || form.find('\0') != std::string_view::npos) return false;
    ++m_entries;
    const std::tuple info{stringId(tag), stringId(label), stringId(qualifiers)};
    const auto [it, added] = m_infoIds.try_emplace(info, uint32_t(m_infos.size()));
    if (added) m_infos.push_back(info);
    std::string reading(form);
    reading += '\0';
    reading += code;
    reading += '\0';
    for (uint32_t i = kInfoWidth; i-- > 0;) reading += char((it->second >> (8 * i)) & 0xFF);
    m_readings.push_back(std::move(reading));
    return true;
}

bool MorphologyBuilder::write(const std::string& outputPath, MorphologyStats* stats, std::string* error) {
    MorphologyStats numbers;
    numbers.entries = m_entries;
    const std::vector<std::string>& strings = m_strings;
    const auto& infos = m_infos;
    std::vector<std::string>& readings = m_readings;
    if (infos.size() >= (1u << (8 * kInfoWidth))) {
        if (error) *error = "too many (tag, label, qualifiers)";
        return false;
    }
    std::sort(readings.begin(), readings.end());
    readings.erase(std::unique(readings.begin(), readings.end()), readings.end());
    numbers.readings = long(readings.size());
    numbers.infos = long(infos.size());

    AutomatonBuilder builder;
    for (const std::string& reading : readings)
        if (!builder.add(reading)) {
            if (error) *error = "a reading is a prefix of another";
            return false;
        }
    readings.clear();
    readings.shrink_to_fit();
    const uint32_t root = builder.finish();

    const auto& states = builder.states();
    numbers.states = long(states.size());
    for (const auto& arcs : states) numbers.arcs += long(arcs.size());

    // The most frequent labels get a number in the flags byte of an arc.
    std::vector<std::pair<long, uint8_t>> labelCounts(256);
    for (size_t c = 0; c < 256; ++c) labelCounts[c].second = uint8_t(c);
    for (const auto& arcs : states)
        for (const auto& [label, target] : arcs) ++labelCounts[label].first;
    std::sort(labelCounts.rbegin(), labelCounts.rend());
    std::array<uint8_t, kFrequentLabels> frequent{};
    std::array<uint8_t, 256> labelIndex{};  // 1 + index into `frequent`, 0 when written out
    for (size_t i = 0; i + 1 < kFrequentLabels && labelCounts[i].first > 0; ++i) {
        frequent[i + 1] = labelCounts[i].second;
        labelIndex[labelCounts[i].second] = uint8_t(i + 1);
    }

    // The order of the states: depth first from the root, the first child
    // not placed yet right after its parent, reached without an address.
    std::vector<uint32_t> order;
    std::vector<int32_t> nextArc(states.size(), -1);  // the arc whose target follows the state
    std::vector<char> placed(states.size(), 0);
    order.reserve(states.size());
    std::vector<std::pair<uint32_t, size_t>> stack{{root, 0}};
    placed[root] = 1;
    order.push_back(root);
    while (!stack.empty()) {
        auto& [state, arc] = stack.back();
        if (arc == states[state].size()) {
            stack.pop_back();
            continue;
        }
        const uint32_t target = states[state][arc].second;
        const size_t index = arc++;
        if (target == 0 || placed[target]) continue;
        // Right after its parent when it is the parent's first child placed:
        // nothing else was placed since the parent.
        if (order.back() == state) nextArc[state] = int32_t(index);
        placed[target] = 1;
        order.push_back(target);
        stack.push_back({target, 0});
    }

    // Offsets of the states; addresses written in 7-bit groups get longer as
    // offsets grow, so repeated until nothing changes.
    std::vector<uint32_t> offset(states.size(), 0);
    const auto arcBytes = [&](uint32_t state, size_t a) {
        const auto [label, target] = states[state][a];
        size_t bytes = 1 + (labelIndex[label] ? 0 : 1);
        if (target != 0 && int32_t(a) != nextArc[state]) bytes += varintSize(offset[target]);
        return bytes;
    };
    uint64_t total = 0;
    for (bool changed = true; changed;) {
        changed = false;
        uint64_t at = 0;
        for (const uint32_t state : order) {
            if (offset[state] != at) {
                offset[state] = uint32_t(at);
                changed = true;
            }
            for (size_t a = 0; a < states[state].size(); ++a) at += arcBytes(state, a);
        }
        total = at;
        if (total >= (1ull << 32)) {
            if (error) *error = "the automaton is too large";
            return false;
        }
    }
    std::string automaton;
    automaton.reserve(size_t(total));
    for (const uint32_t state : order)
        for (size_t a = 0; a < states[state].size(); ++a) {
            const auto [label, target] = states[state][a];
            uint8_t flags = labelIndex[label];
            if (a + 1 == states[state].size()) flags |= kLastFlag;
            if (target == 0) flags |= kEndFlag;
            else if (int32_t(a) == nextArc[state]) flags |= kNextFlag;
            automaton += char(flags);
            if (!labelIndex[label]) automaton += char(label);
            if (target != 0 && int32_t(a) != nextArc[state]) writeVarint(automaton, offset[target]);
        }

    std::string out(kMagic, sizeof kMagic);
    writeU32(out, kInfoWidth);
    writeU32(out, uint32_t(strings.size()));
    for (const std::string& s : strings) {
        writeU32(out, uint32_t(s.size()));
        out += s;
    }
    writeU32(out, uint32_t(infos.size()));
    for (const auto& [tag, label, qualifiers] : infos) {
        writeU32(out, tag);
        writeU32(out, label);
        writeU32(out, qualifiers);
    }
    out.append(reinterpret_cast<const char*>(frequent.data()), frequent.size());
    writeU32(out, offset[root]);
    writeU32(out, uint32_t(automaton.size()));
    out += automaton;
    std::ofstream file(outputPath, std::ios::binary);
    if (!file || !file.write(out.data(), std::streamsize(out.size()))) {
        if (error) *error = "cannot write " + outputPath;
        return false;
    }
    numbers.fileSize = long(out.size());
    if (stats) *stats = numbers;
    return true;
}

bool Morphology::load(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto fail = [&](const char* what) {
        if (error) *error = path + ": " + what;
        return false;
    };
    if (!file && bytes.empty()) return fail("cannot read");
    if (bytes.size() < sizeof kMagic || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0)
        return fail("not a morphology file");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data()) + sizeof kMagic;
    const uint8_t* end = reinterpret_cast<const uint8_t*>(bytes.data()) + bytes.size();
    bool ok = true;
    const auto u32 = [&]() -> uint32_t {
        if (end - p < 4) {
            ok = false;
            return 0;
        }
        const uint32_t value = readU32(p);
        p += 4;
        return value;
    };
    m_infoWidth = u32();
    const uint32_t stringCount = u32();
    m_strings.clear();
    for (uint32_t i = 0; ok && i < stringCount; ++i) {
        const uint32_t size = u32();
        if (!ok || uint32_t(end - p) < size) return fail("truncated");
        m_strings.emplace_back(reinterpret_cast<const char*>(p), size);
        p += size;
    }
    const uint32_t infoCount = u32();
    m_infos.clear();
    for (uint32_t i = 0; ok && i < infoCount; ++i) {
        const Info info{u32(), u32(), u32()};
        if (info.tag >= m_strings.size() || info.label >= m_strings.size() || info.qualifiers >= m_strings.size())
            return fail("bad info");
        m_infos.push_back(info);
    }
    if (!ok || uint32_t(end - p) < kFrequentLabels) return fail("truncated");
    std::copy(p, p + kFrequentLabels, m_labels.begin());
    p += kFrequentLabels;
    m_root = u32();
    const uint32_t size = u32();
    if (!ok || m_infoWidth == 0 || m_infoWidth > 4 || uint64_t(end - p) != size || m_root >= size)
        return fail("truncated");
    m_automaton.assign(p, end);
    return true;
}

Morphology::Arc Morphology::arcAt(size_t pos) const {
    Arc arc;
    const size_t size = m_automaton.size();
    if (pos >= size) return arc;
    const uint8_t flags = m_automaton[pos++];
    arc.last = flags & kLastFlag;
    arc.nextState = flags & kNextFlag;
    if (flags & kLabelMask) {
        arc.label = m_labels[flags & kLabelMask];
    } else {
        if (pos >= size) return arc;
        arc.label = m_automaton[pos++];
    }
    if (flags & kEndFlag) {
        arc.target = kEndState;
    } else if (!arc.nextState) {
        uint32_t value = 0;
        for (int shift = 0; pos < size && shift < 35; shift += 7) {
            const uint8_t b = m_automaton[pos++];
            value |= uint32_t(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
        }
        arc.target = value;
    }
    arc.end = pos;
    arc.valid = true;
    return arc;
}

size_t Morphology::arcsEnd(size_t state) const {
    for (size_t pos = state;;) {
        const Arc arc = arcAt(pos);
        if (!arc.valid) return m_automaton.size();
        if (arc.last) return arc.end;
        pos = arc.end;
    }
}

uint32_t Morphology::next(uint32_t state, uint8_t label) const {
    if (state >= kEndState) return kNoState;
    for (size_t pos = state;;) {
        const Arc arc = arcAt(pos);
        if (!arc.valid) return kNoState;
        if (arc.label == label) return arc.nextState ? uint32_t(arcsEnd(arc.end - 1 + 1 == pos ? pos : state)) : arc.target;
        if (arc.label > label || arc.last) return kNoState;
        pos = arc.end;
    }
}

void Morphology::paths(uint32_t state, std::string& path, const std::function<void(const std::string&)>& onPath) const {
    if (state == kEndState) {
        onPath(path);
        return;
    }
    if (state >= kEndState || path.size() > 1024) return;
    for (size_t pos = state;;) {
        const Arc arc = arcAt(pos);
        if (!arc.valid) return;
        path += char(arc.label);
        paths(arc.nextState ? uint32_t(arcsEnd(state)) : arc.target, path, onPath);
        path.pop_back();
        if (arc.last) break;
        pos = arc.end;
    }
}

bool Morphology::contains(std::string_view form) const {
    uint32_t state = m_root;
    for (const char c : form)
        if ((state = next(state, uint8_t(c))) == kNoState) return false;
    return next(state, 0) != kNoState;
}

std::vector<Reading> Morphology::analyse(std::string_view form) const {
    std::vector<Reading> result;
    if (m_automaton.empty() || form.empty()) return result;
    uint32_t state = m_root;
    for (const char c : form)
        if ((state = next(state, uint8_t(c))) == kNoState) return result;
    if ((state = next(state, 0)) == kNoState) return result;
    std::string path;
    paths(state, path, [&](const std::string& code) {
        // p + 1, k + 1, added bytes, separator, the info number.
        const size_t separator = code.find('\0', 2);
        if (code.size() < 3 || separator == std::string::npos || code.size() != separator + 1 + m_infoWidth) return;
        const size_t cutStart = uint8_t(code[0]) - 1u, cutEnd = uint8_t(code[1]) - 1u;
        if (cutStart + cutEnd > form.size()) return;
        uint32_t infoId = 0;
        for (size_t i = separator + 1; i < code.size(); ++i) infoId = infoId << 8 | uint8_t(code[i]);
        if (infoId >= m_infos.size()) return;
        const Info& info = m_infos[infoId];
        Reading reading;
        reading.lemma.assign(form.substr(cutStart, form.size() - cutStart - cutEnd));
        reading.lemma.append(code, 2, separator - 2);
        reading.tag = m_strings[info.tag];
        reading.label = m_strings[info.label];
        reading.qualifiers = m_strings[info.qualifiers];
        result.push_back(std::move(reading));
    });
    return result;
}

} // namespace qce::grammar
