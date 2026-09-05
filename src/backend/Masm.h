#pragma once

#include "Backend.h"
#include "Spelling.h"
#include "X86_64Linux.h"

#include <iosfwd>
#include <set>
#include <string>
#include <vector>

class MasmSpelling final : public Spelling {
public:
    explicit MasmSpelling(std::string &o) : o_(o) {}

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void functionBegin(const std::string &name, bool exported,
                       bool mergeable = false) override;
    void prologue(int frameSize, const std::string &lsda) override;
    // The unwind codes the prologue described, written out by functionEnd -
    // which is where the labels they measure against exist.
    std::string unwindData_;
    int unwindCodes_ = 0;
    std::string fnName_;
    // The frame this function allocated. The Microsoft exception tables measure
    // every offset from the stack pointer after the prologue and cxx1 addresses
    // locals from rbp before it, so this number bridges the two.
    int frameSize_ = 0;
    // Whether this function has a handler: it decides the flags in the unwind
    // header and whether __CxxFrameHandler3 and a FuncInfo follow the codes.
    bool hasEh_ = false;
    void raw(const std::string &text);
    // Written by postamble, after every chunk the code generator built. The
    // funclets' .pdata goes here: see funcletPdata_ in MasmCodeGen.
    std::string trailer_;
    std::string mangledName() { return mangle(fnName_); }
    // A label written by the Walker, spelled the way this file spells one. Raw
    // emission goes through the same door as defLabel, or a table names
    // `.L.main.caught.0` where the code defines `$_L_main_caught_0`.
    std::string labelName(const std::string &l) { return mangle(l); }
    void functionEnd(const std::string &name) override;

    void globl(const std::string &name) override;
    void textSection() override;
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &sym, long long off) override;
    void dataBytes(const std::string &bytes) override;

    void predefine(const std::vector<std::string> &names) override;
    void preamble(std::ostream &sink) override;
    void postamble(std::ostream &sink) override;

private:
    std::string &o_;
    enum Seg { None, Code, Data, Const, Bss } seg_ = None;

    std::string pending_;

    std::set<std::string> defined_, exported_, referenced_, unreserved_;

    struct Rendered {
        std::string text;
        bool isMem = false, isImm = false, isXmm = false;
    };
    Rendered render(const Op &x);
    std::string mangle(const std::string &name);
    void flushPending();
    void items(const char *dir, const std::vector<std::string> &it);
};

class MasmCodeGen final : public X86_64Linux {
public:
    MasmCodeGen(std::ostream &sink, const Target &target, const Abi &abi)
        : X86_64Linux(sink, target, abi), masm_(out_) { a_ = &masm_; }

    void run(const Program &program) override;

private:
    bool usesFunclets() const override { return true; }
    bool localsAboveFrameBase() const override { return true; }
    std::string beginFunclet() override;
    void endFunclet(const std::string &resume) override;
    void endCleanupFunclet() override;
    void closeFunclet(const std::string &tail);
    // **A Windows local is `frameSize - slot` above the establisher frame**, and
    // that is the whole translation between how cxx1 addresses a local and how
    // every FH3 table has to describe one. Written here so it is written once.
    int establisherOffset(int slot) const;

    void storeUnwindHelp(int slot) override;
    void emitExceptionTables(const Function &fn) override;
    void emitCleanupTables(const Function &fn);

    // A funclet is written by walking the handler into the ordinary output and
    // lifting the text back out: what the body appended, in order, *is* the
    // funclet, so moving it costs no second code path.
    std::string funclets_;
    // The funclets' .pdata goes to MasmSpelling::trailer_, after every function:
    // .pdata has to be sorted by the address it describes and every funclet
    // lives in .text$x, so emitting beside the parent gives LNK1223.
    std::size_t funcletMark_ = 0;
    int funcletIndex_ = 0;
    std::string funcletSymbol_;
    const char *funcletKind_ = "$catch$";
    bool writesDwarf() const override { return false; }
    // The four objects the Microsoft ABI wants per thrown type. Emitted here
    // because no other target has anything like them.
    void emitThrowInfo(const Program &program);
    // The five objects the Microsoft ABI wants per class with a vftable, for
    // the same reason and in the same place.
    void emitClassRtti(const Program &program);

    MasmSpelling masm_;
};
