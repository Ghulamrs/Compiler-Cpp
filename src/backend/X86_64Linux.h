#pragma once

#include "Backend.h"
#include "Dwarf.h"
#include "Spelling.h"
#include "Walker.h"

#include <iosfwd>
#include <sstream>
#include <string>
#include <vector>

class LinuxX86_64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULong; }
    Kind wcharType() const override { return Kind::Int; }
    bool microsoftNames() const override { return false; }
    const char *name() const override { return "x86_64-linux"; }
};

class X86_64LinuxBackend final : public Backend {
public:
    const char *name() const override { return "x86_64-linux"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink) const override;
    bool emitsLineTable() const override { return true; }
private:
    LinuxX86_64Target target_;
};

class X86_64Linux : public Walker {
public:
    // **The COFF spelling where the names are Microsoft's.** Same syntax and
    // the same code generator; what differs is that a mangled name is quoted
    // and a mergeable definition gets a COMDAT section, neither of which
    // GNU-as on ELF wants. Reached by `-masm=gnu` for x86_64-windows.
    X86_64Linux(std::ostream &sink, const Target &target, const Abi &abi)
        : target_(target), sink_(sink), abi_(abi) {
        if (target.microsoftNames()) a_ = &coff_;
    }

    using Walker::visit;
    void run(const Program &program) override;

    void visit(const Num &) override;
    void visit(const Var &) override;
    void visit(const Assign &) override;
    void visit(const Unary &) override;
    void visit(const Binary &) override;
    void visit(const Postfix &) override;
    void visit(const Call &) override;
    void visit(const Cast &) override;
    void visit(const StrLit &) override;
    void visit(const VaStart &) override;
    void visit(const VaArg &) override;
    void visit(const MemberAccess &) override;
    void visit(const Return &) override;

protected:

    virtual bool writesDwarf() const { return true; }
    // **The MASM path writes its own RTTI records**, in its own run(), so the
    // COFF ones must not be written as well - both reach here through the same
    // base run() and the second set is a duplicate symbol.
    virtual bool emitsOwnRtti() const { return false; }

    // **The exception model follows the target, not the spelling.** This
    // generator serves x86_64-linux, arm64-darwin, and - under `-masm=gnu` -
    // x86_64-windows, where the parser builds the Microsoft shape of a `try`:
    // no landing pad, a funclet per handler. Answering false there sent a
    // Microsoft `Try` down the Itanium branch and dereferenced a pad that is
    // null by construction, which is a segfault rather than a diagnostic. The
    // funclets themselves are written by the MASM spelling alone, so this mode
    // now refuses by name where it used to crash.
    bool usesFunclets() const override { return target_.microsoftNames(); }
    std::string beginFunclet() override;
    void endCleanupFunclet() override;
    void endFunclet(const std::string &resume) override;
    void storeUnwindHelp(int slot) override;
    void closeFunclet(const std::string &tail);
    // A Windows local is `frameSize - slot` above the establisher frame, which
    // is the whole translation between how cxx1 addresses a local and how an
    // FH3 table describes one. The MASM path says the same thing in its own
    // class; both are the same arithmetic on the same frame.
    int establisherOffset(int slot) const { return frameSize_ - slot; }
    void emitCoffCleanupTables(const Function &fn);
    // The five objects the Microsoft ABI wants per class with a vftable.
    void emitCoffClassRtti(const Program &program);

    // A funclet is written by walking the handler into the ordinary output and
    // lifting the text back out - what the body appended, in order, IS the
    // funclet, so moving it costs no second code path. Same device the MASM
    // path uses, and for the same reason.
    std::string funclets_;
    std::size_t funcletMark_ = 0;
    int funcletIndex_ = 0;
    std::string funcletSymbol_;
    const char *funcletKind_ = "$catch$";
    // The function being emitted, which the tables and funclets name.
    std::string fnSymbol_;
    // Whether the function being emitted went into a COMDAT, which its
    // funclets and their unwind data have to join - see closeFunclet.
    bool fnMergeable_ = false;
    // **A funclet's .pdata goes last, after every ordinary function's.** A
    // .pdata contribution is sorted by the address it describes and every
    // funclet lives in .text$x, which the linker lays after .text - so emitting
    // one beside its parent interleaves the two orders and the linker says
    // LNK1223. The MASM path keeps the same pile for the same reason.
    std::string funcletPdata_;

    std::string out_;
    std::size_t emittedSize() override { return out_.size(); }
    Spelling *a_ = &gnu_;

    void landingPad(int pointerSlot, int selectorSlot) override;

protected:
    // The `.gcc_except_table` for the function just emitted. Its shape is
    // documented beside the arm64 one; only the spelling differs here.
    void emitLsda(const std::string &symbol);

    // **Where the frame base sits, relative to the locals.** Itanium takes rbp
    // before allocating, so a local is [rbp-slot]; Microsoft wants the base at
    // the bottom of it, so a local is [rbp + (frameSize - slot)].
    // **Where the frame pointer sits, which is the target's shape and not the
    // spelling's.** Microsoft takes rbp *after* the allocation so that every FH3
    // displacement is an unsigned offset up from the establisher; Itanium takes
    // it before. This answered false for `-masm=gnu` on x86_64-windows, so the
    // epilogue restored rsp to the bottom of the frame and `pop rbp` read the
    // wrong word - the second property found living in the MASM subclass that
    // belongs to the target, after usesFunclets().
    virtual bool localsAboveFrameBase() const { return target_.microsoftNames(); }

