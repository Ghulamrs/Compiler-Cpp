#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iosfwd>
#include <string>
#include <vector>

struct Str {
    const char *p = "";
    std::size_t n = 0;

    Str() = default;

    Str(const char *s) : p(s), n(0) {
        assert(s != nullptr);
        n = std::strlen(s);
    }
    Str(const std::string &s) : p(s.data()), n(s.size()) {}
    Str(const char *s, std::size_t len) : p(s), n(len) {}

    Str substr(std::size_t from) const {
        assert(from <= n);
        return Str(p + from, n - from);
    }
    bool startsWith(const char *s) const {
        std::size_t m = std::strlen(s);
        return n >= m && std::memcmp(p, s, m) == 0;
    }

    explicit operator std::string() const { return std::string(p, n); }
};

inline std::string &operator+=(std::string &s, Str v) {
    return s.append(v.p, v.n);
}

struct Op {
    enum Kind {
        Reg,
        Imm,
        Mem,
        Rip,
        Ind,
        Lbl,
    };
    Kind kind;

    Str text;
    long long disp = 0;
    bool hasDisp = false;

    unsigned long long uimm = 0;
    bool immNeg = false;
    bool immNumeric = false;
};

inline Op reg(Str r)  { return { Op::Reg, r, 0, false, 0, false, false }; }
inline Op imm(long long v) {
    Op o { Op::Imm, {}, 0, false, 0, false, true };
    if (v < 0) { o.immNeg = true; o.uimm = 0ULL - static_cast<unsigned long long>(v); }
    else o.uimm = static_cast<unsigned long long>(v);
    return o;
}
inline Op imm(unsigned long long v) { return { Op::Imm, {}, 0, false, v, false, true }; }
inline Op imm(int v)                { return imm(static_cast<long long>(v)); }
inline Op imm(unsigned int v)       { return imm(static_cast<unsigned long long>(v)); }

inline Op immText(Str t) { return { Op::Imm, t, 0, false, 0, false, false }; }

inline Op mem(Str base) { return { Op::Mem, base, 0, false, 0, false, false }; }
inline Op mem(long long d, Str base)
                                   { return { Op::Mem, base, d, true, 0, false, false }; }
inline Op rip(Str sym) { return { Op::Rip, sym, 0, false, 0, false, false }; }
inline Op ind(Str r)   { return { Op::Ind, r, 0, false, 0, false, false }; }
inline Op lbl(Str l)   { return { Op::Lbl, l, 0, false, 0, false, false }; }

inline void appendNum(std::string &s, unsigned long long v) {
    char b[24];
    int i = 24;
    if (v == 0) b[--i] = '0';
    while (v != 0) { b[--i] = static_cast<char>('0' + v % 10); v /= 10; }
    s.append(b + i, static_cast<std::size_t>(24 - i));
}
inline void appendNum(std::string &s, long long v) {
    if (v < 0) { s += '-'; appendNum(s, 0ULL - static_cast<unsigned long long>(v)); }
    else appendNum(s, static_cast<unsigned long long>(v));
}
inline void appendNum(std::string &s, int v) { appendNum(s, static_cast<long long>(v)); }

class Spelling {
public:
    virtual ~Spelling() = default;

    virtual void ins(const std::string &m) = 0;
    virtual void ins(const std::string &m, const Op &a) = 0;
    virtual void ins(const std::string &m, const Op &a, const Op &b) = 0;

    virtual void defLabel(const std::string &l) = 0;

    virtual void functionBegin(const std::string &name, bool exported,
                               bool mergeable = false) = 0;
// `lsda` names this function's exception table, or is empty where it has no
// landing pad. The personality routine must be named between .cfi_startproc
// and the first instruction, and this is the only place that sees both.
    virtual void prologue(int frameSize, const std::string &lsda) = 0;
    virtual void functionEnd(const std::string &name) = 0;

    virtual void fileEntry(int, const std::string &) {}
    virtual void location(int, int, int) {}

    virtual void predefine(const std::vector<std::string> &) {}
    virtual void preamble(std::ostream &) {}
    virtual void postamble(std::ostream &) {}

    virtual void globl(const std::string &name) = 0;
    // **A definition the linker must fold rather than reject** - an inline
    // function, which may appear in several translation units. Each assembler
    // spells it differently and the default does nothing, so a target that has
    // not been measured says nothing rather than something wrong.
    virtual void weakDefinition(const std::string &name) { (void)name; }
    virtual void textSection() = 0;
    virtual void rodataSection() = 0;
    virtual void dataSection() = 0;
    virtual void bssSection() = 0;
    virtual void objectType(const std::string &name) = 0;
    virtual void objectSize(const std::string &name, int size) = 0;
    virtual void align(int n) = 0;
    virtual void zero(int n) = 0;
    virtual void dataInt(int size, long long v) = 0;

    virtual void dataSym(const std::string &sym, long long off) = 0;
    virtual void dataBytes(const std::string &bytes) = 0;
};

// **The GNU spelling, and the one place a COFF variant needs to differ.**
// `sym` is what every name goes through on its way out. It answers with the
// name unchanged here, so ELF and Mach-O are exactly what they were; the COFF
// spelling below quotes what GNU syntax will not take as an identifier.
class GnuSpelling : public Spelling {
public:
    explicit GnuSpelling(std::string &o) : o_(o) {}
    virtual ~GnuSpelling() {}

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    void functionBegin(const std::string &name, bool exported,
                       bool mergeable = false) override;
    void prologue(int frameSize, const std::string &lsda) override;
    void functionEnd(const std::string &name) override;
    void globl(const std::string &name) override;
    void weakDefinition(const std::string &name) override;
    void textSection() override;
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &s, long long off) override;

protected:
    // How a symbol is written. Identity for GNU-as on ELF and Mach-O.
    virtual std::string sym(const std::string &name) const { return name; }
    void op(const Op &x);
    void dataBytes(const std::string &bytes) override;

protected:
    std::string &o_;
};

// **The same GNU syntax, assembled into a COFF object.** Two things separate it
// from the ELF spelling above, and both were measured against what clang writes
// for x86_64-pc-windows-msvc:
//
//   - **A Microsoft name is not a GNU identifier.** `??_7S@@6B@` begins with a
//     character GNU-as will not take, so every such name is quoted. clang quotes
//     the same ones.
//   - **`.weak` is not what COFF folds by; a COMDAT section is.** ml64 has no
//     directive that reaches the COMDAT bit at all - its objects are COFF and
//     carry it on no section, where cl's carry it on one per inline function -
//     which is the whole reason this spelling exists. `.section .text,"xr",
//     discard,"<sym>"` sets it, and two objects defining one symbol that way
//     link and fold.
//
// The section has to be opened *before* the label, so functionBegin is told
// whether the definition is mergeable rather than being followed by a `.weak`.
class CoffSpelling final : public GnuSpelling {
public:
    explicit CoffSpelling(std::string &o) : GnuSpelling(o) {}

    void functionBegin(const std::string &name, bool exported,
                       bool mergeable) override;
    void weakDefinition(const std::string &name) override;
    void rodataSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;

protected:
    std::string sym(const std::string &name) const override;

private:
    // What functionBegin has already given a COMDAT section, so the
    // weakDefinition that follows it does not open a second one.
    std::string opened_;
};
