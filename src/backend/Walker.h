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
    // runtime arrives at the pad with the exception pointer and the selector
    // in two registers the ABI picks; this stores them into two frame slots,
    // after which they are ordinary locals and the parser's own statements do
    // the rest.
    virtual void landingPad(int pointerSlot, int selectorSlot) = 0;

    // One row of the call-site table: any call between `begin` and `end` that
    // throws goes to `pad`, which catches these types in this order. Collected
    // rather than emitted, because the table is written after the body - and
    // collected here rather than in a backend, because the rows are the same
    // whatever the target spells them like.
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
    const Source *lines_ = nullptr;
    std::string compDir_;
    std::vector<DwarfBlock> blocks_;

    std::size_t notCode_ = 0;
    struct Mark { std::size_t size; std::size_t notCode; };
    std::vector<Mark> marks_;
};
