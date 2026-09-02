#pragma once

#include "Backend.h"
#include "Dwarf.h"

#include <cstddef>
#include <string>
#include <vector>

class Source;

class Walker : public CodeGen {
public:
    void visit(const ExprStmt &n) override;
    void visit(const Block &n) override;
    void visit(const If &n) override;
    void visit(const While &n) override;
    void visit(const For &n) override;
    void visit(const DoWhile &n) override;
    void visit(const Switch &n) override;
    void visit(const Case &n) override;
    void visit(const Goto &n) override;
    void visit(const Label &n) override;
    void visit(const Conditional &n) override;
    void visit(const Comma &n) override;
    void visit(const Break &n) override;
    void visit(const Continue &n) override;
    void visit(const Try &n) override;

    void setLineSource(const Source *s, const std::string &dir) override {
        lines_ = s;
        compDir_ = dir;
    }

    const std::vector<DwarfBlock> &blocks() const { return blocks_; }

protected:

    void msTryStatement(const Try &n);

    void markLine(const Stmt &n);

    void markLine(std::size_t pos);
    const Source *lineSource() const { return lines_; }
    const std::string &compDir() const { return compDir_; }
    virtual void emitLoc(int file, int line, int column) { (void)file; (void)line; (void)column; }

    virtual void defineLabel(const std::string &l) = 0;
    virtual void jump(const std::string &l) = 0;
    virtual void branchIfZero(const std::string &l) = 0;
    virtual void branchIfNotZero(const std::string &l) = 0;

    virtual void caseBranch(long long v, const std::string &l) = 0;

    virtual void genTruth(const Expr &e) = 0;
    virtual std::string label(const char *kind, int id) const = 0;
    virtual std::string userLabel(const std::string &name) const = 0;

    virtual std::size_t emittedSize() = 0;

    // **What a backend has to be told about a landing pad, and no more.** The
    // runtime arrives with the exception pointer and the selector in two
    // registers; this stores them into slots, after which they are locals.
    virtual void landingPad(int pointerSlot, int selectorSlot) = 0;

    // One row of the call-site table: a call between `begin` and `end` that
    // throws goes to `pad`, catching these types in this order. Collected and
    // not emitted, since the table follows the body and the rows are shared.
    struct CallSite {
        std::string begin;
        std::string end;
        std::string pad;
        std::vector<std::string> types;
    };
    void callSite(const std::string &begin, const std::string &end,
                  const std::string &pad,
                  const std::vector<std::string> &types) {
        CallSite s;
        s.begin = begin;
        s.end = end;
        s.pad = pad;
        s.types = types;
        callSites_.push_back(s);
    }
    const std::vector<CallSite> &callSites() const { return callSites_; }
    void clearCallSites() { callSites_.clear(); }

    // **Does this target call handlers, or jump to them?** Itanium unwinds to a
    // pad inside the frame; the Microsoft runtime calls the handler as a
    // separate function. One question; the rest of a `try` is the same shape.
    virtual bool usesFunclets() const { return false; }

    // Write -2 into the runtime's scratch word. The personality routine reads it
    // through the FuncInfo's dispUnwindHelp to know how far this frame had got,
    // and -2 is the value that means "not inside anything yet".
    virtual void storeUnwindHelp(int slot) { (void)slot; }

    // A cleanup funclet is opened the same way and closed differently: it
    // returns nothing, because nothing continues here - the runtime goes on
    // unwinding once the destructors have run.
    virtual void endCleanupFunclet() {}

    // Open a handler funclet and answer its symbol; close it naming the address
    // in the parent to continue at, which a funclet returns in rax. Between the
    // two the body is walked as if inline, the funclet setting rbp from the parent.
    virtual std::string beginFunclet() { return std::string(); }
    virtual void endFunclet(const std::string &resume) { (void)resume; }

    // One `try` as the Microsoft tables describe it: the range guarded, where
    // to continue after a handler, the frame slot the runtime scribbles in,
    // and one row per handler.
    struct MsHandlerRow {
        std::string descriptor;   // empty for catch (...)
        int objectSlot = 0;
        std::string funclet;
    };
    struct MsTryRegion {
        std::string begin;
        std::string end;
        std::string resume;
        int unwindHelpSlot = 0;
        std::vector<MsHandlerRow> handlers;
        // A cleanup region instead of a try: no handler, one funclet that
        // runs destructors while the exception carries on past this frame.
        bool isCleanup = false;
        std::string cleanupFunclet;
    };
    void msTry(const MsTryRegion &r) { msTries_.push_back(r); }
    const std::vector<MsTryRegion> &msTries() const { return msTries_; }
    void clearMsTries() { msTries_.clear(); }

    void resetBlocks(const std::vector<int> &parents);

    void openBlock(int scope);
    void closeBlock(int scope);

    int nextLabel() { return labels_++; }
    void resetLabels() { labels_ = 0; }
    struct JumpTargets { std::string brk; std::string cont; };
    std::vector<JumpTargets> jumps_;

private:
    int labels_ = 0;
    std::vector<CallSite> callSites_;
    std::vector<MsTryRegion> msTries_;
    const Source *lines_ = nullptr;
    std::string compDir_;
    std::vector<DwarfBlock> blocks_;

    std::size_t notCode_ = 0;
    struct Mark { std::size_t size; std::size_t notCode; };
    std::vector<Mark> marks_;
};
