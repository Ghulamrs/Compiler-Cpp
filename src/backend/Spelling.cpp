#include "Spelling.h"

#include <ostream>
#include <string>

void GnuSpelling::op(const Op &x) {
    switch (x.kind) {
    case Op::Reg: o_ += x.text; return;
    case Op::Imm:
        o_ += '$';
        if (!x.immNumeric) { o_ += x.text; return; }
        if (x.immNeg) o_ += '-';
        appendNum(o_, x.uimm);
        return;
    case Op::Mem:
        if (x.hasDisp) appendNum(o_, x.disp);
        o_ += '(';
        o_ += x.text;
        o_ += ')';
        return;
    case Op::Rip: o_ += sym(std::string(x.text.p, x.text.n)); o_ += "(%rip)"; return;
    case Op::Ind: o_ += '*'; o_ += x.text; return;
    case Op::Lbl: o_ += sym(std::string(x.text.p, x.text.n)); return;
    }
}

void GnuSpelling::ins(const std::string &m) { o_ += "  "; o_ += m; o_ += '\n'; }

void GnuSpelling::ins(const std::string &m, const Op &a) {
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += '\n';
}

void GnuSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += ", ";
    op(b);
    o_ += '\n';
}

void GnuSpelling::defLabel(const std::string &l) { o_ += sym(l); o_ += ":\n"; }

// The flag is for COFF, where a mergeable definition needs its section opened
// before the label. ELF and Mach-O say it afterwards, with `.weak`, exactly as
// they did - so this ignores it and the emitted text is unchanged.
void GnuSpelling::functionBegin(const std::string &name, bool exported,
                                bool mergeable) {
    (void)mergeable;
    if (exported) globl(name);
    textSection();
    defLabel(name);
}

// **Unwind data, and it is the same three directives in every function.** A
// cxx1 frame has one shape, so the CFA is rbp + 16 throughout; without it a
// backtrace stops here and no exception passes. MASM has always said this.
void GnuSpelling::prologue(int frameSize, const std::string &lsda) {
    o_ += "  .cfi_startproc\n";
    if (!lsda.empty()) {
        o_ += "  .cfi_personality 155, DW.ref.__gxx_personality_v0\n";
        o_ += "  .cfi_lsda 27, " + lsda + "\n";
    }
    ins("push", reg("%rbp"));
    o_ += "  .cfi_def_cfa_offset 16\n";
    o_ += "  .cfi_offset %rbp, -16\n";
    ins("mov", reg("%rsp"), reg("%rbp"));
    o_ += "  .cfi_def_cfa_register %rbp\n";
    if (frameSize > 0) ins("sub", imm(frameSize), reg("%rsp"));
}

void GnuSpelling::functionEnd(const std::string &) {
    o_ += "  .cfi_endproc\n";
}

void GnuSpelling::globl(const std::string &name) {
    o_ += "  .globl "; o_ += sym(name); o_ += '\n';
}

// Measured from clang: `.weak` beside the `.globl`, which is what makes the
// linker fold the copies of an inline function rather than reject them.
void GnuSpelling::weakDefinition(const std::string &name) {
    o_ += "  .weak "; o_ += sym(name); o_ += '\n';
}

void GnuSpelling::fileEntry(int n, const std::string &name) {
    o_ += "  .file ";
    appendNum(o_, n);
    o_ += " \"";
    o_ += name;
    o_ += "\"\n";
}

void GnuSpelling::location(int file, int line, int column) {
    o_ += "  .loc ";
    appendNum(o_, file);
    o_ += ' ';
    appendNum(o_, line);
    o_ += ' ';
    appendNum(o_, column);
    o_ += '\n';
}

void GnuSpelling::textSection()   { o_ += "  .text\n"; }
void GnuSpelling::rodataSection() { o_ += "  .section .rodata\n"; }
void GnuSpelling::dataSection()   { o_ += "  .data\n"; }
void GnuSpelling::bssSection()    { o_ += "  .bss\n"; }

void GnuSpelling::objectType(const std::string &name) {
    o_ += "  .type "; o_ += sym(name); o_ += ", @object\n";
}

void GnuSpelling::objectSize(const std::string &name, int size) {
    o_ += "  .size "; o_ += sym(name); o_ += ", "; appendNum(o_, size); o_ += '\n';
}

void GnuSpelling::align(int n) { o_ += "  .align "; appendNum(o_, n); o_ += '\n'; }
void GnuSpelling::zero(int n)  { o_ += "  .zero ";  appendNum(o_, n); o_ += '\n'; }

