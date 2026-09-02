#include "Walker.h"

#include "../Source.h"

void Walker::markLine(const Stmt &n) { markLine(n.pos()); }

void Walker::markLine(std::size_t pos) {
    if (lines_ == nullptr) return;

    if (pos == 0) return;
    Source::Place at = lines_->locate(pos);
    std::size_t before = emittedSize();
    emitLoc(at.file + 1, at.line, at.column);

    notCode_ += emittedSize() - before;
}

void Walker::visit(const ExprStmt &n) { markLine(n); n.expr().accept(*this); }

void Walker::resetBlocks(const std::vector<int> &parents) {
    blocks_.clear();
    marks_.clear();
    notCode_ = 0;
    for (std::size_t i = 0; i < parents.size(); i++) {
        DwarfBlock b;
        b.parent = parents[i];
        blocks_.push_back(b);
    }
}

void Walker::openBlock(int scope) {
    if (lines_ == nullptr || scope <= 0) return;
    if (static_cast<std::size_t>(scope) >= blocks_.size()) return;
    std::size_t before = emittedSize();
    blocks_[scope].begin = label("blk.b", scope);
    defineLabel(blocks_[scope].begin);
    notCode_ += emittedSize() - before;
    Mark m;
    m.size = emittedSize();
    m.notCode = notCode_;
    marks_.push_back(m);
}

void Walker::closeBlock(int scope) {
    if (lines_ == nullptr || scope <= 0) return;
    if (static_cast<std::size_t>(scope) >= blocks_.size()) return;
    if (marks_.empty()) return;
    Mark m = marks_.back();
    marks_.pop_back();
    bool code = (emittedSize() - m.size) > (notCode_ - m.notCode);

    std::size_t before = emittedSize();
    blocks_[scope].end = label("blk.e", scope);
    defineLabel(blocks_[scope].end);
    notCode_ += emittedSize() - before;

    if (!code) {
        blocks_[scope].begin.clear();
        blocks_[scope].end.clear();
    }
}

void Walker::visit(const Block &n) {
    markLine(n);
    openBlock(n.scope());
    for (const StmtPtr &s : n.body()) s->accept(*this);
    closeBlock(n.scope());
}

void Walker::visit(const If &n) {
    markLine(n);
    int id = nextLabel();
    genTruth(n.cond());
    if (n.elseArm()) {
        branchIfZero(label("else", id));
        n.thenArm().accept(*this);
        jump(label("end", id));
        defineLabel(label("else", id));
        n.elseArm()->accept(*this);
    } else {
        branchIfZero(label("end", id));
        n.thenArm().accept(*this);
    }
    defineLabel(label("end", id));
}

void Walker::visit(const While &n) {
    markLine(n);
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("begin", id) });
    defineLabel(label("begin", id));
    genTruth(n.cond());
    branchIfZero(label("end", id));
    n.body().accept(*this);
    jump(label("begin", id));
    defineLabel(label("end", id));
    jumps_.pop_back();
}

void Walker::visit(const For &n) {
    markLine(n);

    openBlock(n.scope());
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("step", id) });

    if (n.init()) n.init()->accept(*this);
    defineLabel(label("begin", id));
    if (n.cond()) {

        markLine(n);
        genTruth(*n.cond());
        branchIfZero(label("end", id));
    }
    n.body().accept(*this);
    defineLabel(label("step", id));
    if (n.step()) {
        markLine(n);
        n.step()->accept(*this);
    }
    jump(label("begin", id));
    defineLabel(label("end", id));
    closeBlock(n.scope());

    jumps_.pop_back();
}

void Walker::visit(const DoWhile &n) {
    markLine(n);
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("step", id) });

    defineLabel(label("begin", id));
    n.body().accept(*this);
    defineLabel(label("step", id));
    genTruth(n.cond());
    branchIfNotZero(label("begin", id));
    defineLabel(label("end", id));

    jumps_.pop_back();
}

