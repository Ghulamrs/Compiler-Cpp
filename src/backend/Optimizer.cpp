#include "Optimizer.h"

#include "OptPasses.h"

#include <algorithm>
#include <cassert>
#include <set>
#include <utility>

using opt::Entry;

namespace {

// **What each level does, in one place.** A round is every pass once; each
// pass leaves work for the others, so rounds run until one finds nothing, or
// the level's limit.
struct Level {
    int rounds;
    int registers;        // callee-saved registers locals may be kept in
    long minWeight;       // the accesses, loop-weighted, that earn one
};

Level levelFor(int n) {
    return n <= 1 ? Level{8, 2, 6} : Level{16, 5, 2};
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
    if (inlining_)
        for (opt::Operand *o : {&e.ins.a, &e.ins.b})
            if (o->isMem() && o->reg.id == opt::RBP) {
                assert(o->disp < 0 && "an inlined callee reads only its own frame");
                o->disp -= inlineBase_;
                o->hasDisp = true;
            }
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
    flush(false);
    under_.functionBegin(name, exported, mergeable);
    inFunction_ = true;
    cut_ = promotable_ = false;
    prologueAt_ = -1;
    inlining_ = false;
    inlineTop_ = 0;
}

// **A callee walked in place keeps its own frame, below the caller's locals**:
// every slot it names moves down by `base`, and the frame must reach the
// bottom of the largest callee held.
void Optimizer::inlineBegin(int base, int calleeFrame, const std::vector<opt::Local> &calleeLocals) {
    inlining_ = true;
    inlineBase_ = base;
    inlineTop_ = std::max(inlineTop_, base + ((calleeFrame + 15) & ~15));
    for (const opt::Local &l : calleeLocals) locals_.push_back(opt::Local{l.disp - base, l.size});
}

void Optimizer::inlineEnd() { inlining_ = false; }

void Optimizer::jumpOnly(const std::string &label) { jumpOnly_.insert(label); }

// **A label only jumps name, that no jump names any more**, joins its block to
// the one before, so what is known flows through it.
void Optimizer::dropUnnamedLabels() {
    std::set<std::string> named;
    for (const Entry &e : stream_)
        if (e.kind == Entry::Ins && !e.dead && e.ins.a.kind == opt::Operand::Label) named.insert(e.ins.a.text);
    for (Entry &e : stream_)
        if (e.kind == Entry::Label && jumpOnly_.count(e.label) && !named.count(e.label)) e.dead = true;
}

void Optimizer::functionEnd(const std::string &name) {
    flush(!cut_);
    inFunction_ = false;
    under_.functionEnd(name);
}

void Optimizer::frame(std::vector<opt::Local> locals, bool promotable) {
    locals_ = std::move(locals);
    promotable_ = promotable;
}

void Optimizer::returnsPair(bool pair) {
    convention_.returned = opt::bit(opt::RAX) | opt::bit(opt::kXmm0);
    if (pair) convention_.returned |= opt::bit(opt::RDX) | opt::bit(opt::kXmm0 + 1);
}

void Optimizer::rounds(opt::Flow &flow, int limit) {
    for (int round = 0; round < limit; ++round) {
        dropUnnamedLabels();
        flow.build(stream_, convention_);
        bool changed = opt::forwardValues(stream_, flow, convention_);
        changed = opt::removeUnreachable(stream_) || changed;
        changed = opt::removeDead(stream_, flow) || changed;
        changed = opt::coalesceCopies(stream_, flow, convention_) || changed;
        changed = opt::foldLoads(stream_, flow, convention_) || changed;
        changed = opt::foldOffsets(stream_, flow, convention_) || changed;
        if (!changed) break;
    }
}

// **Locals go to registers once the frame is as small as it gets**, so an
// address folded away no longer counts as escaping; then everything again.
void Optimizer::improve(bool whole) {
    const Level level = levelFor(level_);
    opt::Flow flow;
    rounds(flow, level.rounds);
    // What the frame gains goes below what it had: the saves, then the shadow
    // space at the floor, where a callee finds it.
    // First the region inlined callees live in, then the saves, then the shadow.
    int size = std::max(frameSize_, inlineTop_);
    std::vector<SavedReg> saves;
    if (whole && promotable_ && prologueAt_ >= 0) {
        saves = opt::promoteLocals(stream_, convention_, locals_, size, level.registers, level.minWeight);
        if (opt::removeDeadStores(stream_) || !saves.empty()) rounds(flow, level.rounds);
        opt::dropUnusedSaves(stream_, saves);
        size += (8 * static_cast<int>(saves.size()) + 15) & ~15;
        if (opt::reserveShadow(stream_, flow, convention_)) size += (convention_.shadow + 15) & ~15;
    }
    if (size != frameSize_) {
        assert(prologueAt_ >= 0 && "a frame can grow only while its prologue is held");
        const std::string lsda = lsda_;
        stream_[prologueAt_].event = [=](Spelling &s) { s.calleeSaves(saves); s.prologue(size, lsda); };
    }
    // A shorter spelling last: narrowed arithmetic leaves extensions to delete.
    for (int again = 0; again < 3; ++again) {
        flow.build(stream_, convention_);
        if (!opt::shrink(stream_, flow, convention_)) break;
        rounds(flow, level.rounds);
    }
}

void Optimizer::settle() {
    if (inFunction_) cut_ = true;
    flush(false);
}

void Optimizer::flush(bool whole) {
    if (stream_.empty()) return;
    improve(whole);
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
    prologueAt_ = -1;       // written out: from here on the frame is what it is
}

// Everything else is an event: held where it stood, with its arguments copied.
void Optimizer::prologue(int frameSize, const std::string &lsda) {
    prologueAt_ = static_cast<int>(stream_.size());
    frameSize_ = frameSize;
    lsda_ = lsda;
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