void GnuSpelling::dataInt(int size, long long v) {
    switch (size) {
    case 1: o_ += "  .byte "; break;
    case 2: o_ += "  .word "; break;
    case 4: o_ += "  .long "; break;
    default: o_ += "  .quad "; break;
    }
    appendNum(o_, v);
    o_ += '\n';
}

void GnuSpelling::dataSym(const std::string &s, long long off) {
    o_ += "  .quad ";
    o_ += sym(s);
    if (off > 0) { o_ += '+'; appendNum(o_, off); }
    else if (off < 0) { o_ += '-'; appendNum(o_, -off); }
    o_ += '\n';
}

void GnuSpelling::dataBytes(const std::string &bytes) {
    o_ += "  .byte ";
    for (std::size_t k = 0; k < bytes.size(); k++) {
        if (k) o_ += ", ";
        appendNum(o_, static_cast<long long>(
                          static_cast<unsigned char>(bytes[k])));
    }
    o_ += '\n';
}


// --- CoffSpelling -----------------------------------------------------------

// Quoted where GNU-as would not take the name as an identifier. A Microsoft
// mangled name always carries a '?' or an '@'; a C name carries neither, so
// `printf` and `__CxxFrameHandler3` are written plainly, as clang writes them.
std::string CoffSpelling::sym(const std::string &name) const {
    std::string n = name;
    // **A `.L` label is a temporary, and a table cannot name one.** The
    // assembler discards them, so `.long .L...try.0@IMGREL` is refused as an
    // undefined label - the same rule that made the Itanium LSDA need
    // `GCC_except_table0` rather than an `L` name. Rewritten to a real symbol
    // exactly as the MASM spelling rewrites it, dots to underscores.
    if (n.compare(0, 2, ".L") == 0) {
        std::string out = "$";
        for (char c : n) out += (c == '.') ? '_' : c;
        n = out;
    }
    if (n.find('?') == std::string::npos && n.find('@') == std::string::npos &&
        n.find('$') == std::string::npos)
        return n;
    return "\"" + n + "\"";
}

void CoffSpelling::functionBegin(const std::string &name, bool exported,
                                 bool mergeable) {
    fnName_ = name;
    mergeable_ = mergeable;
    if (mergeable) {
        // The section carries the COMDAT bit and names the symbol it folds on;
        // `discard` is IMAGE_COMDAT_SELECT_ANY, which is what an inline
        // definition wants - keep one, drop the rest.
        o_ += "  .section .text,\"xr\",discard," + sym(name) + "\n";
        opened_ = name;
    } else {
        textSection();
    }
    if (exported) globl(name);
    defLabel(name);
}

// For a global the code generator says this *before* the label, which is where
// the section directive has to go; for a function functionBegin has already
// opened one, and a second would start an empty section.
void CoffSpelling::weakDefinition(const std::string &name) {
    if (opened_ == name) { opened_.clear(); return; }
    o_ += "  .section .rdata,\"dr\",discard," + sym(name) + "\n";
}

// COFF spells the read-only segment .rdata, and has no .type or .size.
void CoffSpelling::rodataSection() { o_ += "  .section .rdata,\"dr\"\n"; }
void CoffSpelling::objectType(const std::string &name) { (void)name; }
void CoffSpelling::objectSize(const std::string &name, int size) {
    (void)name; (void)size;
}