    // One local. Every frame-relative operand in this file is written against
    // rbp as Itanium establishes it, so a target whose base is elsewhere moves
    // all of them by one constant, applied once where operands are rendered.
    Op local(long long slot) const { return mem(-slot, "%rbp"); }
    int frameSize_ = 0;

    // Whatever this target writes after a function to describe its handlers.
    // Itanium writes one .gcc_except_table; the Microsoft ABI writes funclets
    // and four tables, so the hook is the shape rather than the table.
    virtual void emitExceptionTables(const Function &fn) {
        // Microsoft frames carry FH3 tables, not an LSDA. The MASM path says
        // this in its own class; this is the same decision for the COFF one.
        if (target_.microsoftNames()) {
            if (!msTries().empty()) emitCoffCleanupTables(fn);
            else { out_ += funclets_; funclets_.clear(); funcletIndex_ = 0; }
            return;
        }
        if (!callSites().empty()) emitLsda(fn.symbol());
    }
    std::vector<std::string> lsdaTypes_;
    std::vector<std::string> lsdaStubs_;
    bool lsdaPersonality_ = false;
    // Reachable from the MASM subclass, which needs the target to size the
    // objects the Microsoft ABI wants a throw to carry.
    const Target &target_;

private:
    std::vector<std::string> chunks_;
    std::vector<DwarfFunction> dwarfFns_;
    std::vector<DwarfGlobal> dwarfGlobals_;
    std::ostream &sink_;
    GnuSpelling gnu_{out_};
    CoffSpelling coff_{out_};

    const Abi &abi_;
    int depth_ = 0;
    std::string returnLabel_;
    void emitLoc(int file, int line, int column) override { a_->location(file, line, column); }
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    std::string labelPrefix_;
    int sretSlot_ = 0;
    int regSave_ = 0;
    int varGp_ = 0, varFp_ = 48, varOverflow_ = 16;

    void emit(const Function &fn);
    void finishChunk();
    std::string label(const char *kind, int id) const override;
    std::string userLabel(const std::string &name) const override;
    void emitData(const Program &program);
    void emitGlobal(const Global &g, Segment seg);
    void push();
    void pop(const char *into);
    void pushF();
    void popF(const char *into);

    void pushX87();
    void popX87();

    bool isX87(const Type *t) const { return t->isX87(target_); }

    Kind genKind(const Type *t) const;

    void loadX87Const(long double v);
    void x87ToInt(const Type *to);
    void intToX87(const Type *from);
    void genX87Binary(const Binary &n);

    void genAddr(const Expr &e);

    void load(const Type *t);
    void store(const Type *t);
    void storeAt(const Type *t, int offset);
    void bitFieldUnitAddr(const MemberAccess &m);
    void bitFieldExtract(const MemberAccess &m);
    void bitFieldInsert(const MemberAccess &m);

    void copyBlock(int size);

    void canonicalise(const Type *t);
    void genFloatBinary(const Binary &n);
    void genConversion(const Type *from, const Type *to);
    void genTruth(const Expr &e) override;

    const char *acc(const Type *t) const;
    const char *rhs(const Type *t) const;

    void unsupported(const char *what);

    // **The last lane of an aggregate is composed, never approximated.** These
    // three write and read exactly `left` bytes and never touch a byte past the
    // object, where one widened move took whatever the destination held.
    void storeTailFromReg(const char *reg64, long long off, const char *base,
                          int left);          // clobbers reg64
    void copyTailMem(long long from, long long to, int left);   // via %rax
    void loadTailToReg(const char *reg64, long long off, const char *base,
                       int left);             // clobbers %rcx
    void msAggregateToRax(const Type *t, int slot);
    void msCopyToSlot(const Type *t, int slot, const char *from);
    int takeSlot(bool sse, int &ints, int &sses) const;

    // **Where one argument goes, decided once for both ends of the call.** The
    // caller and the callee each classified the parameter list by hand, and
    // agreeing was a property of two copies staying in step - which is what A-01
    // broke, consistently on both sides, where no suite could see it.
    struct ArgPlace {
        std::vector<bool> lanes;   // empty when the argument travels in memory
        std::vector<int> regs;     // one register slot per lane
        bool inMemory = false;
        bool padBelow = false;     // the caller pushes 8 bytes under this one
        int stackOffset = 0;       // bytes from the base the callee supplies
        int stackWords = 0;        // and how many 8-byte words it occupies
    };

    // The whole list, in order. `sret` says a hidden return pointer is passed,
    // `hasThis` that the first argument is an object - between them they decide
    // which register the first written argument actually gets.
    // The list, and what it used up: the register counts a variadic call needs
    // for its save area and al, and the words the arguments occupy on the stack.
    struct Placement {
        std::vector<ArgPlace> args;
        int intsUsed = 0;
        int ssesUsed = 0;
        int stackWords = 0;
    };
    Placement placeArguments(const std::vector<const Type *> &types,
                             bool hasThis, bool sret) const;
};

std::vector<bool> classifyEightbytes(const Type *t, const Target &target);