void Walker::visit(const Switch &n) {
    markLine(n);
    int id = nextLabel();

    n.cond().accept(*this);
    for (const Case *c : n.cases())
        caseBranch(c->value(), label("case", c->id()));
    jump(n.defaultCase() ? label("default", n.defaultCase()->id())
                         : label("end", id));

    jumps_.push_back({ label("end", id), "" });
    n.body().accept(*this);
    jumps_.pop_back();
    defineLabel(label("end", id));
}

void Walker::visit(const Case &n) {
    markLine(n);
    defineLabel(label(n.isDefault() ? "default" : "case", n.id()));
    n.body().accept(*this);
}

void Walker::visit(const Goto &n) { markLine(n); jump(userLabel(n.label())); }

void Walker::visit(const Label &n) {
    markLine(n);
    defineLabel(userLabel(n.name()));
    n.body().accept(*this);
}

void Walker::visit(const Conditional &n) {
    int id = nextLabel();
    genTruth(n.cond());
    branchIfZero(label("else", id));
    n.thenArm().accept(*this);
    jump(label("end", id));
    defineLabel(label("else", id));
    n.elseArm().accept(*this);
    defineLabel(label("end", id));
}

void Walker::visit(const Comma &n) {
    n.left().accept(*this);
    n.right().accept(*this);
}

void Walker::visit(const Break &n) { markLine(n); jump(jumps_.back().brk); }

// **The shape is the same on both targets, so it lives here.** Labels before
// and after the body bound the range the call-site table talks about, the pad
// is where the runtime arrives, and the jump over it keeps the ordinary path out.
void Walker::visit(const Try &n) {
    markLine(n);
    if (usesFunclets()) { msTryStatement(n); return; }
    const int id = nextLabel();
    const std::string begin = label("try", id);
    const std::string end = label("tryend", id);
    const std::string pad = label("pad", id);
    const std::string done = label("caught", id);

    defineLabel(begin);
    for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(*this);
    defineLabel(end);
    jump(done);

    defineLabel(pad);
    landingPad(n.pointerSlot(), n.selectorSlot());
    n.pad().accept(*this);
    defineLabel(done);

    callSite(begin, end, pad, n.types());
}

// **The Microsoft shape, and what is missing from it is the point.** No pad, no
// selector, no jump over a handler: each handler becomes a function of its own
// and a table says which to call. This frame contributes three labels.
void Walker::msTryStatement(const Try &n) {
    const int id = nextLabel();
    MsTryRegion r;
    r.begin = label("try", id);
    r.end = label("tryend", id);
    r.resume = label("caught", id);
    r.unwindHelpSlot = n.unwindHelpSlot();

    storeUnwindHelp(n.unwindHelpSlot());
    r.isCleanup = n.cleanup() != nullptr;

    defineLabel(r.begin);
    for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(*this);
    defineLabel(r.end);
    defineLabel(r.resume);

    if (r.isCleanup) {
        r.cleanupFunclet = beginFunclet();
        n.cleanup()->accept(*this);
        endCleanupFunclet();
        msTry(r);
        return;
    }

    // The funclets come after the range they belong to is closed, so nothing
    // they emit lands between `begin` and `end` - those bound the addresses the
    // runtime matches a thrown object against, and a handler must not be inside.
    for (std::size_t i = 0; i < n.handlers().size(); i++) {
        const MsHandler &h = n.handlers()[i];
        MsHandlerRow row;
        row.descriptor = h.descriptor;
        row.objectSlot = h.objectSlot;
        row.funclet = beginFunclet();
        h.body->accept(*this);
        endFunclet(r.resume);
        r.handlers.push_back(row);
    }
    msTry(r);
}

void Walker::visit(const Continue &n) {
    markLine(n);
    for (std::size_t i = jumps_.size(); i-- > 0;) {
        if (!jumps_[i].cont.empty()) {
            jump(jumps_[i].cont);
            return;
        }
    }
}