// **Hand-written, because `.seh_handlerdata` cannot live in a COMDAT.** The
// `.seh_*` directives are the tidy way and the assembler builds .pdata and
// .xdata from them - measured, and it works for every function in plain .text.
// It refuses inside a COMDAT section: "expected relocatable expression", for
// `.text,"xr",discard,"sym"` and for a uniquely-named variant alike. Every
// inline definition is exactly such a section, and an inline member with a
// destructor carries EH, so the shortcut fails for the functions COMDAT exists
// for. Written out, the unwind data goes in `associative` sections instead,
// which does assemble - so this is MASM's arithmetic in GNU spelling.
void CoffSpelling::prologue(int frameSize, const std::string &lsda) {
    hasEh_ = !lsda.empty();
    frameSize_ = frameSize;
    const std::string b = "\"$LNbeg$" + fnName_ + "\"";
    o_ += b + ":\n";

    // The frame pointer is taken after the allocation on this target, so every
    // FH3 displacement is an unsigned offset up from the establisher.
    o_ += "  push %rbp\n";
    o_ += "\"$LNpush$" + fnName_ + "\":\n";
    if (frameSize > 0) {
        o_ += "  sub $"; appendNum(o_, frameSize); o_ += ", %rsp\n";
    }
    o_ += "\"$LNalloc$" + fnName_ + "\":\n";
    o_ += "  mov %rsp, %rbp\n";
    o_ += "\"$LNprolog$" + fnName_ + "\":\n";

    // Last instruction first, which is the order an unwinder undoes them in.
    unwindCodes_ = 0;
    unwindData_.clear();
    const std::string p = "\"$LNprolog$" + fnName_ + "\"";
    const std::string al = "\"$LNalloc$" + fnName_ + "\"";
    const std::string pu = "\"$LNpush$" + fnName_ + "\"";
    // UWOP_SET_FPREG is 3; the frame offset is in the header and is zero
    // because rbp is set to rsp exactly.
    unwindData_ += "  .byte " + p + "-" + b + "\n  .byte 3\n";
    unwindCodes_ += 1;
    if (frameSize > 0) {
        unwindData_ += "  .byte " + al + "-" + b + "\n";
        if (frameSize <= 128 && frameSize % 8 == 0) {
            // UWOP_ALLOC_SMALL is 2 with (size/8 - 1) in the high nibble.
            unwindData_ += "  .byte " +
                std::to_string(((frameSize / 8 - 1) << 4) | 2) + "\n";
            unwindCodes_ += 1;
        } else {
            unwindData_ += "  .byte 1\n  .short " +
                std::to_string((frameSize + 7) / 8) + "\n";
            unwindCodes_ += 2;
        }
    }
    // UWOP_PUSH_NONVOL is 0 with the register in the high nibble - rbp is 5.
    unwindData_ += "  .byte " + pu + "-" + b + "\n  .byte 0x50\n";
    unwindCodes_ += 1;
}

void CoffSpelling::functionEnd(const std::string &name) {
    const std::string b = "\"$LNbeg$" + name + "\"";
    const std::string e = "\"$LNend$" + name + "\"";
    const std::string u = "\"$unwind$" + name + "\"";
    o_ += e + ":\n";

    // **Associative where the function is mergeable**, so the unwind data is
    // discarded with the copy it belongs to rather than surviving it.
    const std::string assoc =
        mergeable_ ? ",associative," + sym(name) : std::string();
    o_ += "  .section .xdata,\"dr\"" + assoc + "\n";
    o_ += "  .p2align 3\n";
    o_ += u + ":\n";
    // Version 1, and the flags in the top five bits. 0x19 is EHANDLER and
    // UHANDLER together, which is what a frame with a FuncInfo needs.
    o_ += std::string("  .byte ") + (hasEh_ ? "0x19" : "0x01") + "\n";
    o_ += "  .byte \"$LNprolog$" + name + "\"-" + b + "\n";
    o_ += "  .byte " + std::to_string(unwindCodes_) + "\n";
    o_ += "  .byte 0x05\n";
    o_ += unwindData_;
    // The codes are padded to an even count, and the handler goes after.
    if (unwindCodes_ % 2 != 0) o_ += "  .short 0\n";
    if (hasEh_) {
        o_ += "  .long __CxxFrameHandler3@IMGREL\n";
        o_ += "  .long \"$cppxdata$" + name + "\"@IMGREL\n";
    }
    o_ += "  .section .pdata,\"dr\"" + assoc + "\n";
    o_ += "  .p2align 2\n";
    o_ += "  .long " + b + "@IMGREL\n";
    o_ += "  .long " + e + "@IMGREL\n";
    o_ += "  .long " + u + "@IMGREL\n";
    o_ += "  .text\n";

    unwindData_.clear();
    unwindCodes_ = 0;
    hasEh_ = false;
    mergeable_ = false;
}

// **The whole frame moved, so every offset into it moves with it.** This target
// takes rbp *after* the allocation - exactly frameSize lower than the Itanium
// one every operand is written against - so `d(%rbp)` becomes
// `(d + frameSize)(%rbp)`, the positive ones included. The MASM spelling makes
// the same adjustment at the same one place, and for the same reason: an FH3
// displacement has to be an unsigned offset up from the establisher, which is
// what the frame pointer is once it sits at the bottom of the allocation.
void CoffSpelling::op(const Op &x) {
    if (x.kind != Op::Mem || std::string(x.text.p, x.text.n) != "%rbp") {
        GnuSpelling::op(x);
        return;
    }
    long long d = (x.hasDisp ? x.disp : 0) + frameSize_;
    if (d != 0) appendNum(o_, d);
    o_ += '(';
    o_ += std::string(x.text.p, x.text.n);
    o_ += ')';
}
