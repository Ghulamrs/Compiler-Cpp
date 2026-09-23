#include "Optimizer.h"

#include "OptPasses.h"

#include <utility>

using opt::Entry;

namespace {

// **What each level does, in one place.** A round is every pass once; each
// pass leaves work for the others, so rounds run until one finds nothing, or
// the level's limit.
struct Level {
    bool forward;
    int rounds;
};

Level levelFor(int n) {
    switch (n) {
    case 0: return Level{false, 0};
    case 1: return Level{true, 8};
    default: return Level{true, 16};
    }
}

}

Optimizer::Optimizer(Spelling &under, const Abi &abi, int level)
    : under_(under), convention_(opt::conventionOf(abi)), level_(level) {}

void Optimizer::hold(Entry e) {
    stream_.push_back(std::move(e));
    held_++;
}

void Optimizer::instruction(const std::string &m, int operands, const Op *a, const Op *b) {
    if (!inFunction_) {
        if (operands == 0) under_.ins(m);
        else if (operands == 1) under_.ins(m, *a);
        else under_.ins(m, *a, *b);
        return;
    }
    Entry e;
    e.ins.m = m;
    e.ins.operands = operands;
    if (a) e.ins.a = opt::Operand::from(*a);
    if (b) e.ins.b = opt::Operand::from(*b);
    hold(std::move(e));
}

void Optimizer::event(std::function<void(Spelling &)> call) {
    if (!inFunction_) { call(under_); return; }
    Entry e;
    e.kind = Entry::Event;
    e.event = std::move(call);
    hold(std::move(e));
}

void Optimizer::ins(const std::string &m) { instruction(m, 0, nullptr, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a) { instruction(m, 1, &a, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a, const Op &b) { instruction(m, 2, &a, &b); }

void Optimizer::defLabel(const std::string &l) {
    if (!inFunction_) { under_.defLabel(l); return; }
    Entry e;
    e.kind = Entry::Label;
    e.label = l;
    hold(std::move(e));
}

void Optimizer::functionBegin(const std::string &name, bool exported, bool mergeable) {
    settle();
    under_.functionBegin(name, exported, mergeable);
    inFunction_ = true;
}

void Optimizer::functionEnd(const std::string &name) {
    settle();
    inFunction_ = false;
    under_.functionEnd(name);
}

void Optimizer::returnsPair(bool pair) {
    convention_.returned = opt::bit(opt::RAX) | opt::bit(opt::kXmm0);
    if (pair) convention_.returned |= opt::bit(opt::RDX) | opt::bit(opt::kXmm0 + 1);
}

void Optimizer::improve() {
    const Level level = levelFor(level_);
    opt::Flow flow;
    for (int round = 0; round < level.rounds; ++round) {
        flow.build(stream_, convention_);
        bool changed = level.forward && opt::forwardValues(stream_, flow, convention_);
        changed = opt::removeUnreachable(stream_) || changed;
        changed = opt::removeDead(stream_, flow) || changed;
        changed = (level.forward && opt::coalesceCopies(stream_, flow)) || changed;
        changed = (level.forward && opt::foldLoads(stream_, flow, convention_)) || changed;
        if (!changed) break;
    }
    // Last, once: a shorter spelling hides the widths the other passes match on.
    opt::shrink(stream_, flow, convention_);
}

void Optimizer::settle() {
    if (stream_.empty()) return;
    improve();
    for (const Entry &e : stream_) {
        if (e.dead) continue;
        switch (e.kind) {
        case Entry::Ins: {
            const opt::Instr &i = e.ins;
            if (i.operands == 0) under_.ins(i.m);
            else if (i.operands == 1) under_.ins(i.m, i.a.op());
            else under_.ins(i.m, i.a.op(), i.b.op());
            break;
        }
        case Entry::Label: under_.defLabel(e.label); break;
        case Entry::Event: e.event(under_); break;
        }
    }
    stream_.clear();
}

// Everything else is an event: held where it stood, with its arguments copied.
void Optimizer::prologue(int frameSize, const std::string &lsda) {
    event([=](Spelling &s) { s.prologue(frameSize, lsda); });
}
void Optimizer::fileEntry(int n, const std::string &name) {
    event([=](Spelling &s) { s.fileEntry(n, name); });
}
void Optimizer::location(int file, int line, int column) {
    event([=](Spelling &s) { s.location(file, line, column); });
}
void Optimizer::globl(const std::string &name) { event([=](Spelling &s) { s.globl(name); }); }
void Optimizer::weakDefinition(const std::string &name) {
    event([=](Spelling &s) { s.weakDefinition(name); });
}
void Optimizer::textSection() { event([](Spelling &s) { s.textSection(); }); }
void Optimizer::rodataSection() { event([](Spelling &s) { s.rodataSection(); }); }
void Optimizer::dataSection() { event([](Spelling &s) { s.dataSection(); }); }
void Optimizer::bssSection() { event([](Spelling &s) { s.bssSection(); }); }
void Optimizer::objectType(const std::string &name) {
    event([=](Spelling &s) { s.objectType(name); });
}
void Optimizer::objectSize(const std::string &name, int size) {
    event([=](Spelling &s) { s.objectSize(name, size); });
}
void Optimizer::align(int n) { event([=](Spelling &s) { s.align(n); }); }
void Optimizer::zero(int n) { event([=](Spelling &s) { s.zero(n); }); }
void Optimizer::dataInt(int size, long long v) { event([=](Spelling &s) { s.dataInt(size, v); }); }
void Optimizer::dataSym(const std::string &sym, long long off) {
    event([=](Spelling &s) { s.dataSym(sym, off); });
}
void Optimizer::noteHasEh(bool yes) { event([=](Spelling &s) { s.noteHasEh(yes); }); }
void Optimizer::initialiserEntry(const std::string &fn, bool dsoHandle) {
    event([=](Spelling &s) { s.initialiserEntry(fn, dsoHandle); });
}
void Optimizer::dataBytes(const std::string &bytes) {
    event([=](Spelling &s) { s.dataBytes(bytes); });
}