// **One table, two spellings.** Every byte below was measured against clang for
// both Itanium targets and was written out twice by hand until now; what differs
// is in LsdaSpelling, where the next reader can see the whole of it.
std::string Walker::lsdaTable(const LsdaSpelling &sp, const std::string &symbol,
                              std::vector<std::string> &types) const {
    const std::string L = sp.label;
    const std::string ex = L + "exception." + symbol;
    const std::string ttbase = L + "ttbase." + symbol;
    const std::string ttref = L + "ttbaseref." + symbol;
    const std::string cstBegin = L + "cst.begin." + symbol;
    const std::string cstEnd = L + "cst.end." + symbol;
    const std::string fnBegin = L + "func.begin." + symbol;
    const std::string fnEnd = L + "func.end." + symbol;

    std::string o;
    o += std::string("  ") + sp.section + "\n";
    o += "  .p2align 2\n";
    // A label that is not a temporary, where the linker cuts sections at symbols:
    // with only temporaries the second table in a file was never reached.
    if (sp.atomSymbol) o += "GCC_except_table." + symbol + ":\n";
    o += ex + ":\n";
    o += "  .byte 255\n";                 // LPStart omitted: pads are function-relative
    o += "  .byte 155\n";                 // the type table is indirect, pc-relative
    o += "  .uleb128 " + ttbase + "-" + ttref + "\n";
    o += ttref + ":\n";
    o += "  .byte 1\n";                   // the call-site table is uleb128
    o += "  .uleb128 " + cstEnd + "-" + cstBegin + "\n";
    o += cstBegin + ":\n";

    // **Every call in the function is a row, the ones outside a try included**: a
    // miss makes libc++abi call terminate. **And the action field is a byte offset
    // plus one**, not an index - each record is two bytes, so twice the count.
    int action = 1;
    std::string at = fnBegin;
    for (std::size_t i = 0; i < callSites().size(); i++) {
        const CallSite &c = callSites()[i];
        o += "  .uleb128 " + at + "-" + fnBegin + "\n";
        o += "  .uleb128 " + c.begin + "-" + at + "\n";
        o += "  .byte 0\n";
        o += "  .byte 0\n";
        o += "  .uleb128 " + c.begin + "-" + fnBegin + "\n";
        o += "  .uleb128 " + c.end + "-" + c.begin + "\n";
        o += "  .uleb128 " + c.pad + "-" + fnBegin + "\n";
        // No handler at all is a *cleanup*: the pad runs destructors and hands
        // the exception back, and action 0 is how the table says so.
        o += "  .uleb128 " + std::to_string(c.types.empty() ? 0 : action) + "\n";
        action += 2 * static_cast<int>(c.types.size());
        at = c.end;
    }
    o += "  .uleb128 " + at + "-" + fnBegin + "\n";
    o += "  .uleb128 " + fnEnd + "-" + at + "\n";
    o += "  .byte 0\n";
    o += "  .byte 0\n";
    o += cstEnd + ":\n";

    // The action table: a type index and the offset to the next record, 0 saying
    // there is no next, so the chain ends and the exception goes on unwinding.
    types.clear();
    for (std::size_t i = 0; i < callSites().size(); i++) {
        const CallSite &c = callSites()[i];
        for (std::size_t k = 0; k < c.types.size(); k++) {
            o += "  .byte " + std::to_string(types.size() + 1) + "\n";
            o += "  .byte " + std::string(k + 1 < c.types.size() ? "1" : "0") + "\n";
            types.push_back(c.types[k]);
        }
    }
    o += "  .p2align 2\n";
    // **Written backwards**: index 1 is the entry just before Lttbase.
    for (std::size_t i = types.size(); i-- > 0; ) {
        const std::string here = L + "ti." + symbol + "." + std::to_string(i);
        o += here + ":\n";
        if (types[i].empty()) o += "  .long 0\n";          // catch (...)
        else o += "  .long " + std::string(sp.typePrefix) + types[i] +
                  sp.typeSuffix + "-" + here + "\n";
    }
    o += ttbase + ":\n";
    o += "  .p2align 2\n";
    return o;
}
